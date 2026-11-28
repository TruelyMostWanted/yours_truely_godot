/**************************************************************************/
/*  test_data_tree_adapter.cpp                                            */
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

TEST_FORCE_LINK(test_data_tree_adapter)

#ifdef TOOLS_ENABLED

#include "core/io/csv_format.h"
#include "core/io/ini_format.h"
#include "core/io/json.h"
#include "core/io/xml_format.h"
#include "core/os/os.h"
#include "editor/data/data_tree_adapter.h"
#include "editor/inspector/csv_inspector_plugin.h"

namespace TestDataTreeAdapter {

using Adapter = DataTreeAdapter;

static PackedStringArray path_of(std::initializer_list<String> p_segments) {
	PackedStringArray path;
	for (const String &segment : p_segments) {
		path.push_back(segment);
	}
	return path;
}

static LocalVector<Adapter::Entry> children_of(const Ref<Adapter> &p_adapter, const PackedStringArray &p_path, int p_from = 0, int p_count = -1) {
	LocalVector<Adapter::Entry> entries;
	p_adapter->get_children(p_path, p_from, p_count, entries);
	return entries;
}

TEST_CASE("[DataTreeAdapter] Path text escapes \"~\" and \"/\" as YAML.get_value() does") {
	CHECK(Adapter::escape_segment("a/b~c") == "a~1b~0c");
	CHECK(Adapter::unescape_segment("a~1b~0c") == "a/b~c");
	CHECK(Adapter::unescape_segment("~01") == "~1");
	CHECK(Adapter::join_path(path_of({ "This", "a/b", "0" })) == "This/a~1b/0");
	CHECK(Adapter::join_path(PackedStringArray()).is_empty());
	CHECK(Adapter::split_path("This/a~1b/0") == path_of({ "This", "a/b", "0" }));
	CHECK(Adapter::split_path("/This/Is") == path_of({ "This", "Is" })); // JSON Pointer form.
	CHECK(Adapter::split_path(String()).is_empty());
	// No adapter for a missing resource or a CSV table.
	CHECK(Adapter::create_for(Ref<Resource>()).is_null());
	Ref<CSV> table;
	table.instantiate();
	table->set_header(PackedStringArray({ "name", "hp" }));
	CHECK(Adapter::create_for(table).is_null());
}

TEST_CASE("[DataTreeAdapter][JSON] Node kinds, entries and paths") {
	Ref<JSON> json;
	json.instantiate();
	REQUIRE(json->parse("{\"This\": {\"Is\": {\"A\": {\"Test\": 42}}}, \"tags\": [\"a\", \"b\"], \"units\": [{\"id\": \"knight\"}, {\"id\": \"archer\"}], \"none\": null}") == OK);
	const Ref<Adapter> adapter = Adapter::create_for(json);
	REQUIRE(Object::cast_to<JSONTreeAdapter>(adapter.ptr()) != nullptr);

	CHECK(adapter->get_kind(PackedStringArray()) == Adapter::NODE_KIND_NAMED);
	const LocalVector<Adapter::Entry> top = children_of(adapter, PackedStringArray());
	REQUIRE(top.size() == 4);
	CHECK(top[0].key == "This");
	CHECK(top[0].type == "Dictionary");
	CHECK(top[0].value == "1 key");
	// An Array of scalars is one value, an Array of containers a node.
	CHECK(top[1].kind == Adapter::NODE_KIND_VALUE);
	CHECK(top[1].type == "Array");
	CHECK(top[1].value == "[a, b]");
	CHECK(top[2].kind == Adapter::NODE_KIND_INDEXED);
	CHECK(top[2].value == "2 items");
	CHECK(top[3].value == "null");

	const PackedStringArray test = path_of({ "This", "Is", "A", "Test" });
	CHECK(double(adapter->get_value(test)) == 42.0);
	CHECK(adapter->format_path(test) == "/This/Is/A/Test");
	CHECK(adapter->get_child_count(path_of({ "units" })) == 2);
	CHECK(adapter->get_value(path_of({ "units", "1", "id" })) == Variant("archer"));
	CHECK(adapter->get_child_count(path_of({ "tags" })) == 0);
	CHECK(adapter->get_value(path_of({ "tags", "0" })).get_type() == Variant::NIL); // Inside a value.
	CHECK(adapter->get_value(path_of({ "missing" })).get_type() == Variant::NIL);
	Adapter::Entry entry;
	CHECK_FALSE(adapter->get_entry(path_of({ "missing" }), entry));
	REQUIRE(adapter->get_entry(path_of({ "units", "0" }), entry));
	CHECK(entry.segment == "0");
	CHECK(entry.kind == Adapter::NODE_KIND_NAMED);
	CHECK(adapter->get_value_types(test).has(Variant::FLOAT));
}

TEST_CASE("[DataTreeAdapter][JSON] Paging, long values and search") {
	Array items;
	for (int i = 0; i < 450; i++) {
		Dictionary item;
		item["name"] = vformat("unit_%d", i);
		items.push_back(item);
	}
	Dictionary root;
	root["items"] = items;
	root["long"] = String("x").repeat(300) + "\nend";
	Ref<JSON> json;
	json.instantiate();
	json->set_data(root);
	const Ref<Adapter> adapter = Adapter::create_for(json);

	const PackedStringArray path = path_of({ "items" });
	CHECK(children_of(adapter, path, 0, 200).size() == 200);
	const LocalVector<Adapter::Entry> last = children_of(adapter, path, 400, 200);
	REQUIRE(last.size() == 50);
	CHECK(last[0].segment == "400");
	CHECK(children_of(adapter, path, 450, 200).is_empty());
	CHECK(children_of(adapter, path, -1, 200).is_empty());

	Adapter::Entry entry;
	REQUIRE(adapter->get_entry(path_of({ "long" }), entry));
	CHECK(entry.value.length() == Adapter::MAX_VALUE_LENGTH + 3);
	CHECK_FALSE(entry.value.contains("\n"));

	// Keys and values (not container sizes), depth-first, at most p_max.
	Vector<PackedStringArray> found = adapter->find("UNIT_44", 100);
	REQUIRE(found.size() == 11); // unit_44 and unit_440 .. unit_449.
	CHECK(found[0] == path_of({ "items", "44", "name" }));
	CHECK(adapter->find("unit_", 5).size() == 5);
	CHECK(adapter->find("items", 10).size() == 1);
	CHECK(adapter->find("2 items", 10).is_empty());
	CHECK(adapter->find(String(), 10).is_empty());
}

TEST_CASE("[DataTreeAdapter][JSON] Edits change `data` and drop the kept text") {
	Ref<JSON> json;
	json.instantiate();
	REQUIRE(json->parse("{\"a\": 1, \"b\": {\"c\": 2}, \"list\": [{\"x\": 1}, {\"x\": 2}]}", true) == OK);
	REQUIRE_FALSE(json->get_parsed_text().is_empty());
	const Ref<Adapter> adapter = Adapter::create_for(json);

	CHECK(adapter->set_value(path_of({ "a" }), "one") == OK);
	CHECK(json->get_parsed_text().is_empty());
	CHECK(Dictionary(json->get_data())["a"] == Variant("one"));
	CHECK(adapter->set_value(path_of({ "missing" }), 1) == ERR_DOES_NOT_EXIST);
	CHECK(adapter->add_value(path_of({ "b" }), "d", 3) == OK);
	CHECK(adapter->add_value(path_of({ "b" }), "d", 4) == ERR_ALREADY_EXISTS);
	CHECK(adapter->add_value(path_of({ "a" }), "x", 4) == ERR_INVALID_PARAMETER);
	CHECK(adapter->add_value(path_of({ "list" }), String(), Dictionary()) == OK);
	CHECK(adapter->get_child_count(path_of({ "list" })) == 3);
	CHECK(adapter->add_container(PackedStringArray(), "spawn", Adapter::NODE_KIND_INDEXED) == OK);
	CHECK(adapter->get_value(path_of({ "spawn" })).get_type() == Variant::ARRAY);
	CHECK(adapter->add_container(PackedStringArray(), "v", Adapter::NODE_KIND_VALUE) == ERR_INVALID_PARAMETER);

	// Rename and move keep the order of the other keys.
	CHECK(adapter->rename(path_of({ "a" }), "first") == OK);
	CHECK(Dictionary(json->get_data()).keys()[0] == Variant("first"));
	CHECK(adapter->rename(path_of({ "first" }), "b") == ERR_ALREADY_EXISTS);
	CHECK(adapter->rename(path_of({ "list", "0" }), "x") == ERR_INVALID_PARAMETER);
	CHECK(adapter->move(path_of({ "spawn" }), 0) == OK);
	CHECK(Dictionary(json->get_data()).keys()[0] == Variant("spawn"));
	CHECK(adapter->move(path_of({ "list", "1" }), 0) == OK);
	CHECK(double(adapter->get_value(path_of({ "list", "0", "x" }))) == 2.0);

	CHECK(adapter->remove(path_of({ "list", "0" })) == OK);
	CHECK(double(adapter->get_value(path_of({ "list", "0", "x" }))) == 1.0);
	CHECK(adapter->remove(path_of({ "b" })) == OK);
	CHECK_FALSE(Dictionary(json->get_data()).has("b"));
	CHECK(adapter->remove(PackedStringArray()) == ERR_DOES_NOT_EXIST);
}

static const char *ini_text = "version=2\n\n[player]\nname=\"knight\"\nhp=100\nspawn={\"x\": 1}\n\n[MyItems/0]\nID=0\nName=\"Sword\"\n\n[MyItems/1]\nID=1\nName=\"Shield\"\n\n[This/Is/A]\nTest=42\n";

TEST_CASE("[DataTreeAdapter][INI] Sections are containers, numbered sections a list") {
	Ref<INI> ini;
	ini.instantiate();
	REQUIRE(ini->parse(ini_text) == OK);
	const Ref<Adapter> adapter = Adapter::create_for(ini);
	REQUIRE(Object::cast_to<INITreeAdapter>(adapter.ptr()) != nullptr);

	const LocalVector<Adapter::Entry> top = children_of(adapter, PackedStringArray());
	REQUIRE(top.size() == 4);
	CHECK(top[0].key == "version"); // Keys before the first section are top-level values.
	CHECK(top[0].type == "int");
	CHECK(top[1].key == "player");
	CHECK(top[1].type == "Section");
	CHECK(top[2].key == "MyItems");
	CHECK(top[2].kind == Adapter::NODE_KIND_INDEXED);
	CHECK(top[2].type == "List");
	CHECK(top[2].value == "2 items"); // No count is stored.
	CHECK(top[3].key == "This");

	CHECK(adapter->get_value(path_of({ "MyItems", "1", "Name" })) == Variant("Shield"));
	CHECK(int(adapter->get_value(path_of({ "This", "Is", "A", "Test" }))) == 42);
	// A Dictionary in a value stays a value.
	CHECK(adapter->get_kind(path_of({ "player", "spawn" })) == Adapter::NODE_KIND_VALUE);
	CHECK(adapter->format_path(path_of({ "This", "Is", "A", "Test" })) == "[This/Is/A] Test");
	CHECK(adapter->format_path(path_of({ "MyItems", "0" })) == "[MyItems/0]");
	CHECK(adapter->format_path(path_of({ "version" })) == "version");
	CHECK(adapter->find("Shield", 10).size() == 1);

	// The tree follows the resource's `changed` signal.
	ini->set_value("player", "mana", 5);
	CHECK(int(adapter->get_value(path_of({ "player", "mana" }))) == 5);
}

TEST_CASE("[DataTreeAdapter][INI] Edits through the INI API") {
	Ref<INI> ini;
	ini.instantiate();
	REQUIRE(ini->parse(ini_text) == OK);
	const Ref<Adapter> adapter = Adapter::create_for(ini);

	CHECK(adapter->set_value(path_of({ "player", "hp" }), 120) == OK);
	CHECK(int(ini->get_value("player", "hp")) == 120);
	CHECK(adapter->set_value(path_of({ "This", "Is", "A", "Test" }), 43) == OK);
	CHECK(int(ini->get_value("This/Is/A", "Test")) == 43);
	CHECK(adapter->set_value(path_of({ "player", "hp" }), Variant()) == ERR_INVALID_PARAMETER);
	CHECK(adapter->remove(path_of({ "player", "spawn" })) == OK);
	CHECK_FALSE(ini->has_section_key("player", "spawn"));

	// A new list item is the next numbered section; an empty one is kept as a section without keys.
	CHECK(adapter->add_container(path_of({ "MyItems" }), String(), Adapter::NODE_KIND_NAMED) == OK);
	CHECK(ini->has_section("MyItems/2"));
	CHECK(ini->get_section_keys("MyItems/2").is_empty());
	CHECK_FALSE(ini->has_section("MyItems"));
	CHECK(adapter->add_value(path_of({ "MyItems", "2" }), "Name", "Bow") == OK);
	CHECK(String(ini->get_value("MyItems/2", "Name")) == "Bow");

	// Removing and moving items numbers the list again.
	CHECK(adapter->remove(path_of({ "MyItems", "0" })) == OK);
	CHECK(String(ini->get_value("MyItems/0", "Name")) == "Shield");
	CHECK(String(ini->get_value("MyItems/1", "Name")) == "Bow");
	CHECK_FALSE(ini->has_section("MyItems/2"));
	CHECK(adapter->move(path_of({ "MyItems", "1" }), 0) == OK);
	CHECK(String(ini->get_value("MyItems/0", "Name")) == "Bow");

	// Renaming a container renames its sections.
	CHECK(adapter->rename(path_of({ "This" }), "That") == OK);
	CHECK(int(ini->get_value("That/Is/A", "Test")) == 43);
	CHECK_FALSE(ini->has_section("This/Is/A"));
	CHECK(adapter->move(path_of({ "player", "hp" }), 0) == OK);
	CHECK(ini->get_section_keys("player")[0] == "hp");
	CHECK(adapter->remove(path_of({ "version" })) == OK);
	CHECK_FALSE(ini->has_section_key("", "version"));
}

static const char *xml_text = "<game version=\"2\"><title>Quest</title><units><unit id=\"knight\" hp=\"100\"/><unit id=\"archer\" hp=\"60\"/></units><spawn x=\"1\"/><spawn x=\"2\"/><note>Hello <b>world</b></note></game>";

TEST_CASE("[DataTreeAdapter][XML] Elements, attributes, lists and repeated tags") {
	Ref<XML> xml;
	xml.instantiate();
	REQUIRE(xml->parse(xml_text) == OK);
	const Ref<Adapter> adapter = Adapter::create_for(xml);
	REQUIRE(Object::cast_to<XMLTreeAdapter>(adapter.ptr()) != nullptr);

	// The root element is the only top-level node.
	const LocalVector<Adapter::Entry> top = children_of(adapter, PackedStringArray());
	REQUIRE(top.size() == 1);
	CHECK(top[0].key == "game");
	CHECK(top[0].type == "Element");

	const LocalVector<Adapter::Entry> game = children_of(adapter, path_of({ "game" }));
	REQUIRE(game.size() == 6);
	CHECK(game[0].segment == "@version");
	CHECK(game[0].type == "Attribute");
	CHECK(game[1].segment == "title");
	CHECK(game[1].kind == Adapter::NODE_KIND_VALUE); // An element with only text is a value.
	CHECK(game[1].value == "Quest");
	CHECK(game[2].segment == "units");
	CHECK(game[2].kind == Adapter::NODE_KIND_INDEXED); // Same-tag children are a list.
	CHECK(game[3].segment == "spawn");
	CHECK(game[4].segment == "spawn[1]");
	CHECK(game[5].segment == "note");

	CHECK(adapter->get_value(path_of({ "game", "units", "1", "@id" })) == Variant("archer"));
	CHECK(adapter->format_path(path_of({ "game", "units", "1", "@hp" })) == "game/units/1/@hp");
	CHECK(adapter->get_value(path_of({ "game", "spawn[1]", "@x" })) == Variant("2"));
	// Mixed content: the text is one value "#text".
	const LocalVector<Adapter::Entry> note = children_of(adapter, path_of({ "game", "note" }));
	REQUIRE(note.size() == 2);
	CHECK(note[0].segment == "#text");
	CHECK(note[1].segment == "b");
	CHECK(adapter->get_value_types(PackedStringArray()) == PackedInt32Array({ Variant::STRING }));
}

TEST_CASE("[DataTreeAdapter][XML] Edits through XMLElement") {
	Ref<XML> xml;
	xml.instantiate();
	REQUIRE(xml->parse(xml_text) == OK);
	const Ref<Adapter> adapter = Adapter::create_for(xml);

	CHECK(adapter->set_value(path_of({ "game", "@version" }), 3) == OK);
	CHECK(xml->get_root()->get_attribute("version") == "3");
	CHECK(adapter->set_value(path_of({ "game", "title" }), "Saga") == OK);
	CHECK(adapter->get_value(path_of({ "game", "title" })) == Variant("Saga"));
	CHECK(adapter->set_value(path_of({ "game", "units" }), "x") == ERR_INVALID_PARAMETER);

	// New values become attributes; a new list item copies the tag of its siblings.
	CHECK(adapter->add_value(path_of({ "game", "spawn" }), "y", 5) == OK);
	CHECK(adapter->get_value(path_of({ "game", "spawn", "@y" })) == Variant("5"));
	CHECK(adapter->add_value(path_of({ "game", "spawn" }), "@x", 1) == ERR_ALREADY_EXISTS);
	CHECK(adapter->add_container(path_of({ "game", "units" }), String(), Adapter::NODE_KIND_NAMED) == OK);
	CHECK(adapter->get_child_count(path_of({ "game", "units" })) == 3);
	CHECK(adapter->get_value(path_of({ "game", "units", "2" })) == Variant(""));
	CHECK(adapter->add_container(path_of({ "game" }), "rules", Adapter::NODE_KIND_NAMED) == OK);
	CHECK(adapter->get_kind(path_of({ "game", "rules" })) == Adapter::NODE_KIND_VALUE); // An empty element.

	// Rename keeps the order of the attributes.
	CHECK(adapter->rename(path_of({ "game", "spawn", "@x" }), "left") == OK);
	const LocalVector<Adapter::Entry> spawn = children_of(adapter, path_of({ "game", "spawn" }));
	REQUIRE(spawn.size() == 2);
	CHECK(spawn[0].segment == "@left");
	CHECK(spawn[1].segment == "@y");
	CHECK(adapter->rename(path_of({ "game", "title" }), "name") == OK);
	CHECK(adapter->get_value(path_of({ "game", "name" })) == Variant("Saga"));

	CHECK(adapter->move(path_of({ "game", "units", "1" }), 0) == OK);
	CHECK(adapter->get_value(path_of({ "game", "units", "0", "@id" })) == Variant("archer"));
	CHECK(adapter->remove(path_of({ "game", "units", "2" })) == OK);
	CHECK(adapter->get_child_count(path_of({ "game", "units" })) == 2);
	CHECK(adapter->remove(path_of({ "game", "note", "#text" })) == OK);
	CHECK(adapter->get_child_count(path_of({ "game", "note" })) == 1);
	CHECK(adapter->remove(path_of({ "game", "@version" })) == OK);
	CHECK_FALSE(xml->get_root()->has_attribute("version"));

	CHECK(adapter->remove(path_of({ "game" })) == OK);
	CHECK(adapter->get_child_count(PackedStringArray()) == 0);
	CHECK(adapter->add_container(PackedStringArray(), "world", Adapter::NODE_KIND_NAMED) == OK);
	CHECK(xml->get_root()->get_name() == "world");
	CHECK(adapter->add_container(PackedStringArray(), "second", Adapter::NODE_KIND_NAMED) == ERR_ALREADY_EXISTS);
}

static Ref<CSV> make_path_csv() {
	Ref<CSV> csv;
	csv.instantiate();
	csv->set_header(PackedStringArray({ "Path", "Key", "Value" }));
	Array rows;
	rows.push_back(PackedStringArray({ "", "version", "2" }));
	rows.push_back(PackedStringArray({ "This/Is/A", "Test", "42" }));
	rows.push_back(PackedStringArray({ "MyItems/0", "Name", "Sword" }));
	rows.push_back(PackedStringArray({ "MyItems/1", "Name", "Shield" }));
	rows.push_back(PackedStringArray({ "spawn", "", "" }));
	csv->set_data(rows);
	return csv;
}

TEST_CASE("[DataTreeAdapter][CSV] The path form is a tree") {
	const Ref<CSV> csv = make_path_csv();
	CHECK(CSVTreeAdapter::is_path_form(csv));
	const Ref<Adapter> adapter = Adapter::create_for(csv);
	REQUIRE(Object::cast_to<CSVTreeAdapter>(adapter.ptr()) != nullptr);

	const LocalVector<Adapter::Entry> top = children_of(adapter, PackedStringArray());
	REQUIRE(top.size() == 4);
	CHECK(top[0].key == "version");
	CHECK(top[0].type == "String");
	CHECK(top[2].key == "MyItems");
	CHECK(top[2].kind == Adapter::NODE_KIND_INDEXED);
	CHECK(top[3].key == "spawn"); // A row with no key keeps an empty container.
	CHECK(top[3].kind == Adapter::NODE_KIND_NAMED);
	CHECK(top[3].child_count == 0);
	CHECK(adapter->get_value(path_of({ "This", "Is", "A", "Test" })) == Variant("42"));
	CHECK(adapter->format_path(path_of({ "This", "Is", "A", "Test" })) == "This/Is/A,Test");
	CHECK(adapter->get_value_types(PackedStringArray()) == PackedInt32Array({ Variant::STRING }));
}

TEST_CASE("[DataTreeAdapter][CSV] Edits through the CSV API") {
	const Ref<CSV> csv = make_path_csv();
	const Ref<Adapter> adapter = Adapter::create_for(csv);

	CHECK(adapter->set_value(path_of({ "This", "Is", "A", "Test" }), 43) == OK);
	CHECK(csv->get_cell(1, 2) == "43");
	CHECK(adapter->add_value(path_of({ "spawn" }), "x", 1) == OK);
	CHECK(adapter->get_value(path_of({ "spawn", "x" })) == Variant("1"));
	CHECK(adapter->remove(path_of({ "MyItems", "0" })) == OK);
	CHECK(adapter->get_value(path_of({ "MyItems", "0", "Name" })) == Variant("Shield"));
	CHECK(adapter->rename(path_of({ "This" }), "That") == OK);
	CHECK(adapter->get_value(path_of({ "That", "Is", "A", "Test" })) == Variant("43"));
	CHECK(adapter->remove(path_of({ "version" })) == OK);
	CHECK(adapter->get_child_count(PackedStringArray()) == 3);
	// The header stays, so the file stays a tree.
	CHECK(CSVTreeAdapter::is_path_form(csv));
	for (int i = 0; i < csv->get_row_count(); i++) {
		CHECK(csv->get_row(i).size() == 3);
	}
}

TEST_CASE("[DataTreeAdapter] Big data: the first page stays cheap") {
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
	Ref<JSON> json;
	json.instantiate();
	json->set_data(objects);
	const Ref<Adapter> json_adapter = Adapter::create_for(json);
	uint64_t start = OS::get_singleton()->get_ticks_usec();
	CHECK(children_of(json_adapter, PackedStringArray(), 0, 200).size() == 200);
	const uint64_t json_usec = OS::get_singleton()->get_ticks_usec() - start;

	// INI: 10 000 numbered sections; the first read builds the tree.
	Dictionary sections;
	for (int i = 0; i < 10000; i++) {
		Dictionary keys;
		keys["id"] = i;
		keys["name"] = vformat("unit_%d", i);
		sections[vformat("units/%d", i)] = keys;
	}
	Ref<INI> ini;
	ini.instantiate();
	ini->set_data(sections);
	const Ref<Adapter> ini_adapter = Adapter::create_for(ini);
	start = OS::get_singleton()->get_ticks_usec();
	CHECK(children_of(ini_adapter, path_of({ "units" }), 0, 200).size() == 200);
	const uint64_t ini_usec = OS::get_singleton()->get_ticks_usec() - start;
	CHECK(ini_adapter->get_kind(path_of({ "units" })) == Adapter::NODE_KIND_INDEXED);

	// 20 000 CSV rows: the preview reads the first 100.
	Array rows;
	for (int i = 0; i < 20000; i++) {
		rows.push_back(PackedStringArray({ vformat("unit_%d", i), "warrior", itos(i % 200), i % 2 ? "true" : "false" }));
	}
	start = OS::get_singleton()->get_ticks_usec();
	CHECK(CSVInspectorView::get_column_chars(PackedStringArray({ "name", "class", "hp", "ranged" }), rows, 100, 32, 24).size() == 4);
	const uint64_t csv_usec = OS::get_singleton()->get_ticks_usec() - start;
	MESSAGE(vformat("First page of 20 000 deep JSON objects: %d us; first page of 10 000 INI list sections (with the build): %d us; CSV preview widths of 20 000 rows: %d us.", json_usec, ini_usec, csv_usec));
}

} // namespace TestDataTreeAdapter

#endif // TOOLS_ENABLED
