# Pipeline-Konventionen der Truely-Engine

Regeln, die aus der CI (GitHub Actions) kommen. Lokal wird unter Windows mit MSVC gebaut; die CI baut zusätzlich mit
GCC, Clang, clang-cl, MinGW, Emscripten (Web), dem Android-NDK und Apple-Clang, mit `dev_mode=yes`
(`warnings=extra werror=yes`), SCU, Sanitizern und einem Minimal-Template. Was MSVC durchlässt, kann dort scheitern.
Jede Regel unten steht für einen echten CI-Fehler dieser Fork (Issue oder Commit in Klammern).

Ablauf und Branches: [`FEATURE-WORKFLOW.md`](FEATURE-WORKFLOW.md). Code-Regeln: [`CONVENTIONS.md`](CONVENTIONS.md).

---

## 1. Lokal prüfen, bevor die CI es tut

| Schritt | Befehl | Was er abdeckt |
|---|---|---|
| **pre-commit-Hooks** (laufen bei jedem Commit; die CI führt **dieselbe** Konfiguration als „Static checks“ über die geänderten Dateien aus) | automatisch, oder `pre-commit run --files <dateien>` / `pre-commit run --all-files` | `clang-format`, `codespell`, `file-format` (Zeilenenden, Leerzeichen am Zeilenende, Newline am Dateiende), `header-guards`, `copyright-headers`, `validate-includes`, `validate-xml`, `make-rst`/`doc-status` (Doku-XML), `dotnet-format` (C#), `ruff`/`mypy` (Python), `eslint` (Web), `svgo` (Icons), **`truely-pipeline-lint`** |
| **Pipeline-Lint** | `python truely/pipeline_lint.py <dateien>`; ganzer Branch: `python truely/pipeline_lint.py --base truely-godot-engine $(git diff --name-only truely-godot-engine -- '*.cpp' '*.h')` | Muster P1–P5 aus Abschnitt 2 (statisch, ohne Compiler) |
| **Build mit Warnungen als Fehler** | `./truely/build.ps1 module_mono_enabled=yes tests=yes dev_mode=yes` | MSVC mit `/W4 /WX`: fängt einen Teil der GCC/Clang-Warnungen |
| **Unit-Tests** | `bin/<exe> --headless --test` | wie der Schritt „Unit tests“ der CI |
| **doctool** | `bin/<exe> --headless --doctool .` → `git status` muss leer sein | wie der Schritt „Check for class reference updates“ |
| **C#-Glue + Assemblies** | `--generate-mono-glue modules/mono/glue`, dann `build_assemblies.py` | C#-Warnungen (CS0108, CS1570, …) |
| **Volle CI auf dem Branch** (bei C++ in `core/`, `servers/`, Plattform- oder Thread-Code, vor jedem Release) | `gh workflow run runner.yml --ref <branch>` | alle Compiler und Plattformen |

Hooks nie mit `--no-verify` umgehen. Schlägt ein Hook fehl, die Ursache beheben (clang-format formatiert die Datei selbst um: neu stagen).

---

## 2. C++: was MSVC durchlässt, die CI aber nicht

| Nr. | Regel | Grund (Compiler, Beleg) | Lint |
|---|---|---|---|
| P1 | `\n` in String-Literalen als Escape schreiben, **nie** als echten Zeilenumbruch. | Werkzeuge schreiben `\n` manchmal als Umbruch; kein Compiler akzeptiert das (#279, 2026-11-27). | ja |
| P2 | Über eine Liste von String-Literalen mit `for (const char *name : { "a", "b" })` iterieren, nicht mit `const String &`. | GCC/Clang: `-Werror=range-loop-construct`, jede Iteration erzeugt eine Kopie (#207, #228, #237). | ja |
| P3 | Keine Bezeichner `near`/`far`. | Makros in den Windows-Headern von MinGW („Template w/ GCC“) (#214). | ja |
| P4 | `static`-Hilfsfunktionen in `.cpp`-Dateien bekommen einen Datei-Präfix (`_csv_format_cell_text`, nicht `_cell_text`). | SCU fügt alle `.cpp` eines Ordners zu einer Einheit zusammen: gleiche Namen kollidieren (#214). Tests liegen in eigenen `namespace`s und sind ausgenommen. | ja (nur neue Namen) |
| P5 | Variablen, die nur per `std::move` befüllt werden (z. B. um etwas außerhalb eines Locks freizugeben), danach ausdrücklich benutzen: `old.clear();`. | Clang: `-Werror,-Wunused-but-set-variable` (#386, Release-CI 2026-11-28). | ja |
| P6 | Vorzeichen in Vergleichen angleichen (`int` mit `int`, `uint32_t` mit `uint32_t`; Casts sichtbar). | GCC: `-Werror=sign-compare` (#278). | – |
| P7 | Keine Arithmetik zwischen Enum und Gleitkomma: `float(Variant::VARIANT_MAX)` statt `Variant::VARIANT_MAX` in Divisionen. | GCC/Clang: `-Werror=deprecated-enum-float-conversion` (#257). | – |
| P8 | Code, der `RenderingDevice` braucht, in `#ifdef RD_ENABLED`; Code, der optionale Module braucht, in `#ifdef MODULE_<NAME>_ENABLED`. | Plattformen ohne RD und das Minimal-Template (#207, #214). | – |
| P9 | Thread-Code ohne `threads=no`-Annahme: im Web gibt es Builds ohne Threads; Bibliotheken mit eigener Thread-Konfiguration (libvpx) entsprechend schalten. | Web „Template w/o threads“ (#214). | – |
| P10 | Ressourcen in `finish()`/Destruktoren freigeben: Listen leeren, Nodes `memdelete`n, keine globalen `Ref`s übrig lassen. | ASan/LSan in „Editor with … sanitizers“ (#214, #375). | – |
| P11 | Upstream-Dateien nur minimal ändern und die Stelle kommentieren; lieber eigene Datei. | Merge-Konflikte beim Sync, Upstream-Warnungsniveau gilt auch für unsere Zeilen. | – |
| P12 | Keine Bezeichner, die in System-Headern einer Plattform Makros sind: `PAGE_SIZE`/`PAGESIZE` (`limits.h` von Emscripten/musl), `linux`/`unix` (GCC/Clang). Konstanten mit Datei-Präfix benennen (`SQLITE_TABLE_PAGE_ROWS`). | Web-Editor: `expected unqualified-id` bei `static constexpr int PAGE_SIZE` (#419, Release-Dry-Run 2026-11-28). | ja |

---

## 3. Tests

| Regel | Grund (Beleg) |
|---|---|
| Tests, die Nodes mit Servern brauchen (`TileMapLayer`, `Window`, Viewports), tragen `[SceneTree]` im Namen. | Ohne SceneTree-Setup stürzen sie in der CI ab. |
| Tests für optionale Formate/Module (YAML, TOML, Ogg, JPG, WebP, 3D) in `#ifdef MODULE_<NAME>_ENABLED` bzw. passende Guards. | „Minimal template (everything disabled)“ baut ohne sie (#237, #375). |
| Neue Testordner in die SCU-Ordnerliste in `scu_builders.py` aufnehmen. | SCU-Build „Editor with doubles and GCC sanitizers“ (#223, #278). |
| Test-Daten nach `user://` schreiben und den Ordner vorher anlegen (`DirAccess::make_dir_recursive_absolute(OS::get_singleton()->get_user_data_dir())`). | Frische CI-Maschinen haben ihn nicht (#207). |
| Speicher in Tests sauber: Nodes freigeben, nachdem Argumente, die sie referenzieren, gebaut sind; kein `has()` auf `TypedArray<T>` mit fremdem Typ. | Sanitizer: use-after-free und Fehlermeldungen als Testfehler (#251). |
| Keine Annahmen über Messwerte des Betriebssystems (z. B. Spitzen-Speicher ≥ aktueller Speicher sicherstellen). | Unterschiede zwischen Linux, macOS, BSD (#214). |

---

## 4. Doku und C#

| Regel | Grund (Beleg) |
|---|---|
| Nach jeder API- oder Doku-Änderung `--doctool` laufen lassen und das Ergebnis committen; `git status` muss danach leer sein. | Die CI bricht bei einem doctool-Diff ab. |
| In der Klassenreferenz-XML `>` als `&gt;` schreiben (auch in Codebeispielen). | doctool schreibt es so; sonst Diff (#48). |
| Keine Methoden/Properties, deren C#-Name ein Member von `System.Object` oder `GodotObject` verdeckt (`get_type` → `GetType`, `to_string`, `equals`, `get_hash_code`). Beispiel: `get_port_type` statt `get_type`. | C# `CS0108` mit `--werror` beim Bau der Assemblies (#255). |
| BBCode in Doku-XML, die in C#-Kommentare übernommen wird, muss gültiges XML ergeben (keine nackten `<`/`&`). | C# `CS1570` (#276). |
| Wörter, die codespell „korrigiert“, aber richtig sind (Formatschlüsselwörter), in die codespell-Ignore-Liste aufnehmen, nicht umbenennen. | codespell schrieb das ASS-Schlüsselwort `Dialogue` um (#233). |

---

## 5. Release

| Regel | Grund (Beleg) |
|---|---|
| Vor `truely/release.ps1`: volle CI auf dem zu releasenden Commit von `truely-godot-engine` grün (`gh workflow run runner.yml --ref truely-godot-engine`). | Die Release-CI auf `truely-godot-engine-releases` fand am 2026-11-28 einen Clang-Fehler (P5), den lokal niemand sah. |
| Versions-Commit (`truely/version.txt`) ohne `[skip ci]`; sonst startet die CI nach `release.ps1` nicht und muss mit `gh workflow run runner.yml --ref truely-godot-engine-releases` angestoßen werden. | 0.2.2 (2026-11-28): Push auf `truely-godot-engine-releases` ohne CI-Lauf. |
| Sehr große Debug-Builds (ASan + UBSan, double, dev) können `.text` über 2 GB treiben (mold: „relocation … out of range“). Im Sanitizer-Job `optimize=debug` (-Og). `cxxflags=-fno-sanitize=…` wirkt nicht: `detect.py` hängt die Sanitizer-Flags nach den Nutzer-Flags an, das letzte Flag gewinnt. | 0.2.2 (2026-11-28): Release-CI, Job „Editor with doubles and GCC sanitizers“, zweimal. |
| TSan meldet Races in Upstream-Code, die erst durch neue Tests sichtbar werden (z. B. Lade-Fortschritt in `load_threaded_get_status()`): gezielt per Funktionsname in `misc/error_suppressions/tsan.txt` unterdrücken, mit Kommentar `# Truely:`; Races in eigenem Code beheben. | 0.2.2 (2026-11-28): `TransitionScreen`-Test, Job „Editor with ThreadSanitizer“. |
| `truely/version.txt` vor dem Release erhöhen (PATCH für Fixes, MINOR für Features); `truely/godot-status.txt` passt zur Godot-Basis. | Das Skript bricht bei einem vorhandenen Tag ab. |
| Release-Builds ohne SCons-Cache. | Der Cache lieferte alte Objekte mit altem Versions-Header. |
| Der `workflow_run` von „Truely Release“ meldet als `headSha` den Standard-Branch, nicht den Release-Commit. | Zuordnung von Läufen über die Zeit, nicht über den SHA (0.2.0). |
| Cross-Toolchains (arm32: `gcc`→`arm-linux-gnueabihf-gcc` über `PATH`) erst **nach** `godot-deps` einrichten. | pip baut `zstandard` für neue Python-Versionen aus dem Quelltext mit dem `gcc` aus `PATH`; der Cross-gcc kennt `-mno-omit-leaf-frame-pointer` nicht (#419, Dry-Run 2026-11-28). |
| Jeder Job, der `gradlew` in `platform/android/java` aufruft, braucht `godot-deps` (SCons im `PATH`), auch wenn nur fertige Bibliotheken gepackt werden. | `lib/build.gradle` sucht `scons` beim Konfigurieren: „Unable to find executable path for the 'scons' command“ (#419). Die Zeile `digest-mismatch: error` im Log ist nur ein Parameter von `download-artifact`, nicht der Fehler. |
