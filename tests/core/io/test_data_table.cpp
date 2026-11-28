/**************************************************************************/
/*  test_data_table.cpp                                                   */
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

TEST_FORCE_LINK(test_data_table)

#include "core/io/csv_format.h"
#include "core/io/data_table.h"
#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "tests/signal_watcher.h"

namespace TestDataTable {

// Units: id (int), name (String), hp (int); Knight has no hp.
static Ref<DataTable> make_units() {
	Ref<DataTable> table;
	table.instantiate();
	table->add_column("id", Variant::INT);
	table->add_column("name", Variant::STRING);
	table->add_column("hp", Variant::INT);
	table->add_row(Dictionary{ { "id", 1 }, { "name", "Archer" }, { "hp", 40 } });
	table->add_row(Dictionary{ { "id", 2 }, { "name", "Knight" } });
	table->add_row(Dictionary{ { "id", 3 }, { "name", "Mage" }, { "hp", 30 } });
	return table;
}

TEST_CASE("[DataTable] Columns come from the schema") {
	Ref<DataTable> table;
	table.instantiate();
	CHECK(table->get_schema().is_valid());
	table->add_column("id", Variant::INT);
	table->add_column("any");
	table->add_column("name", Variant::STRING, 1);
	CHECK(table->get_column_count() == 3);
	CHECK(table->get_column_name(1) == StringName("name"));
	CHECK(table->get_column_type(0) == Variant::INT);
	CHECK(table->get_column_type(2) == Variant::NIL);
	CHECK(table->find_column("any") == 2);
	CHECK(table->find_column("missing") == -1);
	CHECK(table->get_schema()->get_fields().size() == 3);
	CHECK(table->get_schema()->find_field("name") == 1);

	table->rename_column(2, "value");
	CHECK(table->find_column("value") == 2);
	CHECK(table->get_schema()->find_field("value") == 2);
	table->remove_column(1);
	CHECK(table->get_column_count() == 2);
	CHECK(table->get_schema()->get_fields().size() == 2);

	ERR_PRINT_OFF;
	table->add_column("id", Variant::INT);
	table->add_column(StringName(), Variant::INT);
	table->add_column("node", Variant::OBJECT);
	table->rename_column(1, "id");
	ERR_PRINT_ON;
	CHECK(table->get_column_count() == 2);
	CHECK(table->get_column_name(1) == StringName("value"));
}

TEST_CASE("[DataTable] Rows, cells and absent cells") {
	Ref<DataTable> table = make_units();
	CHECK(table->get_row_count() == 3);
	CHECK(table->has_cell(0, 2));
	CHECK_FALSE(table->has_cell(1, 2));
	CHECK(table->get_cell(1, 2) == Variant());
	CHECK(table->get_cell(0, 1) == Variant("Archer"));

	const Dictionary knight = table->get_row(1);
	CHECK(knight.size() == 2);
	CHECK_FALSE(knight.has("hp"));
	CHECK(int(knight["id"]) == 2);

	// Values are converted to the column type.
	table->set_cell(1, 2, "55");
	CHECK(table->get_cell(1, 2).get_type() == Variant::INT);
	CHECK(int(table->get_cell(1, 2)) == 55);
	table->set_cell(1, 2, 56.0);
	CHECK(table->get_cell(1, 2) == Variant(56));
	ERR_PRINT_OFF;
	table->set_cell(1, 2, "many");
	table->set_cell(1, 2, 56.5);
	ERR_PRINT_ON;
	CHECK(table->get_cell(1, 2) == Variant(56));

	table->erase_cell(1, 2);
	CHECK_FALSE(table->has_cell(1, 2));

	// Inserting and removing rows keeps the absent cells with their rows.
	table->insert_row(0, Dictionary{ { "name", "Scout" } });
	CHECK(table->get_row_count() == 4);
	CHECK_FALSE(table->has_cell(0, 0));
	CHECK(table->has_cell(1, 2));
	CHECK_FALSE(table->has_cell(2, 2));
	CHECK(table->has_cell(3, 2));
	table->remove_rows(0, 2);
	CHECK(table->get_row_count() == 2);
	CHECK(table->get_cell(0, 1) == Variant("Knight"));
	CHECK_FALSE(table->has_cell(0, 2));
	CHECK(table->get_cell(1, 2) == Variant(30));

	// set_row() replaces the row: keys it does not have become absent.
	table->set_row(1, Dictionary{ { "id", 9 }, { "hp", 31 } });
	CHECK(table->get_cell(1, 0) == Variant(9));
	CHECK_FALSE(table->has_cell(1, 1));
	CHECK(table->get_cell(1, 2) == Variant(31));

	ERR_PRINT_OFF;
	table->add_row(Dictionary{ { "id", 10 }, { "unknown", true } });
	table->remove_rows(2, 5);
	table->insert_row(9);
	ERR_PRINT_ON;
	CHECK(table->get_row_count() == 3);
	CHECK(table->get_cell(2, 0) == Variant(10));
}

TEST_CASE("[DataTable] Columns are packed arrays") {
	Ref<DataTable> table = make_units();
	table->add_column("alive", Variant::BOOL);
	table->add_column("speed", Variant::FLOAT);
	table->add_column("tint", Variant::COLOR);
	table->add_column("extra");
	table->set_cell(0, 3, true);
	table->set_cell(2, 4, 1);
	table->set_cell(2, 5, Color(1, 0, 0));
	table->set_cell(1, 6, Vector2i(1, 2));

	const Variant hp = table->get_column(2);
	REQUIRE(hp.get_type() == Variant::PACKED_INT64_ARRAY);
	const PackedInt64Array hp_values = hp;
	CHECK(hp_values == PackedInt64Array{ 40, 0, 30 });
	CHECK(table->get_column(1).get_type() == Variant::PACKED_STRING_ARRAY);
	CHECK(table->get_column(3) == Variant(PackedByteArray{ 1, 0, 0 }));
	CHECK(table->get_column(4) == Variant(PackedFloat64Array{ 0.0, 0.0, 1.0 }));
	const Array tints = table->get_column(5);
	CHECK(tints[2] == Variant(Color(1, 0, 0)));
	CHECK(tints[0] == Variant());
	const Array extras = table->get_column(6);
	CHECK(extras[1] == Variant(Vector2i(1, 2)));

	// A column without a fixed type keeps null as a value of its own.
	table->set_cell(0, 6, Variant());
	CHECK(table->has_cell(0, 6));
	CHECK_FALSE(table->has_cell(2, 6));
}

// SignalWatcher supports at most three arguments; cells_changed has four.
static LocalVector<Vector4i> cells_changed_events;

static void record_cells_changed(int p_row, int p_column, int p_row_count, int p_column_count) {
	cells_changed_events.push_back(Vector4i(p_row, p_column, p_row_count, p_column_count));
}

TEST_CASE("[DataTable] Signals") {
	Ref<DataTable> table = make_units();
	cells_changed_events.clear();
	table->connect("cells_changed", callable_mp_static(&record_cells_changed));
	SIGNAL_WATCH(table.ptr(), "rows_inserted");
	SIGNAL_WATCH(table.ptr(), "rows_removed");
	SIGNAL_WATCH(table.ptr(), "columns_changed");

	table->set_cell(1, 2, 50);
	REQUIRE(cells_changed_events.size() == 1);
	CHECK(cells_changed_events[0] == Vector4i(1, 2, 1, 1));

	table->add_row();
	Array insert_args;
	insert_args.push_back(Array({ 3, 1 }));
	SIGNAL_CHECK("rows_inserted", insert_args);

	table->remove_rows(0, 2);
	Array remove_args;
	remove_args.push_back(Array({ 0, 2 }));
	SIGNAL_CHECK("rows_removed", remove_args);

	table->add_column("level", Variant::INT);
	Array no_args;
	no_args.push_back(Array());
	SIGNAL_CHECK("columns_changed", no_args);

	// Erasing an absent cell changes nothing.
	table->erase_cell(1, 3);
	CHECK(cells_changed_events.size() == 1);

	table->sort_rows(0);
	REQUIRE(cells_changed_events.size() == 2);
	CHECK(cells_changed_events[1] == Vector4i(0, 0, 2, 4));

	table->disconnect("cells_changed", callable_mp_static(&record_cells_changed));
	cells_changed_events.clear();
	SIGNAL_UNWATCH(table.ptr(), "rows_inserted");
	SIGNAL_UNWATCH(table.ptr(), "rows_removed");
	SIGNAL_UNWATCH(table.ptr(), "columns_changed");
}

TEST_CASE("[DataTable] Column type changes and schema changes") {
	Ref<DataTable> table = make_units();
	Ref<DataValidation> validation = table->set_column_type(2, Variant::STRING);
	CHECK(validation->is_valid());
	CHECK(table->get_cell(0, 2) == Variant("40"));
	CHECK_FALSE(table->has_cell(1, 2));

	// "Archer" does not convert to int: the cells become absent and are reported.
	validation = table->set_column_type(1, Variant::INT);
	CHECK(validation->get_issue_count() == 3);
	CHECK(validation->get_issue_kind(0) == DataValidation::ISSUE_KIND_TYPE_MISMATCH);
	CHECK(validation->get_issue_column(0) == 1);
	CHECK_FALSE(table->has_cell(0, 1));
	CHECK(table->get_schema()->get_field("name")->get_value_type() == Variant::INT);

	// A field edited directly retypes its column.
	table->get_schema()->get_field("id")->set_value_type(Variant::FLOAT);
	CHECK(table->get_column_type(0) == Variant::FLOAT);
	CHECK(table->get_cell(2, 0) == Variant(3.0));

	// Fields added or removed in the schema add or remove columns; cells follow their field.
	Ref<DataField> level;
	level.instantiate();
	level->set_field_name("level");
	level->set_value_type(Variant::INT);
	Ref<DataSchema> schema = table->get_schema();
	schema->add_field(level);
	CHECK(table->get_column_count() == 4);
	CHECK_FALSE(table->has_cell(0, 3));
	schema->remove_field(1);
	CHECK(table->get_column_count() == 3);
	CHECK(table->get_column_name(1) == StringName("hp"));
	CHECK(table->get_cell(0, 1) == Variant("40"));

	// A new schema keeps the cells of fields with the same name.
	Ref<DataSchema> shared = DataSchema::create_from_dictionary(Dictionary{ { "hp", (int)Variant::INT }, { "id", (int)Variant::INT } });
	table->set_schema(shared);
	CHECK(table->get_schema() == shared);
	CHECK(table->get_column_count() == 2);
	CHECK(table->get_column_name(0) == StringName("hp"));
	CHECK(table->get_cell(0, 0) == Variant(40));
	CHECK(table->get_cell(2, 1) == Variant(3));

	// Two tables can share a schema.
	Ref<DataTable> other;
	other.instantiate();
	other->set_schema(shared);
	table->add_column("name", Variant::STRING);
	CHECK(other->get_column_count() == 3);
}

TEST_CASE("[DataTable] Find and sort rows") {
	Ref<DataTable> table = make_units();
	table->add_row(Dictionary{ { "id", 4 }, { "name", "Rogue" }, { "hp", 30 } });
	CHECK(table->find_rows(2, 30) == PackedInt32Array{ 2, 3 });
	CHECK(table->find_rows(2, "40") == PackedInt32Array{ 0 });
	CHECK(table->find_rows(2, "none").is_empty());

	// Stable, absent cells last.
	table->sort_rows(2);
	CHECK(table->get_column(0) == Variant(PackedInt64Array{ 3, 4, 1, 2 }));
	CHECK_FALSE(table->has_cell(3, 2));
	CHECK(table->get_cell(0, 1) == Variant("Mage"));
	table->sort_rows(2, DataTable::SORT_ORDER_DESCENDING);
	CHECK(table->get_column(0) == Variant(PackedInt64Array{ 1, 3, 4, 2 }));
	table->sort_rows(1);
	CHECK(table->get_column(1) == Variant(PackedStringArray{ "Archer", "Knight", "Mage", "Rogue" }));
	CHECK(table->get_cell(0, 2) == Variant(40));
}

TEST_CASE("[DataTable] Records round trip") {
	Array records;
	records.push_back(Dictionary{ { "id", 1 }, { "name", "Archer" }, { "speed", 1.5 } });
	records.push_back(Dictionary{ { "id", 2 }, { "speed", 2 }, { "tag", "x" } });
	records.push_back(Dictionary{ { "id", 3 }, { "tag", 7 }, { "note", Variant() } });
	Ref<DataTable> table = DataTable::create_from_records(records);
	CHECK(table->get_column_count() == 5);
	CHECK(table->get_column_name(3) == StringName("tag"));
	CHECK(table->get_column_type(0) == Variant::INT);
	CHECK(table->get_column_type(1) == Variant::STRING);
	// int and float values together: a Variant column, so every value keeps its type.
	CHECK(table->get_column_type(2) == Variant::NIL);
	CHECK(table->get_column_type(3) == Variant::NIL);
	CHECK(table->get_column_type(4) == Variant::NIL);
	CHECK_FALSE(table->has_cell(1, 1));
	CHECK(table->has_cell(2, 4));

	const Array result = table->to_records();
	REQUIRE(result.size() == 3);
	const Dictionary second = result[1];
	CHECK(second.size() == 3);
	CHECK(second["speed"].get_type() == Variant::INT);
	CHECK(second["speed"] == Variant(2));
	const Dictionary first = result[0];
	CHECK(first["speed"] == Variant(1.5));
	const Dictionary third = result[2];
	CHECK(third.has("note"));
	CHECK(third["tag"] == Variant(7));
	CHECK_FALSE(third.has("speed"));

	// Keys that are not Strings (YAML, TOML) become String column names and keep their cells.
	Array int_keys;
	int_keys.push_back(Dictionary{ { 1, "x" }, { 2, true } });
	Ref<DataTable> numbered = DataTable::create_from_records(int_keys);
	CHECK(numbered->get_column_name(0) == StringName("1"));
	CHECK(numbered->get_cell(0, 0) == Variant("x"));
	CHECK(numbered->get_cell(0, 1) == Variant(true));
}

TEST_CASE("[DataTable] CSV conversion") {
	Ref<CSV> csv;
	csv.instantiate();
	Array rows;
	rows.push_back(PackedStringArray{ "1", "Archer", "40" });
	rows.push_back(PackedStringArray{ "2", "Knight", "" });
	rows.push_back(PackedStringArray{ "3", "", "lots" });
	csv->set_data(rows);
	csv->set_header(PackedStringArray{ "id", "name", "hp" });

	Ref<DataSchema> schema = DataSchema::create_from_dictionary(Dictionary{ { "id", (int)Variant::INT }, { "hp", (int)Variant::INT } });
	ERR_PRINT_OFF;
	Ref<DataTable> table = DataTable::create_from_csv(csv, schema);
	ERR_PRINT_ON;
	CHECK(table->get_column_count() == 3);
	CHECK(table->get_column_type(0) == Variant::INT);
	CHECK(table->get_column_type(1) == Variant::STRING);
	CHECK(table->get_cell(0, 2) == Variant(40));
	CHECK_FALSE(table->has_cell(1, 2));
	CHECK_FALSE(table->has_cell(2, 2));
	CHECK(table->get_cell(2, 1) == Variant(""));
	// The fields are copies: the table can change its columns without changing the schema.
	CHECK(table->get_schema() != schema);
	CHECK(table->get_schema()->get_field("id")->is_required());

	Ref<CSV> written = table->to_csv();
	CHECK(written->get_header() == PackedStringArray{ "id", "name", "hp" });
	CHECK(written->get_row_count() == 3);
	CHECK(written->get_row(0) == PackedStringArray{ "1", "Archer", "40" });
	CHECK(written->get_row(1) == PackedStringArray{ "2", "Knight", "" });

	Ref<CSV> plain;
	plain.instantiate();
	Array plain_rows;
	plain_rows.push_back(PackedStringArray{ "a", "b" });
	plain->set_data(plain_rows);
	Ref<DataTable> unnamed = DataTable::create_from_csv(plain);
	CHECK(unnamed->get_column_name(1) == StringName("column_2"));
	CHECK(unnamed->get_cell(0, 1) == Variant("b"));
}

TEST_CASE("[DataTable] Validate through the schema") {
	Ref<DataTable> table = make_units();
	CHECK(table->validate()->is_valid());
	table->get_schema()->get_field("hp")->set_required(true);
	const Ref<DataValidation> validation = table->validate();
	REQUIRE(validation->get_issue_count() == 1);
	CHECK(validation->get_issue_kind(0) == DataValidation::ISSUE_KIND_MISSING_FIELD);
	CHECK(validation->get_issue_field(0) == StringName("hp"));
}

static Ref<DataTable> make_mixed_units() {
	Ref<DataTable> table = make_units();
	table->add_column("extra");
	table->add_column("alive", Variant::BOOL);
	table->add_column("speed", Variant::FLOAT);
	table->set_cell(2, 3, Vector2(1, 2));
	table->set_cell(0, 4, true);
	table->set_cell(1, 4, false);
	table->set_cell(0, 5, 1.5);
	return table;
}

static void check_mixed_units(const Ref<DataTable> &p_copy, const Ref<DataTable> &p_table) {
	REQUIRE(p_copy.is_valid());
	CHECK(p_copy->get_column_count() == 6);
	CHECK(p_copy->get_row_count() == 3);
	CHECK(p_copy->to_records() == p_table->to_records());
	CHECK_FALSE(p_copy->has_cell(1, 2));
	CHECK(p_copy->get_cell(2, 3) == Variant(Vector2(1, 2)));
	CHECK(p_copy->get_cell(0, 4) == Variant(true));
	CHECK(p_copy->get_cell(1, 4) == Variant(false));
	CHECK_FALSE(p_copy->has_cell(2, 4));
	CHECK(p_copy->get_cell(0, 5) == Variant(1.5));
}

TEST_CASE("[DataTable] Stored data survives a copy") {
	Ref<DataTable> table = make_mixed_units();
	check_mixed_units(table->duplicate(true), table);
	check_mixed_units(table->duplicate(false), table);
}

TEST_CASE("[DataTable] Stored data survives save and load") {
	Ref<DataTable> table = make_mixed_units();
	DirAccess::make_dir_recursive_absolute(OS::get_singleton()->get_user_data_dir());
	const String path = "user://test_data_table.tres";
	REQUIRE(ResourceSaver::save(table, path) == OK);
	Ref<DataTable> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	check_mixed_units(loaded, table);
	DirAccess::remove_absolute(path);
}

TEST_CASE("[DataTable] A shallow copy does not share the table's own schema") {
	Ref<DataTable> table = make_units();
	Ref<DataTable> copy = table->duplicate(false);
	REQUIRE(copy.is_valid());
	CHECK(copy->get_schema() != table->get_schema());
	copy->add_column("level", Variant::INT);
	copy->rename_column(0, "key");
	CHECK(table->get_column_count() == 3);
	CHECK(table->get_column_name(0) == StringName("id"));

	// A schema saved in a file is shared on purpose.
	Ref<DataSchema> shared = DataSchema::create_from_dictionary(Dictionary{ { "id", (int)Variant::INT } });
	shared->set_path_cache("res://shared_units_schema.tres");
	table->set_schema(shared);
	Ref<DataTable> shared_copy = table->duplicate(false);
	CHECK(shared_copy->get_schema() == shared);
}

TEST_CASE("[DataTable] Absent cells across many rows") {
	// More than one 64-bit word of absent bits: forward fill, insert and remove in the middle.
	Ref<DataTable> table;
	table.instantiate();
	table->add_column("id", Variant::INT);
	for (int i = 0; i < 130; i++) {
		table->add_row(Dictionary{ { "id", i } });
	}
	table->add_column("hp", Variant::INT);
	for (int i = 0; i < 130; i += 2) {
		table->set_cell(i, 1, i);
	}
	CHECK(table->has_cell(64, 1));
	CHECK_FALSE(table->has_cell(65, 1));
	table->insert_row(1, Dictionary{ { "id", -1 } });
	CHECK_FALSE(table->has_cell(1, 1));
	CHECK(table->has_cell(3, 1));
	CHECK(table->has_cell(129, 1));
	CHECK_FALSE(table->has_cell(130, 1));
	table->remove_rows(0, 70);
	CHECK(table->get_row_count() == 61);
	// Row 70 (id 69, odd) is now row 0; row 71 (id 70) is row 1.
	CHECK_FALSE(table->has_cell(0, 1));
	CHECK(table->get_cell(1, 1) == Variant(70));
	CHECK(table->find_rows(1, 128) == PackedInt32Array{ 59 });
	table->erase_cell(1, 1);
	CHECK_FALSE(table->has_cell(1, 1));
	CHECK(table->has_cell(3, 1));
}

// A table whose cells live row by row in its own storage, as a source of another format would keep them.
class RowStorageTable : public DataTable {
	struct Cell {
		Variant value;
		bool present = false;
	};
	LocalVector<Vector<Cell>> cell_rows;
	int storage_columns = 0;

public:
	int row_edits = 0;

protected:
	int _get_row_count() const override { return cell_rows.size(); }
	Variant _get_cell(int p_row, int p_column) const override { return cell_rows[p_row][p_column].value; }
	bool _has_cell(int p_row, int p_column) const override { return cell_rows[p_row][p_column].present; }
	void _set_cell(int p_row, int p_column, const Variant &p_value) override {
		cell_rows[p_row].write[p_column].value = p_value;
		cell_rows[p_row].write[p_column].present = true;
	}
	void _erase_cell(int p_row, int p_column) override { cell_rows[p_row].write[p_column] = Cell(); }
	void _insert_rows(int p_row, int p_count) override {
		row_edits++;
		for (int i = 0; i < p_count; i++) {
			Vector<Cell> cells;
			cells.resize(storage_columns);
			cell_rows.insert(p_row, cells);
		}
	}
	void _remove_rows(int p_row, int p_count) override {
		row_edits++;
		for (int i = 0; i < p_count; i++) {
			cell_rows.remove_at(p_row);
		}
	}
	void _insert_column(int p_column) override {
		storage_columns++;
		for (Vector<Cell> &cells : cell_rows) {
			cells.insert(p_column, Cell());
		}
	}
	void _remove_column(int p_column) override {
		storage_columns--;
		for (Vector<Cell> &cells : cell_rows) {
			cells.remove_at(p_column);
		}
	}
};

TEST_CASE("[DataTable] Storage virtuals") {
	Ref<RowStorageTable> storage = memnew(RowStorageTable);
	Ref<DataTable> table = storage;
	table->add_column("id", Variant::INT);
	table->add_row(Dictionary{ { "id", 2 } });
	table->add_row();
	table->add_row(Dictionary{ { "id", "1" } });
	table->add_column("name", Variant::STRING, 0);
	table->set_cell(1, 0, "Knight");
	CHECK(table->get_row_count() == 3);
	CHECK(table->get_cell(2, 1) == Variant(1));
	CHECK_FALSE(table->has_cell(1, 1));
	CHECK(table->get_column(1) == Variant(PackedInt64Array{ 2, 0, 1 }));

	// sort_rows() rewrites the cells in place: the source keeps its rows.
	const int row_edits = storage->row_edits;
	table->sort_rows(1);
	CHECK(storage->row_edits == row_edits);
	CHECK(table->get_cell(0, 1) == Variant(1));
	CHECK(table->get_cell(2, 0) == Variant("Knight"));
	CHECK_FALSE(table->has_cell(2, 1));

	const Ref<DataValidation> validation = table->set_column_type(1, Variant::STRING);
	CHECK(validation->is_valid());
	CHECK(table->get_cell(1, 1) == Variant("2"));
	table->remove_rows(0, 1);
	CHECK(table->to_records().size() == 2);
}

} // namespace TestDataTable
