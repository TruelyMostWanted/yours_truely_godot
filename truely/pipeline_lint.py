#!/usr/bin/env python3
"""Finds code that builds with MSVC on Windows but fails in the GitHub Actions CI (GCC/Clang, -Werror, SCU).

Each check stands for a CI failure this fork had. See truely/PIPELINE-CONVENTIONS.md for the rules and reasons.
Usage: python truely/pipeline_lint.py [--base <ref>] <files...>
  pre-commit passes the staged .cpp/.h files; --base compares new static functions against <ref>
  instead of the staged changes (for a whole branch: --base truely-godot-engine).
Exit code 1 when a finding is printed.
"""

import re
import subprocess
import sys
from pathlib import Path

# P2: range-for binding `const String &` to a braced list of string literals (-Werror=range-loop-construct).
RANGE_FOR_TEMPORARIES = re.compile(r'for\s*\(\s*const\s+(String|StringName|NodePath)\s*&\s*\w+\s*:\s*\{\s*"')
# P3: names that are macros in MinGW's Windows headers.
MINGW_MACRO_NAMES = re.compile(r"\b(?:float|double|real_t|int|bool)\s+(near|far)\b")
# P12: names that are macros in system headers of some platform (Emscripten/musl limits.h, GCC/Clang predefines).
SYSTEM_MACRO_NAMES = re.compile(r"\b(PAGE_SIZE|PAGESIZE)\b|\b(?:float|double|int|bool|auto|String)\s+(linux|unix)\b")
# P4: a `static` free function; in an SCU build the .cpp files of a folder become one unit.
STATIC_FUNCTION = re.compile(r"^static\s+(?:inline\s+)?[\w:<>,\s\*&]+?\b(\w+)\s*\(", re.MULTILINE)
# P5: a local variable that is only the target of std::move (-Wunused-but-set-variable in Clang).
MOVE_TARGET = re.compile(r"^\s*(\w+)\s*=\s*std::move\(", re.MULTILINE)
RAW_STRING = re.compile(r'R"([^(\s]*)\((.*?)\)\1"', re.DOTALL)

BASE = "HEAD"


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def _blank_raw_strings(text: str) -> str:
    # Raw strings may span lines on purpose: keep their line breaks, drop the rest.
    return RAW_STRING.sub(lambda m: 'R""' + "\n" * m.group(0).count("\n"), text)


def _open_string_lines(text: str) -> list[int]:
    """P1: lines where a string literal is still open at the end (a tool wrote a real line break for \\n)."""
    found = []
    for number, line in enumerate(_blank_raw_strings(text).split("\n"), 1):
        in_string = None
        i = 0
        while i < len(line):
            c = line[i]
            if in_string:
                if c == "\\":
                    i += 1
                elif c == in_string:
                    in_string = None
            elif line.startswith("//", i):
                break
            elif c in "\"'":
                if c == "'" and i > 0 and line[i - 1].isdigit():
                    i += 1  # Digit separator: 1'000.
                    continue
                in_string = c
            i += 1
        if in_string == '"' and not line.rstrip().endswith("\\"):
            found.append(number)
    return found


NAMED_NAMESPACE = re.compile(r"^namespace\s+\w+\s*\{", re.MULTILINE)


def _static_functions(text: str) -> set[str]:
    code = _strip_comments(text)
    # Functions inside a named namespace (e.g. `namespace TestX {` in tests) don't clash across files.
    namespace = NAMED_NAMESPACE.search(code)
    if namespace:
        code = code[: namespace.start()]
    return {m.group(1) for m in STATIC_FUNCTION.finditer(code)}


def _added_text(path: Path) -> str:
    # Only what this change adds: upstream folders already build in SCU with their own duplicates.
    commands = [["git", "diff", "-U0", BASE, "--", str(path)]]
    if BASE == "HEAD":
        commands.insert(0, ["git", "diff", "--cached", "-U0", "--", str(path)])
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
        added = "\n".join(
            line[1:] for line in result.stdout.split("\n") if line.startswith("+") and not line.startswith("+++")
        )
        if added:
            return added
    return ""


def check(paths: list[Path]) -> list[str]:
    findings = []
    for path in paths:
        if path.suffix not in (".cpp", ".h", ".hpp", ".inc") or "thirdparty" in path.parts or not path.is_file():
            continue
        raw = path.read_text(encoding="utf-8", errors="replace")
        for number in _open_string_lines(raw):
            findings.append(f"{path}:{number}: P1 string literal with a real line break; write \\n")
        code = _strip_comments(raw)
        for m in RANGE_FOR_TEMPORARIES.finditer(code):
            line = code.count("\n", 0, m.start()) + 1
            findings.append(
                f"{path}:{line}: P2 `const {m.group(1)} &` over a list of string literals; iterate `const char *`"
            )
        for m in MINGW_MACRO_NAMES.finditer(code):
            line = code.count("\n", 0, m.start()) + 1
            findings.append(f"{path}:{line}: P3 `{m.group(1)}` is a MinGW macro; rename it (e.g. {m.group(1)}_value)")
        for m in SYSTEM_MACRO_NAMES.finditer(code):
            line = code.count("\n", 0, m.start()) + 1
            name = m.group(1) or m.group(2)
            findings.append(
                f"{path}:{line}: P12 `{name}` is a macro in system headers (Web, Linux); use a prefixed name"
            )
        for m in MOVE_TARGET.finditer(code):
            name = m.group(1)
            before = code[: m.start()].split("\n")[-30:]
            if not any(re.match(rf"^\s*[\w:<>,\s]+\s+{re.escape(name)}\s*;", line) for line in before):
                continue  # A member or a variable declared further away: the move is its use.
            rest = code[m.end() :]
            scope = rest[: rest.find("\n}")] if "\n}" in rest else rest
            after = scope.split(";", 1)[1] if ";" in scope else ""
            if not re.search(rf"\b{re.escape(name)}\b", after):
                line = code.count("\n", 0, m.start()) + 1
                findings.append(
                    f"{path}:{line}: P5 `{name}` is set by std::move and never read (Clang -Wunused-but-set-variable); use it, e.g. {name}.clear()"
                )
        if path.suffix == ".cpp":
            new_names = _static_functions(raw) & _static_functions(_added_text(path))
            for other in sorted(path.parent.glob("*.cpp")):
                if other.resolve() == path.resolve():
                    continue
                for name in sorted(new_names & _static_functions(other.read_text(encoding="utf-8", errors="replace"))):
                    findings.append(
                        f"{path}: P4 static function `{name}` also in {other.name}; SCU builds join the folder, give it a file prefix"
                    )
    return findings


def main() -> int:
    global BASE
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == "--base":
        BASE = args[1]
        args = args[2:]
    findings = check([Path(p) for p in args])
    for finding in findings:
        print(finding)
    if findings:
        print("See truely/PIPELINE-CONVENTIONS.md.")
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
