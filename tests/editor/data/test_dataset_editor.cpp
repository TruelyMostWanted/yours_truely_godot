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
#include "core/io/ini_format.h"
#include "core/io/json.h"
#include "core/io/xml_format.h"
#include "editor/data/csv_table_editor.h"
#include "editor/data/dataset_editor.h"

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

	// A header with a known language: the Localization Table edits it.
	csv->parse("keys,en,de\nHELLO,Hello,Hallo\n");
	CHECK(CSVTableEditor::is_translation_csv(csv.ptr()));
	CHECK(CSVTableEditor::can_edit(csv.ptr()));
	CHECK_FALSE(DatasetEditor::can_edit(csv.ptr()));
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

} // namespace TestDatasetEditor

#endif // TOOLS_ENABLED
