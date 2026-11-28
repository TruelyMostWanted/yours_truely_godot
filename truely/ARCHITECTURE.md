# Architektur der Truely-Engine

Wie Features in dieser Fork aufgebaut werden. Die Regeln sind aus der Architektur der Godot Engine abgeleitet. Jede Regel
zeigt zwei kurze Beispiele: **Godot** (das Vorbild im Engine-Code) und **Truely** (dieselbe Regel in unserem Code).

Code-Stil, Namen und Issues: [`CONVENTIONS.md`](CONVENTIONS.md). Regeln der CI: [`PIPELINE-CONVENTIONS.md`](PIPELINE-CONVENTIONS.md).
Prüfliste für neue Features: [Abschnitt 8](#8-prüfliste-für-neue-features).

| Abschnitt | Inhalt |
|---|---|
| [1. Schichten](#1-schichten-und-abhängigkeiten) | Abhängigkeiten, Editor-Code, optionale Module |
| [2. Klassenarten](#2-welche-klassenart-wofür) | Resource, Playback, Node, Server, Registry, Editor-Plugin |
| [3. Server-API](#3-server-als-api-endpunkte) | RIDs, Speicher im Server, Threads |
| [4. Data-Oriented](#4-data-oriented-unter-der-haube) | Arrays statt Objekte, Container, Dirty-Flags, Messen |
| [5. Code-Ebene](#5-code-ebene) | eine Anweisung pro Zeile, Single Responsibility, Separation of Concerns, Verträge, Virtuals, Notifications |
| [6. Editor](#6-editor) | Undo/Redo, Inspector-Plugins |
| [7. Editor-Docks](#7-editor-docks) | Dock-Klasse, Plugin-Muster, Lebenszeit, Dateisystem, große Listen, Größen |
| [8. Prüfliste](#8-prüfliste-für-neue-features) | Fragen vor der Umsetzung |

---

## 1. Schichten und Abhängigkeiten

### 1.1 Abhängigkeiten zeigen nur nach unten

`core` → `servers` → `scene` → `editor`. Eine Schicht kennt nur die Schichten darunter. Server kennen keine Nodes,
Laufzeit-Code kennt keinen Editor. Der pre-commit-Hook `validate-includes` prüft einen Teil davon.

**Godot** · `servers/physics_2d/physics_server_2d.h`: nur `core` und der eigene Server.

```cpp
#include "core/object/ref_counted.h"
#include "servers/physics_2d/direct_states/physics_direct_body_state_2d.h"
#include "servers/physics_2d/direct_states/physics_direct_space_state_2d.h"
#include "servers/physics_2d/physics_server_2d_enums.h"
#include "servers/physics_2d/physics_server_2d_types.h"
```

**Truely** · `servers/audio/audio_server.h` (mit Fork-Erweiterungen): ebenso.

```cpp
#include "core/templates/safe_list.h"
#include "core/variant/variant.h"
#include "servers/audio/audio_bus_layout.h"
#include "servers/audio/audio_effect.h"
```

### 1.2 Editor-Code nur mit `TOOLS_ENABLED`

Editor-Teile liegen in `editor/` oder `modules/<modul>/editor/` und werden nur im Editor-Build übersetzt. Export-Templates
enthalten sie nicht.

**Godot** · `modules/gridmap/register_types.cpp`

```cpp
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_VIRTUAL_CLASS(GridMapEditorPlugin);
		EditorPlugins::add_by_type<GridMapEditorPlugin>();
	}
#endif
```

**Truely** · `modules/flow_graph/register_types.cpp`

```cpp
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorPlugins::add_by_type<FlowGraphEditorPlugin>();
		EditorPlugins::add_by_type<FlowGraphFormatsEditorPlugin>();
	}
#endif
```

### 1.3 Module sind optional

Wer ein Modul benutzt, prüft `MODULE_<NAME>_ENABLED`. Ein fehlendes Modul ergibt einen Fehlercode, keinen Absturz
und keinen Build-Fehler: das Minimal-Template baut ohne Module.

**Godot** · `tests/core/io/test_image.cpp`

```cpp
#ifdef MODULE_TINYEXR_ENABLED
```

**Truely** · `tests/core/io/test_resource_text_serializer_xml.cpp`

```cpp
#include "modules/modules_enabled.gen.h"

#ifdef MODULE_YAML_ENABLED
#include "modules/yaml/yaml.h"
#endif
```

---

## 2. Welche Klassenart wofür

Jedes Feature wählt für jeden Teil die passende Art. Was abgespielt oder ausgeführt wird, folgt dem Muster
**Resource + Playback + Node**, wie `AudioStream` + `AudioStreamPlayback` + `AudioStreamPlayer` in Godot.

| Art | Wofür | Godot | Truely |
|---|---|---|---|
| `Resource` | Daten und Assets: im Inspector bearbeiten, als Datei speichern, teilen | `Animation`, `AudioStream`, `TileSet` | `FlowGraph`, `Subtitle`, `SpriteSheet` |
| `RefCounted` (Playback) | Laufzeitzustand einer Nutzung | `AudioStreamPlayback`, `Tween` | `FlowGraphPlayback` |
| `Node` | dünne Verbindung zur Szene | `AudioStreamPlayer`, `AnimationPlayer` | `FlowGraphPlayer`, `SubtitlePlayer` |
| Server (Singleton) | gemeinsamer, szenenübergreifender Zustand | `AudioServer`, `TextServer` | `PathPrefixServer`, Settings-Server |
| Registry | austauschbare Implementierungen | `TextServerManager`, `ResourceLoader` | `ResourceTextSerializer::register_format` |
| Editor-Plugin, Dock | Bedienung im Editor | `EditorInspectorPlugin`, `AnimationPlayerEditor` | Katalog-, CSV-, Flow-, SpriteSheet-Editor |

### 2.1 Resource: Daten, keine Laufzeit

Eine Resource beschreibt Daten. Sie wird im Inspector bearbeitet, gespeichert und geteilt. Sie hält keinen Zustand einer
laufenden Nutzung (keine aktuelle Position, keinen Timer) und kennt ihr Dateiformat nicht.

**Godot** · `scene/resources/animation.h`: speichert Spuren, nicht die Abspielposition.

```cpp
class Animation : public Resource {
	GDCLASS(Animation, Resource);
	RES_BASE_EXTENSION("anim");
```

**Truely** · `modules/flow_graph/flow_graph.h`

```cpp
class FlowGraph : public Resource {
	GDCLASS(FlowGraph, Resource);

public:
	static constexpr int NODE_ID_NONE = -1;
```

### 2.2 Playback: Zustand einer Nutzung als `RefCounted`

Der Zustand einer Nutzung (aktueller Schritt, Variablen, Zeit) liegt in einem eigenen `RefCounted`. So nutzen mehrere
Spieler dieselbe Resource gleichzeitig, und die Logik läuft auch ohne Szene (Tests, Server, Tools).

**Godot** · `scene/resources/audio/audio_stream.h`

```cpp
class AudioStreamPlayback : public RefCounted {
	GDCLASS(AudioStreamPlayback, RefCounted);
```

**Truely** · `modules/flow_graph/flow_graph_playback.h`

```cpp
class FlowGraphPlayback : public RefCounted {
	GDCLASS(FlowGraphPlayback, RefCounted);
```

### 2.3 Node: dünne Verbindung zur Szene

Einen Node gibt es nur, wenn das Feature in der Szene lebt: Lebenszeit im Baum, `_process`, Transform, Platzierung im
Editor. Er hält Resource und Playback und leitet weiter; die Logik steckt nicht im Node.

**Godot** · `scene/audio/audio_stream_player_internal.cpp`

```cpp
case Node::NOTIFICATION_INTERNAL_PROCESS: {
	process();
} break;
```

**Truely** · `modules/flow_graph/flow_graph_player.cpp`

```cpp
case NOTIFICATION_INTERNAL_PROCESS: {
	playback->process(get_process_delta_time());
} break;
case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
	playback->process(get_physics_process_delta_time());
} break;
```

### 2.4 Server: gemeinsamer Zustand als Singleton

Ein Server ist ein C++-Singleton (`Object`), registriert in `Engine`. Er besitzt Zustand, den viele Teile teilen
(Registrierungen, Geräte, Einstellungen), und ist der einzige Weg dorthin.

**Godot** · `servers/audio/audio_server.h`

```cpp
class AudioServer : public Object {
	GDCLASS(AudioServer, Object);
	...
	static AudioServer *get_singleton();
```

**Truely** · `core/io/path_prefix_server.h`

```cpp
class PathPrefixServer : public Object {
	GDCLASS(PathPrefixServer, Object);
	...
	static PathPrefixServer *get_singleton();
	Error register_prefix(const String &p_prefix, const Ref<PathPrefix> &p_path_prefix, const PackedStringArray &p_platforms = PackedStringArray());
```

### 2.5 Registry: Implementierungen anmelden statt fest einbauen

Gibt es mehrere Implementierungen (Formate, Treiber, Backends), meldet jede sich bei einer Registry an. Der Aufrufer
kennt nur die Schnittstelle.

**Godot** · `drivers/register_driver_types.cpp`

```cpp
image_loader_png.instantiate();
ImageLoader::add_image_format_loader(image_loader_png);

resource_saver_png.instantiate();
ResourceSaver::add_resource_format_saver(resource_saver_png);
```

**Truely** · `modules/toml/register_types.cpp`

```cpp
ResourceSaver::add_resource_format_saver(resource_saver_text_data_toml);
ResourceTextSerializer::register_format(ResourceTextSerializer::FORMAT_TOML, &TOML::write_resource, &_parse_toml_format);
```

### 2.6 Editor-Plugin: nur Bedienung

Inspector-Plugins, Docks und Importer bedienen Resources und Server; sie enthalten keine Logik, die zur Laufzeit
gebraucht wird. Details in Abschnitt 6 und 7.

**Godot** · `editor/audio/audio_stream_editor_plugin.cpp`

```cpp
bool EditorInspectorPluginAudioStreamWAV::can_handle(Object *p_object) {
	return Object::cast_to<AudioStreamWAV>(p_object) != nullptr;
}
```

**Truely** · `editor/scene/texture/sprite_sheet_editor_plugin.cpp`

```cpp
bool EditorInspectorPluginSpriteSheet::can_handle(Object *p_object) {
	return Object::cast_to<SpriteSheet>(p_object) != nullptr;
}
```

### 2.7 Basisklasse einer Familie: nur der gemeinsame Lebenszyklus

Behandeln Loader, Saver, Serializer und Editor mehrere Klassen gleich, bekommen sie eine abstrakte Basisklasse. Die
Basis hält nur, was alle gleich tun (Lebenszyklus), nie ein gemeinsames Datenmodell: Form der Daten, API zum Bearbeiten,
Inspector und Dock gehören der einzelnen Klasse. Neue Virtuals der Basis nur für den Lebenszyklus; alles Formatspezifische
kommt in die Unterklasse.

**Godot** · `core/object/script_language.h`: `Script` teilt Laden, Quelltext und Instanzen; jede Sprache hat ihre eigene
API, der `ScriptEditor` bedient die ganze Familie.

```cpp
class Script : public Resource {
	GDCLASS(Script, Resource);
```

**Truely** · `core/io/text_data_format.h` (#421, #434): `TextDataFormat` ist das Textdokument der Datenformate `CSV`,
`INI`, `XML`, `TOML`, `YAML`: `parse()` mit behaltenem Text, Fehler mit Zeile/Spalte, `options`, `load()`/`save()`,
`validate()`, Endungen, `changed`. `data` ist pro Klasse typisiert; bearbeitet wird über die API der Klasse
(`CSV.set_cell()`, `INI.set_value()`, `XML.get_root()`, `TOML.get_value()`, `YAML.get_value()`). Die Inspector-Plugins
sind pro Format (#435, Abschnitt 6.2), ebenso die Docks (INI-Dock, Dataset-Dock).

```cpp
class TextDataFormat : public Resource {
	GDCLASS(TextDataFormat, Resource);
	...
	virtual Error _parse_data(const String &p_text, const Ref<SerializerOptions> &p_options, Variant &r_data, String &r_error, int &r_line, int &r_column);
	virtual Error _encode_data(const Variant &p_data, const Ref<SerializerOptions> &p_options, String &r_text, String &r_error) const;
```

---

## 3. Server als API-Endpunkte

### 3.1 Viele Instanzen: Handles (`RID`) statt Objekte

Verwaltet ein Server viele gleichartige Einträge, die sich pro Frame ändern, hält er die Daten selbst und gibt `RID`s
heraus. Die API besteht aus Endpunkten: `<x>_create()`, `<x>_set_*()`, `<x>_get_*()`, `free_rid()`. Nodes und
Resources halten nur den RID. Server mit wenigen globalen Einstellungen (Settings-Server, `PathPrefixServer`) bleiben
Objekt-APIs. Bestehende Server werden nicht umgebaut; **neue** Server mit vielen Instanzen werden so entworfen.

**Godot** · `scene/main/canvas_item.cpp`

```cpp
	canvas_item = RenderingServer::get_singleton()->canvas_item_create();
}

CanvasItem::~CanvasItem() {
	ERR_FAIL_NULL(RenderingServer::get_singleton());
	RenderingServer::get_singleton()->free_rid(canvas_item);
}
```

**Truely** · noch kein RID-Server. Kandidaten: Wegfindung auf TileMaps, Untertitel-Cues, Performance-Monitore.

### 3.2 Der Server besitzt den Speicher

Die Einträge liegen in einem `RID_Owner` (oder `RID_PtrOwner`) des Servers. Er legt an, prüft Handles und gibt frei;
niemand außerhalb hält Zeiger darauf.

**Godot** · `servers/rendering/renderer_canvas_cull.h`

```cpp
RID_Owner<LightOccluderPolygon, true> canvas_light_occluder_polygon_owner;
RID_Owner<RendererCanvasRender::LightOccluderInstance, true> canvas_light_occluder_owner;
```

**Truely** · ohne RID: der Server hält seine Einträge privat und gibt nur Kopien oder Namen heraus
(`PathPrefixServer::get_prefixes()`).

### 3.3 Signale und Aufrufe aus anderen Threads

Ein Server sendet Signale nur auf dem Main-Thread und nimmt Arbeit aus anderen Threads über `call_deferred` oder eine
Command-Queue an. Im Web gibt es Builds ohne Threads: der Code darf keine eigenen Threads voraussetzen.

**Godot** · `scene/main/canvas_item.cpp` (`queue_redraw()`): Neuzeichnen aus jedem Thread, ausgeführt auf dem Main-Thread.

```cpp
	if (pending_update) {
		return;
	}

	pending_update = true;
	callable_mp(this, &CanvasItem::_redraw_callback).call_deferred();
}
```

**Truely** · `core/io/path_prefix_server.cpp`

```cpp
void PathPrefixServer::_emit_deferred(const StringName &p_signal, const String &p_prefix) {
	if (Thread::is_main_thread()) {
		emit_signal(p_signal, p_prefix);
	} else {
		call_deferred(SNAME("emit_signal"), p_signal, p_prefix);
	}
}
```

---

## 4. Data-Oriented unter der Haube

### 4.1 API objektorientiert, Speicher datenorientiert

Nach außen gibt es Objekte, Properties und Methoden. Innen liegen Daten, die zusammen verarbeitet werden, nebeneinander
in Arrays (Structure of Arrays), nicht verstreut in Objekten.

**Godot** · `servers/rendering/renderer_scene_cull.h`: Culling über parallele Arrays statt über Instanz-Objekte.

```cpp
		PagedArray<InstanceBounds> instance_aabbs;
		PagedArray<InstanceData> instance_data;
		VisibilityArray instance_visibility;
```

**Truely** · `modules/flow_graph/flow_graph.h`

```cpp
	PackedInt32Array node_ids;
	LocalVector<Ref<FlowNode>> nodes;
	LocalVector<Connection> connections;
```

### 4.2 Engine-Container im heißen Pfad

Was pro Frame läuft, nutzt `LocalVector`, Packed-Arrays und `HashMap` mit `StringName`-Schlüsseln. Kein `Dictionary`,
kein `Variant`, kein `String`-Vergleich und keine Allokation pro Frame. `Variant` und `Dictionary` gehören an die
API-Grenze.

**Godot** · `servers/rendering/renderer_scene_cull.h`

```cpp
		LocalVector<RID> dynamic_lights;
```

**Truely** · `modules/flow_graph/flow_graph_playback.h`

```cpp
	LocalVector<Frame> stack;
	LocalVector<Evaluation> evaluations;
```

### 4.3 Änderungen sammeln, einmal verarbeiten

Viele Änderungen hintereinander lösen eine Neuberechnung aus, nicht viele. Ein Dirty-Flag merkt sich die Arbeit, ein
verzögerter Aufruf erledigt sie einmal. Godots `queue_redraw()` (Beispiel in 3.3) ist genau das.

**Godot** · `scene/main/canvas_item.cpp`

```cpp
	pending_update = true;
	callable_mp(this, &CanvasItem::_redraw_callback).call_deferred();
```

**Truely** · `editor/data/csv_table_editor.cpp`

```cpp
void CSVTableEditor::_csv_changed() {
	status_label->set_text(String());
	if (!update_queued) {
		update_queued = true;
		callable_mp(this, &CSVTableEditor::_update).call_deferred();
	}
}
```

### 4.4 Messen statt vermuten

Für heiße Pfade steht eine Messung im Issue (Zeit, Speicher, Plattformen inkl. Web), dazu der Abschnitt „Measurements“
der Wiki-Seite. Die Regel betrifft das Issue, nicht den Code.

**Godot** · Benchmarks und Performance-Monitore (`Performance.get_monitor()`), Messwerte in den PRs.

**Truely** · finaler Playtest von 0.2.1 (#389): die Tabelle eines Katalogs mit 1200 Keys öffnet in 229 ms, ein neuer
Key braucht 53 ms.

---

## 5. Code-Ebene

### 5.1 Eine Anweisung pro Zeile

Jede Zeile tut eine Sache. Keine Zuweisung in Bedingungen, keine Ketten mit Seiteneffekten, Zwischenergebnisse bekommen
einen Namen. clang-format erzwingt das nicht.

**Godot** · `scene/2d/node_2d.cpp`

```cpp
void Node2D::set_rotation(real_t p_radians) {
	ERR_THREAD_GUARD;
	if (_is_xform_dirty()) {
		_update_xform_values();
	}
	rotation = p_radians;
	_update_transform();
}
```

**Truely** · `modules/flow_graph/flow_graph_player.cpp`

```cpp
void FlowGraphPlayer::set_callback_mode_process(CallbackModeProcess p_mode) {
	ERR_FAIL_INDEX(p_mode, CALLBACK_MODE_PROCESS_MAX);
	callback_mode_process = p_mode;
	_update_processing();
}
```

### 5.2 Single Responsibility

Eine Klasse hat eine Aufgabe. Wird sie zu groß, wird sie geteilt: Daten (`Resource`), Zustand (`Playback`),
Szene (`Node`), Format (`ResourceFormatLoader`/`Saver`), Prüfung (eigene Klasse), Bedienung (Editor).

**Godot** · `servers/text/text_server.h`: die Auswahl der Implementierung ist getrennt von der Textarbeit (`TextServer`).

```cpp
class TextServerManager : public Object {
	GDCLASS(TextServerManager, Object);
```

**Truely** · `modules/flow_graph/flow_graph.h`: das Prüfergebnis als eigene Klasse neben dem Graphen.

```cpp
class FlowGraphValidation : public RefCounted {
	GDCLASS(FlowGraphValidation, RefCounted);
```

### 5.3 Separation of Concerns

Daten wissen nichts von ihrem Dateiformat, ihrer Darstellung oder ihrem Editor. Formate liegen in Loader/Saver oder
Format-Klassen, die Darstellung im Editor.

**Godot** · `drivers/register_driver_types.cpp`: `Image` kennt PNG nicht; Loader und Saver tun es.

```cpp
ImageLoader::add_image_format_loader(image_loader_png);
ResourceSaver::add_resource_format_saver(resource_saver_png);
```

**Truely** · `modules/toml/register_types.cpp`: TOML meldet sich an, die Resources bleiben unverändert.

```cpp
ResourceTextSerializer::register_format(ResourceTextSerializer::FORMAT_TOML, &TOML::write_resource, &_parse_toml_format);
```

### 5.4 Verträge am Anfang prüfen

Ungültige Eingaben werden am Anfang mit `ERR_FAIL_*` abgewiesen: früher Return, Meldung mit Grund. Keine Exceptions,
kein stilles Weitermachen.

**Godot** · `scene/main/node.cpp` (`add_child()`)

```cpp
	ERR_FAIL_COND_MSG(child == this, vformat("Can't add child '%s' to itself.", child->get_name()));
```

**Truely** · `modules/flow_graph/flow_graph_player.cpp`

```cpp
	ERR_FAIL_INDEX(p_mode, CALLBACK_MODE_PROCESS_MAX);
```

### 5.5 Erweiterbar über Virtuals und Callables

Skripte und GDExtensions erweitern ein Feature über `GDVIRTUAL`-Methoden oder registrierte `Callable`s, nicht über
C++-Vererbung. Gebundene Methoden nehmen keine Rohzeiger.

**Godot** · `scene/gui/control.cpp`

```cpp
Size2 Control::get_minimum_size() const {
	ERR_READ_THREAD_GUARD_V(Size2());
	Vector2 ms;
	GDVIRTUAL_CALL(_get_minimum_size, ms);
	return ms;
}
```

**Truely** · `core/io/path_prefix_server.cpp`

```cpp
bool PathPrefix::is_read_only() const {
	bool ret = false;
	GDVIRTUAL_CALL(_is_read_only, ret);
	return ret;
}
```

### 5.6 Lebenszyklus über Notifications, Änderungen über `changed`

Nodes reagieren in `_notification()` auf Lebenszyklus und Verarbeitung, nicht über eigene Timer oder Polling. Wer eine
Resource anzeigt oder benutzt, verbindet sich mit ihrem Signal `changed` und trennt sich, wenn sie wechselt.

**Godot** · `scene/2d/sprite_2d.cpp` (`set_texture()`)

```cpp
	if (texture.is_valid()) {
		texture->disconnect_changed(callable_mp(this, &Sprite2D::_texture_changed));
	}
	texture = p_texture;
	if (texture.is_valid()) {
		texture->connect_changed(callable_mp(this, &Sprite2D::_texture_changed));
	}
```

**Truely** · `editor/scene/texture/sprite_sheet_editor_plugin.cpp`

```cpp
		atlas->disconnect_changed(callable_mp(this, &SpriteSheetRegionList::_atlas_changed));
	...
		atlas->connect_changed(callable_mp(this, &SpriteSheetRegionList::_atlas_changed));
```

---

## 6. Editor

### 6.1 Jede Änderung über Undo/Redo

Was der Nutzer im Editor ändert, läuft über `EditorUndoRedoManager` mit einem sprechenden Namen; ein Schritt pro
Handlung. Die Aktion nennt das geänderte Objekt, damit sie in dessen Verlauf landet (Szene oder Resource). Werden
Schritte zusammengeführt (`MERGE_ENDS`), steht der Gegenstand im Namen, damit nichts Fremdes verschmilzt.

**Godot** · `editor/scene/2d/path_2d_editor_plugin.cpp`

```cpp
undo_redo->create_action(TTR("Remove Point from Curve"));
undo_redo->add_do_method(curve.ptr(), "remove_point", i);
undo_redo->add_undo_method(curve.ptr(), "add_point", curve->get_point_position(i), curve->get_point_in(i), curve->get_point_out(i), i);
```

**Truely** · `modules/flow_graph/editor/flow_graph_editor_plugin.cpp`

```cpp
undo_redo->create_action(vformat(TTR("Set %s on Node %d"), p_property, p_id), UndoRedo::MERGE_ENDS, graph.ptr());
undo_redo->force_fixed_history();
undo_redo->add_do_property(node.ptr(), p_property, p_value);
undo_redo->add_undo_property(node.ptr(), p_property, node->get(p_property));
```

### 6.2 Inspector-Plugin für die Darstellung einer Resource

Zusätzliche Anzeigen und Knöpfe im Inspector (Vorschau, Tabelle, „Edit Regions“) kommen aus einem
`EditorInspectorPlugin`, das über `can_handle()` die Klasse wählt. Properties bleiben normale Properties mit Hints.
Beispiele: [2.6](#26-editor-plugin-nur-bedienung).

---

## 7. Editor-Docks

### 7.1 Ein Dock ist ein `EditorDock`

Docks und Bottom-Panels erben von `EditorDock`. Sie setzen Name, Icon, Tastenkürzel, Standard-Platz und erlaubte
Layouts im Konstruktor, wie Godots eigene Docks.

**Godot** · `editor/animation/animation_player_editor_plugin.cpp`

```cpp
set_name(TTRC("Animation"));
set_icon_name("Animation");
set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_animation_bottom_panel", TTRC("Toggle Animation Dock"), KeyModifierMask::ALT | Key::N));
set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
```

**Truely** · `editor/data/csv_table_editor.cpp`

```cpp
set_name(TTRC("CSV Table"));
set_icon_name("TextFile");
set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_csv_table_bottom_panel", TTRC("Toggle CSV Table Dock")));
set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
```

### 7.2 Das Plugin entscheidet, das Dock bearbeitet

Ein `EditorPlugin` meldet mit `handles()`, welche Objekte es bearbeitet, reicht sie in `edit()` an das Dock weiter und
zeigt es in `make_visible()`. Das Dock selbst prüft keine Auswahl; es bekommt ein Objekt oder `null`.

**Godot** · `modules/gridmap/editor/grid_map_editor_plugin.cpp`

```cpp
void GridMapEditorPlugin::edit(Object *p_object) {
	ERR_FAIL_NULL(grid_map_editor);
	grid_map_editor->edit(Object::cast_to<GridMap>(p_object));
}

bool GridMapEditorPlugin::handles(Object *p_object) const {
	return p_object->is_class("GridMap");
}
```

**Truely** · `editor/scene/texture/sprite_sheet_editor_plugin.cpp`

```cpp
bool SpriteSheetEditorPlugin::handles(Object *p_object) const {
	return Object::cast_to<SpriteSheet>(p_object) != nullptr;
}

void SpriteSheetEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		atlas_editor->make_visible();
	}
```

### 7.3 Lebenszeit: das Dock bleibt beim zuletzt bearbeiteten Objekt

Wählt der Nutzer etwas anderes aus, bleibt das Dock beim letzten passenden Objekt; es wechselt nur, wenn ein neues
passendes Objekt kommt, und schließt nur durch den Nutzer. So funktionieren Aktionen wie „Assign to Selected Nodes“, die
eine andere Auswahl brauchen. Vorbild: das Animation-Dock behält seinen `AnimationPlayer`, wenn ein anderer Node
ausgewählt wird.

**Godot** · `editor/animation/animation_player_editor_plugin.cpp`: `make_visible()` öffnet nur, schließt nie.

```cpp
void AnimationPlayerEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		anim_editor->make_visible();
		anim_editor->set_process(true);
		anim_editor->ensure_visibility();
	}
}
```

**Truely** · `editor/scene/texture/sprite_sheet_editor_plugin.cpp`

```cpp
void SpriteSheetEditorPlugin::edit(Object *p_object) {
	Ref<SpriteSheet> atlas(Object::cast_to<SpriteSheet>(p_object));
	if (atlas.is_valid()) {
		atlas_editor->edit(atlas);
```

### 7.4 Dateisystem beobachten

Ein Dock, das eine Datei bearbeitet, reagiert auf Verschieben, Umbenennen und Löschen (`FileSystemDock`-Signale
`files_moved`, `file_removed`, dazu `EditorFileSystem::filesystem_changed` für Änderungen von außen). Eine gelöschte
Datei schließt das Dock; „Save“ legt sie nie neu an.

**Godot** · `editor/docks/inspector_dock.cpp`

```cpp
FileSystemDock::get_singleton()->connect("files_moved", callable_mp(this, &InspectorDock::_files_moved));
```

**Truely** · `editor/data/csv_table_editor.cpp`

```cpp
void CSVTableEditor::_file_removed(const String &p_file) {
	if (!edited_path.is_empty() && p_file == edited_path) {
		edit(Ref<Resource>());
		close();
	}
}
```

### 7.5 Große Listen in Seiten

Listen, die groß werden können (Zeilen, Keys, Regionen), werden seitenweise angezeigt, nicht als ein riesiger `Tree`.
Bearbeiten und Speichern betreffen trotzdem alle Einträge.

**Godot** · `editor/inspector/editor_properties_array_dict.cpp`

```cpp
page_length = int(EDITOR_GET("interface/inspector/max_array_dictionary_items_per_page"));
```

**Truely** · `editor/data/csv_table_editor.h`

```cpp
	static constexpr int PAGE_SIZE = 500;
```

### 7.6 Größen mit `EDSCALE`, Mindestgrößen setzen

Alle Pixelmaße werden mit `EDSCALE` multipliziert (HiDPI). Docks und Bottom-Panels setzen eine Mindesthöhe, mit der
sie benutzbar sind. Labels mit Zeilenumbruch (`AUTOWRAP_*`) bekommen eine Mindestbreite, sonst rechnet ein Dialog beim
ersten Öffnen mit Breite 0 und wird zu hoch (#85).

**Godot** · `editor/audio/editor_audio_buses.cpp`

```cpp
spacer_top->set_custom_minimum_size(Size2(0, 6 * EDSCALE));
```

**Truely** · `editor/scene/texture/sprite_sheet_editor_plugin.cpp`

```cpp
atlas_editor->set_custom_minimum_size(Size2(0, 300) * EDSCALE);
EditorDockManager::get_singleton()->add_dock(atlas_editor);
```

### 7.7 Inhalt einpassen, wenn das Dock sichtbar wird

Ein Dock öffnet oft versteckt und wächst danach. Ansichten, die sich an die Größe anpassen (Zoom auf den Inhalt),
passen bei Größenänderungen erneut an, bis der Nutzer selbst zoomt oder verschiebt (#412).

**Godot** · Editor-Ansichten wie `GraphEdit` reagieren auf `NOTIFICATION_RESIZED`.

**Truely** · `editor/scene/texture/sprite_sheet_editor_plugin.cpp`

```cpp
		case NOTIFICATION_RESIZED: {
			if (fit_pending) {
				fit();
			}
		} break;
```

---

## 8. Prüfliste für neue Features

Gehört in den „Implementation plan“ des Feature-Issues, bevor die Umsetzung beginnt:

1. **Klassenarten** (Abschnitt 2): Welche Teile sind Resource, Playback, Node, Server, Registry, Editor? Warum ist ein Node nötig? Eine Basisklasse nur für den gemeinsamen Lebenszyklus (2.7).
2. **Schichten** (Abschnitt 1): Abhängigkeiten nur nach unten; Editor-Teile hinter `TOOLS_ENABLED`; optionale Module geprüft.
3. **Server-API** (Abschnitt 3): viele Instanzen → RID-Endpunkte; Threads und Web ohne Threads bedacht.
4. **Heißer Pfad** (Abschnitt 4): Was läuft pro Frame? Daten in Arrays, keine `Variant`/`Dictionary`/Allokation; Messung geplant.
5. **Erweiterung** (Abschnitt 5.5): Virtuals oder Callables für Skripte und GDExtension.
6. **Editor** (Abschnitt 6–7): Undo/Redo pro Handlung; Dock als `EditorDock`, Plugin-Muster, Lebenszeit, Dateisystem, Seiten, Größen.
7. **CI** ([`PIPELINE-CONVENTIONS.md`](PIPELINE-CONVENTIONS.md)): Minimal-Template, SCU, Clang/GCC.
