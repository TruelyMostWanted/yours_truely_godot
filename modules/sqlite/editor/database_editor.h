/**************************************************************************/
/*  database_editor.h                                                     */
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

#include "../sqlite_column.h"
#include "../sqlite_database.h"

#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "editor/docks/editor_dock.h"

class AcceptDialog;
class Button;
class ConfirmationDialog;
class Label;
class LineEdit;
class MenuButton;
class TextEdit;
class Tree;
class TreeItem;
class UndoRedo;

// Edits the tables of SQLite database files in a bottom dock (#437): a side bar of the open databases with their
// tables and views, the rows of the selected table one page at a time, and the schema actions of the Table menu.
// Row edits stay pending (per table, with an undo history per database) until Save writes them in one transaction.
class DatabaseEditor : public EditorDock {
	GDCLASS(DatabaseEditor, EditorDock);

	static inline DatabaseEditor *singleton = nullptr;

	enum TableMenuItem {
		TABLE_MENU_CREATE,
		TABLE_MENU_RENAME,
		TABLE_MENU_DELETE,
		TABLE_MENU_REFRESH,
	};

	enum ValuesMenuItem {
		VALUES_MENU_INSERT_ROW,
		VALUES_MENU_DELETE_ROWS,
		VALUES_MENU_SET_NULL,
		VALUES_MENU_UNDO,
		VALUES_MENU_REDO,
	};

	// One open database file.
	struct OpenDatabase {
		String path; // The path as opened (res://, user:// or absolute), shown in the side bar.
		Dictionary pending; // Table name -> [updates, deletes, inserts] in the format of apply_changes().
		UndoRedo *undo_redo = nullptr; // The history of the pending edits.
	};

	struct Column {
		String name;
		String declared_type;
		SQLiteColumn::Affinity affinity = SQLiteColumn::AFFINITY_BLOB;
	};

	HashMap<String, OpenDatabase> databases; // Keyed by the globalized path: the module's file system treats res:// as read-only.
	String database; // Globalized path of the selected database, or empty.
	String table; // Selected table or view, or empty.
	bool view = false; // The selected table is a view: rows are read-only.
	LocalVector<Column> columns;
	Vector<String> key_columns; // The primary key columns, or "rowid"; empty for views.
	Vector<Array> rows; // The current page: key values, then column values.
	Vector<Variant> row_ids; // Per tree item: the key Array, or the index in inserts.
	int page = 0;
	int page_count = 1;
	int sort_column = -1;
	bool sort_descending = false;
	Variant failed_row; // The key Array or insert index of the row that failed to save.
	bool updating_side_bar = false;
	String closing_database; // Asked about in close_prompt.

	MenuButton *table_menu = nullptr;
	MenuButton *values_menu = nullptr;
	Button *query_button = nullptr;
	LineEdit *filter_edit = nullptr;
	Button *revert_button = nullptr;
	Button *save_button = nullptr;
	Tree *side_bar = nullptr;
	Tree *tree = nullptr;
	Label *empty_label = nullptr;
	Label *status_label = nullptr;
	Button *previous_button = nullptr;
	Label *page_label = nullptr;
	Button *next_button = nullptr;

	ConfirmationDialog *close_prompt = nullptr; // Save, Discard or Cancel when closing a database with pending edits.
	ConfirmationDialog *sql_dialog = nullptr;
	TextEdit *sql_edit = nullptr;
	Label *sql_status = nullptr;
	ConfirmationDialog *rename_dialog = nullptr;
	LineEdit *rename_edit = nullptr;
	ConfirmationDialog *delete_dialog = nullptr;
	AcceptDialog *query_dialog = nullptr;
	TextEdit *query_edit = nullptr;
	Tree *query_tree = nullptr;
	Label *query_status = nullptr;

	Ref<SQLiteDatabase> _open(const String &p_database, SQLiteDatabase::OpenMode p_mode, Label *p_status);
	OpenDatabase *_get_selected();
	Array _get_changes() const;
	bool _has_pending(const String &p_table) const;
	void _update_side_bar();
	void _update_actions();
	void _select_table(const String &p_database, const String &p_table);
	void _load_page();
	void _render();
	void _set_pending(const String &p_database, const Dictionary &p_pending, const String &p_table);
	void _commit(const String &p_action, const Array &p_changes);
	void _set_cell(const Variant &p_row, int p_column, const Variant &p_value);
	Error _save_database(const String &p_database);
	void _close_database(const String &p_database);
	void _report_written(const String &p_database, const String &p_message);

	void _side_bar_selected();
	void _side_bar_button_clicked(TreeItem *p_item, int p_column, int p_id, MouseButton p_button);
	void _cell_edited();
	void _table_menu_pressed(int p_id);
	void _values_menu_pressed(int p_id);
	void _insert_row();
	void _delete_rows();
	void _set_null();
	void _undo_redo(bool p_redo);
	void _save();
	void _revert();
	void _close_saving();
	void _close_discarding();
	void _create_confirmed();
	void _rename_confirmed();
	void _delete_confirmed();
	void _open_query();
	void _run_query();
	void _filter_changed(const String &p_text);
	void _change_page(int p_delta);
	void _sort(int p_column, int p_mouse_button);
	void _tree_gui_input(const Ref<InputEvent> &p_event);
	void _file_removed(const String &p_file);
	void _files_moved(const String &p_old_file, const String &p_new_file);

protected:
	void _notification(int p_what);

public:
	static DatabaseEditor *get_singleton() { return singleton; }

	// Writes pending edits of one table. p_changes is [updates, deletes, inserts]:
	// - updates: key Array -> Dictionary of column name -> value,
	// - deletes: key Array -> true,
	// - inserts: Array of Dictionary of column name -> value (missing columns get their default).
	// A key Array holds the values of p_key_columns. Outside a transaction, the edits are written in one transaction
	// and rolled back on an error; inside one, the caller commits or rolls back. On an error, r_failed_row is the key
	// or insert index of the failing row, and r_message is SQLite's message.
	static Error apply_changes(const Ref<SQLiteDatabase> &p_database, const String &p_table, const Vector<String> &p_key_columns, const Array &p_changes, Variant &r_failed_row, String &r_message);
	// The primary key columns of a table in key order, or "rowid" for a table without a primary key.
	static Vector<String> get_key_columns(const Ref<SQLiteDatabase> &p_database, const String &p_table);
	// The views of the main database, sorted by name.
	static PackedStringArray get_view_names(const Ref<SQLiteDatabase> &p_database);

	// Opens a database file in the side bar and selects p_table, or the database when p_table is empty.
	void edit(const String &p_path, const String &p_table = String());
	// Database paths (as opened) with pending edits.
	PackedStringArray get_unsaved_paths() const;
	// Saves the pending edits of every open database.
	void save_all();

	DatabaseEditor();
	~DatabaseEditor();
};
