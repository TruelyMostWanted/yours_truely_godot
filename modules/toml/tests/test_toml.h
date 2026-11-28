/**************************************************************************/
/*  test_toml.h                                                           */
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

#include "../toml.h"

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
#include "../editor/toml_inspector_plugin.h"
#include "../editor/toml_tree_adapter.h"
#endif

namespace TestTOML {

class TOMLTestEntry : public Resource {
	GDCLASS(TOMLTestEntry, Resource);

	String name;
	int64_t level = 1;
	double speed = 1.0;
	int mode = 0;
	Vector2 offset;
	Ref<TOMLTestEntry> child;

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_entry_name", "value"), &TOMLTestEntry::set_entry_name);
		ClassDB::bind_method(D_METHOD("get_entry_name"), &TOMLTestEntry::get_entry_name);
		ClassDB::bind_method(D_METHOD("set_level", "value"), &TOMLTestEntry::set_level);
		ClassDB::bind_method(D_METHOD("get_level"), &TOMLTestEntry::get_level);
		ClassDB::bind_method(D_METHOD("set_speed", "value"), &TOMLTestEntry::set_speed);
		ClassDB::bind_method(D_METHOD("get_speed"), &TOMLTestEntry::get_speed);
		ClassDB::bind_method(D_METHOD("set_mode", "value"), &TOMLTestEntry::set_mode);
		ClassDB::bind_method(D_METHOD("get_mode"), &TOMLTestEntry::get_mode);
		ClassDB::bind_method(D_METHOD("set_offset", "value"), &TOMLTestEntry::set_offset);
		ClassDB::bind_method(D_METHOD("get_offset"), &TOMLTestEntry::get_offset);
		ClassDB::bind_method(D_METHOD("set_child", "value"), &TOMLTestEntry::set_child);
		ClassDB::bind_method(D_METHOD("get_child"), &TOMLTestEntry::get_child);

		ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "set_entry_name", "get_entry_name");
		ADD_PROPERTY(PropertyInfo(Variant::INT, "level"), "set_level", "get_level");
		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "speed"), "set_speed", "get_speed");
		ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Calm,Angry:5,Happy"), "set_mode", "get_mode");
		ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "offset"), "set_offset", "get_offset");
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "child", PROPERTY_HINT_RESOURCE_TYPE, "TOMLTestEntry"), "set_child", "get_child");
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
	void set_child(const Ref<TOMLTestEntry> &p_value) { child = p_value; }
	Ref<TOMLTestEntry> get_child() const { return child; }
};

class TOMLTestSettings : public Resource {
	GDCLASS(TOMLTestSettings, Resource);

	double volume = 0.5;
	String title = "Default";
	Ref<TOMLTestEntry> main;
	Ref<TOMLTestEntry> spare;
	TypedArray<TOMLTestEntry> entries;
	Dictionary extra;
	PackedInt32Array numbers;
	Color tint = Color(1, 1, 1, 1);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_volume", "value"), &TOMLTestSettings::set_volume);
		ClassDB::bind_method(D_METHOD("get_volume"), &TOMLTestSettings::get_volume);
		ClassDB::bind_method(D_METHOD("set_title", "value"), &TOMLTestSettings::set_title);
		ClassDB::bind_method(D_METHOD("get_title"), &TOMLTestSettings::get_title);
		ClassDB::bind_method(D_METHOD("set_main", "value"), &TOMLTestSettings::set_main);
		ClassDB::bind_method(D_METHOD("get_main"), &TOMLTestSettings::get_main);
		ClassDB::bind_method(D_METHOD("set_spare", "value"), &TOMLTestSettings::set_spare);
		ClassDB::bind_method(D_METHOD("get_spare"), &TOMLTestSettings::get_spare);
		ClassDB::bind_method(D_METHOD("set_entries", "value"), &TOMLTestSettings::set_entries);
		ClassDB::bind_method(D_METHOD("get_entries"), &TOMLTestSettings::get_entries);
		ClassDB::bind_method(D_METHOD("set_extra", "value"), &TOMLTestSettings::set_extra);
		ClassDB::bind_method(D_METHOD("get_extra"), &TOMLTestSettings::get_extra);
		ClassDB::bind_method(D_METHOD("set_numbers", "value"), &TOMLTestSettings::set_numbers);
		ClassDB::bind_method(D_METHOD("get_numbers"), &TOMLTestSettings::get_numbers);
		ClassDB::bind_method(D_METHOD("set_tint", "value"), &TOMLTestSettings::set_tint);
		ClassDB::bind_method(D_METHOD("get_tint"), &TOMLTestSettings::get_tint);

		ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume"), "set_volume", "get_volume");
		ADD_PROPERTY(PropertyInfo(Variant::STRING, "title"), "set_title", "get_title");
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "main", PROPERTY_HINT_RESOURCE_TYPE, "TOMLTestEntry"), "set_main", "get_main");
		ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "spare", PROPERTY_HINT_RESOURCE_TYPE, "TOMLTestEntry"), "set_spare", "get_spare");
		ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "entries", PROPERTY_HINT_ARRAY_TYPE, "TOMLTestEntry"), "set_entries", "get_entries");
		ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "extra"), "set_extra", "get_extra");
		ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "numbers"), "set_numbers", "get_numbers");
		ADD_PROPERTY(PropertyInfo(Variant::COLOR, "tint"), "set_tint", "get_tint");
	}

public:
	void set_volume(double p_value) { volume = p_value; }
	double get_volume() const { return volume; }
	void set_title(const String &p_value) { title = p_value; }
	String get_title() const { return title; }
	void set_main(const Ref<TOMLTestEntry> &p_value) { main = p_value; }
	Ref<TOMLTestEntry> get_main() const { return main; }
	void set_spare(const Ref<TOMLTestEntry> &p_value) { spare = p_value; }
	Ref<TOMLTestEntry> get_spare() const { return spare; }
	void set_entries(const TypedArray<TOMLTestEntry> &p_value) { entries = p_value; }
	TypedArray<TOMLTestEntry> get_entries() const { return entries; }
	void set_extra(const Dictionary &p_value) { extra = p_value; }
	Dictionary get_extra() const { return extra; }
	void set_numbers(const PackedInt32Array &p_value) { numbers = p_value; }
	PackedInt32Array get_numbers() const { return numbers; }
	void set_tint(const Color &p_value) { tint = p_value; }
	Color get_tint() const { return tint; }
};

inline void register_toml_test_classes() {
	static bool registered = false;
	if (!registered) {
		GDREGISTER_CLASS(TOMLTestEntry);
		GDREGISTER_CLASS(TOMLTestSettings);
		registered = true;
	}
}

inline Ref<TOMLTestEntry> make_toml_entry(const String &p_name, int64_t p_level, int p_mode) {
	Ref<TOMLTestEntry> entry;
	entry.instantiate();
	entry->set_entry_name(p_name);
	entry->set_level(p_level);
	entry->set_mode(p_mode);
	entry->set_speed(0.1 * double(p_level));
	entry->set_offset(Vector2(0.1, -2.5));
	return entry;
}

inline Ref<TOMLTestSettings> make_toml_settings() {
	register_toml_test_classes();
	Ref<TOMLTestSettings> settings;
	settings.instantiate();
	settings->set_volume(0.8);
	settings->set_title("Main \"menu\", line\nbreak\tand tab");
	Ref<TOMLTestEntry> main = make_toml_entry("Ann", 3, 5);
	main->set_child(make_toml_entry("Ann's child", 4, 6));
	settings->set_main(main);
	TypedArray<TOMLTestEntry> entries;
	entries.push_back(make_toml_entry("Bob", 1, 0));
	Ref<TOMLTestEntry> cleo = make_toml_entry("Cleo, the 2nd", 2, 6);
	cleo->set_child(make_toml_entry("Dan", 7, 0));
	entries.push_back(cleo);
	settings->set_entries(entries);
	Dictionary extra;
	extra["difficulty"] = "hard";
	extra["lives"] = 3;
	extra["key with spaces"] = true;
	settings->set_extra(extra);
	PackedInt32Array numbers;
	numbers.push_back(4);
	numbers.push_back(-7);
	settings->set_numbers(numbers);
	settings->set_tint(Color(1, 0, 0, 1));
	return settings;
}

inline void check_toml_entry(const Ref<TOMLTestEntry> &p_entry, const String &p_name, int64_t p_level, int p_mode) {
	REQUIRE(p_entry.is_valid());
	CHECK(p_entry->get_entry_name() == p_name);
	CHECK(p_entry->get_level() == p_level);
	CHECK(p_entry->get_mode() == p_mode);
	CHECK(p_entry->get_speed() == doctest::Approx(0.1 * double(p_level)));
	CHECK(p_entry->get_offset().is_equal_approx(Vector2(0.1, -2.5)));
}

inline void check_toml_settings(const Ref<Resource> &p_resource) {
	const Ref<TOMLTestSettings> settings = p_resource;
	REQUIRE(settings.is_valid());
	CHECK(settings->get_volume() == 0.8);
	CHECK(settings->get_title() == "Main \"menu\", line\nbreak\tand tab");
	check_toml_entry(settings->get_main(), "Ann", 3, 5);
	check_toml_entry(settings->get_main()->get_child(), "Ann's child", 4, 6);
	CHECK(settings->get_spare().is_null());
	const TypedArray<TOMLTestEntry> entries = settings->get_entries();
	REQUIRE(entries.size() == 2);
	check_toml_entry(entries[0], "Bob", 1, 0);
	check_toml_entry(entries[1], "Cleo, the 2nd", 2, 6);
	const Ref<TOMLTestEntry> cleo = entries[1];
	check_toml_entry(cleo->get_child(), "Dan", 7, 0);
	const Dictionary extra = settings->get_extra();
	CHECK(String(extra.get("difficulty", "")) == "hard");
	CHECK(int(extra.get("lives", 0)) == 3);
	CHECK(bool(extra.get("key with spaces", false)));
	const PackedInt32Array numbers = settings->get_numbers();
	REQUIRE(numbers.size() == 2);
	CHECK(numbers[0] == 4);
	CHECK(numbers[1] == -7);
	CHECK(settings->get_tint() == Color(1, 0, 0, 1));
}

TEST_CASE("[TOML] Parse tables, arrays of tables and inline tables") {
	const String text = R"(# Comment line
title = "TOML" # Comment after a value
"quoted key" = 1
site."google.com" = true

[owner]
name = "Tom"
physical.color = "orange"

[servers.alpha]
ip = "10.0.0.1"

[[products]]
name = "Hammer"
sku = 738594937

[[products]]

[[products]]
name = "Nail"
color = "gray"
point = { x = 1, y = 2, nested.deep = "yes" }
)";
	TOML toml;
	REQUIRE(toml.parse(text) == OK);
	const Dictionary data = toml.get_data();
	CHECK(String(data["title"]) == "TOML");
	CHECK(int(data["quoted key"]) == 1);
	const Dictionary site = data["site"];
	CHECK(bool(site["google.com"]));
	const Dictionary owner = data["owner"];
	CHECK(String(owner["name"]) == "Tom");
	CHECK(String(Dictionary(owner["physical"])["color"]) == "orange");
	const Dictionary servers = data["servers"];
	CHECK(String(Dictionary(servers["alpha"])["ip"]) == "10.0.0.1");
	const Array products = data["products"];
	REQUIRE(products.size() == 3);
	CHECK(String(Dictionary(products[0])["name"]) == "Hammer");
	CHECK(int64_t(Dictionary(products[0])["sku"]) == 738594937);
	CHECK(Dictionary(products[1]).is_empty());
	const Dictionary point = Dictionary(products[2])["point"];
	CHECK(int(point["x"]) == 1);
	CHECK(int(point["y"]) == 2);
	CHECK(String(Dictionary(point["nested"])["deep"]) == "yes");

	// Keys keep the order of the file.
	const Array keys = data.keys();
	REQUIRE(keys.size() == 6);
	CHECK(String(keys[0]) == "title");
	CHECK(String(keys[1]) == "quoted key");
	CHECK(String(keys[2]) == "site");
	CHECK(String(keys[3]) == "owner");
	CHECK(String(keys[4]) == "servers");
	CHECK(String(keys[5]) == "products");
}

TEST_CASE("[TOML] Parse strings") {
	const String text = String(R"(basic = "tab\there \"quoted\" \\ \u00E9 \U0001F600"
literal = 'C:\Users\nodejs\templates'
multi = """
Roses are red
Violets are blue"""
trimmed = """\
       The quick brown \
       fox."""
quotes = """Here are two quotation marks: "". Simple enough."""
five = """"This," she said, "is just a pointless statement.""""
lines = '''
The first newline is
trimmed in raw strings.
   All other whitespace
   is preserved.
'''
apos = ''''That,' she said, 'is still pointless.''''
empty = ""
)");
	const Dictionary data = TOML::parse_string(text);
	CHECK(String(data["basic"]) == String::utf8("tab\there \"quoted\" \\ \xC3\xA9 \xF0\x9F\x98\x80"));
	CHECK(String(data["literal"]) == "C:\\Users\\nodejs\\templates");
	CHECK(String(data["multi"]) == "Roses are red\nViolets are blue");
	CHECK(String(data["trimmed"]) == "The quick brown fox.");
	CHECK(String(data["quotes"]) == "Here are two quotation marks: \"\". Simple enough.");
	CHECK(String(data["five"]) == "\"This,\" she said, \"is just a pointless statement.\"");
	CHECK(String(data["lines"]) == "The first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n");
	CHECK(String(data["apos"]) == "'That,' she said, 'is still pointless.'");
	CHECK(String(data["empty"]).is_empty());

	// Windows line breaks.
	const Dictionary crlf = TOML::parse_string("a = 1\r\nb = \"\"\"\r\nx\r\ny\"\"\"\r\n");
	CHECK(int(crlf["a"]) == 1);
	CHECK(String(crlf["b"]) == "x\ny");
}

TEST_CASE("[TOML] Parse numbers, booleans and dates") {
	const String text = R"(int1 = +99
int2 = -17
int3 = 1_000_000
hex = 0xDEAD_beef
oct = 0o755
bin = 0b1101_0110
max = 9223372036854775807
min = -9223372036854775808
flt1 = +1.0
flt2 = -0.01
flt3 = 5e+22
flt4 = 6.626e-34
flt5 = 224_617.445_991
pinf = inf
ninf = -inf
not = nan
yes = true
no = false
odt = 1979-05-27T07:32:00.999999-07:00
odt2 = 1979-05-27 07:32:00Z
ldt = 1979-05-27T07:32:00
ld = 1979-05-27
lt = 00:32:00.5
mixed = [1, "two", 3.0, [4], { five = 5 }]
nested = [
	1, # one
	2,
]
)";
	const Dictionary data = TOML::parse_string(text);
	CHECK(int64_t(data["int1"]) == 99);
	CHECK(int64_t(data["int2"]) == -17);
	CHECK(int64_t(data["int3"]) == 1000000);
	CHECK(int64_t(data["hex"]) == int64_t(0xDEADBEEF));
	CHECK(int64_t(data["oct"]) == 0755);
	CHECK(int64_t(data["bin"]) == 0b11010110);
	CHECK(int64_t(data["max"]) == INT64_MAX);
	CHECK(int64_t(data["min"]) == INT64_MIN);
	CHECK(data["int1"].get_type() == Variant::INT);
	CHECK(data["flt1"].get_type() == Variant::FLOAT);
	CHECK(double(data["flt1"]) == 1.0);
	CHECK(double(data["flt2"]) == doctest::Approx(-0.01));
	CHECK(double(data["flt3"]) == 5e22);
	CHECK(double(data["flt4"]) == 6.626e-34);
	CHECK(double(data["flt5"]) == doctest::Approx(224617.445991));
	CHECK(double(data["pinf"]) == Math::INF);
	CHECK(double(data["ninf"]) == -Math::INF);
	CHECK(Math::is_nan(double(data["not"])));
	CHECK(bool(data["yes"]));
	CHECK_FALSE(bool(data["no"]));
	CHECK(data["odt"].get_type() == Variant::STRING);
	CHECK(String(data["odt"]) == "1979-05-27T07:32:00.999999-07:00");
	CHECK(String(data["odt2"]) == "1979-05-27 07:32:00Z");
	CHECK(String(data["ldt"]) == "1979-05-27T07:32:00");
	CHECK(String(data["ld"]) == "1979-05-27");
	CHECK(String(data["lt"]) == "00:32:00.5");
	const Array mixed = data["mixed"];
	REQUIRE(mixed.size() == 5);
	CHECK(String(mixed[1]) == "two");
	CHECK(int(Array(mixed[3])[0]) == 4);
	CHECK(int(Dictionary(mixed[4])["five"]) == 5);
	const Array nested = data["nested"];
	CHECK(nested.size() == 2);
}

TEST_CASE("[TOML] Errors with line and column") {
	struct Case {
		const char *text;
		int line;
	};
	const Case cases[] = {
		{ "a = 1\na = 2\n", 2 }, // Duplicate key.
		{ "[a]\nx = 1\n[a]\n", 3 }, // Table defined twice.
		{ "a = { x = 1 }\n[a.b]\n", 2 }, // Inline tables are closed.
		{ "a = [1]\n[[a]]\n", 2 }, // Static arrays are closed.
		{ "[a.b.c]\nz = 9\n[a]\nb.c.t = 1\n", 4 }, // Dotted keys can't extend a [header] table.
		{ "a.b = 1\n[a]\n", 2 }, // Dotted tables can't be defined by a [header].
		{ "x = \"bad \\q escape\"\n", 1 },
		{ "x = \"not closed\n", 1 },
		{ "x = 9223372036854775808\n", 1 }, // Out of range.
		{ "x = 012\n", 1 }, // Leading zero.
		{ "x = 1__2\n", 1 },
		{ "x = .5\n", 1 },
		{ "x = 1.\n", 1 },
		{ "x = +0x10\n", 1 },
		{ "x = 1 y = 2\n", 1 },
		{ "x = { a = 1,\n b = 2 }\n", 1 }, // Inline tables are on one line.
		{ "x = { a = 1, }\n", 1 }, // No trailing comma in inline tables.
		{ "x =\n", 1 },
		{ "\n\n= 1\n", 3 },
		{ "x = 1979-13-27\n", 1 },
		{ "x = 07:32\n", 1 }, // TOML 1.0 requires seconds.
		{ "x = truee\n", 1 },
	};
	for (const Case &c : cases) {
		TOML toml;
		CHECK_MESSAGE(toml.parse(String(c.text)) == ERR_PARSE_ERROR, c.text);
		CHECK_MESSAGE(toml.get_error_line() == c.line, c.text);
		CHECK_MESSAGE(toml.get_error_column() > 0, c.text);
		CHECK_FALSE_MESSAGE(toml.get_error_message().is_empty(), c.text);
	}

	ERR_PRINT_OFF;
	CHECK(TOML::parse_string("a = ").is_empty());
	ERR_PRINT_ON;
}

TEST_CASE("[TOML] Stringify keeps key order and reads back") {
	Dictionary data;
	data["name"] = "Game";
	data["$type"] = "Settings";
	Dictionary window;
	window["width"] = 1280;
	window["scale"] = 1.5;
	window["integral"] = 2.0;
	data["window"] = window;
	data["volume"] = 0.1;
	Array players;
	Dictionary p1;
	p1["name"] = "Ann";
	Dictionary pos;
	pos["x"] = 1;
	p1["pos"] = pos;
	players.push_back(p1);
	Dictionary p2;
	p2["name"] = "Line\nbreak";
	players.push_back(p2);
	data["players"] = players;
	Array mixed;
	mixed.push_back(1);
	mixed.push_back(window);
	data["mixed"] = mixed;
	data["empty"] = Dictionary();
	data["nothing"] = Variant();
	data["special"] = Math::INF;
	PackedStringArray tags;
	tags.push_back("a");
	tags.push_back("b");
	data["tags"] = tags;
	data["odd\nkey"] = 1;

	const String text = TOML::stringify(data);
	CHECK(text.contains("\"odd\\nkey\" = 1\n"));
	CHECK(text.begins_with("name = \"Game\"\n\"$type\" = \"Settings\"\nvolume = 0.1\n"));
	CHECK(text.contains("\n[window]\nwidth = 1280\nscale = 1.5\nintegral = 2.0\n"));
	CHECK(text.contains("\n[[players]]\nname = \"Ann\"\n\n[players.pos]\nx = 1\n"));
	CHECK(text.contains("name = \"\"\"\nLine\nbreak\"\"\"\n"));
	CHECK(text.contains("mixed = [1, { width = 1280, scale = 1.5, integral = 2.0 }]\n"));
	CHECK_FALSE(text.contains("nothing")); // TOML has no null: the key is left out.
	CHECK(text.contains("special = inf\n"));
	CHECK(text.contains("tags = [\"a\", \"b\"]\n"));
	CHECK(text.find("[window]") < text.find("[[players]]"));
	CHECK(text.find("[[players]]") < text.find("[empty]"));

	const Dictionary back = TOML::parse_string(text);
	CHECK(String(back["name"]) == "Game");
	CHECK(double(Dictionary(back["window"])["integral"]) == 2.0);
	CHECK(Dictionary(back["window"])["integral"].get_type() == Variant::FLOAT);
	CHECK(double(back["volume"]) == 0.1);
	const Array players_back = back["players"];
	REQUIRE(players_back.size() == 2);
	CHECK(int(Dictionary(Dictionary(players_back[0])["pos"])["x"]) == 1);
	CHECK(String(Dictionary(players_back[1])["name"]) == "Line\nbreak");
	CHECK(Dictionary(back["empty"]).is_empty());
	CHECK(int(back["odd\nkey"]) == 1);

	// Objects can't be written.
	Dictionary invalid;
	invalid["object"] = Vector2(1, 2);
	ERR_PRINT_OFF;
	CHECK(TOML::stringify(invalid).is_empty());
	ERR_PRINT_ON;
}

TEST_CASE("[TOML] Resource round trip as text") {
	const Ref<TOMLTestSettings> settings = make_toml_settings();
	const String text = TOML::serialize(settings);
	CHECK(text.begins_with("\"$type\" = \"TOMLTestSettings\"\n\"$format_version\" = 1\n"));
	// Plain keys in property order, then sub-tables and arrays of tables in property order.
	CHECK(text.find("volume = ") < text.find("title = "));
	CHECK_FALSE(text.contains("spare")); // null: left out.
	CHECK(text.find("title = ") < text.find("numbers = [4, -7]"));
	CHECK(text.find("numbers = ") < text.find("tint = \"#ff0000ff\""));
	CHECK(text.find("tint = ") < text.find("\n[main]\n"));
	CHECK(text.find("\n[main]\n") < text.find("\n[main.child]\n"));
	CHECK(text.find("\n[main.child]\n") < text.find("\n[[entries]]\n"));
	CHECK(text.find("\n[[entries]]\n") < text.find("\n[entries.child]\n"));
	CHECK(text.find("\n[entries.child]\n") < text.find("\n[extra]\n"));
	CHECK(text.contains("mode = \"Angry\""));
	CHECK(text.contains("\"key with spaces\" = true"));
	CHECK(text.count("\"$type\"") == 1);

	check_toml_settings(TOML::deserialize(text));

	// Without "$type", the type hint gives the class.
	const String untyped = text.replace("\"$type\" = \"TOMLTestSettings\"\n", "");
	check_toml_settings(TOML::deserialize(untyped, "TOMLTestSettings"));
	ERR_PRINT_OFF;
	CHECK(TOML::deserialize(untyped).is_null());
	ERR_PRINT_ON;

	Ref<SerializerOptions> numbers_options;
	numbers_options.instantiate();
	numbers_options->set_enums_as_names(false);
	const String numbers = TOML::serialize(settings, numbers_options);
	CHECK(numbers.contains("mode = 5"));
	check_toml_settings(TOML::deserialize(numbers));
}

TEST_CASE("[TOML] Hand-written resource file") {
	register_toml_test_classes();
	const String text = R"(# Settings written by hand.
"$type" = "TOMLTestSettings"
volume = 1
title = 'Literal \ title'
entries = [
	{ name = "Inline", level = 2, mode = "Happy" },
]

[main]
name = "Ann"
level = 0x10
offset = [1.5, 2]
)";
	const Ref<TOMLTestSettings> settings = TOML::deserialize(text);
	REQUIRE(settings.is_valid());
	CHECK(settings->get_volume() == 1.0);
	CHECK(settings->get_title() == "Literal \\ title");
	REQUIRE(settings->get_main().is_valid());
	CHECK(settings->get_main()->get_level() == 16);
	CHECK(settings->get_main()->get_offset() == Vector2(1.5, 2));
	const TypedArray<TOMLTestEntry> entries = settings->get_entries();
	REQUIRE(entries.size() == 1);
	const Ref<TOMLTestEntry> entry = entries[0];
	CHECK(entry->get_entry_name() == "Inline");
	CHECK(entry->get_mode() == 6);
}

TEST_CASE("[TOML] Through ResourceSaver and ResourceLoader") {
	const Ref<TOMLTestSettings> settings = make_toml_settings();
	const String path = TestUtils::get_temp_path("toml_settings.toml");
	CHECK(ResourceSaver::save(Ref<Resource>(settings), path) == OK);
	CHECK(ResourceLoader::get_resource_type(path) == "TOMLTestSettings");

	check_toml_settings(ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE));
	check_toml_settings(ResourceLoader::load(path, "TOMLTestSettings", ResourceFormatLoader::CACHE_MODE_IGNORE));
	DirAccess::remove_absolute(path);
}

TEST_CASE("[TOML] Registered as text format FORMAT_TOML") {
	ResourceTextSerializer *serializer = ResourceTextSerializer::get_singleton();
	REQUIRE(serializer != nullptr);
	CHECK(serializer->get_available_formats().has(ResourceTextSerializer::FORMAT_TOML));
	CHECK(serializer->is_format_available(ResourceTextSerializer::FORMAT_TOML));
	CHECK(serializer->can_serialize(make_toml_settings(), ResourceTextSerializer::FORMAT_TOML));

	const Ref<TOMLTestSettings> settings = make_toml_settings();
	const String text = serializer->serialize(settings, ResourceTextSerializer::FORMAT_TOML);
	CHECK(text == TOML::serialize(settings));
	check_toml_settings(serializer->deserialize(text, ResourceTextSerializer::FORMAT_TOML));
}

TEST_CASE("[TOML] Resource API: dotted table paths and plain files") {
	Ref<TOML> toml;
	toml.instantiate();
	CHECK(toml->get_recognized_extensions() == PackedStringArray({ "toml" }));
	toml->set_value("server.limits", "players", 8);
	toml->set_value("", "title", "Game");
	toml->set_value(TOML::escape_table_name("a.b"), "x", 1);
	CHECK(toml->has_table("server"));
	CHECK(toml->has_table("server.limits"));
	CHECK(toml->has_table_key("server.limits", "players"));
	CHECK(int(toml->get_value("server.limits", "players")) == 8);
	CHECK(toml->get_value("missing", "key", 3) == Variant(3));
	CHECK(toml->get_tables() == PackedStringArray({ "server", "server.limits", "a\\.b" }));
	CHECK(TOML::split_table_path("a\\.b.c") == Vector<String>({ "a.b", "c" }));
	CHECK(toml->get_table_keys("") == PackedStringArray({ "server", "title", "a.b" }));
	toml->erase_table_key("server.limits", "players");
	CHECK_FALSE(toml->has_table_key("server.limits", "players"));
	toml->erase_table("server");
	CHECK_FALSE(toml->has_table("server"));

	const String path = TestUtils::get_temp_path("toml_plain.toml");
	REQUIRE(ResourceSaver::save(toml, path) == OK);
	CHECK_FALSE(FileAccess::get_file_as_string(path).contains("$type"));
	CHECK(ResourceLoader::get_resource_type(path) == "TOML");
	const Ref<TOML> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());
	CHECK(loaded->get_value("", "title") == "Game");
	DirAccess::remove_absolute(path);
}

#ifdef TOOLS_ENABLED
TEST_CASE("[SceneTree][TOMLInspectorView] Tables in TOML terms") {
	Ref<TOML> toml;
	toml.instantiate();
	REQUIRE(toml->parse("title = \"Game\"\n\n[server]\nport = 8080\n\n[[unit]]\nname = \"knight\"\n\n[[unit]]\nname = \"archer\"\n") == OK);
	Ref<EditorInspectorPluginTOML> plugin;
	plugin.instantiate();
	CHECK(plugin->can_handle(toml.ptr()));
	for (const char *other : { "CSV", "INI", "XML", "JSON", "YAML" }) {
		if (ClassDB::class_exists(other)) {
			const Ref<RefCounted> format = Object::cast_to<RefCounted>(ClassDB::instantiate(other));
			CHECK_FALSE(plugin->can_handle(format.ptr()));
		}
	}
	TOMLInspectorView *view = Object::cast_to<TOMLInspectorView>(plugin->create_view(toml));
	REQUIRE(view != nullptr);
	CHECK(view->get_summary_text(toml->get_data()) == "2 tables, 1 key");

	// The view shows the tree of the TOML adapter.
	const Ref<DataTreeAdapter> adapter = view->get_adapter();
	REQUIRE(Object::cast_to<TOMLTreeAdapter>(adapter.ptr()) != nullptr);
	LocalVector<DataTreeAdapter::Entry> rows;
	adapter->get_children(PackedStringArray(), 0, -1, rows);
	REQUIRE(rows.size() == 3);
	CHECK(rows[0].key == "title");
	CHECK(rows[0].type == "String");
	CHECK(rows[1].type == "Table");
	CHECK(rows[1].value == "1 key");
	CHECK(rows[2].type == "Array of Tables");
	CHECK(rows[2].kind == DataTreeAdapter::NODE_KIND_INDEXED);

	CHECK_FALSE(TOMLTreeAdapter::is_array_of_tables(Array()));
	CHECK_FALSE(TOMLTreeAdapter::is_array_of_tables(Array({ 1, 2 })));
	memdelete(view);
}

TEST_CASE("[TOML][DataTreeAdapter] Paths and edits through the TOML API") {
	Ref<TOML> toml;
	toml.instantiate();
	REQUIRE(toml->parse("title = \"Game\"\n\n[server]\nport = 8080\n\n[[unit]]\nname = \"knight\"\n\n[[unit]]\nname = \"archer\"\n") == OK);
	const Ref<DataTreeAdapter> adapter = DataTreeAdapter::create_for(toml);
	REQUIRE(Object::cast_to<TOMLTreeAdapter>(adapter.ptr()) != nullptr);

	// Tables are named, an array of tables is indexed; TOML dotted keys as path text.
	CHECK(adapter->get_kind(PackedStringArray({ "server" })) == DataTreeAdapter::NODE_KIND_NAMED);
	CHECK(adapter->get_value(PackedStringArray({ "unit", "1", "name" })) == Variant("archer"));
	CHECK(adapter->format_path(PackedStringArray({ "unit", "1", "name" })) == "unit[1].name");
	CHECK(adapter->format_path(PackedStringArray({ "server", "host name" })) == "server.\"host name\"");
	CHECK(adapter->get_value_types(PackedStringArray()).has(Variant::INT));

	// In tables: TOML.set_value() and erase_table_key().
	CHECK(adapter->set_value(PackedStringArray({ "server", "port" }), 9090) == OK);
	CHECK(int(toml->get_value("server", "port")) == 9090);
	CHECK(adapter->set_value(PackedStringArray({ "server", "port" }), Variant()) == ERR_INVALID_PARAMETER);
	CHECK(adapter->add_value(PackedStringArray({ "server" }), "host", "localhost") == OK);
	CHECK(adapter->add_value(PackedStringArray({ "server" }), "host", "again") == ERR_ALREADY_EXISTS);
	CHECK(String(toml->get_value("server", "host")) == "localhost");
	CHECK(adapter->remove(PackedStringArray({ "server", "host" })) == OK);
	CHECK_FALSE(toml->has_table_key("server", "host"));

	// Inside an array of tables: through `data`; the text is encoded again.
	CHECK(adapter->set_value(PackedStringArray({ "unit", "0", "name" }), "paladin") == OK);
	CHECK(adapter->get_value(PackedStringArray({ "unit", "0", "name" })) == Variant("paladin"));
	CHECK(adapter->move(PackedStringArray({ "unit", "1" }), 0) == OK);
	CHECK(adapter->get_value(PackedStringArray({ "unit", "0", "name" })) == Variant("archer"));
	CHECK(adapter->get_value(PackedStringArray({ "unit", "1", "name" })) == Variant("paladin"));
	CHECK(adapter->remove(PackedStringArray({ "server" })) == OK);
	CHECK_FALSE(toml->has_table("server"));
	CHECK(toml->encode_to_text().contains("paladin"));
}
#endif // TOOLS_ENABLED

} // namespace TestTOML
