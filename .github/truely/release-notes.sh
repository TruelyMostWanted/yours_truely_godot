#!/usr/bin/env bash
# Writes the Markdown release notes of .github/workflows/truely_release.yml to stdout.
#
# Usage: .github/truely/release-notes.sh <sha> <tag> <config> [<previous-tag>]
#   sha           the commit the release is built from
#   tag           the new release tag, e.g. truely-v0.1.0-godot4.8-beta5
#   config        the Godot base, e.g. 4.8.beta5
#   previous-tag  optional, overrides the search for the previous release (any revision works)
#
# Needs git (full history with tags) and gh (GH_TOKEN or GITHUB_TOKEN in CI). The repository comes
# from GITHUB_REPOSITORY, else from the origin remote.
#
# The Truely version comes from truely/version.txt at <sha> (and at the previous release).
#
# Changes: the first-parent commits since the previous release tag, truely-v* or the older
# truely-godot* (without one: the last
# 50), merge commits and "[skip ci]" commits left out. The issue number "(#N)" at the end of each
# subject is looked up with gh and grouped by category (Godot's topic groups) and type (Feature,
# Enhancement, Bugfix) with the rules of .github/truely/changelog-categories.tsv.
set -euo pipefail

SHA="$(git rev-parse "$1^{commit}")"
TAG="$2"
CONFIG="$3"

REPO="${GITHUB_REPOSITORY:-}"
if [ -z "$REPO" ]; then
	REPO="$(git remote get-url origin | sed -E 's#^.*github\.com[:/]##; s#\.git$##')"
fi
URL="https://github.com/$REPO"

# Previous release: the newest release tag (truely-v*, before #265 truely-godot*) reachable from SHA
# that does not point at SHA itself.
PREV="${4:-}"
[ -n "$PREV" ] || while read -r t; do
	[ -n "$t" ] || continue
	if [ "$(git rev-parse "$t^{commit}")" != "$SHA" ]; then
		PREV="$t"
		break
	fi
done < <(git tag --merged "$SHA" --sort=-committerdate --list 'truely-v*' 'truely-godot*')

if [ -n "$PREV" ]; then
	RANGE=("$PREV..$SHA")
else
	RANGE=(-n 50 "$SHA")
fi

declare -A seen=()
order=()
others=()
while IFS= read -r subject; do
	case "$subject" in *"[skip ci]"*) continue ;; esac
	if [[ "$subject" =~ \(#([0-9]+)\)( \(#([0-9]+)\))?$ ]]; then
		for n in "${BASH_REMATCH[1]}" "${BASH_REMATCH[3]}"; do
			[ -n "$n" ] || continue
			if [ -z "${seen[$n]:-}" ]; then
				seen[$n]=1
				order+=("$n")
			fi
		done
	elif [ -n "$PREV" ]; then
		others+=("$subject")
	fi
done < <(git log --first-parent --no-merges --reverse --format='%s' "${RANGE[@]}")

# Category of an issue: its row in changelog-categories.tsv (issue number), else the first label row
# it carries, else Core. "-" leaves the issue out. See that file for the rules.
MAP="$(git show "$SHA:.github/truely/changelog-categories.tsv" 2>/dev/null || true)"
declare -A cat_of_issue=() cat_of_label=()
label_order=()
while IFS=$'	' read -r key cat; do
	case "$key" in '' | '#'*) continue ;; esac
	if [[ "$key" =~ ^[0-9]+$ ]]; then
		cat_of_issue[$key]="$cat"
	else
		cat_of_label[$key]="$cat"
		label_order+=("$key")
	fi
done <<<"$MAP"

# Godot's order of topic groups (interactive changelog), then Feature, Enhancement, Bugfix inside each.
CATEGORIES=(2D 3D Animation Audio Buildsystem "C#" Core Editor Export GDExtension GDScript GUI Import Input Navigation Network Physics Platforms Rendering Shaders Tests Thirdparty XR)
TYPES=(Feature Enhancement Bugfix)
declare -A lines=()
for n in "${order[@]}"; do
	# One call for issues and pull requests; squash merges may append the PR number after the issue.
	if ! info="$(gh api "repos/$REPO/issues/$n" --jq '[(if .pull_request then "pr" else "issue" end), .title, .html_url, ([.labels[].name] | join(","))] | @tsv' 2>/dev/null)"; then
		# Not an issue of this repository (a number from merged upstream history).
		continue
	fi
	IFS=$'	' read -r kind title link labels <<<"$info"
	[ "$kind" = "issue" ] || continue
	# Drop the template prefix "[Feature] ", "[Bug] ", ...
	title="$(sed -E 's/^\[[^]]*\] *//' <<<"$title")"
	cat="${cat_of_issue[$n]:-}"
	if [ -z "$cat" ]; then
		for l in "${label_order[@]+"${label_order[@]}"}"; do
			case ",$labels," in *",$l,"*) cat="${cat_of_label[$l]}"; break ;; esac
		done
	fi
	[ -n "$cat" ] || cat="Core"
	[ "$cat" != "-" ] || continue
	case ",$labels," in
		*",type: feature,"*) type="Feature" ;;
		*",type: bug,"*) type="Bugfix" ;;
		*",type: refactoring,"*) type="Enhancement" ;;
		*",type: task,"* | *",type: concept,"*) [ -n "${cat_of_issue[$n]:-}" ] || continue; type="Enhancement" ;;
		*) continue ;;
	esac
	lines["$cat|$type"]+="- $type: $title ([#$n]($link))"$'
'
done

section() {
	local heading="$1"
	shift
	[ "$#" -gt 0 ] || return 0
	printf '### %s\n\n' "$heading"
	printf '%s\n' "$@"
	printf '\n'
}

# Godot base: number and status at SHA and at the previous release.
base_of() {
	local rev="$1" major minor patch status
	major="$(git show "$rev:version.py" | sed -nE 's/^major = ([0-9]+).*/\1/p')"
	minor="$(git show "$rev:version.py" | sed -nE 's/^minor = ([0-9]+).*/\1/p')"
	patch="$(git show "$rev:version.py" | sed -nE 's/^patch = ([0-9]+).*/\1/p')"
	status="$(git show "$rev:truely/godot-status.txt" 2>/dev/null | tr -d '[:space:]' || true)"
	[ -n "$status" ] || status="$(git show "$rev:version.py" | sed -nE 's/^status = "([^"]*)".*/\1/p')"
	if [ "${patch:-0}" != "0" ]; then
		printf '%s.%s.%s %s' "$major" "$minor" "$patch" "$status"
	else
		printf '%s.%s %s' "$major" "$minor" "$status"
	fi
}

# Truely version (truely/version.txt) at a revision; empty before #265.
truely_of() {
	git show "$1:truely/version.txt" 2>/dev/null | tr -d '[:space:]' || true
}

NOW="$(base_of "$SHA")"
HASH="${SHA:0:9}"
TRUELY="$(truely_of "$SHA")"

if [ -n "$TRUELY" ]; then
	printf '**Truely v%s**, based on Godot %s, at commit [`%s`](%s/commit/%s).\n' "$TRUELY" "$NOW" "${SHA:0:7}" "$URL" "$SHA"
	if [ -n "$PREV" ]; then
		BEFORE_TRUELY="$(truely_of "$PREV")"
		if [ -n "$BEFORE_TRUELY" ] && [ "$BEFORE_TRUELY" != "$TRUELY" ]; then
			printf 'Truely v%s → v%s.\n' "$BEFORE_TRUELY" "$TRUELY"
		fi
	fi
	printf 'In the engine: `Engine.get_version_info()["truely_version"]`, `--truely-version`.\n'
else
	printf 'Truely Godot Engine at commit [`%s`](%s/commit/%s), based on Godot %s.\n' "${SHA:0:7}" "$URL" "$SHA" "$NOW"
fi
printf 'Version string: `%s.truely.%s` (.NET: `%s.mono.truely.%s`, Godot naming for the .NET build).\n\n' "$CONFIG" "$HASH" "$CONFIG" "$HASH"

printf '## Changes\n\n'
if [ -n "$PREV" ]; then
	printf 'Since [%s](%s/releases/tag/%s).\n\n' "$PREV" "$URL" "$PREV"
else
	printf 'First release: changes of the last 50 commits.\n\n'
fi
for c in "${CATEGORIES[@]}"; do
	body=""
	for t in "${TYPES[@]}"; do body+="${lines["$c|$t"]:-}"; done
	[ -n "$body" ] || continue
	printf '### %s

%s
' "$c" "$body"
done
if [ "${#others[@]}" -gt 0 ]; then
	others_lines=()
	for s in "${others[@]}"; do others_lines+=("- $s"); done
	section "Other commits" "${others_lines[@]}"
fi
printf 'How to use the features from GDScript, C# and GDExtension: [wiki](%s/wiki).\n\n' "$URL"

printf '## Godot base\n\n'
if [ -n "$PREV" ]; then
	BEFORE="$(base_of "$PREV")"
	if [ "$BEFORE" != "$NOW" ]; then
		if [ "${BEFORE%% *}" = "${NOW%% *}" ]; then
			printf 'Godot %s → %s.\n\n' "$BEFORE" "${NOW#* }"
		else
			printf 'Godot %s → %s.\n\n' "$BEFORE" "$NOW"
		fi
	else
		printf 'Godot %s (unchanged).\n\n' "$NOW"
	fi
else
	printf 'Godot %s.\n\n' "$NOW"
fi
carried=()
while IFS=$'\t' read -r branch pr issue title; do
	case "$branch" in '' | '#'*) continue ;; esac
	carried+=("- $title: [godotengine/godot#$pr](https://github.com/godotengine/godot/pull/$pr) ([#$issue]($URL/issues/$issue))")
done < <(git show "$SHA:truely/upstream-prs.tsv" 2>/dev/null || true)
if [ "${#carried[@]}" -gt 0 ]; then
	printf 'Godot pull requests included before Godot merges them:\n\n'
	printf '%s\n' "${carried[@]}"
	printf '\n'
fi

cat <<EOF
## Downloads

The same set of files as a Godot release. \`*\` stands for the release name (\`Truely-v<truely>_Godot<godot>-<status>_<hash>\`); the .NET (C#) files end in \`_dotnet\`.

| Platform | Editor | Editor, .NET |
|---|---|---|
| Windows x86_64, x86_32, arm64 | \`*_windows-x86_64.zip\`, \`*_windows-x86_32.zip\`, \`*_windows-arm64.zip\` | \`*_windows-<arch>_dotnet.zip\` |
| Linux x86_64, x86_32, arm64, arm32 | \`*_linux-x86_64.zip\`, \`*_linux-x86_32.zip\`, \`*_linux-arm64.zip\`, \`*_linux-arm32.zip\` | \`*_linux-<arch>_dotnet.zip\` |
| macOS (Intel and Apple Silicon) | \`*_macos-universal.zip\` | \`*_macos-universal_dotnet.zip\` |
| Android, Horizon OS, PICO OS | \`*_android-editor.apk\`, \`*_android-editor-horizonos.apk\`, \`*_android-editor-picoos.apk\` | – |
| Web | \`*_web-editor.zip\` | – |
| **Export templates** | \`*_export-templates.tpz\`: Windows and Linux (all architectures), macOS, Android (APKs and \`android_source.zip\`), iOS, Web (with and without threads and GDExtension) | \`*_export-templates_dotnet.tpz\`: the same without Web |
| **Checksums** | \`SHA512-SUMS.txt\` (\`sha512sum -c SHA512-SUMS.txt\`) | |

## Install

- **Desktop editors:** unzip and keep the folder together.
- **Self-contained:** every desktop editor folder contains a \`._sc_\` file. Settings, caches and export templates stay in that folder, so this build and the official Godot of the same version never overwrite each other's templates.
- **Export templates:** in the editor: Editor → Manage Export Templates → Install from File, then pick the \`.tpz\` of the same variant as the editor.
- **Unsigned:** no file is signed for a store. macOS: first start with right-click Godot.app → Open; for the self-contained mode, run \`xattr -dr com.apple.quarantine <folder>\` once, otherwise macOS starts the app from a random path and does not find \`._sc_\`. Android: the editor APKs carry a debug key; allow installing from unknown sources. iOS: sign the exported Xcode project with your own team.
- **.NET (C#):** needs the .NET SDK 10 or newer. The editor brings its own Godot NuGet packages (\`GodotSharp/Tools/nupkgs\`). The Web export has no C# support.

EOF
if [ -n "$PREV" ]; then
	printf '**Full diff:** [%s...%s](%s/compare/%s...%s)\n' "$PREV" "$TAG" "$URL" "$PREV" "$TAG"
else
	printf '**Full history:** [%s](%s/commits/%s)\n' "$TAG" "$URL" "$TAG"
fi
