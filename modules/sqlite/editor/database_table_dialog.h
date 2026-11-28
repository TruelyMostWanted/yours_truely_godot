/**************************************************************************/
/*  database_table_dialog.h                                               */
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

#include "core/templates/local_vector.h"
#include "scene/gui/dialogs.h"

class Label;
class Tree;
class TreeItem;

// Edits the columns of one table (#437): name, declared type, NOT NULL, default and primary key, in order.
// Renames, removed columns and columns added at the end use ALTER TABLE, which keeps every other part of the
// table definition; any other change rebuilds the table with SQLite's copy procedure in one transaction.
class DatabaseTableDialog : public ConfirmationDialog {
	GDCLASS(DatabaseTableDialog, ConfirmationDialog);

public:
	struct ColumnSpec {
		String name;
		String source; // The column it was before the edit; empty for a new column.
		String declared_type;
		bool not_null = false;
		String default_value; // An SQL expression, as SQLite reports it (`'none'`, `1.5`, `CURRENT_TIMESTAMP`); empty for none.
		bool primary_key = false;
	};

private:
	enum ColumnField {
		COLUMN_FIELD_NAME,
		COLUMN_FIELD_TYPE,
		COLUMN_FIELD_NOT_NULL,
		COLUMN_FIELD_DEFAULT,
		COLUMN_FIELD_PRIMARY_KEY,
		COLUMN_FIELD_MAX,
	};

	String path; // Globalized database path.
	String table;
	Callable applied_callback; // Called with the status message after the change is written.

	Tree *tree = nullptr;
	Label *status_label = nullptr;

	TreeItem *_add_item(const ColumnSpec &p_spec);
	void _add_column();
	void _remove_column();
	void _move_column(int p_delta);
	void _apply();

public:
	// The columns of a table, each with itself as source.
	static LocalVector<ColumnSpec> read_columns(const Ref<SQLiteDatabase> &p_database, const String &p_table);
	// Changes the table to p_columns in one transaction; on an error nothing changes and r_message says why.
	static Error apply_columns(const Ref<SQLiteDatabase> &p_database, const String &p_table, const LocalVector<ColumnSpec> &p_columns, String &r_message);

	// Shows the columns of p_table; with p_add_column a new column is added at the end and its name edited.
	void edit_table(const String &p_path, const String &p_table, bool p_add_column, const Callable &p_applied);

	DatabaseTableDialog();
};
