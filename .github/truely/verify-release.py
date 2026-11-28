#!/usr/bin/env python3
"""Checks the release files of .github/truely/package-release.sh before they are published.

Usage: python .github/truely/verify-release.py <release-dir> <name> <config>
  release-dir  folder with the zips, APKs, .tpz files and SHA512-SUMS.txt
  name         e.g. Truely-v0.1.0_Godot4.8-beta5_a1b2c3d
  config       version.txt of the standard templates, e.g. 4.8.beta5

Checks:
  - every file of a full release is there (PARTIAL=1: macOS and iOS may be missing), and
    SHA512-SUMS.txt lists every file with the right checksum
  - every Mono editor zip carries Godot.NET.Sdk, Godot.SourceGenerators, GodotSharp and GodotSharpEditor
    in exactly the version that build_assemblies.py gives this engine (e.g. 4.8.0-beta.5),
    and the version inside each .nuspec matches its file name
  - executables match the platform of the zip: PE in windows, ELF in linux, Mach-O in macos
    (no Linux apphost such as GodotTools.OpenVisualStudio without .exe in a Windows zip),
    and the engine executables (outside GodotSharp/) match the architecture of the file name
  - the Windows Mono editor zips have GodotTools.OpenVisualStudio.exe
  - templates/version.txt of the .tpz files is <config> or <config>.mono, the packs hold Godot's
    template files of every platform (the .NET pack without Web), each of the right kind and architecture
  - Android APKs carry libgodot_android.so for their ABIs, Web zips a .wasm, ios.zip both xcframeworks

Runs with the standard library only, on Linux (CI) and on Windows. Exits with 1 when a check failed.
"""

import hashlib
import os
import re
import struct
import sys
import zipfile
from pathlib import Path

PACKAGES = ["Godot.NET.Sdk", "Godot.SourceGenerators", "GodotSharp", "GodotSharpEditor"]

WINDOWS_ARCHES = ["x86_64", "x86_32", "arm64"]
LINUX_ARCHES = ["x86_64", "x86_32", "arm64", "arm32"]
ANDROID_TEMPLATE_ABIS = ["arm64-v8a", "armeabi-v7a", "x86", "x86_64"]
ANDROID_EDITOR_ABIS = ["arm64-v8a"]

ELF = b"\x7fELF"
PE = b"MZ"
MACHO = {
    b"\xcf\xfa\xed\xfe",  # 64-bit
    b"\xce\xfa\xed\xfe",  # 32-bit
    b"\xca\xfe\xba\xbe",  # universal (also Java class files, which a release does not hold)
}
# Machine fields: PE COFF header, ELF e_machine.
PE_ARCH = {0x8664: "x86_64", 0x014C: "x86_32", 0xAA64: "arm64"}
ELF_ARCH = {0x3E: "x86_64", 0x03: "x86_32", 0xB7: "arm64", 0x28: "arm32"}

errors: list[str] = []


def fail(message: str) -> None:
    errors.append(message)
    print(f"  FAIL  {message}")


def ok(message: str) -> None:
    print(f"  ok    {message}")


def package_version(config: str) -> str:
    """GodotSharp package version as build_assemblies.py computes it: 4.8.beta5 -> 4.8.0-beta.5."""
    number, _, status = config.rpartition(".")
    parts = number.split(".")
    while len(parts) < 3:
        parts.append("0")
    version = ".".join(parts)
    if status == "stable":
        return version
    m = re.fullmatch(r"(.*?)(\d+)", status)
    return f"{version}-{m.group(1)}.{m.group(2)}" if m else f"{version}-{status}"


def kind(head: bytes) -> tuple[str, str]:
    """Executable format and architecture ("" when unknown) from the first bytes of a file."""
    if head.startswith(ELF) and len(head) >= 20:
        return "ELF", ELF_ARCH.get(struct.unpack_from("<H", head, 18)[0], "")
    if head.startswith(PE):
        arch = ""
        if len(head) >= 0x40:
            offset = struct.unpack_from("<I", head, 0x3C)[0]
            if offset + 6 <= len(head) and head[offset : offset + 4] == b"PE\0\0":
                arch = PE_ARCH.get(struct.unpack_from("<H", head, offset + 4)[0], "")
        return "PE", arch
    if head[:4] in MACHO:
        return "Mach-O", ""
    return "", ""


def read_head(z: zipfile.ZipFile, info: zipfile.ZipInfo) -> bytes:
    with z.open(info) as f:
        return f.read(4096)


def check_binaries(z: zipfile.ZipFile, platform: str, arch: str) -> None:
    expected = {"windows": "PE", "linux": "ELF", "macos": "Mach-O"}[platform]
    count = 0
    for info in z.infolist():
        if info.is_dir() or info.file_size < 4:
            continue
        k, a = kind(read_head(z, info))
        if not k:
            continue
        # .NET assemblies (.dll) are PE files on every platform.
        if k == "PE" and info.filename.endswith(".dll"):
            continue
        count += 1
        if k != expected:
            fail(f"{info.filename} is {k}, expected {expected}")
        elif platform == "windows" and not info.filename.endswith(".exe"):
            fail(f"{info.filename} is a Windows program without .exe")
        # The engine executables match the zip; the GodotSharp/ tools are the same in every zip.
        elif arch and "/GodotSharp/" not in info.filename and a != arch:
            fail(f"{info.filename} is {a or 'of an unknown architecture'}, expected {arch}")
    ok(f"{count} native executables checked ({expected}{' ' + arch if arch else ''})")


def check_packages(z: zipfile.ZipFile, version: str) -> None:
    nupkgs = {Path(n).name: n for n in z.namelist() if "/GodotSharp/Tools/nupkgs/" in n and n.endswith(".nupkg")}
    for package in PACKAGES:
        wanted = f"{package}.{version}.nupkg"
        if wanted not in nupkgs:
            found = sorted(n for n in nupkgs if n.startswith(package + ".") and n[len(package) + 1].isdigit())
            fail(f"{wanted} missing (found: {', '.join(found) or 'none'})")
            continue
        with zipfile.ZipFile(z.open(nupkgs[wanted])) as pkg:
            nuspec = next((n for n in pkg.namelist() if n.endswith(".nuspec")), None)
            text = pkg.read(nuspec).decode("utf-8") if nuspec else ""
        m = re.search(r"<version>([^<]+)</version>", text)
        if not m or m.group(1) != version:
            fail(f"{wanted}: .nuspec has version {m.group(1) if m else 'none'}")
        else:
            ok(wanted)
    others = sorted(n for n in nupkgs if not n.endswith(f".{version}.nupkg"))
    if others:
        fail(f"packages of another version: {', '.join(others)}")


def check_editor(path: Path, platform: str, arch: str, mono: bool, version: str) -> None:
    with zipfile.ZipFile(path) as z:
        check_binaries(z, platform, arch)
        if not any(n.endswith("._sc_") for n in z.namelist()):
            fail("._sc_ missing (self-contained mode)")
        if mono:
            check_packages(z, version)
            if platform == "windows":
                if any(n.endswith("/GodotSharp/Tools/GodotTools.OpenVisualStudio.exe") for n in z.namelist()):
                    ok("GodotTools.OpenVisualStudio.exe")
                else:
                    fail("GodotSharp/Tools/GodotTools.OpenVisualStudio.exe missing")


def check_apk(label: str, z: zipfile.ZipFile, abis: list[str]) -> None:
    names = set(z.namelist())
    if "AndroidManifest.xml" not in names:
        fail(f"{label}: AndroidManifest.xml missing")
    missing = [abi for abi in abis if f"lib/{abi}/libgodot_android.so" not in names]
    if missing:
        fail(f"{label}: libgodot_android.so missing for {', '.join(missing)}")
    else:
        ok(f"{label}: {', '.join(abis)}")


def check_web(label: str, z: zipfile.ZipFile) -> None:
    if any(n.endswith(".wasm") for n in z.namelist()):
        ok(f"{label}: .wasm")
    else:
        fail(f"{label}: no .wasm")


def check_ios(label: str, z: zipfile.ZipFile) -> None:
    for target in ["debug", "release"]:
        for slice_ in ["ios-arm64", "ios-arm64_x86_64-simulator"]:
            name = f"libgodot.ios.{target}.xcframework/{slice_}/libgodot.a"
            try:
                size = z.getinfo(name).file_size
            except KeyError:
                fail(f"{label}: {name} missing")
                continue
            # misc/dist/apple_embedded_xcode only has placeholders.
            if size < 1024 * 1024:
                fail(f"{label}: {name} has only {size} bytes")
    ok(f"{label}: xcframeworks checked")


def expected_templates(mono: bool) -> list[str]:
    files = ["version.txt", "android_debug.apk", "android_release.apk", "android_source.zip", "macos.zip", "ios.zip"]
    for target in ["debug", "release"]:
        for arch in WINDOWS_ARCHES:
            files += [f"windows_{target}_{arch}.exe", f"windows_{target}_{arch}_console.exe"]
        files += [f"linux_{target}.{arch}" for arch in LINUX_ARCHES]
        if not mono:
            files += [f"web_{v}{target}.zip" for v in ["", "nothreads_", "dlink_", "dlink_nothreads_"]]
    return files


def check_templates(path: Path, config: str, mono: bool, partial: bool) -> None:
    with zipfile.ZipFile(path) as z:
        actual = z.read("templates/version.txt").decode("utf-8").strip()
        if actual != config:
            fail(f"templates/version.txt is {actual}, expected {config}")
        else:
            ok(f"version.txt {actual}")
        present = {i.filename[len("templates/") :] for i in z.infolist() if not i.is_dir()}
        wanted = expected_templates(mono)
        optional = {"macos.zip", "ios.zip"} if partial else set()
        missing = [f for f in wanted if f not in present and f not in optional]
        extra = sorted(present - set(wanted))
        if missing:
            fail(f"templates missing: {', '.join(missing)}")
        if extra:
            fail(f"unexpected template files: {', '.join(extra)}")
        if not missing and not extra:
            ok(f"{len(present)} template files")
        for name in sorted(present):
            info = z.getinfo("templates/" + name)
            if name.startswith("windows_") or name.startswith("linux_"):
                k, a = kind(read_head(z, info))
                want = "PE" if name.startswith("windows_") else "ELF"
                m = re.search(r"(x86_64|x86_32|arm64|arm32)", name)
                if k != want or (m and a != m.group(1)):
                    fail(f"{name} is {k or 'unknown'} {a}, expected {want} {m.group(1) if m else ''}")
            elif name.endswith(".apk"):
                with zipfile.ZipFile(z.open(info)) as apk:
                    check_apk(name, apk, ANDROID_TEMPLATE_ABIS)
            elif name.startswith("web_"):
                with zipfile.ZipFile(z.open(info)) as web:
                    check_web(name, web)
            elif name == "ios.zip":
                with zipfile.ZipFile(z.open(info)) as ios:
                    check_ios(name, ios)
        ok("template executables checked")


def expected_files(name: str, partial: bool) -> list[str]:
    files = []
    for s in ["", "_dotnet"]:
        files += [f"{name}_windows-{arch}{s}.zip" for arch in WINDOWS_ARCHES]
        files += [f"{name}_linux-{arch}{s}.zip" for arch in LINUX_ARCHES]
        if not partial:
            files.append(f"{name}_macos-universal{s}.zip")
        files.append(f"{name}_export-templates{s}.tpz")
    files += [f"{name}_android-editor{d}.apk" for d in ["", "-horizonos", "-picoos"]]
    files.append(f"{name}_web-editor.zip")
    return files


def check_sums(folder: Path, files: list[Path]) -> None:
    sums = folder / "SHA512-SUMS.txt"
    if not sums.is_file():
        fail("SHA512-SUMS.txt missing")
        return
    listed = {}
    for line in sums.read_text(encoding="utf-8").splitlines():
        # "<digest>  <file>" (text mode) or "<digest> *<file>" (binary mode, e.g. on Windows)
        m = re.fullmatch(r"([0-9a-f]{128}) [ *](.+)", line)
        if m:
            listed[m.group(2)] = m.group(1)
        elif line.strip():
            fail(f"SHA512-SUMS.txt: unreadable line {line!r}")
    for path in files:
        h = hashlib.sha512()
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(1 << 20), b""):
                h.update(chunk)
        if listed.pop(path.name, None) != h.hexdigest():
            fail(f"SHA512-SUMS.txt: wrong or no checksum for {path.name}")
    if listed:
        fail(f"SHA512-SUMS.txt lists files that are not there: {', '.join(sorted(listed))}")
    ok(f"SHA512-SUMS.txt: {len(files)} files")


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    folder, name, config = Path(sys.argv[1]), sys.argv[2], sys.argv[3]
    partial = bool(os.environ.get("PARTIAL"))
    version = package_version(config)
    print(f"Release {name}, config {config}, .NET packages {version}{', PARTIAL' if partial else ''}")

    files = sorted(folder.glob(f"{name}_*"))
    if not files:
        print(f"No release files {name}_* in {folder}")
        return 1
    print("== file set")
    missing = [f for f in expected_files(name, partial) if not (folder / f).is_file()]
    if missing:
        fail(f"missing: {', '.join(missing)}")
    else:
        ok(f"{len(files)} files")
    check_sums(folder, files)

    for path in files:
        print(f"== {path.name}")
        rest = path.name[len(name) + 1 :]
        mono = path.stem.endswith("_dotnet")
        m = re.fullmatch(r"(windows|linux|macos)-(\w+?)(_dotnet)?\.zip", rest)
        if path.suffix == ".tpz":
            check_templates(path, config + (".mono" if mono else ""), mono, partial)
        elif m:
            check_editor(path, m.group(1), "" if m.group(2) == "universal" else m.group(2), mono, version)
        elif rest.startswith("android-editor"):
            with zipfile.ZipFile(path) as z:
                check_apk(path.name, z, ANDROID_EDITOR_ABIS)
        elif rest == "web-editor.zip":
            with zipfile.ZipFile(path) as z:
                check_web(path.name, z)
        else:
            print("  skipped")

    print()
    if errors:
        print(f"{len(errors)} problem(s):")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("All release files passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
