/**************************************************************************/
/*  test_yaml.h                                                           */
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

#pragma once

#include "../yaml.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/math/math_defs.h"
#include "core/object/class_db.h"
#include "core/variant/typed_array.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifdef TOOLS_ENABLED
#include "../editor/yaml_inspector_plugin.h"
#include "../editor/yaml_tree_adapter.h"
#endif

namespace TestYAML {

class YAMLTestEntry : public Resource {
	GDCLASS(YAMLTestEntry, Resource);

	String name;
	int64_t level = 1;
	double speed = 1.0;
	int mode = 0;
	Vector2 offset;
	Ref<YAMLTestEntry> child;

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_entry_name", "value"), &YAMLTestEntry::set_entry_name);
		ClassDB::bind_method(D_METHOD("get_entry_name"), &YAMLTestEntry::get_entry_name);
		ClassDB::bind_method(D_METHOD("set_level", "value"), &YAMLTestEntry::set_level);
		ClassDB::bind_method(D_METHOD("get_level"), &YAMLTestEntry::get_level);
		ClassDB::bind_method(D_METHOD("set_speed", "value"), &YAMLTestEntry::set_speed);
		ClassDB::bind_method(D_METHOD("get_speed"), &YAMLTestEntry::get_speed);
		ClassDB::bind_method(D_METHOD("set_mode", "value"), &YAMLTestEntry::set_mode);
		ClassDB::bind_method(D_METHOD("get_mode"), &YAMLTestEntry::get_mode);
		ClassDB::bind_method(D_METHOD("set_offset", "value"), &YAMLTestEntry::set_offset);
		ClassDB::bind_method(D_METHOD("get_offset"), &YAMLTestEntry::get_offset);
		ClassDB::bind_method(D_METHOD("set_child", "value"), &YAMLTestEntry::set_child);
		ClassDB::bind_method(D_METHOD("get_child"), &YAMLTestEntry::get_child);

		ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "set_entry_name", "get_entry_name");
		ADD_PROPERTY(PropertyInfo(Variant::INT, "level"), "set_level", "get_level");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed"), "set_speed", "get_speed");
		ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Calm,Angry:5,Happy"), "set_mode", "get_mode");
		ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset"), "set_offset", "get_offset");
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "child", PROPERTY_HINT_RESOURCE_TYPE, "YAMLTestEntry"), "set_child", "get_child");
	}

public:
	void set_entry_name(const String &p_value) { name = p_value; }
	String get_entry_name() const { return name; }
	void set_level(int64_t p_value) { level = p_value; }
	int64_t get_level() const { return level; }
	void set_speed(double p_value) { speed = p_value; }
	double get_speed() const { return speed; }
	void set_mode(int p_value) { mode = p_value; }
	int get_mode() const { return mode; }
	void set_offset(const Vector2 &p_value) { offset = p_value; }
	Vector2 get_offset() const { return offset; }
	void set_child(const Ref<YAMLTestEntry> &p_value) { child = p_value; }
	Ref<YAMLTestEntry> get_child() const { return child; }
};

class YAMLTestSettings : public Resource {
	GDCLASS(YAMLTestSettings, Resource);

	double volume = 0.5;
	String title = "Default";
	String notes;
	Ref<YAMLTestEntry> main;
	Ref<YAMLTestEntry> spare;
	TypedArray<YAMLTestEntry> entries;
	Dictionary extra;
	PackedInt32Array numbers;
	Color tint = Color(1, 1, 1, 1);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_volume", "value"), &YAMLTestSettings::set_volume);
		ClassDB::bind_method(D_METHOD("get_volume"), &YAMLTestSettings::get_volume);
		ClassDB::bind_method(D_METHOD("set_title", "value"), &YAMLTestSettings::set_title);
		ClassDB::bind_method(D_METHOD("get_title"), &YAMLTestSettings::get_title);
		ClassDB::bind_method(D_METHOD("set_notes", "value"), &YAMLTestSettings::set_notes);
		ClassDB::bind_method(D_METHOD("get_notes"), &YAMLTestSettings::get_notes);
		ClassDB::bind_method(D_METHOD("set_main", "value"), &YAMLTestSettings::set_main);
		ClassDB::bind_method(D_METHOD("get_main"), &YAMLTestSettings::get_main);
		ClassDB::bind_method(D_METHOD("set_spare", "value"), &YAMLTestSettings::set_spare);
		ClassDB::bind_method(D_METHOD("get_spare"), &YAMLTestSettings::get_spare);
		ClassDB::bind_method(D_METHOD("set_entries", "value"), &YAMLTestSettings::set_entries);
		ClassDB::bind_method(D_METHOD("get_entries"), &YAMLTestSettings::get_entries);
		ClassDB::bind_method(D_METHOD("set_extra", "value"), &YAMLTestSettings::set_extra);
		ClassDB::bind_method(D_METHOD("get_extra"), &YAMLTestSettings::get_extra);
		ClassDB::bind_method(D_METHOD("set_numbers", "value"), &YAMLTestSettings::set_numbers);
		ClassDB::bind_method(D_METHOD("get_numbers"), &YAMLTestSettings::get_numbers);
		ClassDB::bind_method(D_METHOD("set_tint", "value"), &YAMLTestSettings::set_tint);
		ClassDB::bind_method(D_METHOD("get_tint"), &YAMLTestSettings::get_tint);

		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume"), "set_volume", "get_volume");
		ADD_PROPERTY(PropertyInfo(Variant::STRING, "title"), "set_title", "get_title");
		ADD_PROPERTY(PropertyInfo(Variant::STRING, "notes"), "set_notes", "get_notes");
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "main", PROPERTY_HINT_RESOURCE_TYPE, "YAMLTestEntry"), "set_main", "get_main");
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spare", PROPERTY_HINT_RESOURCE_TYPE, "YAMLTestEntry"), "set_spare", "get_spare");
		ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entries", PROPERTY_HINT_ARRAY_TYPE, "YAMLTestEntry"), "set_entries", "get_entries");
		ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "extra"), "set_extra", "get_extra");
		ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "numbers"), "set_numbers", "get_numbers");
		ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");
	}

public:
	void set_volume(double p_value) { volume = p_value; }
	double get_volume() const { return volume; }
	void set_title(const String &p_value) { title = p_value; }
	String get_title() const { return title; }
	void set_notes(const String &p_value) { notes = p_value; }
	String get_notes() const { return notes; }
	void set_main(const Ref<YAMLTestEntry> &p_value) { main = p_value; }
	Ref<YAMLTestEntry> get_main() const { return main; }
	void set_spare(const Ref<YAMLTestEntry> &p_value) { spare = p_value; }
	Ref<YAMLTestEntry> get_spare() const { return spare; }
	void set_entries(const TypedArray<YAMLTestEntry> &p_value) { entries = p_value; }
	TypedArray<YAMLTestEntry> get_entries() const { return entries; }
	void set_extra(const Dictionary &p_value) { extra = p_value; }
	Dictionary get_extra() const { return extra; }
	void set_numbers(const PackedInt32Array &p_value) { numbers = p_value; }
	PackedInt32Array get_numbers() const { return numbers; }
	void set_tint(const Color &p_value) { tint = p_value; }
	Color get_tint() const { return tint; }
};

inline void register_yaml_test_classes() {
	static bool registered = false;
	if (!registered) {
		GDREGISTER_CLASS(YAMLTestEntry);
		GDREGISTER_CLASS(YAMLTestSettings);
		registered = true;
	}
}

inline Ref<YAMLTestEntry> make_yaml_entry(const String &p_name, int64_t p_level, int p_mode) {
	Ref<YAMLTestEntry> entry;
	entry.instantiate();
	entry->set_entry_name(p_name);
	entry->set_level(p_level);
	entry->set_mode(p_mode);
	entry->set_speed(0.1 * double(p_level));
	entry->set_offset(Vector2(0.1, -2.5));
	return entry;
}

inline Ref<YAMLTestSettings> make_yaml_settings() {
	register_yaml_test_classes();
	Ref<YAMLTestSettings> settings;
	settings.instantiate();
	settings->set_volume(0.8);
	settings->set_title("Main \"menu\": line\nbreak\tand tab");
	settings->set_notes("First line\n  indented # not a comment\n\nlast\n");
	Ref<YAMLTestEntry> main = make_yaml_entry("Ann", 3, 5);
	main->set_child(make_yaml_entry("Ann's child", 4, 6));
	settings->set_main(main);
	TypedArray<YAMLTestEntry> entries;
	entries.push_back(make_yaml_entry("Bob", 1, 0));
	Ref<YAMLTestEntry> cleo = make_yaml_entry("- Cleo, the 2nd", 2, 6);
	cleo->set_child(make_yaml_entry("Dan", 7, 0));
	entries.push_back(cleo);
	settings->set_entries(entries);
	Dictionary extra;
	extra["difficulty"] = "hard";
	extra["lives"] = 3;
	extra["key with spaces"] = true;
	extra["looks like a number"] = "42";
	settings->set_extra(extra);
	PackedInt32Array numbers;
	numbers.push_back(4);
	numbers.push_back(-7);
	settings->set_numbers(numbers);
	settings->set_tint(Color(1, 0, 0, 1));
	return settings;
}

inline void check_yaml_entry(const Ref<YAMLTestEntry> &p_entry, const String &p_name, int64_t p_level, int p_mode) {
	REQUIRE(p_entry.is_valid());
	CHECK(p_entry->get_entry_name() == p_name);
	CHECK(p_entry->get_level() == p_level);
	CHECK(p_entry->get_mode() == p_mode);
	CHECK(p_entry->get_speed() == doctest::Approx(0.1 * double(p_level)));
	CHECK(p_entry->get_offset().is_equal_approx(Vector2(0.1, -2.5)));
}

inline void check_yaml_settings(const Ref<Resource> &p_resource) {
	const Ref<YAMLTestSettings> settings = p_resource;
	REQUIRE(settings.is_valid());
	CHECK(settings->get_volume() == 0.8);
	CHECK(settings->get_title() == "Main \"menu\": line\nbreak\tand tab");
	CHECK(settings->get_notes() == "First line\n  indented # not a comment\n\nlast\n");
	check_yaml_entry(settings->get_main(), "Ann", 3, 5);
	check_yaml_entry(settings->get_main()->get_child(), "Ann's child", 4, 6);
	CHECK(settings->get_spare().is_null());
	const TypedArray<YAMLTestEntry> entries = settings->get_entries();
	REQUIRE(entries.size() == 2);
	check_yaml_entry(entries[0], "Bob", 1, 0);
	check_yaml_entry(entries[1], "- Cleo, the 2nd", 2, 6);
	const Ref<YAMLTestEntry> cleo = entries[1];
	check_yaml_entry(cleo->get_child(), "Dan", 7, 0);
	const Dictionary extra = settings->get_extra();
	CHECK(String(extra.get("difficulty", "")) == "hard");
	CHECK(int(extra.get("lives", 0)) == 3);
	CHECK(bool(extra.get("key with spaces", false)));
	CHECK(extra.get("looks like a number", Variant()).get_type() == Variant::STRING);
	const PackedInt32Array numbers = settings->get_numbers();
	REQUIRE(numbers.size() == 2);
	CHECK(numbers[0] == 4);
	CHECK(numbers[1] == -7);
	CHECK(settings->get_tint() == Color(1, 0, 0, 1));
}

TEST_CASE("[YAML] Parse block and flow collections") {
	const String text = R"(# Comment line
title: Main menu # Comment after a value
"quoted key": 1
list:
- one
- two
nested:
  - [1, 2, {a: b}]
  - key: value
    other: 2
  - - deep
    - deeper
  -
    late: true
flow: {x: 1, y: [true, null], "q k": 'v',
  z: [
    3,
  ]}
empty:
last: {}
)";
	YAML yaml;
	REQUIRE(yaml.parse(text) == OK);
	CHECK(yaml.get_error_line() == 0);
	const Dictionary data = yaml.get_data();
	CHECK(String(data["title"]) == "Main menu");
	CHECK(int(data["quoted key"]) == 1);
	const Array list = data["list"];
	REQUIRE(list.size() == 2);
	CHECK(String(list[0]) == "one");
	CHECK(String(list[1]) == "two");

	const Array nested = data["nested"];
	REQUIRE(nested.size() == 4);
	const Array first = nested[0];
	REQUIRE(first.size() == 3);
	CHECK(int(first[1]) == 2);
	CHECK(String(Dictionary(first[2])["a"]) == "b");
	const Dictionary second = nested[1];
	CHECK(String(second["key"]) == "value");
	CHECK(int(second["other"]) == 2);
	const Array third = nested[2];
	REQUIRE(third.size() == 2);
	CHECK(String(third[1]) == "deeper");
	CHECK(bool(Dictionary(nested[3])["late"]));

	const Dictionary flow = data["flow"];
	CHECK(int(flow["x"]) == 1);
	const Array y = flow["y"];
	REQUIRE(y.size() == 2);
	CHECK(bool(y[0]));
	CHECK(y[1].get_type() == Variant::NIL);
	CHECK(String(flow["q k"]) == "v");
	CHECK(int(Array(flow["z"])[0]) == 3);
	CHECK(data.has("empty"));
	CHECK(data["empty"].get_type() == Variant::NIL);
	CHECK(Dictionary(data["last"]).is_empty());

	// Keys keep the order of the file.
	const Array keys = data.keys();
	REQUIRE(keys.size() == 7);
	CHECK(String(keys[0]) == "title");
	CHECK(String(keys[1]) == "quoted key");
	CHECK(String(keys[6]) == "last");

	// Any node can be the root.
	CHECK(Array(YAML::parse_string("- 1\n- 2\n")).size() == 2);
	CHECK(int(YAML::parse_string("42")) == 42);
	CHECK(YAML::parse_string("").get_type() == Variant::NIL);
	CHECK(YAML::parse_string("# only a comment\n").get_type() == Variant::NIL);
	CHECK(int(Dictionary(YAML::parse_string("%YAML 1.2\n---\na: 1\n...\n# end\n"))["a"]) == 1);
}

TEST_CASE("[YAML] Scalars of the core schema") {
	const String text = R"(int: 12
neg: -12
hex: 0x1F
octal: 0o17
ratio: .5
big: 1e3
float: 2.0
inf: -.inf
nan: .nan
yes: yes
flag: True
off: FALSE
none: ~
null_word: Null
version: 1.2.3
quoted_int: "12"
tagged_str: !!str 12
tagged_float: !!float 3
double: "tab\there \u00e9 \"q\" \\ \x41"
single: 'it''s # no comment'
url: http://example.com/a#b
folded_plain: this is
  folded text

  with a break
)";
	const Dictionary data = YAML::parse_string(text);
	REQUIRE_FALSE(data.is_empty());
	CHECK(data["int"].get_type() == Variant::INT);
	CHECK(int64_t(data["int"]) == 12);
	CHECK(int64_t(data["neg"]) == -12);
	CHECK(int64_t(data["hex"]) == 31);
	CHECK(int64_t(data["octal"]) == 15);
	CHECK(data["ratio"].get_type() == Variant::FLOAT);
	CHECK(double(data["ratio"]) == 0.5);
	CHECK(double(data["big"]) == 1000.0);
	CHECK(data["float"].get_type() == Variant::FLOAT);
	CHECK(double(data["inf"]) == -Math::INF);
	CHECK(Math::is_nan(double(data["nan"])));
	CHECK(data["yes"].get_type() == Variant::STRING); // YAML 1.2: only true and false are booleans.
	CHECK(data["flag"].get_type() == Variant::BOOL);
	CHECK(bool(data["flag"]));
	CHECK_FALSE(bool(data["off"]));
	CHECK(data["none"].get_type() == Variant::NIL);
	CHECK(data["null_word"].get_type() == Variant::NIL);
	CHECK(String(data["version"]) == "1.2.3");
	CHECK(data["quoted_int"].get_type() == Variant::STRING);
	CHECK(data["tagged_str"].get_type() == Variant::STRING);
	CHECK(String(data["tagged_str"]) == "12");
	CHECK(data["tagged_float"].get_type() == Variant::FLOAT);
	CHECK(String(data["double"]) == String::utf8("tab\there \xC3\xA9 \"q\" \\ A"));
	CHECK(String(data["single"]) == "it's # no comment");
	CHECK(String(data["url"]) == "http://example.com/a#b");
	CHECK(String(data["folded_plain"]) == "this is folded text\nwith a break");

	CHECK(int64_t(YAML::parse_string("9223372036854775807")) == INT64_MAX);
	CHECK(int64_t(YAML::parse_string("-9223372036854775808")) == INT64_MIN);
}

TEST_CASE("[YAML] Block scalars") {
	const String text = R"(literal: |
  line 1
   indented
  line 3

folded: >-
  a
  b

  c
keep: |+
  x

strip: |-
  y
indented: |2
    two more
quoted: "first
  second

  third"
end: done
)";
	const Dictionary data = YAML::parse_string(text);
	REQUIRE_FALSE(data.is_empty());
	CHECK(String(data["literal"]) == "line 1\n indented\nline 3\n");
	CHECK(String(data["folded"]) == "a b\nc");
	CHECK(String(data["keep"]) == "x\n\n");
	CHECK(String(data["strip"]) == "y");
	CHECK(String(data["indented"]) == "  two more\n");
	CHECK(String(data["quoted"]) == "first second\nthird");
	CHECK(String(data["end"]) == "done");
}

TEST_CASE("[YAML] Errors with line and column") {
	struct Case {
		const char *text;
		int line;
		const char *message;
	};
	const Case cases[] = {
		{ "a:\n  b: 1\n c: 2\n", 3, "indentation" }, // Less indented than "b", more than "a".
		{ "a: 1\n  b: 2\n", 2, "indentation" }, // More indented than the key before.
		{ "list: [1, 2\nnext: 3\n", 1, "not closed" }, // Unclosed flow sequence.
		{ "list: [1, 2", 1, "not closed" },
		{ "map: {a: 1\n", 1, "not closed" },
		{ "a: [1, 2]]\n", 1, "Unexpected" },
		{ "a: \"open\n", 1, "not closed" },
		{ "a: &anchor 1\n", 1, "Anchors" },
		{ "a: *alias\n", 1, "Aliases" },
		{ "a: !custom 1\n", 1, "tag" },
		{ "a: !!int abc\n", 1, "!!int" },
		{ "<<: {a: 1}\n", 1, "Merge keys" },
		{ "? a\n: b\n", 1, "Complex" },
		{ "---\n- a\n---\nb: c: d\n", 4, "mapping value" }, // Errors in later documents have their line.
		{ "--- a: 1\n", 1, "'---'" },
		{ "a: 1\na: 2\n", 2, "Duplicate" },
		{ "a:\n\tb: 1\n", 2, "Tabs" },
		{ "a: 99999999999999999999\n", 1, "64-bit" },
		{ "a: b: c\n", 1, "mapping value" },
		{ "- a\nb: 1\n", 2, "Unexpected content" },
		{ "a: \"bad \\q escape\"\n", 1, "escape" },
		{ "a: [a: 1]\n", 1, "braces" },
		{ "%TAG ! tag:example.com,2000:\n---\na: 1\n", 1, "%TAG" },
	};
	for (const Case &c : cases) {
		YAML yaml;
		CHECK_MESSAGE(yaml.parse(String(c.text)) == ERR_PARSE_ERROR, c.text);
		CHECK_MESSAGE(yaml.get_error_line() == c.line, c.text);
		CHECK_MESSAGE(yaml.get_error_column() > 0, c.text);
		CHECK_MESSAGE(yaml.get_error_message().contains(c.message), c.text);
		CHECK_MESSAGE(yaml.get_data().get_type() == Variant::NIL, c.text);
	}

	ERR_PRINT_OFF;
	CHECK(YAML::parse_string("a: [").get_type() == Variant::NIL);
	ERR_PRINT_ON;

	// Too deep for the stack of the Web export.
	YAML deep;
	CHECK(deep.parse(String("[").repeat(300) + String("]").repeat(300)) == ERR_PARSE_ERROR);
	CHECK(deep.get_error_message().contains("deeper"));
}

TEST_CASE("[YAML] Stringify and read back") {
	Dictionary data;
	data["name"] = "Game";
	data["$type"] = "Settings";
	Dictionary window;
	window["width"] = 1280;
	window["scale"] = 1.5;
	window["integral"] = 2.0;
	data["window"] = window;
	PackedStringArray tags;
	tags.push_back("a");
	tags.push_back("b: c");
	data["tags"] = tags;
	Array players;
	Dictionary p1;
	p1["name"] = "Ann";
	p1["level"] = 3;
	players.push_back(p1);
	Dictionary p2;
	p2["name"] = "Line\nbreak";
	players.push_back(p2);
	Array pair;
	pair.push_back(1);
	pair.push_back(2);
	players.push_back(pair);
	data["players"] = players;
	data["empty"] = Dictionary();
	data["none"] = Array();
	data["nothing"] = Variant();
	data["looks like bool"] = "true";
	data["special"] = Math::INF;
	data["# hash"] = "-dash";
	data["vector"] = Vector2(1, 2);

	Ref<SerializerOptions> options;
	options.instantiate();
	options->set_indent("  ");
	const String text = YAML::stringify(data, options);
	CHECK(text.begins_with("name: Game\n$type: Settings\nwindow:\n  width: 1280\n  scale: 1.5\n  integral: 2.0\n"));
	CHECK(text.contains("\ntags:\n  - a\n  - \"b: c\"\n"));
	CHECK(text.contains("\nplayers:\n  - name: Ann\n    level: 3\n  - name: |-\n      Line\n      break\n  - [1, 2]\n"));
	CHECK(text.contains("\nempty: {}\nnone: []\nnothing: null\nlooks like bool: \"true\"\nspecial: .inf\n\"# hash\": \"-dash\"\n"));

	const Dictionary back = YAML::parse_string(text);
	CHECK(String(back["name"]) == "Game");
	CHECK(String(back["$type"]) == "Settings");
	CHECK(Dictionary(back["window"])["integral"].get_type() == Variant::FLOAT);
	CHECK(double(Dictionary(back["window"])["integral"]) == 2.0);
	CHECK(String(Array(back["tags"])[1]) == "b: c");
	const Array players_back = back["players"];
	REQUIRE(players_back.size() == 3);
	CHECK(int(Dictionary(players_back[0])["level"]) == 3);
	CHECK(String(Dictionary(players_back[1])["name"]) == "Line\nbreak");
	CHECK(int(Array(players_back[2])[1]) == 2);
	CHECK(Dictionary(back["empty"]).is_empty());
	CHECK(Array(back["none"]).is_empty());
	CHECK(back["nothing"].get_type() == Variant::NIL);
	CHECK(back["looks like bool"].get_type() == Variant::STRING);
	CHECK(double(back["special"]) == Math::INF);
	CHECK(String(back["# hash"]) == "-dash");
	CHECK(String(back["vector"]) == String(Vector2(1, 2)));

	// Sorted keys, four spaces.
	options->set_sort_keys(true);
	options->set_indent("    ");
	const String sorted = YAML::stringify(window, options);
	CHECK(sorted == "integral: 2.0\nscale: 1.5\nwidth: 1280\n");
	CHECK(YAML::stringify(data, options).contains("\nwindow:\n    integral: 2.0\n"));

	// An empty indent writes flow style on one line.
	options->set_sort_keys(false);
	options->set_indent("");
	const String flow = YAML::stringify(players, options);
	CHECK(flow == "[{name: Ann, level: 3}, {name: \"Line\\nbreak\"}, [1, 2]]\n");
	CHECK(Array(YAML::parse_string(flow)).size() == 3);

	// Objects and cycles can't be written.
	Dictionary invalid;
	invalid["object"] = options;
	Array cycle;
	cycle.push_back(cycle);
	ERR_PRINT_OFF;
	CHECK(YAML::stringify(invalid).is_empty());
	CHECK(YAML::stringify(cycle).is_empty());
	ERR_PRINT_ON;
	cycle.clear();
}

TEST_CASE("[YAML] Resource round trip as text") {
	const Ref<YAMLTestSettings> settings = make_yaml_settings();
	const String text = YAML::serialize(settings);
	CHECK(text.begins_with("$type: YAMLTestSettings\n$format_version: 1\n"));
	CHECK(text.find("volume: ") < text.find("title: "));
	CHECK(text.contains("\nnotes: |\n  First line\n    indented # not a comment\n\n  last\n"));
	CHECK(text.contains("\nspare: null\n"));
	CHECK(text.contains("\nmain:\n  "));
	CHECK(text.contains("\n  name: Ann\n"));
	CHECK(text.contains("\n  offset: [")); // Vectors are number arrays, written in flow style.
	CHECK(text.contains("mode: Angry"));
	CHECK(text.contains("\"- Cleo, the 2nd\""));
	CHECK(text.contains("\n  key with spaces: true\n"));
	CHECK(text.contains("\n  looks like a number: \"42\"\n"));
	CHECK(text.contains("\nnumbers: [4, -7]\n"));
	CHECK(text.count("$type") == 1);

	check_yaml_settings(YAML::deserialize(text));

	// Without "$type", the type hint gives the class.
	const String untyped = text.replace("$type: YAMLTestSettings\n", "");
	check_yaml_settings(YAML::deserialize(untyped, "YAMLTestSettings"));
	ERR_PRINT_OFF;
	CHECK(YAML::deserialize(untyped).is_null());
	ERR_PRINT_ON;

	Ref<SerializerOptions> numbers_options;
	numbers_options.instantiate();
	numbers_options->set_enums_as_names(false);
	const String numbers = YAML::serialize(settings, numbers_options);
	CHECK(numbers.contains("mode: 5"));
	check_yaml_settings(YAML::deserialize(numbers));
}

TEST_CASE("[YAML] Hand-written resource file") {
	register_yaml_test_classes();
	const String text = R"(# Settings written by hand.
$type: YAMLTestSettings
volume: 1
title: 'Literal \ title'
entries:
- {name: Inline, level: 2, mode: Happy}
- name: Block
  level: 0x10
main:
  name: Ann
  offset: [1.5, 2]
)";
	const Ref<YAMLTestSettings> settings = YAML::deserialize(text);
	REQUIRE(settings.is_valid());
	CHECK(settings->get_volume() == 1.0);
	CHECK(settings->get_title() == "Literal \\ title");
	REQUIRE(settings->get_main().is_valid());
	CHECK(settings->get_main()->get_offset() == Vector2(1.5, 2));
	const TypedArray<YAMLTestEntry> entries = settings->get_entries();
	REQUIRE(entries.size() == 2);
	const Ref<YAMLTestEntry> inline_entry = entries[0];
	CHECK(inline_entry->get_entry_name() == "Inline");
	CHECK(inline_entry->get_mode() == 6);
	const Ref<YAMLTestEntry> block_entry = entries[1];
	CHECK(block_entry->get_level() == 16);
}

TEST_CASE("[YAML] Through ResourceSaver and ResourceLoader") {
	const Ref<YAMLTestSettings> settings = make_yaml_settings();
	const char *extensions[] = { "yaml", "yml" };
	for (const char *extension : extensions) {
		const String path = TestUtils::get_temp_path(String("yaml_settings.") + extension);
		CHECK(ResourceSaver::save(Ref<Resource>(settings), path) == OK);
		CHECK(ResourceLoader::get_resource_type(path) == "YAMLTestSettings");

		check_yaml_settings(ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE));
		check_yaml_settings(ResourceLoader::load(path, "YAMLTestSettings", ResourceFormatLoader::CACHE_MODE_IGNORE));
		DirAccess::remove_absolute(path);
	}
}

TEST_CASE("[YAML] Resource API: slash paths and plain files") {
	Ref<YAML> yaml;
	yaml.instantiate();
	CHECK(yaml->get_recognized_extensions() == PackedStringArray({ "yaml", "yml" }));
	REQUIRE(yaml->parse("enemies:\n  - name: Orc\n    hp: 10\n  - name: Elf\n") == OK);
	CHECK(yaml->get_value("enemies/0/name") == "Orc");
	CHECK(yaml->get_value("enemies/1/hp", -1) == Variant(-1));
	CHECK(yaml->has_value("enemies/1"));
	CHECK_FALSE(yaml->has_value("enemies/2"));
	yaml->set_value("enemies/1/hp", 5);
	yaml->set_value("enemies/2", "Troll"); // Index == size appends.
	yaml->set_value("meta/" + YAML::escape_key("a/b"), true);
	CHECK(bool(yaml->get_value("meta/a~1b")) == true);
	CHECK(YAML::split_path("x/a~1b~0c") == Vector<String>({ "x", "a/b~c" }));
	yaml->erase_value("enemies/0");
	CHECK(yaml->get_value("enemies/0/name") == "Elf");

	for (const String extension : { "yaml", "yml" }) {
		const String path = TestUtils::get_temp_path("yaml_plain." + extension);
		REQUIRE(ResourceSaver::save(yaml, path) == OK);
		CHECK_FALSE(FileAccess::get_file_as_string(path).contains("$type"));
		CHECK(ResourceLoader::get_resource_type(path) == "YAML");
		const Ref<YAML> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
		REQUIRE(loaded.is_valid());
		CHECK(int(loaded->get_value("enemies/0/hp")) == 5);
		DirAccess::remove_absolute(path);
	}
}

TEST_CASE("[YAML] Several documents") {
	Ref<YAML> yaml;
	yaml.instantiate();
	REQUIRE(yaml->parse("a: 1\n---\n- x\n...\n---\n# empty\n...\nb: 2\n") == OK);
	REQUIRE(yaml->get_document_count() == 4);
	CHECK(yaml->get_data() == yaml->get_document(0));
	CHECK(int(yaml->get_value("a")) == 1);
	CHECK(Array(yaml->get_document(1)) == Array({ "x" }));
	CHECK(yaml->get_document(2).get_type() == Variant::NIL);
	CHECK(int(Dictionary(yaml->get_document(3))["b"]) == 2);
	CHECK(YAML::parse_string("---\nfirst\n---\nsecond\n") == Variant("first"));

	yaml->remove_document(2);
	yaml->add_document(Dictionary({ { "c", 3 } }));
	const String text = yaml->encode_to_text();
	CHECK(text == "a: 1\n---\n- x\n---\nb: 2\n---\nc: 3\n");
	Ref<YAML> again;
	again.instantiate();
	REQUIRE(again->parse(text) == OK);
	CHECK(again->get_document_count() == 4);

	yaml->remove_document(0);
	CHECK(Array(yaml->get_data()) == Array({ "x" }));
	CHECK(yaml->get_document_count() == 3);
	ERR_PRINT_OFF;
	CHECK(yaml->get_document(3).get_type() == Variant::NIL);
	ERR_PRINT_ON;
	yaml->clear();
	CHECK(yaml->get_document_count() == 1);

	// One document, an empty text and a failed parse.
	REQUIRE(yaml->parse("a: 1\n") == OK);
	CHECK(yaml->get_document_count() == 1);
	REQUIRE(yaml->parse("") == OK);
	CHECK(yaml->get_document_count() == 1);
	REQUIRE(yaml->parse("a: 1\n---\nb: [\n") == ERR_PARSE_ERROR);
	CHECK(yaml->get_document_count() == 1);
}

TEST_CASE("[YAML] Registered as text format FORMAT_YAML") {
	ResourceTextSerializer *serializer = ResourceTextSerializer::get_singleton();
	REQUIRE(serializer != nullptr);
	CHECK(serializer->get_available_formats().has(ResourceTextSerializer::FORMAT_YAML));
	CHECK(serializer->is_format_available(ResourceTextSerializer::FORMAT_YAML));
	CHECK(serializer->can_serialize(make_yaml_settings(), ResourceTextSerializer::FORMAT_YAML));

	const Ref<YAMLTestSettings> settings = make_yaml_settings();
	const String text = serializer->serialize(settings, ResourceTextSerializer::FORMAT_YAML);
	CHECK(text == YAML::serialize(settings));
	String expected;
	String error;
	REQUIRE(YAML::write_resource(settings, ResourceTextSerializer::get_default_options(), expected, error) == OK);
	CHECK(text == expected);
	check_yaml_settings(serializer->deserialize(text, ResourceTextSerializer::FORMAT_YAML));
	check_yaml_settings(YAML::deserialize(text));
}

#ifdef TOOLS_ENABLED
TEST_CASE("[SceneTree][YAMLInspectorView] Documents") {
	Ref<YAML> yaml;
	yaml.instantiate();
	REQUIRE(yaml->parse("name: knight\nhp: 100\n") == OK);
	Ref<EditorInspectorPluginYAML> plugin;
	plugin.instantiate();
	CHECK(plugin->can_handle(yaml.ptr()));
	for (const char *other : { "CSV", "INI", "XML", "JSON", "TOML" }) {
		if (ClassDB::class_exists(other)) {
			const Ref<RefCounted> format = Object::cast_to<RefCounted>(ClassDB::instantiate(other));
			CHECK_FALSE(plugin->can_handle(format.ptr()));
		}
	}
	YAMLInspectorView *view = Object::cast_to<YAMLInspectorView>(plugin->create_view(yaml));
	REQUIRE(view != nullptr);

	// One document: the tree of `data`, rows named by their keys.
	const Ref<DataTreeAdapter> adapter = view->get_adapter();
	REQUIRE(Object::cast_to<YAMLTreeAdapter>(adapter.ptr()) != nullptr);
	CHECK(view->get_summary_text(yaml->get_data()) == "Dictionary, 2 keys");
	LocalVector<DataTreeAdapter::Entry> rows;
	adapter->get_children(PackedStringArray(), 0, -1, rows);
	REQUIRE(rows.size() == 2);
	CHECK(rows[0].key == "name");

	// Several documents: one top-level row "Document N" of type Document per document.
	REQUIRE(yaml->parse("name: knight\n---\nname: archer\n---\n- 1\n") == OK);
	CHECK(view->get_summary_text(yaml->get_data()) == "3 documents");
	CHECK(adapter->get_value(PackedStringArray({ "1", "name" })) == Variant("archer"));
	rows.clear();
	adapter->get_children(PackedStringArray(), 0, -1, rows);
	REQUIRE(rows.size() == 3);
	CHECK(rows[0].key == "Document 1");
	CHECK(rows[2].key == "Document 3");
	CHECK(rows[2].type == "Document");
	CHECK(rows[2].value == "[1]"); // An Array of scalars is one value.
	rows.clear();
	adapter->get_children(PackedStringArray({ "0" }), 0, -1, rows);
	REQUIRE(rows.size() == 1);
	CHECK(rows[0].key == "name");
	memdelete(view);
}

TEST_CASE("[YAML][DataTreeAdapter] Paths and edits through the YAML API") {
	Ref<YAML> yaml;
	yaml.instantiate();
	REQUIRE(yaml->parse("units:\n  - name: knight\n    hp: 100\n  - name: archer\n    hp: 60\na/b: 1\n") == OK);
	const Ref<DataTreeAdapter> adapter = DataTreeAdapter::create_for(yaml);
	REQUIRE(Object::cast_to<YAMLTreeAdapter>(adapter.ptr()) != nullptr);

	// The adapter's paths are the YAML.get_value() paths, "~1" for "/".
	const PackedStringArray hp = PackedStringArray({ "units", "1", "hp" });
	CHECK(adapter->format_path(hp) == "units/1/hp");
	CHECK(adapter->get_value(hp) == yaml->get_value("units/1/hp"));
	CHECK(adapter->format_path(PackedStringArray({ "a/b" })) == "a~1b");
	CHECK(adapter->get_value(DataTreeAdapter::split_path("a~1b")) == Variant(1));
	CHECK(adapter->get_kind(PackedStringArray({ "units" })) == DataTreeAdapter::NODE_KIND_INDEXED);

	CHECK(adapter->set_value(hp, 75) == OK);
	CHECK(int(yaml->get_value("units/1/hp")) == 75);
	CHECK(adapter->add_value(PackedStringArray({ "units", "0" }), "class", "warrior") == OK);
	CHECK(String(yaml->get_value("units/0/class")) == "warrior");
	CHECK(adapter->rename(PackedStringArray({ "units", "0", "class" }), "role") == OK);
	CHECK(yaml->has_value("units/0/role"));
	CHECK(adapter->add_container(PackedStringArray(), "spawn", DataTreeAdapter::NODE_KIND_NAMED) == OK);
	CHECK(yaml->get_value("spawn").get_type() == Variant::DICTIONARY);
	CHECK(adapter->remove(PackedStringArray({ "units", "0" })) == OK);
	CHECK(String(yaml->get_value("units/0/name")) == "archer");

	// Several documents: documents are added and removed, the first one is edited with its paths.
	REQUIRE(yaml->parse("name: knight\n---\nname: archer\n") == OK);
	CHECK(adapter->format_path(PackedStringArray({ "1", "name" })) == "name (document 2)");
	CHECK(adapter->set_value(PackedStringArray({ "0", "name" }), "paladin") == OK);
	CHECK(String(yaml->get_value("name")) == "paladin");
	CHECK(adapter->set_value(PackedStringArray({ "1", "name" }), "ranger") == OK);
	CHECK(Dictionary(yaml->get_document(1))["name"] == "ranger");
	CHECK(adapter->add_value(PackedStringArray(), String(), Dictionary()) == OK);
	CHECK(yaml->get_document_count() == 3);
	CHECK(adapter->remove(PackedStringArray({ "2" })) == OK);
	CHECK(yaml->get_document_count() == 2);
	CHECK(adapter->move(PackedStringArray({ "1" }), 0) == ERR_UNAVAILABLE);

	// A top-level list of scalars lists its items.
	REQUIRE(yaml->parse("- a\n- b\n") == OK);
	CHECK(adapter->get_kind(PackedStringArray()) == DataTreeAdapter::NODE_KIND_INDEXED);
	CHECK(adapter->get_child_count(PackedStringArray()) == 2);

	// The key "" is a key, not the whole document (YAML paths skip empty segments).
	Dictionary data;
	data[""] = 1;
	data["b"] = 2;
	yaml->set_data(data);
	CHECK(adapter->set_value(PackedStringArray({ "" }), 5) == OK);
	CHECK(int(Dictionary(yaml->get_data())[""]) == 5);
	CHECK(int(Dictionary(yaml->get_data())["b"]) == 2);
	CHECK(adapter->remove(PackedStringArray({ "" })) == OK);
	CHECK_FALSE(Dictionary(yaml->get_data()).has(""));
	CHECK(int(Dictionary(yaml->get_data())["b"]) == 2);
}
#endif // TOOLS_ENABLED

} // namespace TestYAML
