/**************************************************************************/
/*  test_sqlite.h                                                         */
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

#include "../sqlite_database.h"
#include "../sqlite_statement.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifdef TOOLS_ENABLED
#include "../editor/database_editor.h"
#include "../editor/database_table_dialog.h"
#endif

namespace TestSQLite {

[[maybe_unused]] static String _fresh_db_path(const String &p_name) {
	const String db_path = TestUtils::get_temp_path(p_name);
	if (FileAccess::exists(db_path)) {
		DirAccess::remove_absolute(db_path);
	}
	return db_path;
}

TEST_CASE("[SQLite] Open, create, execute and query") {
	const String db_path = _fresh_db_path("sqlite_basic.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();

	CHECK_FALSE(db->is_open());
	CHECK(db->execute("SELECT 1;") == ERR_UNCONFIGURED);

	REQUIRE(db->open(db_path) == OK);
	CHECK(db->is_open());
	CHECK(db->get_database_path() == db_path);
	CHECK(db->get_open_mode() == SQLiteDatabase::OPEN_MODE_CREATE);

	CHECK(db->execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, weight REAL, data BLOB, flag INTEGER);") == OK);
	PackedByteArray blob;
	blob.push_back(1);
	blob.push_back(2);
	blob.push_back(255);
	CHECK(db->execute("INSERT INTO items (name, weight, data, flag) VALUES (?, ?, ?, ?);", varray("sword", 2.5, blob, true)) == OK);
	CHECK(db->get_last_insert_rowid() == 1);
	CHECK(db->get_changes() == 1);

	Dictionary named;
	named["name"] = "shield";
	named["weight"] = 4;
	CHECK(db->execute("INSERT INTO items (name, weight, data) VALUES (:name, @weight, NULL);", named) == OK);
	CHECK(db->get_last_insert_rowid() == 2);

	const TypedArray<Dictionary> rows = db->query("SELECT * FROM items ORDER BY id;");
	REQUIRE(rows.size() == 2);
	const Dictionary first = rows[0];
	CHECK(first["id"] == Variant(1));
	CHECK(first["name"] == Variant("sword"));
	CHECK(double(first["weight"]) == doctest::Approx(2.5));
	CHECK(first["data"] == Variant(blob));
	CHECK(first["flag"] == Variant(1));
	const Dictionary second = rows[1];
	CHECK(second["name"] == Variant("shield"));
	CHECK(second["data"].get_type() == Variant::NIL);

	const TypedArray<Dictionary> filtered = db->query("SELECT name FROM items WHERE weight > ?;", varray(3));
	REQUIRE(filtered.size() == 1);
	CHECK(Dictionary(filtered[0])["name"] == Variant("shield"));

	// JSON, math functions and FTS5 are compiled in.
	const TypedArray<Dictionary> extras = db->query("SELECT json_extract('{\"a\": 7}', '$.a') AS a, floor(2.7) AS f;");
	REQUIRE(extras.size() == 1);
	CHECK(Dictionary(extras[0])["a"] == Variant(7));
	CHECK(double(Dictionary(extras[0])["f"]) == doctest::Approx(2.0));
	CHECK(db->execute("CREATE VIRTUAL TABLE notes USING fts5(body); INSERT INTO notes VALUES ('the quick brown fox');") == OK);
	CHECK(db->query("SELECT * FROM notes WHERE notes MATCH 'quick';").size() == 1);

	db->close();
	CHECK_FALSE(db->is_open());

	// The data survives reopening.
	REQUIRE(db->open(db_path, SQLiteDatabase::OPEN_MODE_READ_WRITE) == OK);
	CHECK(db->query("SELECT id FROM items;").size() == 2);
	db->close();
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Errors") {
	const String db_path = _fresh_db_path("sqlite_errors.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();

	CHECK(db->open(db_path, SQLiteDatabase::OPEN_MODE_READ_WRITE) == ERR_FILE_NOT_FOUND);
	CHECK(db->open(db_path, SQLiteDatabase::OPEN_MODE_READ_ONLY) == ERR_FILE_NOT_FOUND);
	CHECK(db->open("res://sqlite_test_never_written.db", SQLiteDatabase::OPEN_MODE_CREATE) == ERR_FILE_NO_PERMISSION);
	CHECK_FALSE(db->get_last_error_message().is_empty());

	REQUIRE(db->open(db_path) == OK);
	CHECK(db->get_last_error() == OK);
	CHECK(db->get_last_error_message().is_empty());

	CHECK(db->execute("SELEC nonsense;") == ERR_QUERY_FAILED);
	CHECK(db->get_last_error() == ERR_QUERY_FAILED);
	CHECK(db->get_last_error_message().contains("syntax error"));
	CHECK(db->query("SELECT * FROM missing_table;").is_empty());
	CHECK(db->get_last_error() == ERR_QUERY_FAILED);

	CHECK(db->execute("CREATE TABLE t (a INTEGER NOT NULL);") == OK);
	// Parameter count and type checks.
	CHECK(db->execute("INSERT INTO t VALUES (?);") == ERR_INVALID_PARAMETER);
	CHECK(db->execute("INSERT INTO t VALUES (?);", varray(1, 2)) == ERR_INVALID_PARAMETER);
	CHECK(db->execute("INSERT INTO t VALUES (?);", varray(Vector2(1, 2))) == ERR_INVALID_PARAMETER);
	CHECK(db->execute("INSERT INTO t VALUES (:a);", Dictionary()) == ERR_INVALID_PARAMETER);
	CHECK(db->execute("INSERT INTO t VALUES (?);", 5) == ERR_INVALID_PARAMETER);
	// Constraint violation.
	CHECK(db->execute("INSERT INTO t VALUES (NULL);") == ERR_QUERY_FAILED);
	CHECK(db->get_last_error_message().contains("NOT NULL"));
	db->close();

	// A file that is not a database.
	const String text_path = _fresh_db_path("sqlite_not_a_db.db");
	{
		Ref<FileAccess> f = FileAccess::open(text_path, FileAccess::WRITE);
		REQUIRE(f.is_valid());
		f->store_string("This is not an SQLite database, just some text that is long enough to fill a header.");
	}
	CHECK(db->open(text_path, SQLiteDatabase::OPEN_MODE_READ_ONLY) == ERR_FILE_CORRUPT);
	CHECK_FALSE(db->is_open());

	DirAccess::remove_absolute(text_path);
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Read-only databases") {
	const String db_path = _fresh_db_path("sqlite_read_only.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();
	REQUIRE(db->open(db_path) == OK);
	CHECK(db->execute("CREATE TABLE t (a INTEGER); INSERT INTO t VALUES (1), (2), (3);") == OK);
	db->close();

	REQUIRE(db->open(db_path, SQLiteDatabase::OPEN_MODE_READ_ONLY) == OK);
	CHECK(db->query("SELECT a FROM t;").size() == 3);
	CHECK(db->execute("INSERT INTO t VALUES (4);") == ERR_FILE_NO_PERMISSION);
	db->close();
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Transactions") {
	const String db_path = _fresh_db_path("sqlite_transactions.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();
	REQUIRE(db->open(db_path) == OK);
	CHECK(db->execute("CREATE TABLE t (a INTEGER);") == OK);

	CHECK_FALSE(db->is_in_transaction());
	CHECK(db->begin_transaction() == OK);
	CHECK(db->is_in_transaction());
	CHECK(db->execute("INSERT INTO t VALUES (1);") == OK);
	CHECK(db->rollback() == OK);
	CHECK_FALSE(db->is_in_transaction());
	CHECK(db->query("SELECT * FROM t;").is_empty());

	CHECK(db->begin_transaction() == OK);
	for (int i = 0; i < 100; i++) {
		CHECK(db->execute("INSERT INTO t VALUES (?);", varray(i)) == OK);
	}
	CHECK(db->commit() == OK);
	CHECK(db->query("SELECT * FROM t;").size() == 100);
	CHECK(db->commit() == ERR_QUERY_FAILED); // No transaction is active.

	db->close();
	REQUIRE(db->open(db_path, SQLiteDatabase::OPEN_MODE_READ_ONLY) == OK);
	const TypedArray<Dictionary> sum = db->query("SELECT sum(a) AS s FROM t;");
	REQUIRE(sum.size() == 1);
	CHECK(Dictionary(sum[0])["s"] == Variant(4950));
	db->close();
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Prepared statements") {
	const String db_path = _fresh_db_path("sqlite_statements.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();
	REQUIRE(db->open(db_path) == OK);
	CHECK(db->execute("CREATE TABLE t (i INTEGER, f REAL, s TEXT, b BLOB, n INTEGER);") == OK);

	CHECK(db->prepare("SELEC x;").is_null());
	CHECK(db->get_last_error() == ERR_QUERY_FAILED);
	CHECK(db->prepare("  -- only a comment").is_null());
	CHECK(db->get_last_error() == ERR_INVALID_PARAMETER);

	Ref<SQLiteStatement> insert = db->prepare("INSERT INTO t VALUES (?1, ?2, ?3, ?4, :n);");
	REQUIRE(insert.is_valid());
	CHECK(insert->get_bind_parameter_count() == 5);
	CHECK(insert->get_bind_parameter_index(":n") == 5);
	CHECK(insert->get_sql().begins_with("INSERT"));

	PackedByteArray blob;
	blob.push_back(42);
	for (int i = 0; i < 3; i++) {
		CHECK(insert->bind_int(1, i) == OK);
		CHECK(insert->bind_float(2, i * 0.5) == OK);
		CHECK(insert->bind_text(3, vformat("row %d", i)) == OK);
		CHECK(insert->bind_blob(4, blob) == OK);
		CHECK(insert->bind_null(5) == OK);
		CHECK(insert->step() == SQLiteStatement::STEP_RESULT_DONE);
		CHECK(insert->reset() == OK);
	}
	CHECK(insert->bind_int(6, 1) == ERR_INVALID_PARAMETER);
	CHECK(insert->bind_value(1, Vector3()) == ERR_INVALID_PARAMETER);
	CHECK(insert->bind_values(varray(10, 1.5, "values", PackedByteArray(), Variant())) == OK);
	CHECK(insert->step() == SQLiteStatement::STEP_RESULT_DONE);

	Ref<SQLiteStatement> select = db->prepare("SELECT i, f, s, b, n FROM t ORDER BY i;");
	REQUIRE(select.is_valid());
	CHECK(select->get_column_count() == 5);
	CHECK(select->get_column_name(2) == "s");

	CHECK(select->step() == SQLiteStatement::STEP_RESULT_ROW);
	CHECK(select->get_column_type(0) == SQLiteStatement::COLUMN_TYPE_INTEGER);
	CHECK(select->get_column_type(1) == SQLiteStatement::COLUMN_TYPE_FLOAT);
	CHECK(select->get_column_type(2) == SQLiteStatement::COLUMN_TYPE_TEXT);
	CHECK(select->get_column_type(3) == SQLiteStatement::COLUMN_TYPE_BLOB);
	CHECK(select->get_column_type(4) == SQLiteStatement::COLUMN_TYPE_NULL);
	CHECK(select->get_column_int(0) == 0);
	CHECK(select->get_column_text(2) == "row 0");
	CHECK(select->get_column_blob(3) == blob);
	CHECK(select->get_column_value(4).get_type() == Variant::NIL);

	int count = 1;
	while (select->step() == SQLiteStatement::STEP_RESULT_ROW) {
		count++;
	}
	CHECK(count == 4);
	CHECK(select->step() == SQLiteStatement::STEP_RESULT_DONE);
	CHECK(select->step() == SQLiteStatement::STEP_RESULT_DONE); // Stays done until reset().
	CHECK(select->reset() == OK);
	CHECK(select->step() == SQLiteStatement::STEP_RESULT_ROW);
	CHECK(select->get_column_int(0) == 0);

	// The last row was bound with bind_values(): an empty BLOB stays a BLOB.
	Ref<SQLiteStatement> last = db->prepare("SELECT * FROM t WHERE i = 10;");
	REQUIRE(last.is_valid());
	CHECK(last->step() == SQLiteStatement::STEP_RESULT_ROW);
	const Dictionary row = last->get_row();
	CHECK(row["s"] == Variant("values"));
	CHECK(double(row["f"]) == doctest::Approx(1.5));
	CHECK(last->get_column_type(3) == SQLiteStatement::COLUMN_TYPE_BLOB);
	CHECK(PackedByteArray(row["b"]).is_empty());

	// Statements stop working when their database is closed.
	db->close();
	ERR_PRINT_OFF;
	CHECK(select->step() == SQLiteStatement::STEP_RESULT_ERROR);
	CHECK(insert->bind_int(1, 1) == ERR_UNCONFIGURED);
	ERR_PRINT_ON;

	// close() finalizes the live statements, so the file is closed and can be
	// deleted (on Windows an open file can't be).
	CHECK(DirAccess::remove_absolute(db_path) == OK);
	CHECK_FALSE(FileAccess::exists(db_path));

	// Reopening does not revive old statements.
	REQUIRE(db->open(db_path) == OK);
	ERR_PRINT_OFF;
	CHECK(last->step() == SQLiteStatement::STEP_RESULT_ERROR);
	ERR_PRINT_ON;
	db->close();

	insert.unref();
	select.unref();
	last.unref();
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Schema: tables, columns and row counts") {
	const String db_path = _fresh_db_path("sqlite_schema.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();

	CHECK(db->get_table_names().is_empty());
	CHECK(db->get_last_error() == ERR_UNCONFIGURED);

	REQUIRE(db->open(db_path) == OK);
	CHECK(db->get_table_names().is_empty());
	CHECK(db->get_last_error() == OK);

	CHECK(db->execute(
				  "CREATE TABLE items (id INTEGER PRIMARY KEY, name VARCHAR(40) NOT NULL DEFAULT 'none', weight DOUBLE, data BLOB, price DECIMAL(10,2), note);"
				  "CREATE TABLE \"odd \"\"name\" (a TEXT, b INT, PRIMARY KEY (b, a));"
				  "CREATE VIEW heavy AS SELECT * FROM items WHERE weight > 1;"
				  "CREATE VIRTUAL TABLE notes USING fts5(body);"
				  "INSERT INTO items (name, weight) VALUES ('sword', 2.5), ('shield', 4), ('ring', 0.1);"
				  "INSERT INTO \"odd \"\"name\" VALUES ('x', 1);") == OK);

	// Tables and virtual tables, sorted; no views, no sqlite_* tables, no FTS5 shadow tables.
	const PackedStringArray tables = db->get_table_names();
	REQUIRE(tables.size() == 3);
	CHECK(tables[0] == "items");
	CHECK(tables[1] == "notes");
	CHECK(tables[2] == "odd \"name");

	CHECK(db->has_table("items"));
	CHECK(db->has_table("ITEMS"));
	CHECK(db->has_table("odd \"name"));
	CHECK_FALSE(db->has_table("heavy"));
	CHECK_FALSE(db->has_table("notes_data"));
	CHECK_FALSE(db->has_table("missing"));

	const TypedArray<SQLiteColumn> columns = db->get_columns("items");
	REQUIRE(columns.size() == 6);
	const Ref<SQLiteColumn> id = columns[0];
	CHECK(id->get_name() == "id");
	CHECK(id->get_declared_type() == "INTEGER");
	CHECK(id->get_affinity() == SQLiteColumn::AFFINITY_INTEGER);
	CHECK(id->get_primary_key_index() == 1);
	CHECK_FALSE(id->is_not_null());
	const Ref<SQLiteColumn> name = columns[1];
	CHECK(name->get_declared_type() == "VARCHAR(40)");
	CHECK(name->get_affinity() == SQLiteColumn::AFFINITY_TEXT);
	CHECK(name->is_not_null());
	CHECK(name->get_default_value() == "'none'");
	CHECK(name->get_primary_key_index() == 0);
	CHECK(Ref<SQLiteColumn>(columns[2])->get_affinity() == SQLiteColumn::AFFINITY_REAL);
	CHECK(Ref<SQLiteColumn>(columns[2])->get_default_value().is_empty());
	CHECK(Ref<SQLiteColumn>(columns[3])->get_affinity() == SQLiteColumn::AFFINITY_BLOB);
	CHECK(Ref<SQLiteColumn>(columns[4])->get_affinity() == SQLiteColumn::AFFINITY_NUMERIC);
	CHECK(Ref<SQLiteColumn>(columns[5])->get_declared_type().is_empty());
	CHECK(Ref<SQLiteColumn>(columns[5])->get_affinity() == SQLiteColumn::AFFINITY_BLOB);

	// Composite key: the position in the key, not in the table.
	const TypedArray<SQLiteColumn> odd_columns = db->get_columns("odd \"name");
	REQUIRE(odd_columns.size() == 2);
	CHECK(Ref<SQLiteColumn>(odd_columns[0])->get_primary_key_index() == 2);
	CHECK(Ref<SQLiteColumn>(odd_columns[1])->get_primary_key_index() == 1);

	const PackedStringArray names = db->get_column_names("items");
	REQUIRE(names.size() == 6);
	CHECK(names[0] == "id");
	CHECK(names[5] == "note");
	CHECK(db->get_column_count("items") == 6);
	CHECK(db->get_column_count("notes") == 1);

	CHECK(db->get_row_count("items") == 3);
	CHECK(db->get_row_count("Items") == 3);
	CHECK(db->get_row_count("odd \"name") == 1);
	CHECK(db->get_row_count("notes") == 0);

	// Unknown tables, and names that would break pasted SQL.
	CHECK(db->get_columns("missing").is_empty());
	CHECK(db->get_last_error() == ERR_DOES_NOT_EXIST);
	CHECK(db->get_last_error_message().contains("missing"));
	CHECK(db->get_column_names("missing").is_empty());
	CHECK(db->get_column_count("missing") == -1);
	CHECK(db->get_row_count("missing") == -1);
	CHECK(db->get_row_count("items\"; DROP TABLE items; --") == -1);
	CHECK(db->get_last_error() == ERR_DOES_NOT_EXIST);
	CHECK(db->has_table("items"));
	CHECK(db->get_row_count("heavy") == -1);

	db->close();
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Affinity of declared types") {
	CHECK(SQLiteColumn::get_affinity_for_type("BIGINT") == SQLiteColumn::AFFINITY_INTEGER);
	CHECK(SQLiteColumn::get_affinity_for_type("int") == SQLiteColumn::AFFINITY_INTEGER);
	CHECK(SQLiteColumn::get_affinity_for_type("NVARCHAR(100)") == SQLiteColumn::AFFINITY_TEXT);
	CHECK(SQLiteColumn::get_affinity_for_type("CLOB") == SQLiteColumn::AFFINITY_TEXT);
	CHECK(SQLiteColumn::get_affinity_for_type("") == SQLiteColumn::AFFINITY_BLOB);
	CHECK(SQLiteColumn::get_affinity_for_type("FLOAT") == SQLiteColumn::AFFINITY_REAL);
	CHECK(SQLiteColumn::get_affinity_for_type("DOUBLE PRECISION") == SQLiteColumn::AFFINITY_REAL);
	CHECK(SQLiteColumn::get_affinity_for_type("BOOLEAN") == SQLiteColumn::AFFINITY_NUMERIC);
	CHECK(SQLiteColumn::get_affinity_for_type("DATETIME") == SQLiteColumn::AFFINITY_NUMERIC);
	// The rules apply in order: "INT" comes before "CHAR".
	CHECK(SQLiteColumn::get_affinity_for_type("CHARINT") == SQLiteColumn::AFFINITY_INTEGER);
}

TEST_CASE("[SQLite] Database files as resources") {
	const String db_path = _fresh_db_path("sqlite_resource.db");
	{
		Ref<SQLiteDatabase> writer;
		writer.instantiate();
		REQUIRE(writer->open(db_path) == OK);
		CHECK(writer->execute("CREATE TABLE t (a INTEGER); INSERT INTO t VALUES (1), (2);") == OK);
	}

	CHECK(ResourceLoader::get_resource_type(db_path) == "SQLiteDatabase");
	CHECK(ResourceLoader::get_resource_type("user://x.sqlite3") == "SQLiteDatabase");

	Ref<SQLiteDatabase> db = ResourceLoader::load(db_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(db.is_valid());
	CHECK_FALSE(db->is_open());
	// Without a path, open() opens the file the resource was loaded from.
	REQUIRE(db->open() == OK);
	CHECK(db->get_database_path() == db_path);
	CHECK(db->get_row_count("t") == 2);
	db->close();

	// Created with new(): no file to fall back to.
	Ref<SQLiteDatabase> plain;
	plain.instantiate();
	CHECK(plain->open() == ERR_INVALID_PARAMETER);
	CHECK_FALSE(plain->is_open());

	db.unref();
	DirAccess::remove_absolute(db_path);
}

#ifdef TOOLS_ENABLED
TEST_CASE("[SQLite] Database dock saves pending edits in one transaction") {
	const String db_path = _fresh_db_path("sqlite_table_editor.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();
	REQUIRE(db->open(db_path) == OK);
	REQUIRE(db->execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, price REAL DEFAULT 1.5);"
						"INSERT INTO items VALUES (1, 'a', 2.0), (2, 'b', 3.0), (3, 'c', 4.0);") == OK);
	const Vector<String> id_key = { "id" };
	Variant failed;
	String message;

	// Update, delete and insert; the new row gets the column default.
	Dictionary changed_values;
	changed_values["name"] = "apple";
	changed_values["price"] = 9.5;
	Dictionary changed_rows;
	changed_rows[Array{ 1 }] = changed_values;
	Dictionary deleted_rows;
	deleted_rows[Array{ 2 }] = true;
	Dictionary new_row;
	new_row["name"] = "d";
	CHECK(DatabaseEditor::apply_changes(db, "items", id_key, Array{ changed_rows, deleted_rows, Array{ new_row } }, failed, message) == OK);
	CHECK(failed.get_type() == Variant::NIL);
	TypedArray<Dictionary> rows = db->query("SELECT id, name, price FROM items ORDER BY id;");
	REQUIRE(rows.size() == 3);
	CHECK(Dictionary(rows[0])["name"] == Variant("apple"));
	CHECK(double(Dictionary(rows[0])["price"]) == doctest::Approx(9.5));
	CHECK(int64_t(Dictionary(rows[1])["id"]) == 3);
	CHECK(Dictionary(rows[2])["name"] == Variant("d"));
	CHECK(double(Dictionary(rows[2])["price"]) == doctest::Approx(1.5));

	// A failing row rolls back the whole save and is reported.
	Dictionary rename;
	rename["name"] = "changed";
	Dictionary rename_rows;
	rename_rows[Array{ 3 }] = rename;
	Dictionary null_row;
	null_row["name"] = Variant();
	CHECK(DatabaseEditor::apply_changes(db, "items", id_key, Array{ rename_rows, Dictionary(), Array{ null_row } }, failed, message) != OK);
	CHECK(failed == Variant(0));
	CHECK(message.contains("NOT NULL"));
	CHECK_FALSE(db->is_in_transaction());
	rows = db->query("SELECT name FROM items WHERE id = 3;");
	CHECK(Dictionary(rows[0])["name"] == Variant("c"));

	// Without a primary key, the rowid is the key.
	REQUIRE(db->execute("CREATE TABLE notes (text TEXT); INSERT INTO notes VALUES ('x'), ('y');") == OK);
	const Vector<String> rowid_key = { "rowid" };
	Dictionary deleted_note;
	deleted_note[Array{ 2 }] = true;
	CHECK(DatabaseEditor::apply_changes(db, "notes", rowid_key, Array{ Dictionary(), deleted_note, Array() }, failed, message) == OK);
	CHECK(db->get_row_count("notes") == 1);

	// WITHOUT ROWID tables use their primary key; a duplicate key fails.
	REQUIRE(db->execute("CREATE TABLE pairs (a TEXT, b INTEGER, value TEXT, PRIMARY KEY (a, b)) WITHOUT ROWID;"
						"INSERT INTO pairs VALUES ('k', 1, 'old');") == OK);
	const Vector<String> pair_key = { "a", "b" };
	Dictionary new_value;
	new_value["value"] = "new";
	Dictionary pair_rows;
	pair_rows[Array{ "k", 1 }] = new_value;
	CHECK(DatabaseEditor::apply_changes(db, "pairs", pair_key, Array{ pair_rows, Dictionary(), Array() }, failed, message) == OK);
	rows = db->query("SELECT value FROM pairs;");
	CHECK(Dictionary(rows[0])["value"] == Variant("new"));
	Dictionary duplicate;
	duplicate["a"] = "k";
	duplicate["b"] = 1;
	CHECK(DatabaseEditor::apply_changes(db, "pairs", pair_key, Array{ Dictionary(), Dictionary(), Array{ duplicate } }, failed, message) != OK);
	CHECK(message.contains("UNIQUE"));

	// Rowid tables allow NULL in a primary key; the key still matches.
	REQUIRE(db->execute("CREATE TABLE tags (name TEXT PRIMARY KEY, n INTEGER); INSERT INTO tags VALUES (NULL, 1);") == OK);
	const Vector<String> name_key = { "name" };
	Dictionary count_value;
	count_value["n"] = 2;
	Dictionary tag_rows;
	tag_rows[Array{ Variant() }] = count_value;
	CHECK(DatabaseEditor::apply_changes(db, "tags", name_key, Array{ tag_rows, Dictionary(), Array() }, failed, message) == OK);
	rows = db->query("SELECT n FROM tags;");
	CHECK(int64_t(Dictionary(rows[0])["n"]) == 2);

	db->close();
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Database dock: keys, views and a save of several tables") {
	const String db_path = _fresh_db_path("sqlite_database_dock.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();
	REQUIRE(db->open(db_path) == OK);
	REQUIRE(db->execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL);"
						"CREATE TABLE notes (text TEXT);"
						"CREATE TABLE pairs (b INTEGER, a TEXT, PRIMARY KEY (a, b)) WITHOUT ROWID;"
						"CREATE VIEW item_names AS SELECT name FROM items;"
						"INSERT INTO items VALUES (1, 'a');") == OK);

	// Primary key columns in key order; "rowid" without a primary key.
	CHECK(DatabaseEditor::get_key_columns(db, "items") == Vector<String>{ "id" });
	CHECK(DatabaseEditor::get_key_columns(db, "notes") == Vector<String>{ "rowid" });
	const Vector<String> pair_key = { "a", "b" };
	CHECK(DatabaseEditor::get_key_columns(db, "pairs") == pair_key);

	// A column named "rowid" hides the rowid: the next alias is used.
	REQUIRE(db->execute("CREATE TABLE odd (rowid TEXT, oid TEXT);") == OK);
	CHECK(DatabaseEditor::get_key_columns(db, "odd") == Vector<String>{ "_rowid_" });

	// Quoted names: a table and a column with a double quote and a space.
	REQUIRE(db->execute("CREATE TABLE \"my \"\"items\" (\"the \"\"id\" INTEGER PRIMARY KEY, v TEXT); INSERT INTO \"my \"\"items\" VALUES (7, 'old');") == OK);
	CHECK(DatabaseEditor::get_key_columns(db, "my \"items") == Vector<String>{ "the \"id" });
	Dictionary quoted_value;
	quoted_value["v"] = "new";
	Dictionary quoted_rows;
	quoted_rows[Array{ 7 }] = quoted_value;
	Variant quoted_failed;
	String quoted_message;
	CHECK(DatabaseEditor::apply_changes(db, "my \"items", Vector<String>{ "the \"id" }, Array{ quoted_rows, Dictionary(), Array() }, quoted_failed, quoted_message) == OK);
	CHECK(Dictionary(db->query("SELECT v FROM \"my \"\"items\";")[0])["v"] == Variant("new"));

	// Views are listed apart from the tables.
	CHECK(DatabaseEditor::get_view_names(db) == PackedStringArray{ "item_names" });
	CHECK_FALSE(db->get_table_names().has("item_names"));

	// Inside a transaction, apply_changes() leaves commit and rollback to the caller (Save of several tables).
	Variant failed;
	String message;
	Dictionary note;
	note["text"] = "kept until the rollback";
	Dictionary bad_item;
	bad_item["name"] = Variant(); // NOT NULL fails.
	REQUIRE(db->begin_transaction() == OK);
	CHECK(DatabaseEditor::apply_changes(db, "notes", Vector<String>{ "rowid" }, Array{ Dictionary(), Dictionary(), Array{ note } }, failed, message) == OK);
	CHECK(db->is_in_transaction());
	CHECK(DatabaseEditor::apply_changes(db, "items", Vector<String>{ "id" }, Array{ Dictionary(), Dictionary(), Array{ bad_item } }, failed, message) != OK);
	CHECK(failed == Variant(0));
	CHECK(message.contains("NOT NULL"));
	CHECK(db->is_in_transaction());
	REQUIRE(db->rollback() == OK);
	CHECK(db->get_row_count("notes") == 0);
	CHECK(db->get_row_count("items") == 1);

	db->close();
	DirAccess::remove_absolute(db_path);
}

TEST_CASE("[SQLite] Database dock: Edit Table and Add Column") {
	const String db_path = _fresh_db_path("sqlite_database_dock_columns.db");
	Ref<SQLiteDatabase> db;
	db.instantiate();
	REQUIRE(db->open(db_path) == OK);
	REQUIRE(db->execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, price REAL DEFAULT 1.5);"
						"CREATE INDEX items_price ON items (price);"
						"INSERT INTO items VALUES (1, 'a', 2.0), (2, 'b', 3.0);") == OK);
	String message;

	// Rename and a new column at the end: ALTER TABLE keeps the UNIQUE clause.
	LocalVector<DatabaseTableDialog::ColumnSpec> columns = DatabaseTableDialog::read_columns(db, "items");
	REQUIRE(columns.size() == 3);
	CHECK(columns[0].primary_key);
	CHECK(columns[1].not_null);
	CHECK(columns[2].default_value == "1.5");
	columns[1].name = "title";
	DatabaseTableDialog::ColumnSpec weight;
	weight.name = "weight";
	weight.declared_type = "REAL";
	weight.default_value = "0";
	columns.push_back(weight);
	CHECK(DatabaseTableDialog::apply_columns(db, "items", columns, message) == OK);
	const PackedStringArray renamed_names = { "id", "title", "price", "weight" };
	CHECK(db->get_column_names("items") == renamed_names);
	CHECK(db->execute("INSERT INTO items (id, title) VALUES (3, 'a');") != OK); // Still UNIQUE.
	CHECK(double(Dictionary(db->query("SELECT weight FROM items WHERE id = 1;")[0])["weight"]) == doctest::Approx(0.0));

	// A type change and a new order: the table is copied with its rows and indexes.
	columns = DatabaseTableDialog::read_columns(db, "items");
	columns[2].declared_type = "NUMERIC";
	const DatabaseTableDialog::ColumnSpec moved = columns[3];
	columns.remove_at(3);
	columns.insert(1, moved);
	CHECK(DatabaseTableDialog::apply_columns(db, "items", columns, message) == OK);
	const PackedStringArray copied_names = { "id", "weight", "title", "price" };
	CHECK(db->get_column_names("items") == copied_names);
	CHECK(db->get_row_count("items") == 2);
	CHECK(Dictionary(db->query("SELECT title FROM items WHERE id = 2;")[0])["title"] == Variant("b"));
	CHECK(db->query("SELECT name FROM sqlite_schema WHERE name = 'items_price';").size() == 1);
	CHECK_FALSE(db->has_table("items_dock_copy"));

	// A failing change writes nothing: NOT NULL without a default on a table with rows.
	columns = DatabaseTableDialog::read_columns(db, "items");
	DatabaseTableDialog::ColumnSpec required;
	required.name = "required";
	required.not_null = true;
	columns.insert(0, required);
	CHECK(DatabaseTableDialog::apply_columns(db, "items", columns, message) != OK);
	CHECK(message.contains("NOT NULL"));
	CHECK_FALSE(db->is_in_transaction());
	CHECK(db->get_column_names("items") == copied_names);

	// Duplicate names are refused before anything runs.
	columns = DatabaseTableDialog::read_columns(db, "items");
	columns[1].name = "ID";
	CHECK(DatabaseTableDialog::apply_columns(db, "items", columns, message) == ERR_INVALID_PARAMETER);

	db->close();
	DirAccess::remove_absolute(db_path);
}
#endif // TOOLS_ENABLED

} // namespace TestSQLite
