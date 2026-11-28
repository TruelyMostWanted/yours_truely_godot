/**************************************************************************/
/*  test_data_file_inspector_plugin.cpp                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_data_file_inspector_plugin)

#ifdef TOOLS_ENABLED

#include "core/io/csv_format.h"
#include "core/io/data_field.h"
#include "core/io/data_schema.h"
#include "core/io/ini_format.h"
#include "core/io/json.h"
#include "core/io/xml_format.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/string/localization_catalog.h"
#include "editor/data/csv_table_editor.h"
#include "editor/data/data_tree_adapter.h"
#include "editor/data/dataset_editor.h"
#include "editor/data/ini_editor.h"
#include "editor/inspector/csv_inspector_plugin.h"
#include "editor/inspector/data_file_inspector_plugin.h"
#include "editor/inspector/ini_inspector_plugin.h"
#include "editor/inspector/xml_inspector_plugin.h"

namespace TestDataFileInspectorPlugin {

TEST_CASE("[CSVInspectorView] Preview column widths") {
	Array rows;
	rows.push_back(PackedStringArray({ "knight", "warrior", "100" }));
	rows.push_back(PackedStringArray({ "archer", "ranger", "60", "extra" }));
	rows.push_back(PackedStringArray({ "a very long name that is cut", "x" }));

	PackedInt32Array chars = CSVInspectorView::get_column_chars(PackedStringArray({ "name", "class", "hp" }), rows, 100, 32, 20);
	REQUIRE(chars.size() == 4);
	CHECK(chars[0] == 20);
	CHECK(chars[1] == 7);
	CHECK(chars[2] == 3);
	CHECK(chars[3] == 5);

	// Only the previewed rows count.
	chars = CSVInspectorView::get_column_chars(PackedStringArray({ "name", "class", "hp" }), rows, 1, 32, 20);
	REQUIRE(chars.size() == 3);
	CHECK(chars[0] == 6);
	CHECK(chars[2] == 3);

	// Columns are capped; no header.
	chars = CSVInspectorView::get_column_chars(PackedStringArray(), rows, 100, 2, 20);
	CHECK(chars.size() == 2);
	CHECK(CSVInspectorView::get_column_chars(PackedStringArray(), Array(), 100, 32, 20).is_empty());
}

// The view the plugin creates for p_resource, as T (null if the plugin creates another view). The caller frees it.
template <typename T>
static T *create_view_of(const Ref<EditorInspectorPluginDataFile> &p_plugin, const Ref<Resource> &p_resource) {
	DataFileInspectorView *view = p_plugin->create_view(p_resource);
	T *typed = Object::cast_to<T>(view);
	if (typed == nullptr) {
		memdelete(view);
	}
	return typed;
}

TEST_CASE("[SceneTree][DataFileInspectorView] One plugin per format") {
	Ref<EditorInspectorPluginDataFile> plugins[] = { memnew(EditorInspectorPluginCSV), memnew(EditorInspectorPluginINI), memnew(EditorInspectorPluginXML), memnew(EditorInspectorPluginJSON) };
	Ref<Resource> resources[] = { memnew(CSV), memnew(INI), memnew(XML), memnew(JSON) };
	// Each plugin takes its own format only; the YAML and TOML modules have their own plugins.
	for (int plugin = 0; plugin < 4; plugin++) {
		for (int format = 0; format < 4; format++) {
			CHECK_MESSAGE(plugins[plugin]->can_handle(resources[format].ptr()) == (plugin == format), vformat("plugin %d, format %d", plugin, format));
		}
		for (const char *module_class : { "YAML", "TOML" }) {
			if (ClassDB::class_exists(module_class)) {
				const Ref<RefCounted> module_format = Object::cast_to<RefCounted>(ClassDB::instantiate(module_class));
				CHECK_FALSE(plugins[plugin]->can_handle(module_format.ptr()));
			}
		}
	}

	// Each plugin creates the view of its format.
	CSVInspectorView *csv_view = create_view_of<CSVInspectorView>(plugins[0], resources[0]);
	CHECK(csv_view != nullptr);
	INIInspectorView *ini_view = create_view_of<INIInspectorView>(plugins[1], resources[1]);
	CHECK(ini_view != nullptr);
	XMLInspectorView *xml_view = create_view_of<XMLInspectorView>(plugins[2], resources[2]);
	CHECK(xml_view != nullptr);
	DataFileInspectorView *json_view = plugins[3]->create_view(resources[3]);
	CHECK(json_view->get_class_name() == StringName("DataFileInspectorView"));
	if (csv_view) {
		memdelete(csv_view);
	}
	if (ini_view) {
		memdelete(ini_view);
	}
	if (xml_view) {
		memdelete(xml_view);
	}
	memdelete(json_view);
}

TEST_CASE("[SceneTree][DataFileInspectorView] Summary of a value") {
	Ref<JSON> json;
	json.instantiate();
	DataFileInspectorView *view = memnew(DataFileInspectorView(json));
	Dictionary dictionary;
	dictionary["a"] = 1;
	CHECK(view->get_summary_text(dictionary) == "Dictionary, 1 key");
	CHECK(view->get_summary_text(Array({ 1, 2 })) == "Array, 2 items");
	CHECK(view->get_summary_text(3) == "int");
	memdelete(view);
}

TEST_CASE("[SceneTree][INIInspectorView] Sections and keys") {
	Ref<INI> ini;
	ini.instantiate();
	REQUIRE(ini->parse("version=2\n\n[player]\nname=\"knight\"\nhp=100\n") == OK);
	INIInspectorView *view = memnew(INIInspectorView(ini));
	CHECK(view->get_summary_text(ini->get_data()) == "2 sections, 3 keys");
	CHECK(view->get_summary_text(Dictionary()) == "0 sections, 0 keys");

	// The view shows the tree of the INI adapter: keys without a section at the top, sections as containers.
	const Ref<DataTreeAdapter> adapter = view->get_adapter();
	REQUIRE(adapter.is_valid());
	LocalVector<DataTreeAdapter::Entry> rows;
	adapter->get_children(PackedStringArray(), 0, -1, rows);
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].key == "version");
	CHECK(rows[0].kind == DataTreeAdapter::NODE_KIND_VALUE);
	CHECK(rows[1].key == "player");
	CHECK(rows[1].type == "Section");
	CHECK(rows[1].value == "2 keys");
	memdelete(view);
}

TEST_CASE("[SceneTree][XMLInspectorView] Root element") {
	Ref<XML> xml;
	xml.instantiate();
	REQUIRE(xml->parse("<units version=\"1\"><unit id=\"knight\"/><unit id=\"archer\"/></units>") == OK);
	XMLInspectorView *view = memnew(XMLInspectorView(xml));
	CHECK(view->get_summary_text(xml->get_data()) == "Root element <units>, 2 child nodes");
	CHECK(view->get_summary_text(Dictionary()) == "No root element");
	const Ref<DataTreeAdapter> adapter = view->get_adapter();
	REQUIRE(adapter.is_valid());
	LocalVector<DataTreeAdapter::Entry> top;
	adapter->get_children(PackedStringArray(), 0, -1, top);
	REQUIRE(top.size() == 1);
	CHECK(top[0].key == "units");
	CHECK(top[0].type == "Element");
	memdelete(view);
}

// The texts of the buttons anywhere below `p_node`.
static void collect_button_texts(const Node *p_node, PackedStringArray &r_texts) {
	for (int child_index = 0; child_index < p_node->get_child_count(); child_index++) {
		const Node *child = p_node->get_child(child_index);
		const Button *button = Object::cast_to<Button>(child);
		if (button && !button->is_toggle_mode()) { // Toggles such as "Raw Properties" are view options, not actions.
			r_texts.push_back(button->get_text());
		}
		collect_button_texts(child, r_texts);
	}
}

TEST_CASE("[SceneTree][DataFileInspectorView] No dock buttons, the docks open with the file") {
	Ref<CSV> data_csv;
	data_csv.instantiate();
	REQUIRE(data_csv->parse("name,hp\nknight,100\narcher,80\n") == OK);
	Ref<CSV> translation_csv;
	translation_csv.instantiate();
	REQUIRE(translation_csv->parse("keys,en,de\nHELLO,Hello,Hallo\n") == OK);
	Ref<INI> ini;
	ini.instantiate();
	REQUIRE(ini->parse("[player]\nhp=100\n\n[enemy]\nhp=50\n") == OK);
	Ref<XML> xml;
	xml.instantiate();
	REQUIRE(xml->parse("<units><unit id=\"knight\"/><unit id=\"archer\"/></units>") == OK);
	Ref<JSON> json;
	json.instantiate();
	REQUIRE(json->parse(R"([{"name": "knight"}, {"name": "archer"}])") == OK);

	// Every view is a read-only preview: Validate is its only button, also for files with a table or a dock.
	DataFileInspectorView *views[] = {
		memnew(CSVInspectorView(data_csv)),
		memnew(CSVInspectorView(translation_csv)),
		memnew(INIInspectorView(ini)),
		memnew(XMLInspectorView(xml)),
		memnew(DataFileInspectorView(json)),
	};
	for (DataFileInspectorView *view : views) {
		PackedStringArray texts;
		collect_button_texts(view, texts);
		CHECK_MESSAGE(texts == PackedStringArray({ "Validate" }), vformat("%s: %s", view->get_class(), String(", ").join(texts)));
		memdelete(view);
	}

	// The dock that opens with each file instead (EditorPlugin::handles()).
	CHECK(DatasetEditor::can_open_automatically(data_csv.ptr()));
	CHECK_FALSE(CSVTableEditor::can_edit(data_csv.ptr()));
	CHECK(CSVTableEditor::can_edit(translation_csv.ptr()));
	CHECK_FALSE(DatasetEditor::can_open_automatically(translation_csv.ptr()));
	CHECK(DatasetEditor::can_open_automatically(xml.ptr()));
	CHECK(DatasetEditor::can_open_automatically(json.ptr()));
	Ref<TextLocalizationCatalog> catalog;
	catalog.instantiate();
	CHECK(CSVTableEditor::can_edit(catalog.ptr()));

	// INI: the INI dock for every INI file with a recognized path, never the Dataset dock.
	CHECK_FALSE(DatasetEditor::can_open_automatically(ini.ptr()));
	CHECK_FALSE_MESSAGE(INIEditor::can_edit(ini.ptr()), "No path: nothing to edit and save in the dock.");
	ini->set_path_cache("res://game.ini");
	CHECK(INIEditor::can_edit(ini.ptr()));
	ini->set_path_cache("res://override.cfg");
	CHECK(INIEditor::can_edit(ini.ptr()));
	CHECK_FALSE(INIEditor::can_edit(json.ptr()));
}

TEST_CASE("[DataFileInspectorView] Table search cost on large files") {
	// The Dataset dock looks for the file's tables when the file opens (DatasetEditor::find_tables()).
	String csv_text = "name,class,hp\n";
	for (int i = 0; i < 100000; i++) {
		csv_text += vformat("unit_%d,warrior,%d\n", i, i % 200);
	}
	Ref<CSV> csv;
	csv.instantiate();
	REQUIRE(csv->parse(csv_text) == OK);
	uint64_t start = OS::get_singleton()->get_ticks_usec();
	CHECK(DatasetEditor::find_tables(csv.ptr()).size() == 1);
	const uint64_t csv_usec = OS::get_singleton()->get_ticks_usec() - start;

	Array records;
	for (int i = 0; i < 20000; i++) {
		Dictionary record;
		record["name"] = vformat("unit_%d", i);
		record["hp"] = i % 200;
		records.push_back(record);
	}
	Ref<JSON> json;
	json.instantiate();
	json->set_data(records);
	start = OS::get_singleton()->get_ticks_usec();
	CHECK(DatasetEditor::find_tables(json.ptr()).size() == 1);
	const uint64_t json_usec = OS::get_singleton()->get_ticks_usec() - start;

	String xml_text = "<units>";
	for (int i = 0; i < 20000; i++) {
		xml_text += vformat("<unit id=\"unit_%d\" hp=\"%d\"/>", i, i % 200);
	}
	xml_text += "</units>";
	Ref<XML> xml;
	xml.instantiate();
	REQUIRE(xml->parse(xml_text) == OK);
	start = OS::get_singleton()->get_ticks_usec();
	CHECK(DatasetEditor::find_tables(xml.ptr()).size() == 1);
	const uint64_t xml_usec = OS::get_singleton()->get_ticks_usec() - start;
	MESSAGE(vformat("find_tables(): CSV 100 000 rows %d us, JSON 20 000 records %d us, XML 20 000 elements %d us.", csv_usec, json_usec, xml_usec));
}

TEST_CASE("[CSVInspectorView] Column titles name the schema field") {
	Ref<DataSchema> schema;
	schema.instantiate();
	Ref<DataField> field;
	field.instantiate();
	field->set_field_name("hp");
	field->set_value_type(Variant::INT);
	field->set_required(true);
	field->set_description("Hit points.");
	schema->add_field(field);
	CHECK(CSVInspectorView::get_column_tooltip("hp", schema) == "hp: int, required\nHit points.");
	CHECK(CSVInspectorView::get_column_tooltip("speed", schema) == "speed: not in the data schema.");
	CHECK(CSVInspectorView::get_column_tooltip("hp", Ref<DataSchema>()) == "hp");
}

} // namespace TestDataFileInspectorPlugin

#endif // TOOLS_ENABLED
