/**************************************************************************/
/*  test_dataset_editor.cpp                                               */
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

TEST_FORCE_LINK(test_dataset_editor)

#ifdef TOOLS_ENABLED

#include "core/io/csv_format.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/ini_format.h"
#include "core/io/json.h"
#include "core/io/text_data_format.h"
#include "core/io/xml_format.h"
#include "core/object/class_db.h"
#include "editor/data/csv_table_editor.h"
#include "editor/data/dataset_editor.h"
#include "editor/import/resource_importer_csv_dataset.h"
#include "tests/test_utils.h"

#include "modules/modules_enabled.gen.h" // For yaml, toml.

namespace TestDatasetEditor {

static Ref<JSON> make_json(const String &p_text) {
	Ref<JSON> json;
	json.instantiate();
	CHECK(json->parse(p_text) == OK);
	return json;
}

TEST_CASE("[DatasetEditor] Tables of JSON data") {
	Ref<JSON> json = make_json(R"([{"name": "knight", "hp": 100}, {"name": "archer"}])");
	Vector<DatasetEditor::Table> tables = DatasetEditor::find_tables(json.ptr());
	REQUIRE(tables.size() == 1);
	CHECK(tables[0].source == DatasetEditor::SOURCE_RECORDS);
	CHECK(tables[0].key.get_type() == Variant::NIL);
	CHECK(tables[0].row_count == 2);
	CHECK(DatasetEditor::can_edit(json.ptr()));
	CHECK(DatasetEditor::can_open_automatically(json.ptr()));

	// Opened on double-click only when at least two records share a key; "Open in Dataset Dock" opens any table.
	json = make_json(R"([{"event": "start"}, {"action": "jump"}])");
	CHECK(DatasetEditor::can_edit(json.ptr()));
	CHECK_FALSE(DatasetEditor::can_open_automatically(json.ptr()));
	json = make_json(R"([{"id": 1}])");
	CHECK_FALSE(DatasetEditor::can_open_automatically(json.ptr()));

	json = make_json(R"({"version": 2, "units": [{"id": 1}], "items": [{"id": 1}, {"id": 2}], "tags": ["a"]})");
	tables = DatasetEditor::find_tables(json.ptr());
	REQUIRE(tables.size() == 2);
	CHECK(tables[0].key == Variant("units"));
	CHECK(tables[1].key == Variant("items"));
	CHECK(tables[1].row_count == 2);

	// Not table-shaped: values that are not all dictionaries, or dictionaries without fields.
	json = make_json(R"([{"id": 1}, 2])");
	CHECK(DatasetEditor::find_tables(json.ptr()).is_empty());
	json = make_json(R"([{}, {}])");
	CHECK_FALSE(DatasetEditor::can_edit(json.ptr()));
	json = make_json(R"({"name": "knight"})");
	CHECK_FALSE(DatasetEditor::can_edit(json.ptr()));
}

TEST_CASE("[DatasetEditor] Data CSVs and translation CSVs") {
	Ref<CSV> csv;
	csv.instantiate();
	csv->parse("name,hp\nknight,100\narcher,80\n");
	Vector<DatasetEditor::Table> tables = DatasetEditor::find_tables(csv.ptr());
	REQUIRE(tables.size() == 1);
	CHECK(tables[0].source == DatasetEditor::SOURCE_CSV);
	CHECK(tables[0].row_count == 2);
	CHECK_FALSE(CSVTableEditor::is_translation_csv(csv.ptr()));
	CHECK_FALSE(CSVTableEditor::can_edit(csv.ptr()));

	CHECK(DatasetEditor::can_open_automatically(csv.ptr()));

	// Locales only: the Localization Table edits it; "Open in Dataset Dock" still can.
	csv->parse("keys,en,de,_note\nHELLO,Hello,Hallo,greeting\n");
	CHECK(CSVTableEditor::is_translation_csv(csv.ptr()));
	CHECK(CSVTableEditor::can_edit(csv.ptr()));
	CHECK(DatasetEditor::can_edit(csv.ptr()));
	CHECK_FALSE(DatasetEditor::can_open_automatically(csv.ptr()));

	// A column that looks like a locale ("id" is Indonesian) next to data columns: a data CSV.
	csv->parse("unit,id,hp\nknight,1,100\n");
	CHECK_FALSE(CSVTableEditor::is_translation_csv(csv.ptr()));
	CHECK(DatasetEditor::can_open_automatically(csv.ptr()));
}

static bool is_translation_header(const Vector<String> &p_header) {
	CSV::TranslationHeader header;
	Ref<DataValidation> issues;
	issues.instantiate();
	CSV::check_translation_header(p_header, header, issues.ptr());
	return header.is_translation_table();
}

TEST_CASE("[CSV] Translation tables and data with locale-like columns") {
	CHECK(is_translation_header({ "keys", "en", "de" }));
	CHECK(is_translation_header({ "id", "en_US", "pt_BR", "_note", "?context" }));
	CHECK(is_translation_header({ "keys", "?plural", "en", "fr" }));
	// Data: a column that is no locale ("hp"), or one that only looks like one next to it ("id" is Indonesian).
	CHECK_FALSE(is_translation_header({ "unit", "id", "hp" }));
	CHECK_FALSE(is_translation_header({ "name", "it", "count" }));
	CHECK_FALSE(is_translation_header({ "keys", "en", "" }));
	CHECK_FALSE(is_translation_header({ "name", "hp" }));
	CHECK_FALSE(is_translation_header({ "keys" }));
}

TEST_CASE("[ResourceImporterCSVDataset] The importer of a new CSV") {
	const String folder = TestUtils::get_temp_path("csv_dataset_importer");
	DirAccess::make_dir_recursive_absolute(folder);
	const String data_path = folder.path_join("units.csv");
	const String translation_path = folder.path_join("texts.csv");
	{
		Ref<FileAccess> file = FileAccess::open(data_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("unit,id,hp\nknight,1,100\n");
	}
	{
		Ref<FileAccess> file = FileAccess::open(translation_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("keys,en,de\nHELLO,Hello,Hallo\n");
	}
	// A data CSV with an "id" column stays a dataset; the translation table goes to CSV Translation.
	CHECK_FALSE(ResourceImporterCSVDataset::is_translation_file(data_path));
	CHECK(ResourceImporterCSVDataset::is_translation_file(translation_path));
	Ref<ResourceImporterCSVDataset> importer;
	importer.instantiate();
	CHECK(importer->get_priority_for_file(data_path) > 1.0);
	CHECK(importer->get_priority_for_file(translation_path) < 1.0);
	DirAccess::remove_absolute(data_path);
	DirAccess::remove_absolute(translation_path);
}

TEST_CASE("[DatasetEditor] INI sections and XML elements") {
	Ref<INI> ini;
	ini.instantiate();
	ini->parse("[knight]\nhp=100\n[archer]\nhp=80\n");
	Vector<DatasetEditor::Table> tables = DatasetEditor::find_tables(ini.ptr());
	REQUIRE(tables.size() == 1);
	CHECK(tables[0].source == DatasetEditor::SOURCE_SECTIONS);
	CHECK(tables[0].row_count == 2);
	CHECK(DatasetEditor::can_edit(ini.ptr()));
	// The INI dock opens INI files; the Dataset dock only with "Open in Dataset".
	CHECK_FALSE(DatasetEditor::can_open_automatically(ini.ptr()));

	Ref<XML> xml;
	xml.instantiate();
	xml->parse(R"(<units><unit id="knight" hp="100"/><unit id="archer"/><note>Text</note><group/></units>)");
	tables = DatasetEditor::find_tables(xml.ptr());
	REQUIRE(tables.size() == 1);
	CHECK(tables[0].source == DatasetEditor::SOURCE_ELEMENTS);
	CHECK(tables[0].key == Variant("unit"));
	CHECK(tables[0].row_count == 2);
}

TEST_CASE("[DatasetEditor] Cell text and values") {
	CHECK(DatasetEditor::value_to_text(3.0, true) == "3");
	CHECK(DatasetEditor::value_to_text(1.5, true) == "1.5");
	CHECK(DatasetEditor::value_to_text(Variant(), true) == "null");
	CHECK(DatasetEditor::value_to_text(Variant(), false).is_empty());
	CHECK(DatasetEditor::value_to_text(true, false) == "true");
	CHECK(DatasetEditor::value_to_text("text", false) == "text");

	Variant value;
	CHECK(DatasetEditor::text_to_value("42", Variant::INT, false, value));
	CHECK(value.get_type() == Variant::INT);
	CHECK(int(value) == 42);
	CHECK_FALSE(DatasetEditor::text_to_value("4x", Variant::INT, false, value));
	CHECK(DatasetEditor::text_to_value("2.5", Variant::FLOAT, true, value));
	CHECK(double(value) == 2.5);
	CHECK(DatasetEditor::text_to_value("0", Variant::BOOL, false, value));
	CHECK(value == Variant(false));
	CHECK_FALSE(DatasetEditor::text_to_value("maybe", Variant::BOOL, false, value));
	CHECK(DatasetEditor::text_to_value(" 7 ", Variant::STRING, false, value));
	CHECK(value == Variant(" 7 "));

	// No type yet: taken from the text; JSON numbers are floats.
	CHECK(DatasetEditor::text_to_value("7", Variant::NIL, true, value));
	CHECK(value.get_type() == Variant::FLOAT);
	CHECK(DatasetEditor::text_to_value("7", Variant::NIL, false, value));
	CHECK(value.get_type() == Variant::INT);
	CHECK(DatasetEditor::text_to_value("null", Variant::NIL, true, value));
	CHECK(value.get_type() == Variant::NIL);
	CHECK(DatasetEditor::text_to_value("word", Variant::NIL, false, value));
	CHECK(value == Variant("word"));

	// Other types: JSON text in JSON files, Variant text elsewhere.
	CHECK(DatasetEditor::text_to_value("[1, 2]", Variant::ARRAY, true, value));
	CHECK(Array(value).size() == 2);
	CHECK_FALSE(DatasetEditor::text_to_value("[1, 2", Variant::ARRAY, true, value));
	CHECK(DatasetEditor::text_to_value("Vector2(1, 2)", Variant::VECTOR2, false, value));
	CHECK(Vector2(value) == Vector2(1, 2));
	CHECK(DatasetEditor::value_to_text(Vector2(1, 2), false) == "Vector2(1, 2)");
}

TEST_CASE("[DatasetEditor] Rows are written back in place") {
	using DE = DatasetEditor;
	Array records;
	PackedStringArray names;

	// JSON under a key: the other keys stay, untouched numbers keep their value (full precision).
	Ref<JSON> json = make_json(R"({"version": 2, "items": [{"id": 1, "price": 0.1, "weight": 1234567.123456789}, {"id": 2}]})");
	DE::read_rows(DE::SOURCE_RECORDS, "items", json->get_data(), records, names);
	REQUIRE(records.size() == 2);
	Dictionary second = Dictionary(records[1]).duplicate();
	second["id"] = 3.0;
	records[1] = second;
	Ref<JSON> back = make_json(DE::encode(json.ptr(), DE::build_data(DE::SOURCE_RECORDS, "items", json->get_data(), records, names)));
	const Dictionary root = back->get_data();
	CHECK(int(root["version"]) == 2);
	const Array items = root["items"];
	const Dictionary first = items[0];
	CHECK(double(first["price"]) == 0.1);
	CHECK(double(first["weight"]) == 1234567.123456789);
	CHECK(int(Dictionary(items[1])["id"]) == 3);

	// Deleting every row leaves an empty table that stays open.
	const Variant emptied = DE::build_data(DE::SOURCE_RECORDS, "items", json->get_data(), Array(), names);
	CHECK(DE::has_empty_table(DE::SOURCE_RECORDS, "items", emptied));
	CHECK_FALSE(DE::has_empty_table(DE::SOURCE_RECORDS, "missing", emptied));
	CHECK(DE::has_empty_table(DE::SOURCE_RECORDS, Variant(), Array()));
	CHECK(DE::has_empty_table(DE::SOURCE_SECTIONS, Variant(), Dictionary()));

	// INI: a renamed section keeps its place.
	Ref<INI> ini;
	ini.instantiate();
	ini->parse("[knight]\nhp=100\n[archer]\nhp=80\n");
	DE::read_rows(DE::SOURCE_SECTIONS, Variant(), ini->get_data(), records, names);
	CHECK(names == PackedStringArray({ "knight", "archer" }));
	names.set(0, "paladin");
	const Dictionary sections = DE::build_data(DE::SOURCE_SECTIONS, Variant(), ini->get_data(), records, names);
	CHECK(String(sections.keys()[0]) == "paladin");
	CHECK(String(sections.keys()[1]) == "archer");

	// XML: rows fill the places of their tag, other children stay, new rows follow the last row.
	Ref<XML> xml;
	xml.instantiate();
	xml->parse(R"(<units><unit id="a"/><note/><unit id="b"/><group/></units>)");
	DE::read_rows(DE::SOURCE_ELEMENTS, "unit", xml->get_data(), records, names);
	REQUIRE(records.size() == 2);
	CHECK(String(DE::get_fields(DE::SOURCE_ELEMENTS, records[1])["id"]) == "b");
	Array reordered;
	reordered.push_back(records[1]);
	reordered.push_back(records[0]);
	reordered.push_back(records[0]);
	const Dictionary xml_root = DE::build_data(DE::SOURCE_ELEMENTS, "unit", xml->get_data(), reordered, names);
	const Array children = xml_root[XML::KEY_CHILDREN];
	REQUIRE(children.size() == 5);
	CHECK(String(DE::get_fields(DE::SOURCE_ELEMENTS, children[0])["id"]) == "b");
	CHECK(String(Dictionary(children[1])[XML::KEY_NAME]) == "note");
	CHECK(String(DE::get_fields(DE::SOURCE_ELEMENTS, children[2])["id"]) == "a");
	CHECK(String(DE::get_fields(DE::SOURCE_ELEMENTS, children[3])["id"]) == "a");
	CHECK(String(Dictionary(children[4])[XML::KEY_NAME]) == "group");
	CHECK(DE::has_empty_table(DE::SOURCE_ELEMENTS, "unit", DE::build_data(DE::SOURCE_ELEMENTS, "unit", xml->get_data(), Array(), names)));
}

[[maybe_unused]] static Ref<TextDataFormat> make_format(const StringName &p_class, const String &p_text) {
	Ref<TextDataFormat> format = Ref<TextDataFormat>(Object::cast_to<TextDataFormat>(ClassDB::instantiate(p_class)));
	CHECK(format.is_valid());
	if (format.is_valid()) {
		CHECK(format->parse(p_text) == OK);
	}
	return format;
}

#ifdef MODULE_YAML_ENABLED
TEST_CASE("[DatasetEditor] YAML tables") {
	Ref<TextDataFormat> yaml = make_format("YAML", "- name: knight\n  hp: 100\n  speed: 0.1\n- name: archer\n  hp: 80\n");
	REQUIRE(yaml.is_valid());
	CHECK(DatasetEditor::find_tables(yaml.ptr()).size() == 1);
	// Encoding the data again keeps every value (an untouched record after an edit of another one).
	Ref<TextDataFormat> back = make_format("YAML", DatasetEditor::encode(yaml.ptr(), yaml->get_data()));
	REQUIRE(back.is_valid());
	CHECK(back->get_data() == yaml->get_data());
	// Several documents: not a table (the encoder writes the first document only).
	yaml = make_format("YAML", "- name: knight\n---\n- name: archer\n");
	REQUIRE(yaml.is_valid());
	CHECK(DatasetEditor::find_tables(yaml.ptr()).is_empty());
}
#endif // MODULE_YAML_ENABLED

#ifdef MODULE_TOML_ENABLED
TEST_CASE("[DatasetEditor] TOML arrays of tables") {
	Ref<TextDataFormat> toml = make_format("TOML", "title = \"units\"\n\n[[unit]]\nname = \"knight\"\nhp = 100\nspeed = 0.1\n\n[[unit]]\nname = \"archer\"\nhp = 80\n");
	REQUIRE(toml.is_valid());
	const Vector<DatasetEditor::Table> tables = DatasetEditor::find_tables(toml.ptr());
	REQUIRE(tables.size() == 1);
	CHECK(tables[0].key == Variant("unit"));
	CHECK(tables[0].row_count == 2);
	Ref<TextDataFormat> back = make_format("TOML", DatasetEditor::encode(toml.ptr(), toml->get_data()));
	REQUIRE(back.is_valid());
	CHECK(back->get_data() == toml->get_data());
}
#endif // MODULE_TOML_ENABLED

} // namespace TestDatasetEditor

#endif // TOOLS_ENABLED
