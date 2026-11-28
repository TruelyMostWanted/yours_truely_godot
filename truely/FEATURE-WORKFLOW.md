# Feature-Workflow der Truely-Engine

Wie ein Feature in diese Engine-Fork kommt: vom Issue über Branch, Build und Test bis zum Merge.
Gilt für jedes Feature- und Refactoring-Issue. Branches und Remotes stehen in [`README.md`](README.md).

> Dieses Dokument soll später zu einem Claude-Skill werden. Es ist deshalb in festen Schritten und Prüflisten geschrieben.

---

## 1. Grundregeln

Die Regeln für Code und Issues stehen in [`CONVENTIONS.md`](CONVENTIONS.md). Kurz:
- Jeder Teil eines Features hat die passende Klassenart (Resource, Playback, Node, Server, Registry, Editor), gebunden in `_bind_methods()`; siehe [`ARCHITECTURE.md`](ARCHITECTURE.md).
- C# bekommt die API über den generierten Glue: `TTSServer` (statisch) und `TTSServerInstance`.
- Jedes Feature hat Klassenreferenz, Wiki-Seite und Checks in GDScript und C# (Mono-Build).
- Issues auf Englisch, nach Vorlage, im Godot-Proposal-Stil (Problem, Vorschlag, Umsetzungsplan; `CONVENTIONS.md` 3) und den fünf Abnahmepunkten.

---

## 2. Ablauf pro Issue

```
Issue (Englisch, Vorlage)
  └─► Feature-Branch von truely-godot-engine
        ├─► Implementieren + beliebig oft committen
        ├─► Mono-Build (Editor + Glue + Assemblies)
        ├─► Testprojekt: Ordner res://<feature>/ mit GDScript- und C#-Test
        ├─► Push (nur WIP oder fertig)  ──►  GitHub Actions abwarten
        └─► grün? ──► Squash-Merge nach truely-godot-engine  (Feature-Branch bleibt bestehen!)
```

### 2.1 Feature-Branch

```powershell
git switch truely-godot-engine
git pull
git switch -c 03-feature-path-prefix-server
```

- **Ein Branch pro Issue**, abgezweigt von `truely-godot-engine`.
- **Name:** `<issue-nummer>-feature-<kurzname>`, die Nummer mindestens zweistellig: Issue #3 → `03-feature-path-prefix-server`, Issue #1 → `01-feature-resource-paths-retrieval`, Issue #123 → `123-feature-…`. Für Refactorings und Bugs `…-refactoring-…` bzw. `…-bug-…`.
- Beiträge an Godot selbst (Upstream-PRs) zweigen dagegen von `master` ab. Siehe [`README.md`](README.md).

### 2.2 Committen

- **Beliebig oft** in den Feature-Branch committen.
- Der pre-commit-Hook (clang-format, codespell, doc-status, `truely-pipeline-lint`, …) muss durchlaufen. Korrigiert er Dateien, diese neu hinzufügen und noch einmal committen. Welche CI-Regeln lokal nicht auffallen und wie man sie trotzdem prüft: [`PIPELINE-CONVENTIONS.md`](PIPELINE-CONVENTIONS.md).

### 2.3 Lokal prüfen und pushen

- Vor dem Merge muss lokal alles grün sein: Windows/Mono-Build ohne Fehler und Warnungen, Unit-Tests, Checks in GDScript und C# im Testprojekt.
- Pushen auf den Feature-Branch ist jederzeit erlaubt (Sicherung). **Ein Push auf einen Feature-Branch startet keine CI** (`.github/workflows/runner.yml`).

### 2.4 CI in Stapeln

- Die volle CI läuft nur **auf `truely-godot-engine-releases`**, wenn `truely-godot-engine` per `release.ps1` dorthin kommt (Fast-forward). Merges nach `truely-godot-engine` und Feature-Branches starten keine CI; dort zählt der lokale Windows-Build mit Mono und die Checks in GDScript und C#.
- Rot auf `truely-godot-engine-releases`: auf dem Feature-Branch (oder einem Bug-Branch) beheben, nach `truely-godot-engine` mergen, erneut freigeben.
- Von Hand: `gh workflow run runner.yml --ref <branch>` (auch auf einem Feature-Branch, wenn eine Plattform fraglich ist).
- Vor dem Lauf eines Stapels lokal auf `truely-godot-engine`: `--doctool --headless` ausführen; es darf keinen Diff geben (sonst scheitert „Check for class reference updates“, z. B. an einem `>` in Codebeispielen, das als `&gt;` geschrieben werden muss).
- **Fehler eines Features werden auf seinem Feature-Branch behoben**, nicht mit einem eigenen Bug-Branch: ein Fix-Commit (`... (#N)`) auf `<nn>-feature-…`, lokal prüfen, dann denselben Branch erneut per Squash nach `truely-godot-engine` mergen (Git übernimmt nur den neuen Commit). Kommentar ins Issue des Features.
- Damit das selten nötig ist, prüft der Orchestrator **vor** dem Merge eines Features auf dessen Branch auch das, was die CI prüft und lokal geht: `--doctool --headless` ohne Diff, pre-commit-Hooks, Unit-Tests.
- Ein eigenes Bug-Issue mit eigenem Branch nur für Fehler, die zu keinem Feature gehören (z. B. CI, Werkzeuge). `truely-godot-engine` wird nie zurückgesetzt.

### 2.5 Mergen

Wenn lokal alles grün ist: **Squash-Merge** nach `truely-godot-engine`, also ein Commit pro Feature. Die CI läuft erst bei der Freigabe auf `truely-godot-engine-releases` (2.4).

**Richtung:** Feature-Branches enthalten nur ihre eigenen Änderungen, wie ein PR. Sie gehen nach `truely-godot-engine`, nie umgekehrt: `truely-godot-engine` wird nicht in einen Feature-Branch gemergt.

```powershell
gh pr create --base truely-godot-engine --head 03-feature-path-prefix-server --fill
gh pr merge --squash              # OHNE --delete-branch
```

- **Den Feature-Branch niemals löschen**, weder auf GitHub noch lokal. Er bleibt immer bestehen.
- **Truely-Version** ([`version.txt`](version.txt), SemVer): wird **einmal pro Release** erhöht, nicht in jedem Feature-Branch (sonst Konflikte zwischen parallelen Branches). Wer nach `truely-godot-engine` merged, vergleicht `truely/version.txt` mit dem letzten Release-Tag (`git describe --tags --match 'truely-v*' --abbrev=0`): Ist sie dort noch gleich, wird sie direkt danach in einem eigenen Commit erhöht (`Raise the Truely version to 0.2.0 (#N)`), **MINOR** für ein Feature, **PATCH** nur für Fehlerbehebungen (MINOR schließt PATCH ein: ist schon MINOR erhöht, nichts tun; war nur PATCH erhöht und kommt ein Feature dazu, auf MINOR umstellen). **MAJOR** bei einem neuen Godot-Major/Minor (4.8 → 4.9) oder inkompatiblen API-Änderungen. `release.ps1` und der Release-Workflow brechen ab, wenn der Tag der Version schon existiert.
- In den Repo-Einstellungen ist „Automatically delete head branches“ (`delete_branch_on_merge`) **aus** und bleibt aus.
- Beim PR das *base repository* prüfen: `TruelyMostWanted/truelymostwanted-godot-engine`, nicht `godotengine/godot`.

> **Weiterarbeit auf einem gemergten Feature-Branch:** Nach dem Squash-Merge kennt `truely-godot-engine` die einzelnen Commits nicht.
> Zuerst `git merge truely-godot-engine` im Feature-Branch ausführen, dann weiterarbeiten. Sonst kommen alte Änderungen doppelt oder es gibt Konflikte.

---

## 3. Mono-Build

Ein Feature wird **immer mit Mono (C#)** gebaut, damit die C#-Bindings entstehen und getestet werden können.

```powershell
# 1. Editor mit Mono bauen
./truely/build.ps1 module_mono_enabled=yes

# 2. C#-Glue aus der gebauten Engine erzeugen
./bin/godot.windows.editor.x86_64.mono.exe --headless --generate-mono-glue modules/mono/glue

# 3. C#-Assemblies bauen und als lokale NuGet-Pakete ablegen
python modules/mono/build_scripts/build_assemblies.py --godot-output-dir=./bin --godot-platform=windows --push-nupkgs-local P:/Projects/GitHub/nuget-local
```

- Der Glue (Schritt 2) muss **nach jeder Änderung an gebundenen Klassen** neu erzeugt werden. Sonst fehlen die neuen Klassen in C#.
- Das Testprojekt bezieht `Godot.NET.Sdk` aus dem lokalen NuGet-Ordner (siehe 4.3), damit es die neuen Klassen kennt.
- Der Dateiname in Schritt 2 hängt von Plattform und Build ab. Den tatsächlichen Namen in `bin/` nachsehen.

---

## 4. Testprojekt

### 4.1 Repository

Alle Feature-Tests der Custom-Engine liegen im öffentlichen Repository
**[godot-custom-build-project](https://github.com/TruelyMostWanted/godot-custom-build-project)**.

```powershell
# lokal abrufen (einmalig)
gh repo clone TruelyMostWanted/godot-custom-build-project P:/Projects/GitHub/godot-custom-build-project
```

### 4.2 Aufbau

Wie unsere üblichen Godot-Projekte, aber **mit einem Ordner pro Wiki-Guide** direkt unter `res://`, benannt wie der Guide in Kleinbuchstaben mit Bindestrichen (`Audio-Bus-Settings` → `audio-bus-settings/`), ohne Issue-Nummer. Jeder Ordner trennt das **Beispiel** (Hands-on, die Snippets des Guides) vom **Test** (die ganze API). Vorbild: `seeds/`.

```
godot-custom-build-project/
├── AGENTS.md / CLAUDE.md / README.md
├── documentation/
├── external-projects/
├── godot-exports/                       Export-Ziel (nicht versioniert)
└── godot-project/                       res://
    ├── project.godot
    ├── GodotCustomBuildProject.csproj   Godot.NET.Sdk aus dem lokalen NuGet-Ordner
    ├── nuget.config                     Godot-Pakete nur aus ../../nuget-local
    ├── checks/                          FeatureTest (GDScript und C#), new-feature.ps1, run-checks.ps1
    ├── _feature-template/               Vorlage für neue Feature-Ordner
    └── path-prefixes/
        ├── scripts/gd/                  Example.gd (Snippets des Guides), Test.gd (ganze API)
        ├── scripts/cs/                  Example.cs, Test.cs (dasselbe in C#)
        ├── scenes/                      example_gd.tscn, example_cs.tscn, test_gd.tscn, test_cs.tscn
        └── resources/                   Beispiel-Ressourcen für den Inspector und Testdaten
```

Neuen Feature-Ordner anlegen:

```powershell
./godot-project/checks/new-feature.ps1 -Name path-prefixes
```

Ändert ein Issue ein bestehendes Feature, kommen Beispiel und Test in den vorhandenen Guide-Ordner.

### 4.3 C#-Anbindung an den Custom-Build

- `nuget.config` holt `Godot.NET.Sdk`, `GodotSharp`, `GodotSharpEditor` und `Godot.SourceGenerators` **nur** aus dem lokalen Ordner
  `P:/Projects/GitHub/nuget-local` (relativ: `../../nuget-local`), alles andere von nuget.org.
- Die `.csproj` nutzt `Godot.NET.Sdk/4.8.0-beta`. Die Version folgt `version.py` der Engine. Mit `-Status truely` in `build.ps1`
  wird daraus `4.8.0-truely`; dann die `.csproj` anpassen.
- `build_assemblies.py --push-nupkgs-local` leert den NuGet-Cache für diese Pakete selbst (`ClearNuGetLocalCache=true`).
  Ein neuer Engine-Build mit derselben Version wird also übernommen.

### 4.4 Testcode vorab, Merge nach `main` erst mit fertigem Engine-Build

- Für jedes Feature wird der Testcode **vorab** im Testprojekt geschrieben, auf dem Feature-Branch mit demselben Namen wie in der Engine, auch wenn die Engine dafür noch nicht gebaut ist. Die Checks rufen die API so auf, wie sie im Issue festgelegt ist (GDScript und C#). So laufen Engine-Arbeit und Testcode parallel.
- Der Testprojekt-Branch wird gepusht, sobald der Testcode steht. Er enthält nur den Guide-Ordner `res://<guide>/`.
- **Nach `main` des Testprojekts** kommt der Testcode erst, wenn das Feature in `truely-godot-engine` gemergt und die Engine damit gebaut ist (Glue und NuGet-Pakete). Sonst baut das C#-Projekt auf `main` nicht. Danach werden die Checks auf `main` ausgeführt; das Ergebnis kommt als Kommentar ins Issue.

### 4.5 Tests

- **Jedes Feature hat ein Beispiel und einen Test, je in GDScript und in C#.** Beide Sprachen zeigen bzw. prüfen dasselbe.
- **Beispiel** (`Example.gd`/`Example.cs`): genau die Snippets des Wiki-Guides, in seiner Reihenfolge, mit `print()`; keine Asserts. Läuft headless ohne `ERROR` in der Ausgabe.
- **Test** (`Test.gd`/`Test.cs`): die ganze API des Features (jede Methode, Property, jedes Signal und Enum), dazu Randfälle; erbt von `FeatureTest`, überschreibt `_run_tests()`/`RunTests()`, eine Funktion je Bereich, ruft `expect()` auf.
- **Stil:** weniger Code ist mehr, jede Zeile mit Grund, KISS, **ein Statement pro Zeile**: Wer liest, folgt Zeile für Zeile, was passiert.
- Jede Szene läuft headless und endet mit **Exit-Code 0 (bestanden) oder 1 (fehlgeschlagen)**.

```powershell
./godot-project/checks/run-checks.ps1 -Godot P:/Projects/GitHub/truelymostwanted-godot-engine/bin/godot.windows.editor.x86_64.mono.exe -Feature path-prefixes
```

- `run-checks.ps1` baut vorher das C#-Projekt (`dotnet build`), importiert das Projekt, führt Tests und Beispiele aus (`-Only test|example`, Zeitlimit je Szene) und gibt eine Tabelle aus.
- Die Ergebnisse (bestanden, fehlgeschlagen, Auffälligkeiten) kommen als Kommentar ins Issue der Engine.

---

## 5. Wiki

Die Features werden im **[Wiki der Engine](https://github.com/TruelyMostWanted/truelymostwanted-godot-engine/wiki)** dokumentiert, auf Englisch.
Die Seiten folgen dem Aufbau der **offiziellen Godot-Docs**. Es gibt zwei Arten von Seiten, die Vorlagen liegen im Wiki als `Template-Class-Reference` und `Template-Guide`.

### 5.1 Klassenreferenz: eine Seite pro Klasse

Vorbild: [ResourceLoader](https://docs.godotengine.org/en/stable/classes/class_resourceloader.html). Seitenname `Class-<Klasse>`, zum Beispiel `Class-TTSServer`.
Inhalt wie `doc/classes/<Klasse>.xml`, in dieser Reihenfolge:

| Abschnitt | Inhalt |
|---|---|
| Kopf | `# TTSServer`, darunter **Inherits:** `Object` (bei Bedarf **Inherited By:**) und die Kurzbeschreibung in einem Satz |
| Description | ausführliche Beschreibung, bei Bedarf mit Beispielen |
| Tutorials | Links zu den Guide-Seiten des Features |
| Properties | Tabelle: Typ, Name, Default |
| Methods | Tabelle: Rückgabetyp, Signatur |
| Signals | Signal mit Parametern und Beschreibung |
| Enumerations | `enum` mit Werten und Beschreibung |
| Constants | Konstanten mit Wert und Beschreibung |
| Property Descriptions | pro Property: Typ, Name, Default, Setter/Getter, Beschreibung |
| Method Descriptions | pro Methode: Signatur, Beschreibung |

Codebeispiele zeigen immer **GDScript, C# und GDExtension (C++, godot-cpp)** nacheinander. Die Godot-Docs nutzen dafür Reiter; im Wiki stehen sie als drei Codeblöcke mit den Überschriften **GDScript**, **C#** und **C++ (GDExtension)**.

### 5.2 Guide: eine Seite pro Feature

Vorbild: [Custom resource format loaders](https://docs.godotengine.org/en/4.4/contributing/development/core_and_modules/custom_resource_format_loaders.html). Seitenname wie der Feature-Branch, zum Beispiel `03-feature-path-prefix-server`.

| Abschnitt | Inhalt |
|---|---|
| Titel + Introduction | Was das Feature tut und welches Problem es löst |
| References | Links zu den Klassenreferenz-Seiten, zum Issue und zum Feature-Ordner in godot-custom-build-project |
| What is not covered | Was das Feature bewusst nicht kann (aus „Out of scope“) |
| Schritte | eine Überschrift pro Schritt der Benutzung (z. B. „Registering a prefix“, „Loading a resource“), jeweils mit GDScript, C# und C++ (GDExtension) |
| Limitations | was nicht geht, Plattform-Unterschiede (aus dem „Implementation plan“) |

- **Wiki-Seiten sind immer Pflicht**, für jedes umgesetzte Issue, auch für Refactorings ohne neue API (Guide-Seite und Klassenreferenz jeder betroffenen Klasse). Entscheidungen aus Concept-Issues stehen auf der Seite `Design-Decisions`.
- **Inhaltsverzeichnis:** Jede Seite hat unter dem Titelblock ein nummeriertes Inhaltsverzeichnis (1, 1.1, 1.2, …) mit Sprungmarken zu den Überschriften. Es wird erzeugt, nicht von Hand geschrieben: `python truely/wiki-toc.py <wiki-ordner>` vor jedem Push des Wikis (ersetzt ein vorhandenes Verzeichnis zwischen `<!-- toc -->` und `<!-- /toc -->`).
- Die Seite `Home` listet alle Features mit Status, `_Sidebar` verlinkt Guides und Klassenreferenzen.
- Das Wiki ist ein eigenes Git-Repository: `https://github.com/TruelyMostWanted/truelymostwanted-godot-engine.wiki.git`.

---

## 6. Abnahme (Definition of Done)

Jedes Feature- und Refactoring-Issue endet mit diesen fünf Punkten:

- [ ] C++ data structures and API created
- [ ] C#/GDScript bindings created and registered
- [ ] Documentation written (class reference in `doc/classes/` and wiki page: GDScript, C#, GDExtension)
- [ ] Test build on the current platform done (no errors or warnings)
- [ ] Tested in the test project (`res://<guide>/`: Example and Test) with a Mono build, in GDScript and in C#

Danach: lokal grün → Squash-Merge → CI auf `truely-godot-engine` (im Stapel) → Feature-Branch bleibt bestehen.

---

## 7. Später: Claude-Skill

Geplanter Skill, zum Beispiel `truely-engine-feature`, mit diesen Schritten:
1. Issue lesen, Feature-Branch anlegen.
2. Implementieren nach Abschnitt 1, beliebig oft committen.
3. Mono-Build nach Abschnitt 3.
4. Feature-Ordner im Testprojekt nach Abschnitt 4 anlegen, GDScript- und C#-Checks schreiben und ausführen.
5. Wiki-Seite nach Abschnitt 5 schreiben.
6. Abnahmepunkte aus Abschnitt 6 abhaken.
7. Pushen, CI abwarten, Squash-Merge, Branch behalten.
