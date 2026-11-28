#!/usr/bin/env bash
# Assembles the release files of .github/workflows/truely_release.yml from the build artifacts.
#
# Usage: .github/truely/package-release.sh <artifacts-dir> <out-dir> <name> <config>
#   artifacts-dir  bin/ files of all build jobs (godot.<platform>.<target>.<arch>...),
#                  <platform>/GodotSharp/ for linuxbsd, windows and macos, ios.zip and ios.mono.zip,
#                  android-editor/ (editor APKs), android-standard/ and android-mono/ (Android templates)
#   name           e.g. Truely-v0.1.0_Godot4.8-beta5_a1b2c3d
#   config         version.txt of the standard templates, e.g. 4.8.beta5 (.NET gets ".mono", Godot's template folder name)
#
# File names (#337): <name>_<platform>-<arch>[_dotnet].zip, <name>_android-editor[-<distro>].apk,
# <name>_web-editor.zip, <name>_export-templates[_dotnet].tpz, and SHA512-SUMS.txt for all of them.
# The engine binaries keep Godot's ".mono" suffix.
#
# Desktop editor zips hold one folder with the editor and ._sc_ (self-contained mode).
# The .tpz files hold templates/ with version.txt and the official template file names, the same set
# as a Godot release; the .NET pack has no Web templates (the Web export has no C#).
# PARTIAL=1: macOS and iOS builds that are missing are left out (Apple runner capacity, #334;
# published later by truely_release_update.yml).
set -euo pipefail

A="$(cd "$1" && pwd)"
mkdir -p "$2"
OUT="$(cd "$2" && pwd)"
NAME="$3"
CONFIG="$4"
DIST="${DIST:-$(cd "$(dirname "$0")/../../misc/dist" && pwd)}"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

WINDOWS_ARCHES="x86_64 x86_32 arm64"
LINUX_ARCHES="x86_64 x86_32 arm64 arm32"

bin() { # bin <file>: path of a build, fails if missing
	if [ ! -e "$A/$1" ]; then
		echo "Missing build: $1" >&2
		exit 1
	fi
	echo "$A/$1"
}

partial_missing() { # partial_missing <file>: true when PARTIAL is set and the build is missing
	[ -n "${PARTIAL:-}" ] && [ ! -e "$A/$1" ]
}

zipdir() { # zipdir <dir> <zip>: zips the content of <dir> and removes <dir> (disk space), keeps permissions and symlinks
	rm -f "$2"
	(cd "$1" && zip -q -r -y -9 "$2" .)
	rm -rf "$1"
	echo "  $(basename "$2")"
}

sharp() { # sharp <platform> <dest>: copies GodotSharp/ of a platform
	if [ ! -d "$A/$1/GodotSharp" ]; then
		echo "Missing GodotSharp for $1" >&2
		exit 1
	fi
	cp -R "$A/$1/GodotSharp" "$2/GodotSharp"
}

echo "Desktop editors"
for m in "" ".mono"; do
	s="${m:+_dotnet}"

	for arch in $WINDOWS_ARCHES; do
		f="${NAME}_windows-$arch$s"
		d="$STAGE/$f/$f"
		mkdir -p "$d"
		cp "$(bin "godot.windows.editor.$arch$m.exe")" "$d/$f.exe"
		cp "$(bin "godot.windows.editor.$arch$m.console.exe")" "$d/${f}_console.exe"
		touch "$d/._sc_"
		[ -z "$m" ] || sharp windows "$d"
		zipdir "$STAGE/$f" "$OUT/$f.zip"
	done

	for arch in $LINUX_ARCHES; do
		f="${NAME}_linux-$arch$s"
		d="$STAGE/$f/$f"
		mkdir -p "$d"
		cp "$(bin "godot.linuxbsd.editor.$arch$m")" "$d/$f.$arch"
		chmod +x "$d/$f.$arch"
		touch "$d/._sc_"
		[ -z "$m" ] || sharp linuxbsd "$d"
		zipdir "$STAGE/$f" "$OUT/$f.zip"
	done

	# macOS: Godot.app with ._sc_ next to it (the editor looks outside the bundle)
	if partial_missing "godot.macos.editor.universal$m"; then
		echo "  (skipped: no macOS editor$m build, PARTIAL)"
		continue
	fi
	f="${NAME}_macos-universal$s"
	d="$STAGE/$f/$f"
	mkdir -p "$d"
	cp -R "$DIST/macos_tools.app" "$d/Godot.app"
	mkdir -p "$d/Godot.app/Contents/MacOS"
	cp "$(bin "godot.macos.editor.universal$m")" "$d/Godot.app/Contents/MacOS/Godot"
	chmod +x "$d/Godot.app/Contents/MacOS/Godot"
	touch "$d/._sc_"
	[ -z "$m" ] || sharp macos "$d/Godot.app/Contents/Resources"
	zipdir "$STAGE/$f" "$OUT/$f.zip"
done

echo "Android and Web editors (standard only, like Godot)"
for distro in "" horizonos picoos; do
	f="${NAME}_android-editor${distro:+-$distro}.apk"
	cp "$(bin "android-editor/android_editor${distro:+_$distro}.apk")" "$OUT/$f"
	echo "  $f"
done
cp "$(bin "godot.web.editor.wasm32.zip")" "$OUT/${NAME}_web-editor.zip"
echo "  ${NAME}_web-editor.zip"

echo "Export templates"
for m in "" ".mono"; do
	s="${m:+_dotnet}"
	p="${m:+dotnet_}"
	t="$STAGE/${p}tpz/templates"
	mkdir -p "$t"
	printf '%s' "$CONFIG$m" >"$t/version.txt"

	for target in debug release; do
		for arch in $WINDOWS_ARCHES; do
			cp "$(bin "godot.windows.template_$target.$arch$m.exe")" "$t/windows_${target}_$arch.exe"
			cp "$(bin "godot.windows.template_$target.$arch$m.console.exe")" "$t/windows_${target}_${arch}_console.exe"
		done
		for arch in $LINUX_ARCHES; do
			cp "$(bin "godot.linuxbsd.template_$target.$arch$m")" "$t/linux_$target.$arch"
			chmod +x "$t/linux_$target.$arch"
		done
		# Web: no .NET builds, so only the standard pack has them.
		if [ -z "$m" ]; then
			cp "$(bin "godot.web.template_$target.wasm32.zip")" "$t/web_$target.zip"
			cp "$(bin "godot.web.template_$target.wasm32.nothreads.zip")" "$t/web_nothreads_$target.zip"
			cp "$(bin "godot.web.template_$target.wasm32.dlink.zip")" "$t/web_dlink_$target.zip"
			cp "$(bin "godot.web.template_$target.wasm32.nothreads.dlink.zip")" "$t/web_dlink_nothreads_$target.zip"
		fi
	done

	# Android: APKs and the Gradle build source, already named like Godot's by the Gradle job.
	android="android-standard"
	[ -z "$m" ] || android="android-mono"
	for file in android_debug.apk android_release.apk android_source.zip; do
		cp "$(bin "$android/$file")" "$t/$file"
	done

	# iOS: ios.zip, the Xcode project of misc/dist/apple_embedded_xcode with the xcframeworks.
	if partial_missing "ios$m.zip"; then
		echo "  (no iOS templates$m, PARTIAL)"
	else
		cp "$(bin "ios$m.zip")" "$t/ios.zip"
	fi

	# macOS: macos.zip with macos_template.app
	if partial_missing "godot.macos.template_release.universal$m"; then
		echo "  (no macOS templates$m, PARTIAL)"
	else
		app="$STAGE/${p}macos/macos_template.app"
		mkdir -p "$(dirname "$app")"
		cp -R "$DIST/macos_template.app" "$app"
		mkdir -p "$app/Contents/MacOS"
		for target in debug release; do
			cp "$(bin "godot.macos.template_$target.universal$m")" "$app/Contents/MacOS/godot_macos_$target.universal"
			chmod +x "$app/Contents/MacOS/godot_macos_$target.universal"
		done
		zipdir "$STAGE/${p}macos" "$t/macos.zip" >/dev/null
	fi

	zipdir "$STAGE/${p}tpz" "$OUT/${NAME}_export-templates$s.tpz"
done

echo "Checksums"
(cd "$OUT" && rm -f SHA512-SUMS.txt && sha512sum "${NAME}"_* >SHA512-SUMS.txt)
echo "  SHA512-SUMS.txt ($(wc -l <"$OUT/SHA512-SUMS.txt") files)"
