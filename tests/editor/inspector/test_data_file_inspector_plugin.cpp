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
#include "editor/data/dataset_editor.h"
#include "editor/inspector/csv_inspector_plugin.h"
#include "editor/inspector/data_file_inspector_plugin.h"
#include "editor/inspector/ini_inspector_plugin.h"
#include "editor/inspector/xml_inspector_plugin.h"

namespace TestDataFileInspectorPlugin {

using Adapter = DataFileTreeAdapter;

static LocalVector<Adapter::Entry> children_of(const Variant &p_value, bool p_xml, int p_from = 0, int p_count = -1) {
	LocalVector<Adapter::Entry> entries;
	Adapter::get_children(p_value, p_xml, p_from, p_count, entries);
	return entries;
}

TEST_CASE("[DataFileTreeAdapter] Entries of JSON data") {
	const Variant data = JSON::parse_string(R"({"name": "knight", "hp": 100, "tags": ["melee", "armored"], "boss": null, "stats": {"speed": 1.5}})");

	const Adapter::Entry root = Adapter::make_entry(String(), data, false);
	CHECK(root.type == "Dictionary");
	CHECK(root.value == "5 keys");
	CHECK(root.child_count == 5);

	const LocalVector<Adapter::Entry> entries = children_of(data, false);
	REQUIRE(entries.size() == 5u);
	CHECK(entries[0].key == "name");
	CHECK(entries[0].type == "String");
	CHECK(entries[0].value == "knight");
	CHECK(entries[0].child_count == 0);
	CHECK(entries[1].type == "float");
	CHECK(entries[2].key == "tags");
	CHECK(entries[2].type == "Array");
	CHECK(entries[2].value == "2 items");
	CHECK(entries[2].child_count == 2);
	CHECK(entries[3].value == "null");
	CHECK(entries[4].value == "1 key");

	const LocalVector<Adapter::Entry> tags = children_of(entries[2].data, false);
	REQUIRE(tags.size() == 2u);
	CHECK(tags[0].key == "0");
	CHECK(tags[1].key == "1");
	CHECK(tags[1].value == "armored");
}

TEST_CASE("[DataFileTreeAdapter] Children are paged") {
	Array items;
	for (int i = 0; i < 1000; i++) {
		items.push_back(i);
	}
	CHECK(Adapter::get_child_count(items, false) == 1000);
	const LocalVector<Adapter::Entry> page = children_of(items, false, 200, 200);
	REQUIRE(page.size() == 200u);
	CHECK(page[0].key == "200");
	CHECK(page[199].key == "399");
	CHECK(children_of(items, false, 950, 200).size() == 50u);
	CHECK(children_of(items, false, 1000, 200).is_empty());
	CHECK(children_of(items, false, -1, 10).is_empty());

	Dictionary keys;
	for (int i = 0; i < 10; i++) {
		keys[vformat("key_%d", i)] = i;
	}
	const LocalVector<Adapter::Entry> key_page = children_of(keys, false, 8, 5);
	REQUIRE(key_page.size() == 2u);
	CHECK(key_page[0].key == "key_8");
	CHECK(key_page[1].key == "key_9");

	CHECK(Adapter::get_child_count(42, false) == 0);
	CHECK(children_of("text", false).is_empty());
}

TEST_CASE("[DataFileTreeAdapter] Long values are cut, line breaks removed") {
	const Adapter::Entry entry = Adapter::make_entry("text", String("line 1\nline 2"), false);
	CHECK(entry.value == "line 1 line 2");
	const Adapter::Entry long_entry = Adapter::make_entry("text", String("x").repeat(500), false);
	CHECK(long_entry.value.length() == Adapter::MAX_VALUE_LENGTH + 3);
	CHECK(long_entry.value.ends_with("..."));
}

TEST_CASE("[DataFileTreeAdapter] XML elements") {
	const Dictionary data = XML::parse_string(R"(<level name="Forest" time="120"><spawn enemy="orc"/>intro<music>forest.ogg</music></level>)");
	Array nodes;
	nodes.push_back(data);

	const LocalVector<Adapter::Entry> top = children_of(nodes, true);
	REQUIRE(top.size() == 1u);
	CHECK(top[0].key == "level");
	CHECK(top[0].type == "Element");
	// Two attributes and three child nodes.
	CHECK(top[0].child_count == 5);

	const LocalVector<Adapter::Entry> level = children_of(top[0].data, true);
	REQUIRE(level.size() == 5u);
	CHECK(level[0].key == "@name");
	CHECK(level[0].type == "Attribute");
	CHECK(level[0].value == "Forest");
	CHECK(level[1].key == "@time");
	CHECK(level[2].key == "spawn");
	CHECK(level[2].value == "enemy=\"orc\"");
	CHECK(level[3].key == "#text");
	CHECK(level[3].type == "Text");
	CHECK(level[3].value == "intro");
	CHECK(level[4].key == "music");
	CHECK(level[4].value == "forest.ogg");

	// A page that starts inside the attributes and ends in the child nodes.
	const LocalVector<Adapter::Entry> page = children_of(top[0].data, true, 1, 2);
	REQUIRE(page.size() == 2u);
	CHECK(page[0].key == "@time");
	CHECK(page[1].key == "spawn");
}

TEST_CASE("[DataFileTreeAdapter] Search finds keys and scalar values") {
	const Variant data = JSON::parse_string(R"({"enemies": [{"name": "orc", "hp": 12}, {"name": "Troll", "hp": 40}], "boss": {"name": "dragon"}})");

	Vector<PackedInt32Array> paths = Adapter::find(data, false, "troll", 100);
	REQUIRE(paths.size() == 1);
	CHECK(paths[0] == PackedInt32Array({ 0, 1, 0 }));

	// Keys match; "2 items" of a container does not match "item".
	paths = Adapter::find(data, false, "name", 100);
	CHECK(paths.size() == 3);
	CHECK(Adapter::find(data, false, "item", 100).is_empty());
	CHECK(Adapter::find(data, false, "key", 100).is_empty());

	// Capped, in document order.
	paths = Adapter::find(data, false, "name", 2);
	REQUIRE(paths.size() == 2);
	CHECK(paths[0] == PackedInt32Array({ 0, 0, 0 }));
	CHECK(paths[1] == PackedInt32Array({ 0, 1, 0 }));

	CHECK(Adapter::find(data, false, "", 100).is_empty());

	// XML: element names, attribute values and text nodes (not the text repeated on the element).
	Array nodes;
	nodes.push_back(XML::parse_string(R"(<level><spawn enemy="orc"/><music>forest.ogg</music></level>)"));
	paths = Adapter::find(nodes, true, "orc", 100);
	REQUIRE(paths.size() == 1);
	CHECK(paths[0] == PackedInt32Array({ 0, 0, 0 }));
	paths = Adapter::find(nodes, true, "forest", 100);
	REQUIRE(paths.size() == 1);
	CHECK(paths[0] == PackedInt32Array({ 0, 1, 0 }));
	paths = Adapter::find(nodes, true, "music", 100);
	REQUIRE(paths.size() == 1);
	CHECK(paths[0] == PackedInt32Array({ 0, 1 }));
}

TEST_CASE("[DataFileTreeAdapter] Child paths stay the same after a new parse") {
	const String enemies = Adapter::make_child_path(String(), "enemies", 0, false);
	CHECK(enemies == "/enemies");
	CHECK(Adapter::make_child_path(enemies, "1", 1, false) == "/enemies/1");
	// Keys are used, not the index: a key inserted before keeps the path of the others.
	CHECK(Adapter::make_child_path(String(), "boss", 5, false) == Adapter::make_child_path(String(), "boss", 6, false));
	// "/" and "\" in keys cannot be confused with the separator.
	CHECK(Adapter::make_child_path(String(), "a/b", 0, false) == "/a\\/b");
	CHECK(Adapter::make_child_path(String(), "a\\", 0, false) == "/a\\\\");
	CHECK(Adapter::make_child_path("/a", "b", 0, false) != Adapter::make_child_path(String(), "a/b", 0, false));
	// XML: the child index, because sibling elements share their tag.
	CHECK(Adapter::make_child_path("/0", "spawn", 2, true) == "/0/2");
	CHECK(Adapter::make_child_path("/0", "spawn", 3, true) != Adapter::make_child_path("/0", "spawn", 2, true));
}

TEST_CASE("[DataFileTreeAdapter] CSV preview column widths") {
	Array rows;
	rows.push_back(PackedStringArray({ "knight", "warrior", "100" }));
	rows.push_back(PackedStringArray({ "archer", "ranger", "60", "extra" }));
	rows.push_back(PackedStringArray({ "a very long name that is cut", "x" }));

	PackedInt32Array chars = Adapter::get_column_chars(PackedStringArray({ "name", "class", "hp" }), rows, 100, 32, 20);
	REQUIRE(chars.size() == 4);
	CHECK(chars[0] == 20);
	CHECK(chars[1] == 7);
	CHECK(chars[2] == 3);
	CHECK(chars[3] == 5);

	// Only the previewed rows count.
	chars = Adapter::get_column_chars(PackedStringArray({ "name", "class", "hp" }), rows, 1, 32, 20);
	REQUIRE(chars.size() == 3);
	CHECK(chars[0] == 6);
	CHECK(chars[2] == 3);

	// Columns are capped; no header.
	chars = Adapter::get_column_chars(PackedStringArray(), rows, 100, 2, 20);
	CHECK(chars.size() == 2);
	CHECK(Adapter::get_column_chars(PackedStringArray(), Array(), 100, 32, 20).is_empty());
}

TEST_CASE("[DataFileTreeAdapter] Big data: the first page and the preview stay cheap") {
	// 20 000 CSV rows: the preview reads the first 100.
	Array rows;
	for (int i = 0; i < 20000; i++) {
		rows.push_back(PackedStringArray({ vformat("unit_%d", i), "warrior", itos(i % 200), i % 2 ? "true" : "false" }));
	}
	uint64_t start = OS::get_singleton()->get_ticks_usec();
	const PackedInt32Array chars = Adapter::get_column_chars(PackedStringArray({ "name", "class", "hp", "ranged" }), rows, 100, 32, 24);
	const uint64_t csv_usec = OS::get_singleton()->get_ticks_usec() - start;
	CHECK(chars.size() == 4);

	// Deep JSON: 20 000 objects, each nested 10 levels deep.
	Array objects;
	for (int i = 0; i < 20000; i++) {
		Dictionary leaf;
		leaf["id"] = i;
		Variant value = leaf;
		for (int depth = 0; depth < 10; depth++) {
			Dictionary wrapper;
			wrapper["child"] = value;
			value = wrapper;
		}
		objects.push_back(value);
	}
	start = OS::get_singleton()->get_ticks_usec();
	const LocalVector<Adapter::Entry> page = children_of(objects, false, 0, 200);
	const uint64_t tree_usec = OS::get_singleton()->get_ticks_usec() - start;
	CHECK(page.size() == 200u);
	MESSAGE(vformat("CSV preview widths of 20 000 rows: %d us; first tree page of 20 000 deep JSON objects: %d us.", csv_usec, tree_usec));
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

	// Top-level rows are sections; the keys without a section are listed as in the INI dock.
	LocalVector<Adapter::Entry> rows = children_of(view->get_tree_value(ini->get_data()), false);
	REQUIRE(rows.size() == 2);
	for (Adapter::Entry &row : rows) {
		view->decorate_row(row, true);
		CHECK(row.type == "Section");
	}
	CHECK(rows[0].key == "(no section)");
	CHECK(rows[0].value == "1 key");
	CHECK(rows[1].key == "player");
	// A Dictionary value inside a section stays a Dictionary.
	Adapter::Entry entry = Adapter::make_entry("spawn", Dictionary(), false);
	view->decorate_row(entry, false);
	CHECK(entry.type == "Dictionary");

	// Search matches the names the rows show.
	const Vector<PackedInt32Array> found = Adapter::find(ini->get_data(), false, "no section", 10, [](const void *p_context, Adapter::Entry &r_entry, bool p_top_level) { static_cast<const INIInspectorView *>(p_context)->decorate_row(r_entry, p_top_level); }, view);
	REQUIRE(found.size() == 1);
	CHECK(found[0] == PackedInt32Array({ 0 }));
	CHECK(Adapter::find(ini->get_data(), false, "no section", 10).is_empty());
	memdelete(view);
}

TEST_CASE("[SceneTree][XMLInspectorView] Root element") {
	Ref<XML> xml;
	xml.instantiate();
	REQUIRE(xml->parse("<units version=\"1\"><unit id=\"knight\"/><unit id=\"archer\"/></units>") == OK);
	XMLInspectorView *view = memnew(XMLInspectorView(xml));
	CHECK(view->get_summary_text(xml->get_data()) == "Root element <units>, 2 child nodes");
	CHECK(view->get_summary_text(Dictionary()) == "No root element");
	const Array tree_value = view->get_tree_value(xml->get_data());
	REQUIRE(tree_value.size() == 1);
	const LocalVector<Adapter::Entry> top = children_of(tree_value, true);
	REQUIRE(top.size() == 1);
	CHECK(top[0].key == "units");
	CHECK(top[0].type == "Element");
	CHECK(Array(view->get_tree_value(Dictionary())).is_empty());
	memdelete(view);
}

TEST_CASE("[DataFileInspectorView] Dataset button cost on large files") {
	// The view looks for the file's tables once per change of the data (DatasetEditor::find_tables()).
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
