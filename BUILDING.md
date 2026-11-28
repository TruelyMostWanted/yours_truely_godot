# Building truely-godot-engine

This guide takes you from a fresh machine to a running editor and your own export templates of **truely-godot-engine**, a fork of [Godot Engine](https://github.com/godotengine/godot). It covers Windows, Linux and macOS, the standard and the .NET (C#) build, and the Web and Android templates.

Building works like building Godot itself; Godot's [compiling docs](https://docs.godotengine.org/en/latest/engine_details/development/compiling) have every detail. This page is the short path, plus what is different in this fork. If you only want to use the engine, [download a release](#download-instead) instead.

<!-- toc -->
**Contents**

1\. [What you get](#what-you-get)<br>
2\. [Prerequisites](#prerequisites)<br>
3\. [Get the source](#get-the-source)<br>
4\. [Build the editor](#build-the-editor)<br>
5\. [.NET (C#)](#net-c)<br>
6\. [Export templates](#export-templates)<br>
7\. [Run](#run)<br>
8\. [Troubleshooting](#troubleshooting)<br>
9\. [Download instead](#download-instead)
<!-- /toc -->

## What you get

| Build | What it is | Command target |
|---|---|---|
| **Editor** | The Godot editor with all engine features of this fork. | `target=editor` |
| **Export templates** | The runtime your exported game runs on, one per platform: `template_debug` (debug exports) and `template_release` (release exports). | `target=template_debug`, `target=template_release` |

Each comes in two variants:

- **Standard:** GDScript and GDExtension (C++). Smaller, builds faster, needs no .NET.
- **.NET (Mono):** adds C#. Build with `module_mono_enabled=yes`, then generate the C# glue and assemblies ([section 5](#net-c)). Pick it if your project uses C#. Editor and templates must use the same variant.

A version string tells builds apart, e.g. `4.8.beta5.truely.f031fd0a1`: Godot base version `4.8`, status `beta5`, build name `truely`, commit. The fork's own version, **Truely v&lt;MAJOR.MINOR.PATCH&gt;**, comes from `truely/version.txt`. The editor shows it like Godot shows its own version, without a patch number of 0, next to the Godot base without the build name: `Truely v0.2 (Godot v4.8.beta5.mono) [61d68d72e]`, `Truely v0.2.1 (Godot v4.8.rc1.mono) [...]`. `--truely-version` prints the full number (`0.2.0`) and `Engine.get_version_info()["truely_version"]` returns it.

## Prerequisites

| Tool | Needed for | Check |
|---|---|---|
| Git | the source | `git --version` |
| Python 3.10 or newer | SCons and the build scripts | `python --version` (`python3` on Linux/macOS) |
| SCons 4.4 or newer | the build system | `scons --version` |
| C++17 compiler | everything | see below |
| .NET SDK 11 (10 works for the editor) | the .NET variant | `dotnet --list-sdks` |
| Emscripten 6.0.12 | Web templates | `emcc --version` |
| JDK 17, Android SDK, NDK `29.0.14206865` | Android templates | `java -version`, `echo $ANDROID_HOME` |

Install Python packages once: `python -m pip install scons zstandard`.

Compilers:

- **Windows:** [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/) (or Visual Studio) with the workload "Desktop development with C++". MinGW-w64 also works (`use_mingw=yes`).
- **Linux:** GCC 9+ or Clang 6+, plus the development packages of X11, Wayland, ALSA/PulseAudio, udev and OpenGL. Debian/Ubuntu:

  ```bash
  sudo apt-get install build-essential pkgconf libx11-dev libxcursor-dev libxinerama-dev \
    libgl1-mesa-dev libglu1-mesa-dev libasound2-dev libpulse-dev libudev-dev libxi-dev \
    libxrandr-dev libwayland-dev libwayland-bin
  ```

- **macOS:** Xcode (or `xcode-select --install`) and, for Vulkan, the Vulkan SDK: `sh misc/scripts/install_vulkan_sdk_macos.sh`.

Disk and time: the source takes about 1.5 GB, one editor build 3–6 GB of object files. A first editor build takes 10–60 minutes depending on the CPU; later builds only rebuild what changed. The [SCons cache](#build-the-editor) makes switching branches cheap.

## Get the source

```bash
git clone https://github.com/TruelyMostWanted/truelymostwanted-godot-engine.git
cd truelymostwanted-godot-engine
git switch truely-godot-engine
```

With the GitHub CLI: `gh repo clone TruelyMostWanted/truelymostwanted-godot-engine`. If you keep a mirror on GitLab, clone it the same way (`git clone https://gitlab.com/<group>/truelymostwanted-godot-engine.git` or `glab repo clone <group>/truelymostwanted-godot-engine`).

| Branch | Contents |
|---|---|
| `truely-godot-engine` | **The engine of this fork.** Default branch, build this one. |
| `truely-godot-engine-releases` | Released states only; every release is built from here. |
| `master` | An unchanged copy of Godot's `master`, without the fork's features. |

Update later with `git pull`. The repository has no submodules; all third-party code is in `thirdparty/`.

On Windows, enable long paths once, before cloning: `git config --global core.longpaths true`.

## Build the editor

Plain SCons, on every platform (`-j` sets the number of parallel jobs):

```bash
# Windows (Developer PowerShell or any shell with Python on PATH)
scons platform=windows target=editor -j8

# Linux
scons platform=linuxbsd target=editor -j8

# macOS (Apple Silicon; arch=x86_64 for Intel)
scons platform=macos target=editor arch=arm64 -j8
```

The result is in `bin/`, e.g. `bin/godot.windows.editor.x86_64.exe` (with a `.console.exe` that keeps a console window), `bin/godot.linuxbsd.editor.x86_64`, `bin/godot.macos.editor.arm64`.

On Windows, `truely/build.ps1` wraps this and labels the build like the releases (`BUILD_NAME=truely`, Godot status from `truely/godot-status.txt`, all CPU cores, optional shared cache):

```powershell
./truely/build.ps1                                    # editor
./truely/build.ps1 module_mono_enabled=yes            # editor with .NET
./truely/build.ps1 -Target template_release           # export template
./truely/build.ps1 dev_build=yes                      # any SCons option is passed on
```

To label a plain SCons build the same way on Linux/macOS: `BUILD_NAME=truely GODOT_VERSION_STATUS=$(cat truely/godot-status.txt) scons ...`.

Useful options:

| Option | Effect |
|---|---|
| `dev_build=yes` | Debug symbols, assertions, no optimization: for engine development. |
| `production=yes` | Optimized like official releases (LTO, no debug symbols unless `debug_symbols=yes`). Slow to link. |
| `tests=yes` | Compiles the unit tests into the binary ([section 7](#run)). |
| `cache_path=<dir>` `cache_limit=<GiB>` | SCons cache: reuses object files across branches and worktrees. With `build.ps1`, set `TRUELY_SCONS_CACHE` once (`setx TRUELY_SCONS_CACHE D:\scons-cache`). |
| `-j<N>` | Parallel jobs; about the number of CPU threads. |
| `d3d12=yes`, `angle=yes`, `accesskit=yes` | Optional SDKs; install them first (see below). |

Optional SDKs, downloaded by Godot's own scripts (the release builds use all of them):

```bash
python misc/scripts/install_accesskit.py         # screen reader support, all desktop platforms
python misc/scripts/install_d3d12_sdk_windows.py # Direct3D 12, Windows
python misc/scripts/install_angle.py             # OpenGL via ANGLE, Windows and macOS
```

Local options you always want can go into `custom.py` in the repository root (ignored by Git), e.g. `dev_build = "yes"`.

## .NET (C#)

The .NET editor needs three steps: build, generate the C# glue from the built engine, build the C# assemblies.

```powershell
# 1. Editor with .NET
./truely/build.ps1 module_mono_enabled=yes          # or: scons platform=windows target=editor module_mono_enabled=yes

# 2. C# glue (bindings of all engine classes), from the editor just built
./bin/godot.windows.editor.x86_64.mono.console.exe --headless --generate-mono-glue modules/mono/glue

# 3. Assemblies (GodotSharp/ next to the editor) and NuGet packages
python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows
```

On Linux and macOS use `bin/godot.linuxbsd.editor.x86_64.mono` / `bin/godot.macos.editor.arm64.mono` and `--godot-platform=linuxbsd` / `macos`.

- Repeat steps 2 and 3 after every change to classes bound to scripts, otherwise C# misses them.
- **Local NuGet folder:** C# projects need the `Godot.NET.Sdk` of *this* build, not the one from nuget.org. Add `--push-nupkgs-local <dir>` to step 3 (e.g. `--push-nupkgs-local ../nuget-local`), and point the project's `nuget.config` at that folder:

  ```xml
  <configuration>
    <packageSources>
      <add key="godot-local" value="../nuget-local" />
      <add key="nuget.org" value="https://api.nuget.org/v3/index.json" />
    </packageSources>
  </configuration>
  ```

- In the project's `.csproj`, the SDK version follows Godot's version and status: `<Project Sdk="Godot.NET.Sdk/4.8.0-beta">`.
- .NET export templates are built with `module_mono_enabled=yes` too; they need no glue step.

## Export templates

Build each template once per platform and variant (add `module_mono_enabled=yes` for .NET):

```bash
scons platform=windows target=template_debug
scons platform=windows target=template_release
scons platform=linuxbsd target=template_release
scons platform=macos target=template_release arch=arm64
```

Other architectures with `arch=`: Windows `x86_32` and `arm64` (the x86 and ARM64 tools of Visual Studio), Linux `arm64` (natively on an ARM machine), `x86_32` (`gcc-multilib g++-multilib`) and `arm32` (a cross GCC such as `g++-arm-linux-gnueabihf`, as `gcc`/`g++`/`ar` on `PATH`). The release workflow builds them this way.

Web (needs Emscripten in the shell: `source ./emsdk_env.sh` or `emsdk_env.bat`); the Web has no .NET support:

```bash
scons platform=web target=template_release                 # with threads (needs cross-origin isolation on the server)
scons platform=web target=template_release threads=no      # without threads: works on every web host
```

Android (needs `ANDROID_HOME`; the NDK version is installed automatically if the SDK manager is found), then pack the templates with Gradle:

```bash
scons platform=android target=template_release arch=arm64
scons platform=android target=template_debug arch=arm64
cd platform/android/java && ./gradlew generateGodotTemplates   # gradlew.bat on Windows
```

iOS templates need a Mac with Xcode; follow Godot's [compiling for iOS](https://docs.godotengine.org/en/latest/engine_details/development/compiling/compiling_for_ios.html) (the same commands work in this fork).

The Windows builds come with a `.console.exe`; keep it as `windows_debug_x86_64_console.exe` / `windows_release_x86_64_console.exe` next to the template.

**Where the templates go.** The editor looks in a folder named after the Godot version with status (and `.mono` for .NET), e.g. `4.8.beta5` or `4.8.beta5.mono`, and expects the official file names:

| Built file in `bin/` | Name in the templates folder |
|---|---|
| `godot.windows.template_debug.x86_64.exe` | `windows_debug_x86_64.exe` (also `x86_32`, `arm64`) |
| `godot.windows.template_release.x86_64.exe` | `windows_release_x86_64.exe` |
| `godot.linuxbsd.template_release.x86_64` | `linux_release.x86_64` (also `.x86_32`, `.arm64`, `.arm32`) |
| `godot.web.template_release.wasm32.zip` | `web_release.zip` |
| `godot.web.template_release.wasm32.nothreads.zip` | `web_nothreads_release.zip` |
| `godot.web.template_release.wasm32.dlink.zip` (`dlink_enabled=yes`, GDExtension) | `web_dlink_release.zip` (`web_dlink_nothreads_release.zip` without threads) |
| `android_release.apk`, `android_source.zip` (from Gradle; .NET: `android_monoRelease.apk`) | `android_release.apk`, `android_source.zip` |
| `godot_ios.zip` (`generate_bundle=yes`) | `ios.zip` |

| Editor mode | Templates folder |
|---|---|
| Windows | `%APPDATA%\Godot\export_templates\4.8.beta5\` |
| Linux | `~/.local/share/godot/export_templates/4.8.beta5/` |
| macOS | `~/Library/Application Support/Godot/export_templates/4.8.beta5/` |
| Self-contained (a file `._sc_` next to the editor) | `editor_data/export_templates/4.8.beta5/` next to the editor |

The self-contained mode keeps settings and templates next to the editor, so this fork and the official Godot of the same version never overwrite each other's templates. The releases ship with `._sc_`; for your own build create the empty file yourself. Instead of copying files, you can also install a `.tpz` (a zip with `templates/` and `templates/version.txt`) in the editor: *Editor → Manage Export Templates → Install from File*. Or set custom template paths per export preset.

## Run

```bash
./bin/godot.windows.editor.x86_64.exe                       # project manager
./bin/godot.windows.editor.x86_64.exe -e --path <project>   # open a project in the editor
./bin/godot.windows.editor.x86_64.exe --path <project>      # run a project
./bin/godot.windows.editor.x86_64.exe --version             # Godot version line, e.g. 4.8.beta5.truely.f031fd0a1
./bin/godot.windows.editor.x86_64.exe --truely-version      # Truely version, e.g. 0.1.0
```

Unit tests need a build with `tests=yes`:

```bash
scons platform=windows target=editor tests=yes
./bin/godot.windows.editor.x86_64.console.exe --test --headless                          # all tests
./bin/godot.windows.editor.x86_64.console.exe --test --headless --test-case="*[Engine]*" # a subset
```

Feature checks in GDScript and C# for every feature of this fork are in the [godot-custom-build-project](https://github.com/TruelyMostWanted/godot-custom-build-project) test project; it shows how a C# project uses the local NuGet packages.

## Troubleshooting

| Problem | Fix |
|---|---|
| `Filename too long` while cloning or building on Windows | `git config --global core.longpaths true`, enable long paths in Windows (`LongPathsEnabled`), or clone to a short path such as `C:\src\godot`. |
| `scons` is not found | `python -m pip install scons`, then open a new shell, or run `python -m SCons ...`. |
| Windows: no compiler found | Install the C++ workload of the Visual Studio Build Tools; build from a "Developer PowerShell" if SCons still does not find it. |
| `d3d12=yes`/`angle=yes`/`accesskit=yes` fail with missing headers or libraries | Run the matching `misc/scripts/install_*.py` first, or leave the option out. |
| .NET editor: "Assemblies not found" or a crash when a C# project is imported | Steps 2 and 3 of [section 5](#net-c) were skipped after the build: generate the glue and run `build_assemblies.py`. `bin/GodotSharp/` must sit next to the editor. |
| C# build errors about classes that no longer exist | Old glue files: delete `modules/mono/glue/GodotSharp/GodotSharp/Generated` and `modules/mono/glue/GodotSharp/GodotSharpEditor/Generated`, then generate the glue again. |
| A C# project still uses old engine classes | It uses an older `Godot.NET.Sdk` from the NuGet cache: run `build_assemblies.py` with `--push-nupkgs-local` (it clears the cache for these packages) or `dotnet nuget locals all --clear`. |
| The editor says the export templates are missing | The templates folder name must match the editor's version exactly (`4.8.beta5`, `4.8.beta5.mono`); check *Help → About* and `templates/version.txt`. |
| An old version string after changing the status | The SCons cache served old objects of version files; build once without the cache (`$env:TRUELY_SCONS_CACHE = ''`). |
| Web: export does not start in the browser | The threaded template needs cross-origin isolation headers; use the `threads=no` template on hosts without them. |

## Download instead

Ready builds are on the [releases page](https://github.com/TruelyMostWanted/truelymostwanted-godot-engine/releases), the same set as a Godot release: editors (standard and .NET) for Windows x86_64/x86_32/arm64, Linux x86_64/x86_32/arm64/arm32 and macOS universal, the Android editor (also for Horizon OS and PICO OS) and the Web editor, export templates for Windows, Linux, macOS, Android, iOS and the Web (the .NET pack without the Web), and `SHA512-SUMS.txt`. None of them is signed for a store. Release files are named `Truely-v<truely>_Godot<godot>-<status>_<hash>_<platform>-<arch>[_dotnet]`, e.g. `Truely-v0.2.1_Godot4.8-beta5_f031fd0_windows-arm64_dotnet.zip`; the [wiki page Releases](https://github.com/TruelyMostWanted/truelymostwanted-godot-engine/wiki/Releases) explains the names and the version number. The workflow `.github/workflows/truely_release.yml` shows how each file is built (runners, SCons flags, Gradle tasks, packaging).
