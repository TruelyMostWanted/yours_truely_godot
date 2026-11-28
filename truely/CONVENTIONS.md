# Konventionen der Truely-Engine

Regeln für Code und Issues in dieser Engine-Fork. Wie Features aufgebaut werden (Klassenarten, Schichten, Server-API, Data-Oriented Design): [`ARCHITECTURE.md`](ARCHITECTURE.md). Was zusätzlich die CI verlangt (GCC/Clang, SCU, Sanitizer, Minimal-Template, doctool, C#): [`PIPELINE-CONVENTIONS.md`](PIPELINE-CONVENTIONS.md). Der Ablauf (Branch, Build, Test, Merge) steht in [`FEATURE-WORKFLOW.md`](FEATURE-WORKFLOW.md),
Branches und Remotes in [`README.md`](README.md).

---

## 1. Code

### 1.1 Grundregeln

| Regel | Bedeutung |
|---|---|
| **Passende Klassenart** | Jeder Teil eines Features hat die passende Art: Daten als `Resource`, Laufzeitzustand als `RefCounted`-Playback, gemeinsamer Zustand als Server (C++-Singleton wie `AudioServer`), ein `Node` nur als dünne Verbindung zur Szene (Muster Resource + Playback + Node, wie `AudioStream` + `AudioStreamPlayback` + `AudioStreamPlayer`). Regeln mit Beispielen: [`ARCHITECTURE.md`](ARCHITECTURE.md). |
| **Das C++-Objekt ist die API** | Methoden, Properties, Signale und Enums werden in `_bind_methods()` gebunden. GDScript sieht sie direkt. C# bekommt sie über den generierten Glue. |
| **Doku gehört dazu** | Jede Klasse hat ihre Klassenreferenz in `doc/classes/<Klasse>.xml` oder `modules/<modul>/doc_classes/`. Die pre-commit-Hooks `doc-status` und `make-rst` müssen durchlaufen. Die Benutzung steht im [Wiki](https://github.com/TruelyMostWanted/truelymostwanted-godot-engine/wiki). |
| **Zwei Sprachen** | Jedes Feature wird in **GDScript und C#** getestet, mit einem **Mono-Build** (Testprojekt [godot-custom-build-project](https://github.com/TruelyMostWanted/godot-custom-build-project)). |
| **Cross-Plattform und Web** | Jedes Feature wird für Windows, Linux, macOS, Android, iOS **und den Web-Export** entworfen. Benutzbarkeit und Performance werden pro Plattform bedacht. Der Web-Export ist standardmäßig single-threaded, hat Dateien nur in `user://` (IndexedDB, asynchron gespeichert) und im PCK, keinen Ordner der ausführbaren Datei, keine Prozesse und wenig Speicher. Was pro Plattform geht, langsamer ist oder fehlt, steht im Issue unter „Platforms“. |
| **Stil des Engine-Codes** | Tabs, clang-format, `p_` für Parameter, `r_` für Rückgabeparameter, keine Exceptions (`disable_exceptions=yes`), C++17. |
| **Textformate** | Daten (`Variant` ↔ Text) wie Godots `JSON`: `parse(text) -> Error` (Instanz), `parse_string(text) -> Variant` (statisch), `stringify(data) -> String` (statisch). Ressourcen (`Resource` ↔ Text): `serialize(resource, options = {}) -> String`, `deserialize(text, type_hint = "") -> Resource`, je Formatklasse (`JSON`, `TOML`, `XML`, `INI`, `CSV`, `YAML`) und zentral `ResourceTextSerializer.serialize(resource, format)`/`deserialize(text, format)`. Keine Namen wie `resource_to_x`/`x_to_resource` (#68, #73). |
| **Thread-Sicherheit** | Wo Loader-Threads oder OS-Callbacks beteiligt sind: Locks (`Mutex`, `RWLock`), Signale werden auf dem Main-Thread gesendet (`call_deferred`). **Nie** `ERR_*`, `WARN_*` oder `print_*` aufrufen, während ein Lock gehalten wird: Ausgeben nimmt den globalen Lock (TSAN-Fehler #60); Fehler unter dem Lock sammeln, nach dem Freigeben ausgeben. Kein `ResourceLoader::load()` in einem `WorkerThreadPool`-Task, der auf sich selbst wartet (#9: eigener `Thread`). |
| **Upstream-freundlich** | Eigene Dateien statt großer Änderungen in bestehenden Dateien, wo es geht. Das hält den Sync mit Godot (`sync-upstream.ps1`) konfliktarm. |
| **Ergänzen statt ersetzen** | Langfristig entsteht eine eigene, verbesserte Engine. Bis zu deren echtem Release gilt: **minimal destruction, additive features**. Neue APIs werden unabhängig von Godots Entwurf gebaut (eigene Namen, eigenes Datenmodell) und über Adapter angebunden. Godot-APIs, die sie ablösen, werden in der Klassenreferenz nur als **deprecated** markiert (mit Verweis auf die neue API), nicht entfernt oder umgebaut, z. B. `TranslationServer` → `TextLocalizationServer` (#339). Aktiver Umzug und Overrides im Godot-Kern erst mit dem Release der eigenen Engine-Version. **Ausnahme:** Godots Datenstruktur unter der Haube darf umgebaut werden, wenn die bestehende API dabei erhalten bleibt und nur ergänzt wird (gleiche Methoden und Signale, neue dazu, Datenmodelle teils als `Resource`), und der Umbau einen klaren Gewinn bringt (Performance, Bearbeitbarkeit im Inspector). Vorbild: `InputMap` mit `InputAction`-Ressourcen und Layern (#172, #191). |

### 1.2 Namen

| Wo | Schreibweise | Beispiel |
|---|---|---|
| C++-Klassen | `PascalCase` | `TTSServer`, `TTSPersona` |
| Methoden, Properties, Signale | `snake_case` | `speak()`, `default_persona`, `utterance_started` |
| Booleans | als Property ohne `is_`, Getter `is_*` | `mute`, `borderless` → `is_mute()` |
| Enums | Typ `PascalCase`, Werte `UPPER_SNAKE_CASE` | `UtteranceEvent::UTTERANCE_STARTED` |
| Dateien | `snake_case` | `servers/tts/tts_server.h` |
| Project Settings | `bereich/gruppe/name` in `snake_case` | `filesystem/resources/preloads` |
| C# (automatisch) | `PascalCase` | `TtsServer.Speak()`, `DefaultPersona`, `UtteranceStarted` |

### 1.2a Benennung und Typen der API

| Regel | Inhalt | Beispiel |
|---|---|---|
| **Sprechende Namen** | Der Name sagt, was die Methode tut und womit. Keine Kurzformen, keine fremden Wörter („logic“, „select“, „insert“). | `create_folder_prefix()` statt `create_folder_logic()`, `find_resources()` statt `select()` |
| **Godot-Verben** | `add_*`/`remove_*`, `register_*`/`unregister_*`, `has_*`, `get_*`/`set_*`, `is_*`, `find_*`, `get_*_count()`. Paare sind symmetrisch. | `register_noise`/`unregister_noise`, `get_entry_count()` |
| **Rückgabe im Namen** | Liefert eine Methode Namen, IDs oder Pfade, steht das im Namen. | `get_dirty_setting_names()`, `resolve_voice_id()`, `get_file_paths()` |
| **„save“/„load“ nur mit Datei** | `save_*`/`load_*` lesen oder schreiben Dateien. Text in einen String: `to_text()`, aus einem String: `load_from_text()`. | `to_text(FORMAT_JSON)` statt `save_as_json()` |
| **Akronyme** | In Klassennamen groß, wie in Godot (`JSON`, `XMLParser`, `PCKPacker`, `HTTPRequest`). C# macht daraus automatisch `Tts…`, `Pck…`. | `TTSServer`, `PCKPackage` |
| **Engine-Namen übernehmen** | Gibt es das Konzept in Godot schon, denselben Namen verwenden. | `max_polyphony`, `switch_to_clip`, `filter_clip`, `is_bus_mute` |
| **Keine Namenskonflikte** | Kein Klassenname, der einer Engine-Klasse gleicht oder zum Verwechseln ähnlich ist; vorher `grep -rn "class <Name>\b" core scene servers modules editor`. | nicht `TextureAtlas` neben `AtlasTexture` |
| **Kein Datentyp im Namen** | Ein Element im Singular, eine Sammlung im Plural, egal ob Liste, Array oder Dictionary. Nie den Container-Typ anhängen (`List`, `Array`, `Dict`, `Set`). Vorhandene Namen mit Suffix werden mit deprecated Alias umbenannt, nicht stillschweigend (Besitzer, 2026-11-28). | `TtsPersona` / `TtsPersonas` statt `TtsPersonaList` |
| **Enums statt Strings/Bools** | Feste Auswahl = Enum (`PascalCase`, Werte mit Präfix `UPPER_SNAKE_CASE`, `*_MAX` am Ende, Werte nie umnummerieren). Ein Bool-Parameter, dessen Bedeutung am Aufruf nicht lesbar ist, wird ein Enum. Enum-Werte auch in Signal-Argumenten und virtuellen Methoden binden. | `ResourceTextSerializer.Format.FORMAT_JSON`, `resolve_safe(path, ACCESS_WRITE)` |
| **Keine magischen Zahlen** | Sonderwerte wie `-1` bekommen einen Enum-Wert. | `APPLY_PHASE_NONE = -1` |
| **Konstanten vs. Enum-Präfix** | Eine Konstante darf nicht wie ein Wert eines Enums derselben Klasse aussehen. | `TEXT_LAYOUT_VERSION` statt `FORMAT_VERSION` neben `Format` |
| **Typisierte Optionen** | Keine `options: Dictionary`. Optionen sind eine `Resource` mit typisierten Properties (`SerializerOptions`), Parameter mit Default `null` = Werte der Project Settings. Erweiterungen erben von der Options-Klasse statt `extra: Dictionary`. | `serialize(res, FORMAT_TOML, options)` |
| **Typisierte Ergebnisse** | Mehrere Werte als Ergebnis: eigene `RefCounted`-Klasse statt `Dictionary`. Ausnahme: dieselbe Form wie eine bestehende Engine-Methode (`OS.get_memory_info()`, `DisplayServer.tts_get_voices()`). | `FilesManifestValidation`, `PCKPackageEntry` |
| **Typisierte Parameter** | Engine-Typen statt `Resource`/`Variant`, wo möglich (`Noise`, `AudioStream`). `Variant` nur mit dokumentiertem Grund (z. B. `AudioStream` oder Pfad zum späten Laden). Bezeichner und Klassennamen als `StringName`. | `register_noise(name, noise: Noise)`, `type_hint: StringName` |
| **Inspector oder Server** | Was im Inspector bearbeitet und als `.tres` gespeichert wird, ist eine `Resource` (Properties mit Hints: `PROPERTY_HINT_FILE`, `DIR`, `ENUM`, Bereiche). Laufzeitzustand, Registrierungen und Ablauf gehören in den Server (C++-Singleton). Rückgabe-/Hilfsobjekte ohne Speicherung sind `RefCounted`. | `AudioMixerChannel` (Resource) + `AudioMixerServer` |
| **Parameter nicht wie globale Funktionen** | Parameternamen überdecken keine GDScript-Builtins (`load`, `save`, `print`, `range`). | `loader: Callable` statt `load: Callable` |
| **Textformate** | Eingebaute Formate über `ResourceTextSerializer.Format`; Module registrieren ihre Implementierung in C++ für ihren Enum-Wert (`register_format(FORMAT_TOML, …)`), ein fehlendes Modul ergibt `ERR_UNAVAILABLE`. Eigene Formate aus GDExtension/Skript über `ResourceTextFormat` und die `StringName`-API (`register_custom_format`, `serialize_custom`). `resource_to_dictionary()`/`dictionary_to_resource()` sind die Baum-API, kein Format. | — |
| **Unveröffentlichte API** | Umbenennungen ohne Kompatibilitäts-Aliase, solange das Feature nicht veröffentlicht ist. Jede Umbenennung: Doku-XML, Wiki (Klassen- und Guide-Seite), Checks in GDScript und C#. | — |

Prüfliste für neue APIs (vor dem ersten Commit): Namen nach Tabelle, kein Konflikt-`grep`, keine `Dictionary`-Optionen,
keine Strings für feste Auswahlen, Inspector-Hints gesetzt, Signal- und Virtual-Typen gebunden.

### 1.3 Aufbau eines Servers

```cpp
// servers/tts/tts_server.h
class TTSServer : public Object {
	GDCLASS(TTSServer, Object);
	static TTSServer *singleton;

protected:
	static void _bind_methods();

public:
	static TTSServer *get_singleton();
	int speak(const String &p_text, const Ref<TTSPersona> &p_persona);
	// ...
};
```

Registrierung in `register_core_types()` (`core/register_core_types.cpp`), `register_server_types()` (`servers/register_server_types.cpp`)
oder im `register_types.cpp` eines Moduls:

```cpp
GDREGISTER_CLASS(TTSServer);
Engine::get_singleton()->add_singleton(Engine::Singleton("TTSServer", TTSServer::get_singleton(), "TTSServer"));
```

Aus jedem Singleton erzeugt der C#-Generator zwei Typen (`modules/mono/editor/bindings_generator.cpp`, `CS_SINGLETON_INSTANCE_SUFFIX`):
- `TTSServer`: statische Klasse mit der API, zum Beispiel `TTSServer.Speak(...)`.
- `TTSServerInstance`: der Typ des Objekts, für Signale und Referenzen.

### 1.4 Ressourcen

- Datentypen erben von `Resource` und werden mit `GDREGISTER_CLASS` registriert. Damit lassen sie sich im Inspector anlegen und speichern.
- Gespeicherte Properties haben `PROPERTY_USAGE_STORAGE`. Die Namen sind die späteren Schlüssel in JSON, INI und CSV.

### 1.5 Glossar

- Neue Begriffe (Klassen, Server, Workflow-Begriffe) bekommen im selben Issue einen Eintrag in [`GLOSSARY.md`](../GLOSSARY.md) und, wenn es ein Feature-Bereich ist, eine Zeile im Datei-Index.

---

## 2. Branches

- **Feature-Branches tragen als Präfix die Nummer ihres Issues, mindestens zweistellig:** `<nn>-<art>-<kurzname>`, alles klein mit Bindestrichen.
  - Issue #1 → `01-feature-resource-paths-retrieval`
  - Issue #3 → `03-feature-path-prefix-server`
  - Issue #123 → `123-feature-…`
  - `<art>` folgt dem Typ des Issues: `feature`, `refactoring`, `bug`, `concept`.
- Testprojekt-Ordner und Wiki-Guide tragen den Namen des Themas, nicht den des Branches: Guide `Path-Prefixes`, Ordner `res://path-prefixes/` (Beispiel + Test, siehe `FEATURE-WORKFLOW.md` 4.5).
- Feature-Branches zweigen von `truely-godot-engine` ab und werden **nie gelöscht**. PR-Branches für Beiträge an Godot zweigen von `master` ab (siehe `README.md`).

---

## 3. Issues

Vorbild sind die [godot-proposals](https://github.com/godotengine/godot-proposals/issues/7082): erst das Problem, dann der Vorschlag. Ein Issue enthält beides, den **Vorschlag** und den **Plan für den Pull Request**.

- **Sprache:** Issues, Titel, Commits und Code-Kommentare auf **Englisch**, die Unterhaltung mit dem Nutzer auf Deutsch.
- **Vorlagen:** `.github/ISSUE_TEMPLATE/`. Über die CLI wird der Body als Markdown mit genau den Überschriften der Vorlage geschrieben (`### <Überschrift>`).

| Art | Vorlage | Titel | Label | Überschriften |
|---|---|---|---|---|
| Neue Funktion | `feature_request.yml` | `[Feature] …` | `type: feature` | Describe the problem or limitation you are having · Describe the feature / enhancement and how it helps to overcome the problem or limitation · Describe how your proposal will work, with code, pseudo-code, mock-ups, and/or diagrams · Describe the implementation plan · If this enhancement will not be used often, can it be worked around with a few lines of script? · Is there a reason why this should be core and not an add-on in the asset library? · Acceptance criteria · Area · Platforms |
| Umbau | `refactoring.yml` | `[Refactoring] …` | `type: refactoring` | Describe the problem with the current code · Describe the target structure and how it solves the problem · Describe how it will look, with code, pseudo-code and/or diagrams · Describe the implementation plan · Does the behavior or the API change? · Acceptance criteria · Area |
| Konzept | `concept.yml` | `[Concept] …` | `type: concept` | Describe the problem or limitation you are having · Which questions must the concept answer? · Describe the options, with code, pseudo-code, mock-ups, and/or diagrams · Recommendation and the features it leads to · Acceptance criteria · Area |
| Fehler | `bug_report.yml` | `[Bug] …` | `type: bug` | Tested versions · System information · Issue description · Steps to reproduce · Cause and fix · Logs · Area |
| Organisation, Tracker | `task.yml` | `[Task] …` | `type: task` | Goal · Checklist · References |

### 3.1 Schreiben

- **Kurz, einfach, direkt.** Stichpunkte, keine Füllwörter, ein roter Faden von oben nach unten. Jeder Satz sagt etwas.
- **Konkret:** Klassen, Methoden und Einstellungen in `Code`, Dateien und Zeilen als Link (`https://github.com/TruelyMostWanted/truelymostwanted-godot-engine/blob/<commit>/<pfad>#L10-L20`, Commit statt Branch, damit der Link stimmt).
- **„How it will work“** zeigt die API so, wie Nutzer sie schreiben (GDScript, bei Bedarf C#), dazu Mock-ups oder Mermaid-Diagramme für Editor-Teile.
- **Skizzen:** Jedes Issue mit Editor-Teilen (Inspector, Dock, Dialog, Menü, Node im Viewport) bekommt eine grobe Skizze wie in [godot-proposals#7082](https://github.com/godotengine/godot-proposals/issues/7082): ein SVG- oder PNG-Bild im Wiki-Repository unter `images/concepts/<nr>-<name>.svg`, im Issue eingebunden als `![Dock sketch](https://raw.githubusercontent.com/wiki/TruelyMostWanted/truelymostwanted-godot-engine/images/concepts/<nr>-<name>.svg)`. Grau-Töne wie der Godot-Editor, Beschriftungen auf Englisch, nur das Nötige.
- **„Implementation plan“** listet **New** (Dateien, Klassen), **Changed** (Dateien und Methoden mit Link auf die Zeilen) und **Steps** in Reihenfolge; dazu, was geprüft wurde (Machbarkeit) und was je Plattform nicht geht (Web: ein Thread, Dateien nur in `user://` und im PCK).
- **Entscheidungen und Änderungen** am Umfang kommen in den Body (der Body ist immer der aktuelle Stand); ein Kommentar sagt kurz, was sich geändert hat.

### 3.2 Kommentare

- Alle Kommentare kommen vom Konto des Besitzers: **in der Ich-Form schreiben**, wie Review-Kommentare und Ergänzungen unter Godot-Issues und -PRs. **Keine Präfixe** wie „(Owner, 2026-11-28)“, „Decision (orchestrator, …)“ oder „Agent:“; das Datum zeigt GitHub.
- Ein Kommentar hat einen Zweck: Ergebnis („Implemented in #123: …“), Entscheidung („I went with B, because …“), Fund („The table rebuilds every row on each change: 104 ms with 2000 segments.“), Frage.
- Ergebnisse mit Messwerten und Prüfungen in Stichpunkten; lange Protokolle als `<details>`.

### 3.3 Pull Requests

Die Vorlage für jede Beschreibung liegt in [`.github/PULL_REQUEST_TEMPLATE.md`](../.github/PULL_REQUEST_TEMPLATE.md).

Vorbild ist [godot#99404](https://github.com/godotengine/godot/pull/99404), etwas strenger aufgebaut. Ein PR zeigt das **Ergebnis**; das Issue bleibt der Ort für Problem und Plan.

```markdown
<One or two sentences: what this PR adds or changes.>

Closes #<nr>

<Main screenshot of the result>

## Changes
- <user-visible change>, with a screenshot below the point if it is a UI change
  - <detail>

## API
<New and changed classes, methods, properties, signals, settings; GDScript snippet.>

## Tests
- Unit tests: <n>/<n>
- Test project: `res://<guide>/` Example and Test in GDScript and C#
- Editor playtest: `pt_<nr>` <n> checks, no ERROR lines

## Measurements
| Action | Before | After |

## Not in this PR
- <what is left out, and why> (empty for release mandates: everything goes in)
```

- **Screenshots** aus den Playtests und dem Wiki (`images/<Seite>/…` im Wiki-Repository) mit voller URL `https://raw.githubusercontent.com/wiki/TruelyMostWanted/truelymostwanted-godot-engine/images/<Seite>/<datei>.png` einbinden; jede UI-Änderung hat mindestens ein Bild.
- Titel wie Commits: was der PR tut, Issue-Nummer am Ende (`Subtitles dock: timing tools (#443)`).

- **Öffentlich und für sich verständlich:** Dieses Repository, das Wiki und das Testprojekt sind öffentlich. **Keine Verweise auf private Repositories**: keine Namen, Links, Pfade oder Klassennamen daraus. Ein Issue begründet sich allein über den Stand der Engine (Problem mit Code-Stellen) und den Vorschlag.
- **Labels:** genau ein `type:`, mindestens ein `area:` (Engine-Bereiche: `core`, `scene`, `servers`, `editor`, `modules`, `dotnet`, `platform`, `thirdparty`, `build`, `tests`, `docs`), 0–3 `topic:`.
- **Machbarkeit:** Der „Implementation plan“ jedes Feature-Issues sagt, was in der Engine geht und was nicht. Das Ergebnis steht im Issue, bevor die Umsetzung beginnt.
- **Abnahme:** Jedes Feature- und Refactoring-Issue endet mit diesen fünf Punkten:

```markdown
- [ ] C++ data structures and API created
- [ ] C#/GDScript bindings created and registered
- [ ] Documentation written (class reference in `doc/classes/` and wiki page: GDScript, C#, GDExtension)
- [ ] Test build on the current platform done (no errors or warnings)
- [ ] Tested in the test project (`res://<guide>/`: Example and Test) with a Mono build, in GDScript and in C#
```

- **Wiki:** Klassenreferenz-Seiten (`Class-<Klasse>`) und Guide-Seiten (nach Thema, z. B. `Path-Prefixes`) nach dem Aufbau der offiziellen Godot-Docs, mit Beispielen in GDScript, C# und C++ (GDExtension). Details: `FEATURE-WORKFLOW.md`, Abschnitt 5.
- **Tracker:** Größere Vorhaben bekommen ein `[Task] Tracker: …` mit `flag: tracker`, das die Issues in Reihenfolge und mit Abhängigkeiten auflistet.
