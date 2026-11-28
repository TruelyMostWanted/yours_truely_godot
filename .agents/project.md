# Truely-Run-Agentic-Project · truelymostwanted-godot-engine

Die eine Datei, mit der dieses Projekt geführt wird: Parameter, Ableitungen, Plattform und der Stand für die nächste Session. `/truely-run-agentic-project Mode=Resume` liest sie und setzt fort; den Rest (Commits seit dem letzten Stand, offene Issues) liest es frisch aus Git und von der Plattform. Bei einem Widerspruch gilt die Plattform. Nur der Project-Agent bzw. der Orchestrator schreibt diese Datei.

**Dieses Projekt ist eine Engine-Fork, kein Spiel.** Der Skill liefert nur die Orchestrierung (Ebenen, Board, Wache, Resume, Kosten, Caveman). Ordnerstruktur, Konventionen und Ablauf kommen aus dem Repository:
- Regeln: [`AGENTS.md`](../AGENTS.md), [`truely/CONVENTIONS.md`](../truely/CONVENTIONS.md), [`truely/FEATURE-WORKFLOW.md`](../truely/FEATURE-WORKFLOW.md). Es gibt **kein** `.agents/CONVENTIONS.md`, kein `godot-project/`, kein `upstream` godot-core-systems.
- Reihenfolge und Abhängigkeiten: Tracker-Issue #32.
- `.agents/state/` (Wissen je Issue, Datei-Index, Kosten) bleibt lokal (`.gitignore`); nur diese Datei ist versioniert. Das Repository ist öffentlich.
- Eine lokale Kopie des Skills liegt in `.agents/skills/truely-run-agentic-project/` (gitignored, weil sie private Repositories nennt). Neu kopieren mit `cp -r ~/.claude/skills/truely-run-agentic-project .agents/skills/`.

## Abweichungen vom Skill (Besitzer, 2026-11-23)

| Skill | Hier |
|---|---|
| Branch `<nr>-<name>` von `main` | Branch `<nn>-feature\|refactoring\|bug\|concept-<name>` von `truely-godot-engine`, Nummer mindestens zweistellig |
| Project-Agent mergt lokal mit `--no-ff` und löscht den Branch | Project-Agent pusht den Feature-Branch **bei Abschluss**, wartet auf **grüne GitHub Actions**, dann PR nach `truely-godot-engine` und `gh pr merge --squash` **ohne `--delete-branch`**. **Feature-Branches werden nie gelöscht**, weder auf GitHub noch lokal. Nur der Worktree wird entfernt. |
| Issue-Agent committet im Branch | gleich: **beliebig oft** committen, nie pushen; der pre-commit-Hook muss durchlaufen |
| `.agents/CONVENTIONS.md`, C# 15, `core/` von godot-core-systems | `truely/CONVENTIONS.md`: C++17-Server als Singletons, Resources, Bindings, `doc/classes/`, keine Nodes; die Godot-Quellen dürfen geändert werden (upstream-freundlich, eigene Dateien bevorzugt) |
| Issues aus Goals anlegen | Issues #1–#31 existieren schon; neue Funde als eigenes Issue nach `.github/ISSUE_TEMPLATE/` |
| Concept-Issues setzen um | Concept-Issues (#4, #6, #13, #14, #31) liefern einen Vorschlag als Kommentar; **entscheidet der Besitzer** |
| Abnahme: Akzeptanzkriterien | dazu die fünf festen Abnahmepunkte (C++ API, Bindings, Doku inkl. Wiki, Test-Build, GDScript- und C#-Test im Testprojekt) |

## Parameter

Bei `Start` geschrieben; danach nur über „setze X = Y“, mit Datum unter „Änderungen“.

```ini
MODE='Start'
GOALS='existing GitHub issues #1-#31, order and dependencies from tracker #32'
CONVENTIONS='AGENTS.md + truely/CONVENTIONS.md + truely/FEATURE-WORKFLOW.md'
HOST_PARAMETER=''
REPO_PARAMETER='TruelyMostWanted/truelymostwanted-godot-engine'
APPROVAL='Autonomous'
CHECKPOINTS=''
MAX_ISSUE_AGENTS='5'
MAX_ISSUES_PER_AGENT='1'
ALLOW_TEAMS_PER_ISSUE='false'
MIRROR_GIT_ISSUES='false'
USE_CAVEMAN='true'
```

Änderungen:
- 2026-11-26 (Abend): Besitzer: **voll autonom über Nacht**, ohne Rückfragen, mehrere Blickwinkel, erst prüfen was es gibt. Ablauf je Issue: Planen → Implementieren → Testen → Iterieren → Wiki (Guide + Screenshots, ClassRef) → Push (ohne Pipeline) → Merge. **Pipeline nur etwa alle 8–10 gemergten Features** (Fast-forward `truely-godot-engine-releases` per `release.ps1` bzw. `gh workflow run`). Modelle nach Aufgabe, Kontexte klein, token-sparsam. GLOSSARY.md (#267) mit Datei-Index vorziehen. Wenn alles durch: Release `Truely0.1.0` (neue Benennung, #265) + alten Release/Changelog aktualisieren.
- 2026-11-26: Besitzer (#273): volle CI nur auf `truely-godot-engine-releases`; Feature-Branches nur lokal (Windows, Mono, GDScript+C#).
- 2026-11-24 (ca. 01:30): Besitzer, **voll autonom ohne Rückfragen**: alle Issues (Konzept, Design, Code, Wiki, Tests, Merge) selbst abarbeiten. Vorgaben: (1) Namen von Features/APIs aussagekräftig machen, Parameter und Typen nutzen (Enums statt Strings, typisierte Options-Klassen statt Dictionary, z. B. `ResourceTextSerializer`); was im Inspector bearbeitbar sein soll → `Resource`, Rest Backend/Server. (2) **Wiki zuerst**: Guides kurz, direkt, verständlich; Home/Sidebar nach Themen gruppiert (Text-Serialisierung, Settings, File I/O, Laden & Speichern von Ressourcen, Packs, Audio, Performance …), Klassenreferenz alphabetisch. (3) Neu: `HttpRequestTarget`-Resource für `HTTPRequest`; Input-Settings (#19) einfach für Spiele; Laufzeit-Loader/-Saver für typische Assets (Bilder, Audio, Video …), **je Ressource ein eigenes Issue und Branch**. (4) Session-Limit: auf automatische Fortsetzung warten. Caveman, sparsam.
- 2026-11-24: 5-Stunden-Limit bei 95 %: keine neuen Agenten; laufende mit WIP gestoppt.
- 2026-11-24: Besitzer: **Änderungen an bestehenden Features auf deren Branch** (nicht nur Bugs: Umbenennungen, API-Anpassungen, Doku-/Testfixes), Engine und Testprojekt, danach erneut squash-mergen. Neue Issues mit neuen Klassen behalten ihren Branch.
- 2026-11-24: Besitzer: 3–5 Issues parallel (Wochen-/Session-Limit zurückgesetzt). MAX_ISSUE_AGENTS=5.
- 2026-11-23: angelegt (Besitzer: Ordnerstruktur des Engine-Repositorys, Squash-Merge nach grüner CI, Feature-Branches bleiben bestehen, gemeinsamer SCons-Cache, Board mit Skill-Status und Phasen, `.agents/state/` lokal)
- 2026-11-23: Besitzer: Entscheidungen (auch Concept-Issues) trifft der Orchestrator selbstständig und vorausschauend; Entscheidung als Kommentar „Decision“ ins Issue. APPROVAL=Autonomous.
- 2026-11-23: Besitzer: **Wiki-Seiten sind immer Pflicht**, für jedes umgesetzte Issue (auch Refactorings ohne neue API) Guide-Seite und Klassenreferenz jeder betroffenen Klasse; Concept-Entscheidungen auf der Seite `Design-Decisions`. Jede Seite mit nummeriertem Inhaltsverzeichnis (`python truely/wiki-toc.py <wiki-ordner>` vor jedem Wiki-Push). Der Orchestrator veröffentlicht sie sofort nach dem Bericht des Agenten (Status „in review“ auf `Home`), nach dem Merge Status „done“.
- 2026-11-23: Besitzer: **Testcode vorab:** Für jedes Feature schreibt ein (günstiger) Agent die GDScript- und C#-Checks im Testprojekt schon vor dem Engine-Build, auf dem Feature-Branch gleichen Namens, und pusht ihn. Nach `main` des Testprojekts erst nach dem Merge in `truely-godot-engine` und dem Engine-Build; dann dort testen.
- 2026-11-23: Besitzer: **Bugfixes eines Features auf seinem Feature-Branch**, dann denselben Branch erneut squash-mergen; keine eigenen Fix-Branches für Feature-Fehler. Vor dem Merge `--doctool` auf dem Branch prüfen.
- 2026-11-23: Besitzer: **CI in Stapeln (#46):** keine CI auf Feature-Branches und PRs; Merge, sobald lokal grün; volle CI nur auf `truely-godot-engine`, etwa ein Lauf je fünf Features; rot → Bug-Issue und Fix-Branch.
- 2026-11-23 (ca. 13:35): Besitzer: **Wochenlimit 97 %: keine neuen Agenten.** Die laufenden (#11, #14) zu Ende laufen lassen. Neue Agenten erst nach Freigabe des Besitzers.
- 2026-11-23: Besitzer: **Features parallel** auf eigenen Branches; Auswahl nach getrennten Dateien (gemeinsame Registrierungsdateien nur mit wenigen Zeilen an anderer Stelle), weil `truely-godot-engine` nie in Feature-Branches gemergt wird. Weiter höchstens 2 Agenten (Wochenlimit), höchstens ein Engine-Build gleichzeitig.
- 2026-11-23: Besitzer: Wochenlimit bei 95 %. Höchstens 2 Agenten gleichzeitig, bevorzugt ein Issue nach dem anderen in der Reihenfolge von Tracker #32. MAX_ISSUE_AGENTS=1, MAX_ISSUES_PER_AGENT=1.
- 2026-11-23 (Abend): Besitzer: Resume mit **Decision=FullAutonomous** und **UseCaveman=true** (Chat knapp, ohne Füllwörter). Erst selbst nacheinander (#37, #34), dann: **3 Issues parallel** mit Agenten auf Feature-Branches; Agenten implementieren und schreiben den Testcode im Testprojekt-Branch, **ohne Engine-Build**; Build, Test und Merge macht der Orchestrator danach nacheinander. MAX_ISSUE_AGENTS=3.

## Abgeleitet

```ini
PROJECT_NAME='truelymostwanted-godot-engine'
LOCAL_PATH='P:/Projects/GitHub/truelymostwanted-godot-engine'
BASE_BRANCH='truely-godot-engine'
TECH='Godot 4.8 beta engine fork, C++17 without exceptions, SCons; C# glue .NET 11, Godot.NET.Sdk 4.8.0-beta from local NuGet'
BUILD_COMMAND='powershell -NoProfile -ExecutionPolicy Bypass -File truely/build.ps1 module_mono_enabled=yes accesskit=no d3d12=no cache_path=P:/scons-cache cache_limit=40'  # tests: add tests=yes
GLUE_COMMAND='./bin/godot.windows.editor.x86_64.mono.exe --headless --generate-mono-glue modules/mono/glue'
ASSEMBLIES_COMMAND='python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows --push-nupkgs-local P:/Projects/GitHub/nuget-local'
TEST_PROJECT='P:/Projects/GitHub/godot-custom-build-project'
TEST_COMMAND='./godot-project/checks/run-checks.ps1 -Godot <worktree>/bin/godot.windows.editor.x86_64.mono.exe -Feature <feature-branch>'
SCONS_CACHE='P:/scons-cache (env TRUELY_SCONS_CACHE, limit 40 GiB)'
MAX_PARALLEL_ENGINE_BUILDS='1'
PROCESS_PATTERN='scons*|godot*|cl.exe|link.exe'
UPSTREAM='https://github.com/godotengine/godot.git (fetch only, push disabled)'
WIKI='https://github.com/TruelyMostWanted/truelymostwanted-godot-engine.wiki.git'
TEST_BUDGET='300s (engine build excluded)'
PROCESS_LIMIT='90m for scons, 8m otherwise'
OBSERVATION_INTERVAL='5m'
MAX_CONTEXT_SIZE='35%'
MAX_AGENT_STEPS='100'
MAX_PARALLELISM_LIMIT='85%'
ISSUE_AGENT_MODEL='opus-medium'
TRACK_AGENT_COSTS='true'
LANGUAGE='de'
ISSUE_LANGUAGE='en'
```

## Plattform

```ini
PLATFORM='github'
HOST='github.com'
OWNER='TruelyMostWanted'
REPO='TruelyMostWanted/truelymostwanted-godot-engine'
PROJECT_NUMBER='5'
PROJECT_ID='PVT_kwHOArNCm84Bsymv'
BOARD_NAME='Truely Godot Engine'
BOARD_URL='https://github.com/users/TruelyMostWanted/projects/5'
STATUS_FIELD_ID='PVTSSF_lAHOArNCm84Bsymvzhq_C1Q'
OPT_BACKLOG='f26ba37e'
OPT_PLANNING='60aa0e0c'
OPT_NEEDS_BUGFIX='d5b733fb'
OPT_WORK_IN_PROGRESS='c87261e6'
OPT_REVIEW='dba19b8b'
OPT_COMPLETED='2663aaea'
```

## Stand

```ini
LAST_INTEGRATED_COMMIT='fedfd5bf4b'
CURRENT_MILESTONE='Resource formats, settings domains, API review'
UPDATED='2026-11-26'
```

Gemergt (2026-11-24, nach dem Limit-Reset): #87, #17, #70, #83, #89, #78, #51 (+ Laufzeit-Teil auf Branch 50), #100, #101, #102, #103, #104, #105, #95, #96, #19. Wiki neu nach Themen (#98 erledigt), alle Seiten dieser Features eingepflegt (letzter Batch #96/#19 läuft per Agent).

**Stand (2026-11-24, Mittag):** Gemergt seit #99: #10/#15–#20-Umbenennungen, #90, #26 (+Testfix), #73, #155 YAML, #85, #122, #86 (+PCKBuildPlan typisiert, im Core registriert), #21, #79, #71, #89/#91 Pack-Modus, #22. Wiki aktuell bis #91 (Batch #22 läuft).
**Fallen neu:** Glue-Generator löscht alte generierte Dateien nicht (Klasse zwischen Core/Editor verschoben → alte Datei in `modules/mono/glue/GodotSharp/*/Generated` löschen, Quelle `touch`en, Assemblies neu, im Test-Worktree `.godot/mono/temp` löschen). Datenklassen, die C# zur Laufzeit braucht, im Core registrieren. `integrate.sh` bricht bei roten Unit-Tests ab (ein Merge von #26 ging vorher mit roten Tests durch, korrigiert).
**Stand (2026-11-24, Nacht):** Gemergt: #184, #185 (+ Mischpult unter Klassen-Header), #189 Float-Rundung, Inspector-Panels unter Header (#85/#51). Wiki: alle Settings-Seiten nach Audio-Muster, Rauschen bereinigt. **Offen, Build-Branch `191-feature-input-action-rebinder`** (enthält #193 von Branch 18 + #191; baut, Core-Textnamen vereinheitlicht: Keyboard/KeyboardLabel/KeyboardPrinted/Mouse/Gamepad/GamepadAxis, alte Namen werden gelesen): 3 rote Unit-Tests in tests/servers/test_input_map_settings_server.cpp aus #193 (nie gebaut): (1) Zeile ~422 Gamepad-Achse mit fester DeviceId wirkt auch auf Gerät 0, (2) ~460 save_mode ALL schreibt keine BuiltIn/Custom-Layer bzw. ui_accept, (3) ~520 v2-INI lädt nicht ("contains InputMapSettingsActions, but InputMapSettingsRows was expected": Typ-Hint für v2). Plus Text-Konvention `[Device<N>:]<Type>:[<Modifiers>+]<Input>` umsetzen (Gamepad-Namen nur in Textform: LeftStickLeft/…/RightTrigger, LeftStickPress; Godot-Enums unverändert; Feld Direction entfällt), mit Tests und Doku-Beispielen (Kommentare im Issue #193). Danach: Checks 18/172/191 im Testprojekt (Textnamen!), merge (Closes #193, #191), action_events_changed erst nach dem Anwenden feuern lassen (Doku sagt heute "before applied"), Wiki Input-Map-Settings (Rebind-Knopf, Dateiformat B). Branch 18 kompiliert allein nicht (Deklarationen fehlen) – nur 191 bauen.
**Stand (2026-11-24, Nacht):** Releases: #217 gemergt. `📦 Truely Release` startet automatisch nach grüner CI auf `truely-godot-engine` (nur bei Code-Änderungen seit dem letzten Tag `truely-godot*`), baut den getesteten Commit (Editoren std+mono Win/Linux/macOS, Templates inkl. Web), Release-Notes aus Issues, öffentlich als Pre-release. Name `Truely_Godot<ver>-<status>_<hash>_<plattform>`; Status aus `truely/godot-status.txt` (bei jedem Godot-Sync anpassen!). Lokale Windows-Pakete: `truely/pack-release.ps1` → `P:/Projects/GitHub/truely-releases/`. Falle: SCons-Cache liefert bei geändertem Versions-Header alte Objekte → Release-Builds ohne Cache. #214 Teil 3 (Minimal/SCU) gemergt; ein leerer Squash-Merge passierte, weil der Commit nicht auf dem Branch lag → vor jedem Merge `git log truely-godot-engine..<branch>` prüfen. Wiki #212 noch offen (Process-Memory ohne Bild).
**Stand (2026-11-24, spät):** Gemergt zusätzlich: #198 Input-Icons, #202 Theora-Loader-Fix (Branch 105), #207 + #214 CI-Fixes, #211 SpriteSheet „Edit Region“-Button (Branch 51, TextureRegionEditor bekommt `set_use_undo_redo`). Wiki #212: API-Seiten überarbeitet, 11 Screenshots (editor-shots: neuer Schritt `import_dock`, Projekt `wiki-project/`); offen: Process-Memory ohne Bild. beta5 = `a661dfdbcc` ist schon drin. CI-Lauf nach #214 prüfen; grün → #194 schließen. Beobachten: `[FilesManifestServer] Register, edit, validate, watch` meldet unter Sanitizern „Signal has [...] expected none“ (evtl. flaky). #214 änderte die Upstream-Datei `servers/audio/audio_server.cpp` (2 Zeilen, Leck im Destruktor).
**Stand (2026-11-24, Abend):** Gemergt: #194 Merge-Kette (`truely/merge-chain.ps1` + `truely/upstream-prs.tsv`; ausgeführt und gepusht: 4.8-Sync +40, Opus #195, WebM #196, beide geschlossen), #199/#200 Laufzeit-Loader Opus/WebM, #201 VP8 als Option `webm_vp8` (Standard an; Stronghold-Videos sind alle VP8, laufen, Screenshots im Wiki), #191+#193 Rebinder + Textform `[Device<N>:]<Type>:[<Modifiers>+]<Input>`. Wiki aktuell für alle. **Offen:** #207 CI-Fixes (Branch `207-bug-batch-ci-fixes`, CI war seit #189 rot), #198 InputIconSet/InputIconServer (Agent läuft, Branch `198-feature-input-icons`), #202 Theora-Loader-Bug, #194 schließen nach grüner CI. Upstream-PR-Reviews (Opus: 2× changes requested) sind im aktuellen PR-Stand erledigt.
**Werkzeuge neu:** Build-Sperre `P:/scons-cache/truely-build.lock` (ein Build gleichzeitig, auch in Agent-Worktrees). Worktree-Builds brauchen danach Glue + `build_assemblies.py` (sonst „Assemblies not found“). Testprojekt: `new-feature.ps1` kopiert keine `.uid` mehr; `merge.sh` ruft `tools/uidfix.py` (UID-Duplikate). Lokaler Videotest: `godot-project/_local_video_test/` (ausgeschlossen per `.git/info/exclude`).
**Alt (überholt):** #93 (SerializerOptions + Format-Enum, Branch `05-feature-resource-text-formats`, inkl. Aufrufer und Test-Branches), #88 (`88-feature-streaming-assets-server`), #84 (Branch `10-feature-pck-package-server`), #75 (`75-feature-audio-commands`, ggf. Mini-Commit auf `74-…`).

**Werkzeuge:** `scratch/integrate.sh <N> <engine-branch> <test-branch> <feature-folder> [all|build|checks]` (Merge, Build, Tests, Glue, Assemblies, doctool, Checks; nur Summen) und `scratch/merge.sh` (Kommentar, PR, Squash-Merge, Testprojekt nach main, Board). Wiki: Clone `scratch/issues/98/wiki`, Einpflegen per Sonnet-Agent mit `scratch/brief-wiki-integrate.md`. Brief für Issue-Agenten: `scratch/brief-common.md`.

**Achtung Stash:** `git stash list` hat `stash@{1}` „new feature for resoure-loader“ (vom Besitzer, alt, auf master) und `stash@{0}` „ACCIDENTAL …“ (versehentlich angewendete Kopie davon, mit Konfliktmarkern). Nichts verworfen; die Skripte nutzen keinen Stash mehr. Besitzer entscheidet über das Löschen.

**Danach:** #99 restliche Umbenennungen je Feature-Branch (Reihenfolge in `scratch/issues/99/review.md`), #73 Formatklassen (nach #93), #85/#86 (PCK-Inspector/Exporter), #90/#91 (Apple-Bundles, Pack-Modus), #76, #79/#80, #71 (Branch 15), #121 (Import-Absturz), #122 (Bus-Layout, Branch 16), Settings-Domänen #18, #20–#30.

**Stand (2026-11-25, Nacht, autonom):** Gemergt: #227 Untertitel (`Subtitle`, `SubtitleSegment`, `SubtitleValidation`, `SubtitleServer`, SRT, Inspector-Tabelle; Tracker #230), #228 TileSet `coordinate_axes` (+ CI-Fix #246: `-Werror=range-loop-construct` bei GCC/Clang, MSVC warnt nicht), #233 Untertitel-Formate (VTT, TTML/DFXP, ASS/SSA, SBV; `dialogue` in der codespell-Ignore-Liste, weil codespell das ASS-Schlüsselwort umschreibt), #235 Cached-Resource-Zähler/-Arten (auf Branch 37 wie gewünscht), #237 `ScreenshotServer`, #239 C# `GetChildren<T>()`/`FindChildren<T>()`, #244/#245 Release: Windows-Apphost von OpenVisualStudio + `.github/truely/verify-release.py` vor „Create release“. Konzepte entschieden: #241 (keine `*File`-Klassen), #242 (ViewportServer über `SceneTree.node_added`, Feature später), #229 SQLite (Amalgamation + VFS auf FileAccess, Feature später). **Offen:** #243 Umbenennung der Video-Settings (WindowSettings, RenderQualitySettings, …), Feature-Issue ViewportServer, Feature-Issue SQLite-Modul.
**Werkzeuge neu (Scratchpad dieser Session, nicht im Repo):** Wiki-Klassenseiten aus der Doku-XML (`classref.py`, mit `ONLY=`-Filter für Ergänzungen an Godot-Klassen), Editor-Screenshot-Plugin (Inspector-Ausschnitt, 2D-Viewport mit Overlay). Testprojekt: `checks/use-engine.ps1` nimmt das neueste SDK-Paket; `run-checks.ps1` ruft es auf.
**Fallen neu:** In GDScript-Checks `var x := … + untypisiert` → Parse-Fehler, die Check-Szene beendet sich dann nie (Lauf hängt). Tests mit Nodes, die Server brauchen (TileMapLayer), brauchen `[SceneTree]` im Testnamen. C#-Wrapper sind geteilt: Ressourcen aus Engine-Arrays nicht `Dispose()`en, sondern das Array leeren.
**Stand (2026-11-26, Resume):** CI rot nur Minimal-Template (ScreenshotServer-Test jpg/webp) → Fix gemergt (`f031fd0944`, PR #250, Branch 237). Konzepte #242/#229 geschlossen, Features #251 ViewportServer (Teil 2: Settings-Ziel nach #243), #252 SQLite-Modul. Agenten laufen: #243 (Umbenennung als Kette 20→25, engine+Testprojekt), #251, #252. Arbeitsordner kurz: `P:/wt/<N>/` (Scratchpad-Pfade zu lang für Worktrees), Brief `P:/wt/brief-common.md`. Helfer `integrate.sh`/`merge.sh` im alten Scratchpad `…/e5b04775-…/scratchpad/`.
**Stand (2026-11-26, vor Neustart des Besitzers):**
- Neue Issues #253–#267 (Brainstorming): FlowGraph-Tracker #253 (#254 Konzept, #255–#259), #260 SubtitlePlayer, #261 TileSet-Bools (Branch `228-feature-tile-coordinate-axes`; Wiki: je Kombination ein Bild Inspector links + Grid rechts, X rot, Y grün, Tabelle nebeneinander), #262 Wegfindung-Konzept, #263 ProgressBar-Konzept, #264 Popup-Bug, #265 Versionierung, #266 Build-Guide, #267 Glossar.
- **#254** entschieden, gepostet, geschlossen; Hinweise in #255–#259. **#268** neu: INI/CSV/XML/TOML/YAML als Resource wie JSON (#241 wieder offen, Entscheidung C: Format-Klassen = Resource + statisch wie JSON, INI↔ConfigFile; schließen mit #268). Umsetzen nach dem Neustart.
- **#251** Teil 1 gemergt (`bc995b12d0`), Issue wieder offen für Teil 2 + Wiki-Seiten (Entwürfe `P:/wt/issues/251/wiki/`). Testprojekt-Hauptbaum stand auf `use-engine-packages` → merge.sh vorher `git switch main` im Testprojekt prüfen! uidfix jetzt `P:/wt/tools/uidfix.py` (merge.sh nutzt `$SCR/tools`).
- **#252** fertig (Branch `252-feature-sqlite-module`, `e2ff5b190c` + `caf91836b7`, Test `8490642` in `P:/wt/252/testproject`): bauen nach #251. Wiki-Entwurf `P:/wt/issues/252/wiki/` (Guide in `SQLite.md` umbenennen).
- **#243** fertig: Engine 20 `19fd95cd1e` … 25 `b50aba8c52` (Kette, 25 enthält alles; nur 25 bauen, dann 20→25 nacheinander squash-mergen). Testprojekt-Commits auf neuen Branches `243-tp-2x-*` (`P:/wt/243/tp-2x`): die echten Test-Branches `2x-feature-video-*` darauf vorspulen (alte Worktrees im Scratchpad `…/e5b04775…/scratchpad/issues/2x/testproject` entfernen). Wiki-Entwürfe `P:/wt/243/wiki/` (+ `redirects.txt`, `stubs/`). @GlobalScope.xml-Konflikt mit #251 erwartet.
- **#261** fertig, nicht gebaut: Engine Branch `228-feature-tile-coordinate-axes` (`d3909d70a0` Merge, `f9d90d581a` Refactor; Bools `swap_x_and_y_axis`/`flip_x`/`flip_y`, altes `coordinate_axes` wird gelesen). Test: `git -C P:/Projects/GitHub/godot-custom-build-project-228 merge --ff-only 8283c0e`. Wiki-Entwurf `P:/wt/261/wiki` (`ae84501`, 4x2-Tabelle; `insp-tileset-coordinate-axes.png` löschen). Screenshots nach Build: `editor-shots/run.ps1 -Job P:/wt/261/testproject/godot-project/228-feature-tile-coordinate-axes/wiki_shots/editor_shots_job.json`, dann `coordinate_shots.tscn` mit Fenster (Details `P:/wt/issues/261/knowledge.md`). Risiken: doctest-Ausgabe des Structs `CoordinateSystem`, C#-Namen.
- Wiki (live, `29000b2`): neu gruppiert (ResourceLoader/Saver support, Random, Performance, Sprite Sheet unter 2D, Resource Database unter Resource loading). Offen (Besitzer: „ja gerne“, selbst analysieren): Process Memory → Performance; Gliederung insgesamt prüfen, auch Platz für Text-Formate (Konzept #241: keine `*File`-Klassen, Entscheidung B).
- CI nach Fix #250 auf `truely-godot-engine` prüfen.
- **#270** Konzept: `DataSchema`/`DataField`, `DataValidation`, CSV im Text-Editor (+Highlighter, Live-Fehler), CSV-Tabellen-Editor, Übersetzungs-Schema (nur String-Spalten). Baut auf #268 (API pro Objekt + statisch wie ConfigFile/JSON, Kommentar in #268).
- **To-Do (Besitzer, 2026-11-26): Multi-Agent-Review der Konzepte** #254 (FlowGraph, schon entschieden → gegenprüfen), #241/#268 (Format-Klassen als Resource + API), #270 (Schemas/Validierung/Editor), #262 (Tile-Wegfindung), #263 (ProgressBar). Je Konzept mehrere unabhängige Reviewer (z. B. API-Design / Godot-Präzedenz im Code / Plattformen+Web+Performance / Editor-UX), dann Konsens: was fehlt, finale API, Task-Plan (Implementierungsschritte je Feature-Issue, Reihenfolge, Abhängigkeiten). Ergebnis als „Decision“ + Task-Plan ins Issue, Feature-Issues anpassen/anlegen. Erst nach dem Neustart, Besitzer hat Multi-Agent ausdrücklich gewünscht.
- Besitzer: **keine neuen Agenten bis nach dem Neustart.**

**Stand (2026-11-26, Abend):** Gemergt: #251-Testfix (Sanitizer), #252 SQLite (+ step()/OPEN_MODE_CREATE-Fix), #273 CI-Trigger, #243 Kette 20→25, #261 TileSet-Bools (Wiki mit 8 Bildern). Konzept-Review (4 Reviewer + Konsens, Dateien `P:/wt/review/`): Decisions in #254/#268/#241/#270/#262/#263, Bodies aktualisiert, neue Issues #276–#285; dazu #272 Ordnerfarben, #292 SQLite-Schema-API + Inspector (Branch 252), #293 SQLite-Tabelleneditor. Gemergt seit letzter Pipeline: #251fix, #252, #243, #261 (4).
**Werkzeuge:** editor-shots-Tool im alten Scratchpad `…/e5b04775…/scratchpad/tools/editor-shots` braucht nach jedem Build die aktuelle Exe + GodotSharp in `godot/` (sonst alte Klassen). `P:/wt/243/chain.sh` (Merge-Kette mit `-X ours`, @GlobalScope-Fix).


**Stand (2026-11-27, Nacht, autonom, Usage-Limit erreicht):** Gemergt: #267 Glossar, #264 Popup, #281 CSV, #272 Ordnerfarben, #265 Truely-Version 0.1.0, #266 BUILDING.md, #260 SubtitlePlayer, #268 TextDataFormat (+Follow-ups), #241 zu, #255 FlowGraph, #292/#293 SQLite, #251 Teil 2, #285 (+#284 zu), #282, #283, #306 Icons (neu). Wiki aktuell bis #283 (nicht: 306).
- **CI** Lauf 45760683614 (auf `truely-godot-engine`, kein Release) rot: C# CS0108 `FlowPort/FlowVariable.GetType()`. Fix auf Branch 255 (`8b5d403e40`, set/get_port_type, set/get_variable_type), noch NICHT gemergt.
- **Nächster Schritt:** Branch `276-feature-flow-graph-debugger` (enthält 255-Fix, 256, 257, 276) ist gebaut: Tests grün, Checks 255/256/276 grün; offen: doctool-Diff `FlowGraphPlayer.xml` committen, C#-Warnung CS1570 in `FlowExit` (ungültiges XML in Doku-Kommentar, BBCode in `FlowExit.xml` prüfen) beheben. Dann mergen in Reihenfolge: 255 (Fix-PR „Rename FlowPort and FlowVariable type accessors (#255)“), 256, 257, 276 (Konflikte: `git checkout --ours` + `P:/wt/tools/glossary_merge.py`). Danach 258, 259 bauen/mergen, Wiki-Seiten Class-FlowPort/FlowVariable anpassen, CI erneut (`gh workflow run runner.yml --ref truely-godot-engine`).
- **Kette 277→278→279/280:** Agent führt `origin/truely-godot-engine` in 277 hinein und 277 in 278/279 (lokale Commits). Danach bauen/mergen 277, 278, 279+280 (Agent-Berichte in `P:/wt/issues/<N>/knowledge.md`).
- **Offen danach:** #12/#8 manuelle Prüfungen (Besitzer), #212 Screenshots, Release `Truely-v0.1.0` per `truely/release.ps1` erst wenn alles durch + CI grün; Release-Seite/Changelog aktualisieren, alte Release-Dateien entfernen.
- **Werkzeuge:** `P:/wt/integrate.sh` (zählt jetzt C#-Glue-Warnungen, löst GLOSSARY-Konflikte), `P:/wt/merge.sh` (holt vor dem Push `origin/truely-godot-engine` herein, wartet auf MERGEABLE, kommentiert nur einmal), To-dos `P:/wt/todo.txt`, Wiki-Queue `P:/wt/wiki-queue.txt`. Brief `P:/wt/brief-common.md` (neue Regeln: wenig Code, C#-Namenskonflikte).


**Stand (2026-11-27, Vormittag):** Zusätzlich gemergt: #255-Rename (C# GetType), #256, #257 (+CI-Fix float/enum), #276, #259, #258, #277, #278, #279+#280, Nacharbeiten #283 (Warnung einmal), #260 (Subtitle leitet Segment-`changed` weiter). Geschlossen: Tracker #253, Konzepte #262/#263/#270. Neu: #322–#325 (Untertitel-Tracker #230, nach Release 0.1.0). Wiki: alle Guides bis #280, alle `Class-*`-Seiten aus der XML neu erzeugt (`P:/wt/tools/classref_md.py`). CI-Lauf 45794131388 auf `2d62e3fa17`.
- **Nächster Schritt:** CI grün → `truely/release.ps1` (Release `Truely-v0.1.0`), danach Release-Notes prüfen/ergänzen, alten Entwurf und Assets des alten Pre-Releases `truely-godot4.8-beta5-3bd081f` entfernen (Notes auf neues Release verweisen). #212 Screenshots per Agent (editor-shots-Tool im alten Scratchpad).


**Stand (2026-11-27, Nachmittag):** Alle Issues der Nacht gemergt, zuletzt #277–#280, #332 (YAML im Editor mit Leerzeichen), CI-Fixes (float/enum, signed/unsigned, SCU-Testordner). CI auf `truely-godot-engine` grün (`fff0779c6e`). Release v0.1.0 gestartet: `truely-godot-engine-releases` = `995b042db8`, CI dort grün; Release-Workflow 45826619814 hängt an macOS-Runner-Kapazität (Jobs werden nicht vergeben) → Wiederholungen laufen. Wiki: alle Guides überarbeitet (Stil-Brief `P:/wt/brief-wiki-style.md`, Vorbild Audio-Bus-Settings), Text-Format-Guides neu mit Screenshots (Editor + Inspector), Data-Schemas neu, Class-Seiten aus XML (`P:/wt/tools/classref_md.py`), 36 Screenshots neu/ersetzt.
- **Offen:** Release veröffentlichen (bei Erfolg: alten Entwurf `truely-godot4.8-beta5-3bd081f` löschen, beim alten Pre-Release die Assets entfernen und auf v0.1.0 verweisen); danach `truely/version.txt` erst beim nächsten Release erhöhen. Untertitel #322–#325, #12/#8 (Besitzer am Bildschirm), 9 Screenshots ohne Werkzeug-Schritt (Laufzeit-Bilder, Menüs).


**Release v0.1.0 veröffentlicht (2026-11-27):** auf dem bisherigen Post (alter Tag `truely-godot4.8-beta5-3bd081f` → `truely-v0.1.0-godot4.8-beta5`), 7 Dateien, alter Entwurf gelöscht. macOS (.NET) fehlt (Runner-Kapazität): Hintergrund-Wiederholung des Jobs in Lauf 45826619814, danach `gh workflow run truely_release_update.yml -f run_id=45826619814 -f partial=false`. Neu: #334 Release-Update-Workflow, #337 Namensschema + „dotnet“ statt „mono“ (bei Gelegenheit).

## Übergabe

**Warten auf den Besitzer:** Concepts #65 (PCK als Ressource + Editor-Importer/Extract) und #66 (Streaming Assets): Empfehlungen im Issue, Umsetzung erst nach Zustimmung.

**Entschieden:** #4 und #6 (Kommentare „Decision“ in den Issues): #2 mit Copy-on-write-Snapshot, #34 Endungs-Index darin (umgesetzt; Skript-/Binary-Loader in #53), #5 JSON/INI/CSV im Core plus Plain-INI-Modus und XML-Writer, `.json` über Typ-Marker mit Durchfallen zum alten Loader, TOML als Modul (#36), YAML vorerst nicht.

**Anhänge-Dateien:** `core/io/resource_loader.*`, `resource_saver.*`, `core/core_bind.*`, `core/register_core_types.cpp`: parallel nur mit wenigen Zeilen an anderer Stelle.

**Fragen an den Besitzer:** –

**Am Bildschirm zu prüfen (manual):** #10: Check „Haupt-PCK eines exportierten Builds“ nur im Export. #8: Export-Build mit `user://`/`exec://`-Ordnern, `res://` schreibgeschützt; danach #8 schließen. #12: `TtsServer.speak()` mit echten Stimmen unter Windows (GDScript und C#) und Fallback auf eine Stimme gleicher Sprache; dazu ohne `--headless` mit `audio/general/text_to_speech` an (Checks im Testprojekt `res://12-feature-tts-server/` machen das automatisch, wenn TTS verfügbar ist). Danach #12 schließen.

**Regeln, neu:** –

**Fallen:**
- Testprojekt-Worktrees brauchen die Junction `scratch/issues/<N>/nuget-local` → sonst „Godot.NET.Sdk nicht gefunden“.
- `.tres` sortiert Dictionary-Schlüssel: Reihenfolge separat speichern (#50 `region_order`).
- doctest `const Array a = { { Variant(x) } }` wird flach: `push_back(Array({ ... }))` verwenden (#15).
- Erster `--import` des Testprojekts nach dem Merge von #5 stürzte einmal ab (Access Violation, Exit -1073741819), danach nicht reproduzierbar. Beobachten.
- Agent-Worktrees belegen ihren Branch: vor `git switch <branch>` im Hauptbaum `git -C .claude/worktrees/<agent> switch --detach`. Testprojekt-Worktrees im Scratch brauchen die Junction `scratch/issues/<N>/nuget-local` → `P:/Projects/GitHub/nuget-local` (relativer Pfad in `nuget.config`).
- **`run-checks.ps1` mit der GUI-Exe meldete immer PASSED** (PowerShell wartet nicht, kein Exit-Code; GD und C# liefen gleichzeitig). Seit Testprojekt `95830a0` nimmt das Skript die `.console.exe` daneben; Branches ohne diesen Fix: `-Godot …mono.console.exe` direkt übergeben. Stapel 1 wurde damit erneut geprüft: grün.
- C#-Checks: den von `GetCachedRef()` u. Ä. gelieferten Wrapper nicht `using`/`Dispose()` – er ist mit gehaltenen Referenzen geteilt und gibt die Ressource frei.
- Batch-CI 1 scheiterte nur an `--doctool`: `>` in Codebeispielen der XML muss `&gt;` sein (#48). Vor jedem Stapel `--doctool` auf `truely-godot-engine` prüfen; Agenten-Aussagen „kein Diff“ beziehen sich nur auf ihren Branch.
- Auf diesem Rechner fehlen die Abhängigkeiten für AccessKit und D3D12: lokal immer `accesskit=no d3d12=no` (die CI baut mit allem). Die SCons-Meldung „ANGLE deps missing“ ist keine Warnung.
- `build.ps1` hatte positionelle Parameter (`module_mono_enabled=yes` landete in `-Target`); seit 2026-11-23 behoben.
- Agenten in Worktrees dürfen nicht nach `.agents/state/` im Hauptbaum schreiben: `knowledge.md` im Scratchpad unter `issues/<N>/`, der Orchestrator kopiert sie.
- Testprojekt: Branches brauchen `git -c core.longpaths=true`; `nuget-local` enthält immer die Pakete des zuletzt gebauten Branches, vor C#-Checks auf dem eigenen Branch neu bauen.
- `P:/Projects/GitHub/nuget-local` fehlte am 2026-11-23: der erste volle Mono-Build (Glue + Assemblies) legt ihn an.
- Der vorhandene Build in `bin/` ist ein `dev`-Build ohne SCons-Cache; der erste Build mit Cache füllt `P:/scons-cache`.
- Alter Branch `origin/feature-resource-paths-retrieval` (ohne Nummer) ist ein Vorläufer von `01-feature-resource-paths-retrieval`; nicht löschen.
- Auf P: waren am 2026-11-23 nur 115 GB frei; höchstens ein Engine-Build gleichzeitig.

**Wache und Eingriffe:** –

**Stand (2026-11-28, Nacht, autonom):** Für 0.2.0 gemergt: #357 (Teil 1+2), #344, #345, #354, #361 (+Follow-ups), #362 (+Zeit-Helfer, Track-Namen), #346, #365 SystemFont, #367 (typisierte Virtuals **zurückgenommen** auf Wunsch des Besitzers: keine Signatur-Änderungen an Godot-APIs, nur Doku), #363 (Felder direkt am AudioStream, Format-Zeile im Inspector statt Properties), #322 (subtitle_path, SubtitlePlayer folgt dem Stream, „Add SubtitlePlayer“, Signale playing_changed/stream_changed), #375 (CI: GCC range-for, AudioServer::finish-Leak, Tests ohne YAML/Ogg), #337 (Release-Dateien „dotnet“, Schema `<platform>-<arch>[_dotnet]`; Engine-interne `.mono` bleibt), #334 (Release-Workflow aktualisiert vorhandenen Post). Volle CI grün auf `e3cb20d4cc`. Release 0.2.0: `truely-godot-engine-releases` = `61d68d72e6`; der alte 0.1.0-Post wird auf `truely-v0.2.0-godot4.8-beta5` verschoben, sobald der Release-Workflow seine Prüfung hinter sich hat (vorher verschieben = „already released“ → Skip). Patch Notes: `P:/wt/rel-body.md` (auf dem Post). Wiki: alle neuen Guides hands-on, Localized-Text + Locale-Channels neu.
- **Aufgeräumt:** 117 Agent-Worktrees entfernt (Branches bleiben), alte Binaries in `bin/`.
- **Nach 0.2.0:** #379 Timer (Design offen: volle API am `Timer` oder Uhren + Endzeitpunkte + Heap), #323–#325 Untertitel, #12/#8 manuelle Prüfungen des Besitzers.

### Stand (2026-11-27, morgens): 0.2.0 released

- Release post `truely-v0.2.0-godot4.8-beta5` (the former 0.1.0 post, moved to the new tag) has all 8 builds of `61d68d7` (Windows, Linux, macOS, each standard + dotnet, both template packs) and the curated notes; full CI and "Truely Release" green.
- The release run created a second post because the old post was not moved in time; it was merged into the old post (assets re-uploaded, duplicate deleted, tag kept). Lesson: the `workflow_run` of `truely_release.yml` reports the default-branch SHA as `headSha`, not the release SHA; watch for any new run after the CI on `truely-godot-engine-releases`, then move the post right after its "Names" job.
- Next: #379 Timer (design open, after 0.2.0), #323–#325, #12/#8.

### Stand (2026-11-27, Abend, Session-Limit 96 %)

- **Offene Issues bereinigt:** #339 geschlossen (alle Kinder erledigt); #212 Status-Kommentar; #32/#230/#8/#12 bleiben offen (manuell bzw. Kinder offen).
- **#384 Testprojekt (Ordner je Wiki-Guide, Example + Test):** Sammel-Branch im Testprojekt `384-task-guide-folders` (Worktree `P:/wt/384/tp`), gemergt: misc, loc, resload, loaders, text, settings1, files, settings2, flow2d. **Offen:** Gruppe audio (Agent, Branch `384-audio`, Worktree `P:/wt/384/audio`) → mergen (Konflikte bei alten `.import` in 102/103/104/199/196: Seite des Gruppen-Branches nehmen, alte Ordner löschen). Danach: `run-checks.ps1` über alle Ordner, alte Basis `checks/scripts/*/FeatureCheck*` + `feature_check.gd` löschen, README-Tabelle Guide ↔ alte Ordner, PR nach `main` (Testprojekt). Engine-Doku auf Branch `384-task-test-project-guide-folders` (`c97284b0cf`) → PR/Squash. Wiki: Snippet-Korrekturen aus `P:/wt/384/findings.md` + Testprojekt-Links (7 Seiten) auf neue Ordner. Bugs daraus: #385, #386, #387, #388.
- **0.2.1 UI/UX-Playtest (Tracker #389):** Issues #390–#398 (Besitzer), #400–#404 (Playtest). Fix-Branch `389-bug-editor-playtest-fixes`: #400, #396 (+ right-disconnect, deselect) gebaut und per Editor-Skript verifiziert (`5301fd544`). Laufend: Fix-Agent formats (Worktree `P:/wt/fix/formats`, Branch `389-fix-formats`: #393 Freeze, #395, #394, #392, #402, #387), Fix-Agent flow (#401 Rest, Branch `389-fix-flow`). Playtests fertig: formats, flow, loc, input; laufend: data, media2d. Harnesses `P:/wt/pt/<area>/run.ps1 -Scenario <name>`; Briefs `P:/wt/pt/brief.md`, `P:/wt/pt/fix-brief.md`.
- **Noch ohne Fix-Agent:** #403 (Loc-Editor), #391 (Locale-Picker + UX-Liste), #398 (InputAction null-Slots), #404 (Settings-Editor), #390 (SQLite-Endungen), #397 (Feature), Funde aus data/media2d.
- **Ablauf je Fix:** Agent ohne Build → Orchestrator merged in `389-bug-editor-playtest-fixes`, baut (Exe-Sperre: Testprojekt-Läufe halten `bin/*.exe`, vorher prüfen), Harness-Szenario `fix_<n>` ausführen, Kommentar ins Issue. Release 0.2.1 erst nach finalem Playtest aller Bereiche + CI.
- **0.3.0 Konzepte:** #399 TransitionScreen/SplashScreen/LoadingScreen, #405 Mask2D, #406 Terrains, #407 GridMap. Noch anlegen: Analyse godot-proposals #15808, #15806, #8766 (Agent gestoppt); `DisplayServer.get_resolutions(16, 9)` + `Resolution`-Liste (Besitzer-Idee; 16:9 = 640x360, 960x540, 1280x720, …; Überschneidung mit WindowSettings prüfen).
- **Usage-Limit (2026-11-27, spät):** alle Agenten gestoppt, Arbeit liegt unfertig in den Worktrees (nicht geprüft, nicht gemergt): Testprojekt Audio `P:/wt/384/audio` (bis Subtitles), Fix formats `P:/wt/fix/formats` (#393/#395 begonnen, #394 offen), Fix flow (#401, Mermaid-Layout in Arbeit; Worktree unter `P:/wt/fix/`), Playtest media2d `P:/wt/pt/media2d` (nur Projekt-Setup). Neu: #409 (Daten-Tools), #390 erweitert. Vor dem Weitermachen: Diffs der Worktrees selbst prüfen.
- **Regel neu (Besitzer, 2026-11-27):** Bugs/Iterationen eigener Features als Kommentar ins Feature-Issue (wieder öffnen), Fix auf dem Feature-Branch; eigenes Issue nur ohne Feature-Issue, bei mehreren Features, Upstream-Code oder neuer Funktion.
- **Godot 4.8 RC1** erschienen: nach der Fix-Welle #389 `sync-upstream.ps1` → `truely-godot-engine`, `truely/godot-status.txt` = rc1, Build + alle Tests + Playtest; 0.2.1 auf RC1.

### Stand (2026-11-27, Nacht) — Übergabe für die nächste Session

- **#384 erledigt:** Testprojekt PR #15 gemergt (89 Guide-Ordner, 316/320 Szenen grün; rot nur #385/#388, beide auf dem Fix-Branch behoben), Engine-Doku PR #415 gemergt, Wiki gepusht (Snippets + Links, `2f881e5`).
- **Fix-Branch `389-bug-editor-playtest-fixes` (gepusht):** alle Fix-Gruppen gemergt (formats, flow, input, data, media, loc) + Kompilier-/Warnungs-Fixes. Build 0 Fehler/0 Warnungen, **2151/2151 Unit-Tests grün**. Editor-Szenarien am Build verifiziert: formats (6), input (2), data (fix_388, fix_390, fix_409 Schritte 1-6), flow fix_401 (alles bis auf 1 px Frame-Versatz). **Noch nicht am Build gelaufen:** media `fix_410..413` (P:/wt/pt/media2d), loc `fix_403/391/385/386` (P:/wt/pt/loc), data fix_409 Schritte 7-10. Harnesses zeigen in run.ps1 noch auf die 0.2.0-Exe → Pfad auf `bin/godot.windows.editor.x86_64.mono.console.exe` setzen.
- **Offen:** #416 (Editorstart: add_image-Fehler, Default-Value-Warnungen von FlowBlackboard/TileMapPathGraphOptions, 1 px Frame, fix_409 Rest), #391 Punkt `default_values` (Typed Array? Besitzer), #403 Punkt 8 teilweise (Text-Katalog außerhalb Manifest öffnet als Daten), #412 Panel-Höhe, #397 Feature, #414 Feature.
- **Danach:** Testprojekt-Gesamtlauf mit neuem Build (Glue + Assemblies vorher!), PR des Fix-Branches → Squash-Merge, Issues #385–#413 mit Kommentar schließen (und im jeweiligen Feature-Issue verlinken, neue Regel), dann **Godot 4.8 RC1 sync** (`truely/sync-upstream.ps1`, `truely/godot-status.txt` = rc1), Build, alle Tests, **finaler Playtest aller 6 Bereiche** (Briefs `P:/wt/pt/brief.md`), CI, Release 0.2.1.
- **0.3.0-Ideen offen anzulegen:** Proposals 15808, 15806, 8766 analysieren; `DisplayServer.get_resolutions(16, 9)` + `Resolution`-Liste.
- Entschieden: #391 default_values bleibt Dateipfad (*.tres,*.res), kein Typed Array.

### Stand (2026-11-27, Resume)

- Fix-Branch `389-bug-editor-playtest-fixes` = `1dc24a8df4` (gepusht): #416 (PROPERTY_USAGE_NONE für `FlowGraphPlayback.blackboard`/`TileMapPathGraph.options`, Frame vor dem Speichern einpassen), #403 (Manifeste ohne Neustart, Save As `.lcat` zuerst; Branch `389-fix-loc2`). Build 0/0, 2151/2151 Unit-Tests.
- Am Build verifiziert: media fix_410–413, loc fix_385/386/403 (26/26)/391 (20/21; Drop auf Ressourcen-Zelle von Hand, `P:/wt/pt/queue.txt`), data fix_409 1–10, flow fix_401. `add_image`-Fehler = Upstream (leere `[preset.0.options]` im Harness).
- Testprojekt-Gesamtlauf: 198 Szenen grün, 10 rot (155 YAML, 36 TOML, 268 INI, 341 Katalog, 172 Input-Layer) → Agent analysiert (Branches `389-fix-regress` Engine / `389-checks-update` Testprojekt).
- Danach: Rebuild, Gesamtlauf grün → PR Fix-Branch → Squash-Merge, Issues #385–#416 schließen (Feature-Issues verlinken), RC1-Sync, finaler Playtest, CI, Release 0.2.1.
- **Update (später):** PR #417 gemergt (`e488f51e26`), 22 Bug-Issues zu. Testprojekt PR #17 gemergt (Guide-Ordner, `class_name DatabaseItem`), Gesamtlauf 320/320 grün. 134 alte Testprojekt-Worktrees entfernt (Branches bleiben; Patch `P:/wt/logs/tmp-c91-uncommitted.patch`). Testprojekt-Hauptbaum steht auf altem `main` mit lokalen Änderungen (`project.godot`, csproj) des Besitzers → nicht anfassen; Läufe im Worktree `P:/wt/389/tp`. Von Hand im finalen Playtest: `P:/wt/pt/queue.txt` (#391 Drop, #412 Panelhöhe). Nächstes: RC1-Sync.

### Stand (2026-11-28, Nacht) — 0.2.1 im Release
- **RC1-Sync** (`e5c9391411`, Konflikt `tests/display_server_mock.h` gelöst), `godot-status.txt` = rc1.
- **Finaler Playtest aller 6 Bereiche** auf rc1: keine Regressionen. Neue Funde als Kommentar in die Feature-Issues (#257 #276 #278 #279 #346 #361 #85 #88 #391 #412), Fixes auf den Feature-Branches → Integrations-Branch `389-final-fixes` → PR #418 gemergt (`1491e273ce`). Build 0/0, 2155/2155, Testprojekt 320/320, `fix_final` je Bereich grün.
- **Release 0.2.1** (`truely/version.txt` = 0.2.1, `104c299c88`): `release.ps1` vom Besitzer freigegeben, CI auf `truely-godot-engine-releases` läuft (Run 46080664914), danach „Truely Release“ → Tag `truely-v0.2.1-godot4.8-rc1`. Danach Release-Notes prüfen.
- **Wiki** (gepusht, `6b65c0b`): Guides aller Bereiche hands-on mit Testprojekt-Ordnern, Screenshots aus Playtests/Szenarien (`wiki_*` in `P:/wt/pt/<area>`; Flow-Shots: `bash P:/wt/pt/flow/run_wiki_all.sh`). Brief `P:/wt/brief-wiki-021.md`.
- **Offen (klein):** Testprojekt: `flow-graphs/resources/flow_graph.tres` + `debug_dialogue.tres` Nodes 220→300 px; `units.csv` importer keep→csv_dataset + Kommentare in csv/data-schemas Example.gd; Wiki-Codepfade an Testprojekt angleichen; `insp-stream-subtitle-path.png` zeigt Harness-Pfad. Nicht behoben (kosmetisch): ImageTexture Save As png/webp doppelt, leerer Opus-Stream Preview-Fehler, eigene Settings-Domänen ohne Editor-Gruppe. Von Hand: Datei auf AudioStreamPlaylist-Zeile ziehen. Falle neu: Agenten schreiben `\n` in C++-Strings als echten Umbruch → `P:/wt/tools/find_broken_strings.py`.
- **Danach:** #397, #414 (Features), 0.3.0-Konzepte #399 #405–#407.
- **0.2.1 veröffentlicht (2026-11-28):** Post `truely-v0.2.1-godot4.8-rc1` (aus dem 0.2.0-Post verschoben), 8 Dateien von `f5b22b5`, Text `P:/wt/rel-body-021-final.md` (neuer AI-Disclaimer). Release-Workflow von Hand gestartet (Run 46088380685); Update-Workflow hat jetzt eigene Concurrency-Gruppe. Neu: `truely/PIPELINE-CONVENTIONS.md` + `truely/pipeline_lint.py` (pre-commit), `truely/ARCHITECTURE.md`, Versionsanzeige `Truely v0.2 (Godot v4.8.rc1.mono)`.
- **Offen:** #419 alle Plattformen (Branch `419-feature-release-all-platforms`, Dry-Run 46098649468 → Smoke-Test Android/Web-Export → Merge → Update-Workflow für 0.2.1). Besitzer-Funde: #346 (Katalog-CSV `null`, Save ohne Feedback, Lücken erlauben, horizontales Scrollen) und #361 (INI im Inspector friert ein), Fix-Agenten laufen (Worktrees `P:/wt/fix/loc3`, `P:/wt/fix/ini361`).

### Übergabe (2026-11-28, früh) — Nacht-Auftrag 0.3.0 (Tracker #422)

**Auftrag des Besitzers (Nacht, eigenständig, FullAutonomous):** Version 0.3.0 bauen, nur eigene Features (keine Godot-PR-/Proposal-Features: #405–#407 raus). Alles steht in **Tracker #422**; dort die Reihenfolge abhaken.
1. **Text-Formate (#421):** schlaues Konzept für JSON/CSV/INI/XML/YAML/TOML: je Format eine eigene Klasse direkt von `Resource` (wie `JSON`), eigene API, eigener Inspector, ggf. eigenes Dock; statt gemeinsamer Basisklasse (`TextDataFormat`, #268). Erst Konzept + Decision im Issue, dann Feature-Issues.
2. **Untertitel (#325):** Bearbeitung aus dem Inspector in ein eigenes „Subtitles“-Dock; Inspector = schneller Weg, Dock = volle UI/UX.
3. **Docks (#420):** Dataset- und Database-Dock statt CSV-Table-Dock (Namen offen: „Dataset“/„Database“ vs. „… Viewer“).
4. **Alle Issues chronologisch** abarbeiten (Liste in #422).
5. **Aufräumen:** nicht mehr benötigte Worktrees/Dateien regelmäßig löschen (Branches bleiben).
6. **Vor jedem Merge playtesten:** Build, durch Inspector, Editor-Hilfe (Klassenreferenz), Resource-Properties, Docks klicken; Exceptions vor dem Merge finden, nicht erst in der CI (45 min). Harness `P:/wt/pt/<bereich>/` mit Szenario je Feature; Briefs `P:/wt/pt/brief.md`, `brief-final.md`, `fix-brief.md`.
7. **Review (Besitzer):** nach jedem fertigen Feature 1–3 unabhängige Reviewer-Agenten (Datenmodelle, Code-Review, UI/UX-Review); Funde ins Issue, vor dem Merge beheben oder begründen.
8. Release 0.3.0 am Ende: volle CI grün → `truely/version.txt` 0.3.0 → `release.ps1` **erst nach Freigabe des Besitzers im Chat**.

**Zuerst fertig machen (aus 0.2.x):**
- Branch `389-fixes-022` (gepusht): #361 (Absturz Daten-Baum im Inspector, Fix `b649ae9211`, Worktree `P:/wt/fix/ini361`) + #346 (Katalog-CSV, Worktree `P:/wt/fix/loc3`). Build grün (2157/2157). Offen: **Absturz nach Klick auf TOML-Unterzeile** in `fix_361_all` (Log `P:/wt/pt/formats/logs/fix_361_all.*`; Harness oder Engine? Agent lief beim Neustart noch). `fix_361_ini`: Zähler „inputs=0“ im Harness falsch. `fix_346_owner`: 19 PASS, nur Kopfzeilen-Check erwartet `Keys,Context,en` → Kopfzeile bleibt Godot-kompatibel `keys,?context,en` (Besitzer hat nicht widersprochen), Check anpassen. Danach Merge (0.2.2-Inhalt geht in 0.3.0 auf, oder 0.2.2 vorab, wenn der Besitzer es will).
- #419 alle Plattformen: Branch `419-feature-release-all-platforms`, Dry-Run 46098649468: rot Web editor, Linux arm32 editor (.NET); Rest lief noch.
- Inspector-Buttons „Open as Text“/„Open in Table“: Vorschlag (Doppelklick öffnet Text + Tabelle, Buttons weg, Kontextmenü) → in #420 umsetzen.

**Neue Dokumente dieser Session:** `truely/ARCHITECTURE.md` (Klassenarten, Schichten, Server-API, DOD, Code, Editor, Docks; Godot- und Truely-Beispiel je Regel), `truely/PIPELINE-CONVENTIONS.md` + `truely/pipeline_lint.py` (pre-commit-Hook). Fix-Agenten: `P:/wt/tools/find_broken_strings.py` vor jedem Commit (echte Zeilenumbrüche in C++-Strings).
### Stand (2026-11-28, 04:30) — Nacht-Auftrag 0.2.2
- **Besitzer:** Release heißt **0.2.2** (nicht 0.3.0), Tracker #422 umbenannt. Freigegeben: alle eigenen Feature-Issues umsetzen, prüfen, playtesten, lokal bauen, mergen, Release-Pipeline, Release-Post auf 0.2.2, Builds bereitstellen. Qualität vor Pipeline-Tempo. Konventions-Check je Issue (Abschnitt im Issue). Neue Namensregel: kein Container-Typ im Namen (`TtsPersona`/`TtsPersonas`), CONVENTIONS 1.2, Umbenennung #423.
- **Welle 1 (Agenten, ohne Build, Worktrees `.claude/worktrees/`):** #323+#324 (Opus), #379 (Opus), #414 (Sonnet), #397 (Sonnet), #421+#420 Konzept (+0.2.2-Teil, Opus). Briefs `P:/wt/brief-common.md` + `P:/wt/brief-022.md`; Ergebnisse `P:/wt/issues/<N>/` (issue-comment.md, pt_scenario.gd, wiki/).
- `389-fixes-022` + `f38d88d1ee` (#361 ObjectID-Fix) gebaut, Szenarien laufen. Danach: #325, #399, #423, #419 (Dry-Run 46098649468 queued; Web editor + Linux arm32 .NET editor rot, Logs nicht abrufbar).
- **Freigabe (Besitzer, 2026-11-28 ~04:40, für 5–8 Std.):** Commit, Push, Merge, PRs, Builds, CI/CD, Release voll autonom nach den Konventionen.
- **Falle (2026-11-28):** Harness-Logs: Godot-Fehler stehen in `<scenario>.err.txt` (stderr), nicht in `.out.txt` — immer beide prüfen (`grep -c "^ERROR\|SCRIPT ERROR" *.err.txt`). Klassen mit `GDCLASS`, die nicht registriert sind, haben keine Signale. Harness 0.2.2: `P:/wt/pt/p022` (Szenarien `pt_<N>`), Build-Skript `P:/wt/tools/full-build.sh <tag>` (Engine tests=yes, Glue, Assemblies, Tests, Doctool).

### Stand (2026-11-28, ~07:30) — 0.2.2 Integration
- Engine-Integrations-Branch `022-integration` (lokal): #397, #414, #421, #423, #323/#324, #379 (Timer-Server opt-in), #399, #424, #325; int10 grün (2200/2200, Doctool sauber), Playtests `pt_<N>` in `P:/wt/pt/p022` grün (stdout+stderr). #325 nach int10 gemergt (noch nicht gebaut).
- Testprojekt: Guide-Ordner auf Branch `022-guide-checks` (Worktree `P:/wt/022/tp2`, SDK 4.8.0-rc, Junction `P:/wt/022/nuget-local`); Gesamtlauf 331 grün, rot nur `timers` (3 Engine-Funde #379, Fix-Agent).
- Wiki lokal `P:/wt/022/wiki` (`a0c6196`, `c204099`), Push zum Release. Offen: Timer-Messungen (#379), Screenshot Locale-Option-Button.
- #419: Agent fixt arm32 (pip/zstandard), Android digest-mismatch, Web `PAGE_SIZE`; neuer Dry-Run.
- Danach: int11 (alles), Gesamtlauf Testprojekt, PRs je Feature (squash) in Reihenfolge 423 → 421 → 397 → 414 → 323/324 → 325 → 379 → 399 → 424, Testprojekt-PR, CI auf releases, `version.txt` 0.2.2, `release.ps1`, Post aktualisieren.
- **Falle (2026-11-28):** Versions-Commit mit `[skip ci]` → `release.ps1` pusht, aber die CI auf `truely-godot-engine-releases` startet nicht. Versions-Commits ohne `[skip ci]`, sonst `gh workflow run runner.yml --ref truely-godot-engine-releases`.

### Stand (2026-11-28, ~12:00) — 0.2.2 veröffentlicht
- **Release:** `truely-v0.2.2-godot4.8-rc1` auf `a9982b1`, Pre-release, 8 Dateien (Win/Linux/macOS je std + dotnet, beide Template-Pakete), kuratierte Notes `P:/wt/rel-body-022.md`. Volle CI 46124960705 grün, Release-Lauf 46130271820 grün.
- **Offen (Besitzer):** alter Post `truely-v0.2.1-godot4.8-rc1` steht noch (Löschen per Auto-Mode verweigert). Notes sagen „replaces 0.2.1“ → Post löschen (Tag behalten) oder stehen lassen. Archiv der 0.2.1-Notes: `gh release view truely-v0.2.1-godot4.8-rc1`.
- **CI-Fallen:** `cxxflags=-fno-sanitize=…` wirkt nicht (detect.py hängt Sanitizer-Flags danach an) → Sanitizer-Job mit `optimize=debug` (#449). TSan-Race beim Lade-Fortschritt (Upstream) unterdrückt. Einzelner Workflow testbar: `gh workflow run linux_builds.yml --ref <branch>`.
- **#419:** Agent `ab1d0c15aa0d36a3b` macht Dry-Run + Smoke-Check; danach Merge und Zusatz-Builds per `truely_release_update.yml` an den 0.2.2-Post.
