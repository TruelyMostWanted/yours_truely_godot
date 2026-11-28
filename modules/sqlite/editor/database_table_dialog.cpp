/**************************************************************************/
/*  database_table_dialog.cpp                                             */
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

#include "database_table_dialog.h"

#include "core/object/callable_mp.h"
#include "core/templates/hash_set.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/tree.h"
#include "scene/scene_string_names.h"

static String _database_table_dialog_quote(const String &p_identifier) {
	return "\"" + p_identifier.replace("\"", "\"\"") + "\"";
}

// ADD COLUMN takes literals only; anything else goes in parentheses, which CREATE TABLE accepts.
static String _database_table_dialog_default(const String &p_default) {
	const String upper = p_default.to_upper();
	const bool literal = p_default.begins_with("'") || p_default.begins_with("(") || upper.begins_with("X'") || p_default.is_valid_float() || upper == "NULL" || upper == "TRUE" || upper == "FALSE" || upper.begins_with("CURRENT_");
	return literal ? p_default : "(" + p_default + ")";
}

static String _database_table_dialog_definition(const DatabaseTableDialog::ColumnSpec &p_column) {
	String sql = _database_table_dialog_quote(p_column.name);
	if (!p_column.declared_type.is_empty()) {
		sql += " " + p_column.declared_type;
	}
	if (p_column.not_null) {
		sql += " NOT NULL";
	}
	if (!p_column.default_value.is_empty()) {
		sql += " DEFAULT " + _database_table_dialog_default(p_column.default_value);
	}
	return sql;
}

LocalVector<DatabaseTableDialog::ColumnSpec> DatabaseTableDialog::read_columns(const Ref<SQLiteDatabase> &p_database, const String &p_table) {
	LocalVector<ColumnSpec> result;
	ERR_FAIL_COND_V(p_database.is_null(), result);
	const TypedArray<Dictionary> info = p_database->query("SELECT name, type, \"notnull\", dflt_value, pk FROM pragma_table_info(?) ORDER BY cid;", varray(p_table));
	for (int i = 0; i < info.size(); i++) {
		const Dictionary row = info[i];
		ColumnSpec spec;
		spec.name = String(row["name"]);
		spec.source = spec.name;
		spec.declared_type = String(row["type"]);
		spec.not_null = int64_t(row["notnull"]) != 0;
		spec.default_value = row["dflt_value"].get_type() == Variant::NIL ? String() : String(row["dflt_value"]);
		spec.primary_key = int64_t(row["pk"]) > 0;
		result.push_back(spec);
	}
	return result;
}

Error DatabaseTableDialog::apply_columns(const Ref<SQLiteDatabase> &p_database, const String &p_table, const LocalVector<ColumnSpec> &p_columns, String &r_message) {
	ERR_FAIL_COND_V(p_database.is_null() || p_database->is_in_transaction(), ERR_INVALID_PARAMETER);
	r_message = String();
	if (p_columns.is_empty()) {
		r_message = TTR("A table needs at least one column.");
		return ERR_INVALID_PARAMETER;
	}
	HashSet<String> names;
	for (const ColumnSpec &column : p_columns) {
		const String lower = column.name.to_lower();
		if (column.name.is_empty()) {
			r_message = TTR("Every column needs a name.");
			return ERR_INVALID_PARAMETER;
		}
		if (names.has(lower)) {
			r_message = vformat(TTR("Two columns are named \"%s\"."), column.name);
			return ERR_INVALID_PARAMETER;
		}
		names.insert(lower);
	}
	const LocalVector<ColumnSpec> original = read_columns(p_database, p_table);
	if (original.is_empty()) {
		r_message = vformat(TTR("No table \"%s\" in the database."), p_table);
		return ERR_DOES_NOT_EXIST;
	}

	// ALTER TABLE covers renames, removed columns and plain columns added at the end; it keeps the rest of the table.
	bool alter = true;
	bool seen_new = false;
	int64_t last_original = -1;
	HashSet<String> kept;
	for (const ColumnSpec &column : p_columns) {
		if (column.source.is_empty()) {
			seen_new = true;
			alter = alter && !column.primary_key && !(column.not_null && column.default_value.is_empty());
			continue;
		}
		int64_t index = -1;
		for (uint32_t i = 0; i < original.size(); i++) {
			if (original[i].name == column.source) {
				index = i;
			}
		}
		if (index < 0) {
			r_message = vformat(TTR("No column \"%s\" in the table."), column.source);
			return ERR_DOES_NOT_EXIST;
		}
		const ColumnSpec &before = original[uint32_t(index)];
		alter = alter && !seen_new && index > last_original && before.declared_type == column.declared_type && before.not_null == column.not_null && before.default_value == column.default_value && before.primary_key == column.primary_key;
		last_original = index;
		kept.insert(column.source);
	}
	bool has_key = false;
	for (const ColumnSpec &column : original) {
		has_key = has_key || column.primary_key;
		alter = alter && (kept.has(column.name) || !column.primary_key);
	}

	if (alter) {
		Error err = p_database->begin_transaction();
		for (const ColumnSpec &column : original) {
			if (err == OK && !kept.has(column.name)) {
				err = p_database->execute("ALTER TABLE " + _database_table_dialog_quote(p_table) + " DROP COLUMN " + _database_table_dialog_quote(column.name) + ";");
			}
		}
		for (const ColumnSpec &column : p_columns) {
			if (err != OK) {
				break;
			}
			if (column.source.is_empty()) {
				err = p_database->execute("ALTER TABLE " + _database_table_dialog_quote(p_table) + " ADD COLUMN " + _database_table_dialog_definition(column) + ";");
			} else if (column.source != column.name) {
				err = p_database->execute("ALTER TABLE " + _database_table_dialog_quote(p_table) + " RENAME COLUMN " + _database_table_dialog_quote(column.source) + " TO " + _database_table_dialog_quote(column.name) + ";");
			}
		}
		if (err == OK) {
			err = p_database->commit();
		}
		if (err == OK) {
			return OK;
		}
		// For example a removed column that an index uses: the copy below handles it or reports why not.
		p_database->rollback();
	}

	// The copy procedure of https://sqlite.org/lang_altertable.html#otheralter, in one transaction.
	const TypedArray<Dictionary> foreign_keys_state = p_database->query("PRAGMA foreign_keys;");
	const bool foreign_keys = !foreign_keys_state.is_empty() && int64_t(Dictionary(foreign_keys_state[0])["foreign_keys"]) != 0;
	p_database->execute("PRAGMA foreign_keys = OFF;"); // Has no effect inside a transaction.
	const TypedArray<Dictionary> schema = p_database->query("SELECT sql FROM sqlite_schema WHERE tbl_name = ? AND type IN ('index', 'trigger') AND sql IS NOT NULL;", varray(p_table));
	const String table_sql = _database_table_dialog_quote(p_table);
	const String copy_sql = _database_table_dialog_quote(p_table + "_dock_copy");
	PackedStringArray definitions;
	PackedStringArray keys;
	PackedStringArray targets;
	PackedStringArray sources;
	for (const ColumnSpec &column : p_columns) {
		definitions.push_back(_database_table_dialog_definition(column));
		if (column.primary_key) {
			keys.push_back(_database_table_dialog_quote(column.name));
		}
		if (!column.source.is_empty()) {
			targets.push_back(_database_table_dialog_quote(column.name));
			sources.push_back(_database_table_dialog_quote(column.source));
		}
	}
	if (!keys.is_empty()) {
		definitions.push_back("PRIMARY KEY (" + String(", ").join(keys) + ")");
	}
	if (!has_key && keys.is_empty() && !names.has("rowid")) {
		// Keeps the row ids of a table without a primary key.
		targets.push_back("rowid");
		sources.push_back("rowid");
	}

	Error err = p_database->begin_transaction();
	if (err == OK) {
		// Views that use the table are not checked while it is replaced.
		err = p_database->execute("PRAGMA legacy_alter_table = ON;");
	}
	if (err == OK) {
		err = p_database->execute("CREATE TABLE " + copy_sql + " (" + String(", ").join(definitions) + ");");
	}
	if (err == OK && !targets.is_empty()) {
		err = p_database->execute("INSERT INTO " + copy_sql + " (" + String(", ").join(targets) + ") SELECT " + String(", ").join(sources) + " FROM " + table_sql + ";");
	}
	if (err == OK) {
		err = p_database->execute("DROP TABLE " + table_sql + ";");
	}
	if (err == OK) {
		err = p_database->execute("ALTER TABLE " + copy_sql + " RENAME TO " + table_sql + ";");
	}
	for (int i = 0; err == OK && i < schema.size(); i++) {
		// Indexes and triggers of the table; one on a removed column fails and rolls everything back.
		err = p_database->execute(String(Dictionary(schema[i])["sql"]) + ";");
	}
	if (err == OK && !p_database->query("PRAGMA foreign_key_check;").is_empty()) {
		err = ERR_INVALID_DATA;
		r_message = TTR("The change breaks foreign keys of other tables.");
	}
	if (err == OK) {
		err = p_database->commit();
	}
	if (err != OK) {
		if (r_message.is_empty()) {
			r_message = p_database->get_last_error_message();
		}
		if (p_database->is_in_transaction()) {
			p_database->rollback();
		}
	}
	p_database->execute("PRAGMA legacy_alter_table = OFF;");
	if (foreign_keys) {
		p_database->execute("PRAGMA foreign_keys = ON;");
	}
	return err;
}

TreeItem *DatabaseTableDialog::_add_item(const ColumnSpec &p_spec) {
	TreeItem *item = tree->create_item(tree->get_root());
	item->set_text(COLUMN_FIELD_NAME, p_spec.name);
	item->set_metadata(COLUMN_FIELD_NAME, p_spec.source);
	item->set_text(COLUMN_FIELD_TYPE, p_spec.declared_type);
	item->set_text(COLUMN_FIELD_DEFAULT, p_spec.default_value);
	for (const ColumnField field : { COLUMN_FIELD_NOT_NULL, COLUMN_FIELD_PRIMARY_KEY }) {
		item->set_cell_mode(field, TreeItem::CELL_MODE_CHECK);
	}
	item->set_checked(COLUMN_FIELD_NOT_NULL, p_spec.not_null);
	item->set_checked(COLUMN_FIELD_PRIMARY_KEY, p_spec.primary_key);
	for (int field = 0; field < COLUMN_FIELD_MAX; field++) {
		item->set_editable(field, true);
	}
	item->set_tooltip_text(COLUMN_FIELD_NAME, p_spec.source.is_empty() ? TTR("New column.") : vformat(TTR("Was \"%s\"."), p_spec.source));
	return item;
}

void DatabaseTableDialog::_add_column() {
	ColumnSpec spec;
	spec.name = "new_column";
	spec.declared_type = "TEXT";
	TreeItem *item = _add_item(spec);
	tree->set_selected(item, COLUMN_FIELD_NAME);
	tree->scroll_to_item(item);
	callable_mp(tree, &Tree::edit_selected).call_deferred(true);
}

void DatabaseTableDialog::_remove_column() {
	TreeItem *item = tree->get_selected();
	if (item) {
		tree->get_root()->remove_child(item);
		memdelete(item);
	}
}

void DatabaseTableDialog::_move_column(int p_delta) {
	TreeItem *item = tree->get_selected();
	if (item == nullptr) {
		return;
	}
	TreeItem *neighbor = p_delta < 0 ? item->get_prev() : item->get_next();
	if (neighbor == nullptr) {
		return;
	}
	if (p_delta < 0) {
		item->move_before(neighbor);
	} else {
		item->move_after(neighbor);
	}
	tree->ensure_cursor_is_visible();
}

void DatabaseTableDialog::_apply() {
	LocalVector<ColumnSpec> specs;
	for (TreeItem *item = tree->get_root()->get_first_child(); item; item = item->get_next()) {
		ColumnSpec spec;
		spec.name = item->get_text(COLUMN_FIELD_NAME).strip_edges();
		spec.source = item->get_metadata(COLUMN_FIELD_NAME);
		spec.declared_type = item->get_text(COLUMN_FIELD_TYPE).strip_edges();
		spec.not_null = item->is_checked(COLUMN_FIELD_NOT_NULL);
		spec.default_value = item->get_text(COLUMN_FIELD_DEFAULT).strip_edges();
		spec.primary_key = item->is_checked(COLUMN_FIELD_PRIMARY_KEY);
		specs.push_back(spec);
	}
	Ref<SQLiteDatabase> connection;
	connection.instantiate();
	String message;
	Error err = connection->open(path, SQLiteDatabase::OPEN_MODE_READ_WRITE);
	if (err != OK) {
		message = connection->get_last_error_message();
	} else {
		err = apply_columns(connection, table, specs, message);
		connection->close();
	}
	if (err != OK) {
		status_label->set_text(vformat(TTR("Not changed: %s"), message));
		return; // The dialog stays open, so the columns can be fixed.
	}
	hide();
	applied_callback.call(vformat(TTR("Changed the columns of \"%s\"."), table));
}

void DatabaseTableDialog::edit_table(const String &p_path, const String &p_table, bool p_add_column, const Callable &p_applied) {
	path = p_path;
	table = p_table;
	applied_callback = p_applied;
	set_title(vformat(p_add_column ? TTR("Add Column: %s") : TTR("Edit Table: %s"), table));
	status_label->set_text(TTR("Defaults are SQL: 'text', 1.5, NULL, CURRENT_TIMESTAMP. Renames, removed columns and new columns at the end keep the rest of the table definition. Other changes copy the table in one transaction: its UNIQUE, CHECK, FOREIGN KEY and COLLATE clauses are then not kept."));
	tree->clear();
	tree->create_item();
	Ref<SQLiteDatabase> connection;
	connection.instantiate();
	if (connection->open(path, SQLiteDatabase::OPEN_MODE_READ_ONLY) == OK) {
		for (const ColumnSpec &spec : read_columns(connection, table)) {
			_add_item(spec);
		}
		connection->close();
	} else {
		status_label->set_text(connection->get_last_error_message());
	}
	popup_centered_clamped(Size2(760, 420) * EDSCALE, 0.8);
	if (p_add_column) {
		_add_column();
	}
}

DatabaseTableDialog::DatabaseTableDialog() {
	set_hide_on_ok(false); // Stays open when SQLite rejects the change.
	set_ok_button_text(TTR("Apply"));

	VBoxContainer *main_vbox = memnew(VBoxContainer);
	add_child(main_vbox);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	main_vbox->add_child(toolbar);
	const auto add_tool_button = [toolbar](const String &p_text, const Callable &p_callable) {
		Button *button = memnew(Button(p_text));
		button->connect(SceneStringName(pressed), p_callable);
		toolbar->add_child(button);
	};
	add_tool_button(TTR("Add Column"), callable_mp(this, &DatabaseTableDialog::_add_column));
	add_tool_button(TTR("Remove Column"), callable_mp(this, &DatabaseTableDialog::_remove_column));
	add_tool_button(TTR("Move Up"), callable_mp(this, &DatabaseTableDialog::_move_column).bind(-1));
	add_tool_button(TTR("Move Down"), callable_mp(this, &DatabaseTableDialog::_move_column).bind(1));

	tree = memnew(Tree);
	tree->set_columns(COLUMN_FIELD_MAX);
	tree->set_hide_root(true);
	tree->set_column_titles_visible(true);
	tree->set_column_title(COLUMN_FIELD_NAME, TTR("Name"));
	tree->set_column_title(COLUMN_FIELD_TYPE, TTR("Type"));
	tree->set_column_title_tooltip_text(COLUMN_FIELD_TYPE, TTR("The declared type, for example INTEGER, REAL, TEXT, BLOB or NUMERIC; it gives the column its affinity."));
	tree->set_column_title(COLUMN_FIELD_NOT_NULL, TTR("Not Null"));
	tree->set_column_title(COLUMN_FIELD_DEFAULT, TTR("Default"));
	tree->set_column_title(COLUMN_FIELD_PRIMARY_KEY, TTR("Primary Key"));
	for (const ColumnField field : { COLUMN_FIELD_NOT_NULL, COLUMN_FIELD_PRIMARY_KEY }) {
		tree->set_column_expand(field, false);
		tree->set_column_custom_minimum_width(field, 90 * EDSCALE);
	}
	tree->set_custom_minimum_size(Size2(0, 200) * EDSCALE);
	tree->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_vbox->add_child(tree);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_custom_minimum_size(Size2(400, 0) * EDSCALE);
	main_vbox->add_child(status_label);

	connect(SceneStringName(confirmed), callable_mp(this, &DatabaseTableDialog::_apply));
}
