/**************************************************************************/
/*  database_editor.cpp                                                   */
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

#include "database_editor.h"

#include "../sqlite_statement.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/undo_redo.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/run/editor_run_bar.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/resources/font.h"
#include "scene/scene_string_names.h"
#include "servers/display/display_server.h"

// Rows per page of the table (ARCHITECTURE.md 7.5).
static constexpr int DATABASE_EDITOR_PAGE_SIZE = 200;
// Rows shown by Query...: a SELECT without LIMIT can return a whole table.
static constexpr int DATABASE_EDITOR_QUERY_ROW_LIMIT = 1000;

enum DatabaseEditorSideBarButton {
	DATABASE_EDITOR_BUTTON_CLOSE,
};

// Identifiers are quoted; values are always bound parameters.
static String _database_editor_quote(const String &p_identifier) {
	return "\"" + p_identifier.replace("\"", "\"\"") + "\"";
}

static String _database_editor_cell_text(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return "NULL";
		case Variant::PACKED_BYTE_ARRAY:
			return vformat("[BLOB: %d bytes]", PackedByteArray(p_value).size());
		default:
			return p_value;
	}
}

// Typed by the column affinity; anything else is stored as text, as SQLite does.
static Variant _database_editor_parse_cell(const String &p_text, SQLiteColumn::Affinity p_affinity) {
	if (p_text.is_empty() && (p_affinity == SQLiteColumn::AFFINITY_INTEGER || p_affinity == SQLiteColumn::AFFINITY_REAL || p_affinity == SQLiteColumn::AFFINITY_NUMERIC)) {
		return Variant(); // An empty number is NULL, not an empty text.
	}
	switch (p_affinity) {
		case SQLiteColumn::AFFINITY_INTEGER:
		case SQLiteColumn::AFFINITY_NUMERIC:
			if (p_text.is_valid_int()) {
				return p_text.to_int();
			}
			[[fallthrough]];
		case SQLiteColumn::AFFINITY_REAL:
			if (p_text.is_valid_float()) {
				return p_text.to_float();
			}
			break;
		default:
			break;
	}
	return p_text;
}

// The filter text matches anywhere; % and _ typed by the user match themselves (LIKE ... ESCAPE with a backslash).
static String _database_editor_like_pattern(const String &p_text) {
	return "%" + p_text.replace("\\", "\\\\").replace("%", "\\%").replace("_", "\\_") + "%";
}

static bool _database_editor_is_empty(const Array &p_changes) {
	return Dictionary(p_changes[0]).is_empty() && Dictionary(p_changes[1]).is_empty() && Array(p_changes[2]).is_empty();
}

static int64_t _database_editor_count_rows(const Dictionary &p_pending) {
	int64_t count = 0;
	for (const KeyValue<Variant, Variant> &E : p_pending) {
		const Array changes = E.value;
		count += Dictionary(changes[0]).size() + Dictionary(changes[1]).size() + Array(changes[2]).size();
	}
	return count;
}

Error DatabaseEditor::apply_changes(const Ref<SQLiteDatabase> &p_database, const String &p_table, const Vector<String> &p_key_columns, const Array &p_changes, Variant &r_failed_row, String &r_message) {
	ERR_FAIL_COND_V(p_database.is_null() || p_key_columns.is_empty() || p_changes.size() != 3, ERR_INVALID_PARAMETER);
	const Dictionary changed_rows = p_changes[0];
	const Dictionary deleted_rows = p_changes[1];
	const Array new_rows = p_changes[2];
	r_failed_row = Variant();
	r_message = String();

	const String table_sql = _database_editor_quote(p_table);
	String where;
	for (const String &key : p_key_columns) {
		// IS also matches NULL keys, which rowid tables allow in the primary key.
		where += (where.is_empty() ? "" : " AND ") + _database_editor_quote(key) + " IS ?";
	}

	// Inside the caller's transaction (Save of several tables), the caller commits or rolls back.
	const bool own_transaction = !p_database->is_in_transaction();
	Error err = own_transaction ? p_database->begin_transaction() : OK;
	for (const KeyValue<Variant, Variant> &row : deleted_rows) {
		if (err != OK) {
			break;
		}
		err = p_database->execute("DELETE FROM " + table_sql + " WHERE " + where + ";", row.key);
		r_failed_row = row.key;
	}
	for (const KeyValue<Variant, Variant> &row : changed_rows) {
		const Dictionary values = row.value;
		if (err != OK) {
			break;
		}
		if (deleted_rows.has(row.key) || values.is_empty()) {
			continue;
		}
		String assignments;
		Array params;
		for (const KeyValue<Variant, Variant> &value : values) {
			assignments += (assignments.is_empty() ? "" : ", ") + _database_editor_quote(value.key) + " = ?";
			params.push_back(value.value);
		}
		params.append_array(row.key);
		err = p_database->execute("UPDATE " + table_sql + " SET " + assignments + " WHERE " + where + ";", params);
		r_failed_row = row.key;
	}
	for (int i = 0; err == OK && i < new_rows.size(); i++) {
		const Dictionary values = new_rows[i];
		String names;
		String marks;
		Array params;
		for (const KeyValue<Variant, Variant> &value : values) {
			names += (names.is_empty() ? "" : ", ") + _database_editor_quote(value.key);
			marks += marks.is_empty() ? "?" : ", ?";
			params.push_back(value.value);
		}
		const String sql = values.is_empty() ? "INSERT INTO " + table_sql + " DEFAULT VALUES;" : vformat("INSERT INTO %s (%s) VALUES (%s);", table_sql, names, marks);
		err = p_database->execute(sql, params);
		r_failed_row = i;
	}
	if (err == OK && own_transaction) {
		err = p_database->commit();
		r_failed_row = Variant();
	}
	if (err != OK) {
		r_message = p_database->get_last_error_message();
		if (own_transaction) {
			p_database->rollback();
		}
		return err;
	}
	r_failed_row = Variant();
	return OK;
}

Vector<String> DatabaseEditor::get_key_columns(const Ref<SQLiteDatabase> &p_database, const String &p_table) {
	Vector<String> keys;
	ERR_FAIL_COND_V(p_database.is_null(), keys);
	PackedStringArray names;
	const TypedArray<Dictionary> info = p_database->query("SELECT name, pk FROM pragma_table_info(?) ORDER BY pk;", varray(p_table));
	for (int i = 0; i < info.size(); i++) {
		const Dictionary row = info[i];
		names.push_back(String(row["name"]).to_lower());
		if (int64_t(row["pk"]) > 0) {
			keys.push_back(String(row["name"]));
		}
	}
	if (keys.is_empty()) {
		// A column named "rowid" hides the rowid; SQLite has two more names for it.
		for (const String alias : { "rowid", "_rowid_", "oid" }) {
			if (!names.has(alias)) {
				keys.push_back(alias);
				break;
			}
		}
	}
	return keys;
}

PackedStringArray DatabaseEditor::get_view_names(const Ref<SQLiteDatabase> &p_database) {
	PackedStringArray names;
	ERR_FAIL_COND_V(p_database.is_null(), names);
	const TypedArray<Dictionary> info = p_database->query("SELECT name FROM pragma_table_list WHERE schema = 'main' AND type = 'view' ORDER BY name;");
	for (int i = 0; i < info.size(); i++) {
		names.push_back(String(Dictionary(info[i])["name"]));
	}
	return names;
}

Ref<SQLiteDatabase> DatabaseEditor::_open(const String &p_database, SQLiteDatabase::OpenMode p_mode, Label *p_status) {
	// An own connection per action, closed right after: the file stays unlocked for the game and other tools.
	Ref<SQLiteDatabase> connection;
	connection.instantiate();
	if (connection->open(p_database, p_mode) != OK) {
		p_status->set_text(connection->get_last_error_message());
		return Ref<SQLiteDatabase>();
	}
	return connection;
}

DatabaseEditor::OpenDatabase *DatabaseEditor::_get_selected() {
	return database.is_empty() ? nullptr : databases.getptr(database);
}

String DatabaseEditor::_get_path_of(int p_id) const {
	for (const KeyValue<String, OpenDatabase> &E : databases) {
		if (E.value.id == p_id) {
			return E.key;
		}
	}
	return String();
}

Array DatabaseEditor::_get_changes() const {
	const OpenDatabase *entry = database.is_empty() ? nullptr : databases.getptr(database);
	if (entry && entry->pending.has(table)) {
		return Array(entry->pending[table]).duplicate(true);
	}
	return Array{ Dictionary(), Dictionary(), Array() };
}

bool DatabaseEditor::_has_pending(const String &p_table) const {
	const OpenDatabase *entry = database.is_empty() ? nullptr : databases.getptr(database);
	return entry && entry->pending.has(p_table);
}

void DatabaseEditor::_update_side_bar() {
	// Files gone outside the editor (another tool's save, a checkout) stay open with their pending edits until
	// they are closed or removed in the FileSystem dock.
	for (KeyValue<String, OpenDatabase> &E : databases) {
		E.value.missing = !FileAccess::exists(E.key);
	}
	side_bar_outdated = false;
	updating_side_bar = true;
	side_bar->clear();
	TreeItem *root = side_bar->create_item();
	const Ref<Texture2D> database_icon = get_editor_theme_icon(SNAME("SQLiteDatabase"));
	const Ref<Texture2D> table_icon = get_editor_theme_icon(SNAME("GridContainer"));
	const Ref<Texture2D> view_icon = get_editor_theme_icon(SNAME("GuiVisibilityVisible"));
	const Ref<Texture2D> close_icon = get_editor_theme_icon(SNAME("Close"));
	for (const KeyValue<String, OpenDatabase> &E : databases) {
		TreeItem *database_item = side_bar->create_item(root);
		database_item->set_text(0, E.value.path.get_file() + (E.value.pending.is_empty() ? "" : " (*)"));
		database_item->set_tooltip_text(0, E.value.path);
		database_item->set_icon(0, database_icon);
		database_item->set_metadata(0, E.key);
		database_item->add_button(0, close_icon, DATABASE_EDITOR_BUTTON_CLOSE, false, TTR("Close the database."));
		if (E.key == database && table.is_empty()) {
			database_item->select(0);
		}
		if (E.value.missing) {
			database_item->set_custom_color(0, get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
			database_item->set_tooltip_text(0, vformat(TTR("File not found: %s"), E.value.path));
			continue;
		}

		Ref<SQLiteDatabase> connection = _open(E.key, SQLiteDatabase::OPEN_MODE_READ_ONLY, status_label);
		if (connection.is_null()) {
			continue;
		}
		const PackedStringArray table_names = connection->get_table_names();
		const PackedStringArray view_names = get_view_names(connection);
		connection->close();
		for (int is_view = 0; is_view < 2; is_view++) {
			for (const String &table_name : is_view ? view_names : table_names) {
				TreeItem *item = side_bar->create_item(database_item);
				item->set_text(0, table_name + (E.value.pending.has(table_name) ? " (*)" : ""));
				item->set_tooltip_text(0, is_view ? TTR("View: its rows are read-only.") : TTR("Table"));
				item->set_icon(0, is_view ? view_icon : table_icon);
				item->set_metadata(0, table_name);
				if (E.key == database && table_name == table) {
					item->select(0);
				}
			}
		}
	}
	updating_side_bar = false;
	_update_actions();
}

void DatabaseEditor::_update_actions() {
	const OpenDatabase *entry = _get_selected();
	const bool editable = !key_columns.is_empty();
	const bool has_pending = entry && !entry->pending.is_empty();

	PopupMenu *popup = table_menu->get_popup();
	popup->set_item_disabled(popup->get_item_index(TABLE_MENU_CREATE), entry == nullptr);
	popup->set_item_disabled(popup->get_item_index(TABLE_MENU_RENAME), table.is_empty() || view);
	popup->set_item_disabled(popup->get_item_index(TABLE_MENU_DELETE), table.is_empty());
	popup->set_item_disabled(popup->get_item_index(TABLE_MENU_REFRESH), databases.is_empty());
	popup = values_menu->get_popup();
	popup->set_item_disabled(popup->get_item_index(VALUES_MENU_INSERT_ROW), !editable);
	popup->set_item_disabled(popup->get_item_index(VALUES_MENU_DELETE_ROWS), !editable);
	popup->set_item_disabled(popup->get_item_index(VALUES_MENU_SET_NULL), !editable);
	popup->set_item_disabled(popup->get_item_index(VALUES_MENU_UNDO), !(entry && entry->undo_redo->has_undo()));
	popup->set_item_disabled(popup->get_item_index(VALUES_MENU_REDO), !(entry && entry->undo_redo->has_redo()));
	query_button->set_disabled(entry == nullptr);
	filter_edit->set_editable(!table.is_empty());
	revert_button->set_disabled(!has_pending);
	save_button->set_disabled(!has_pending);

	if (databases.is_empty()) {
		empty_label->set_text(TTR("Double-click a database file (.db, .sqlite, .sqlite3, .db3) in the FileSystem dock, or use \"Open in Database\" in the Inspector."));
	} else if (table.is_empty()) {
		empty_label->set_text(TTR("Select a table or view in the side bar, or create one with Table > Create with SQL."));
	} else if (rows.is_empty() && _database_editor_is_empty(_get_changes())) {
		empty_label->set_text(filter_edit->get_text().is_empty() ? TTR("No rows. Add one with Values > Insert Row.") : TTR("No rows match the filter."));
	} else {
		empty_label->set_text(String());
	}
	empty_label->set_visible(!empty_label->get_text().is_empty());
}

void DatabaseEditor::_select_table(const String &p_database, const String &p_table) {
	database = p_database;
	table = p_table;
	view = false;
	columns.clear();
	key_columns.clear();
	failed_row = Variant();
	page = 0;
	sort_column = -1;
	sort_descending = false;
	filter_edit->set_text(String());

	Ref<SQLiteDatabase> connection = (database.is_empty() || table.is_empty()) ? Ref<SQLiteDatabase>() : _open(database, SQLiteDatabase::OPEN_MODE_READ_ONLY, status_label);
	if (connection.is_valid()) {
		view = get_view_names(connection).has(table);
		// pragma_table_info() lists the columns of views too.
		const TypedArray<Dictionary> info = connection->query("SELECT name, type FROM pragma_table_info(?) ORDER BY cid;", varray(table));
		for (int i = 0; i < info.size(); i++) {
			const Dictionary row = info[i];
			Column column;
			column.name = String(row["name"]);
			column.declared_type = String(row["type"]);
			column.affinity = SQLiteColumn::get_affinity_for_type(column.declared_type);
			columns.push_back(column);
		}
		if (!view) {
			key_columns = get_key_columns(connection, table);
		}
		connection->close();
	}
	if (columns.is_empty()) {
		table = String(); // Removed outside the dock.
	}

	const int column_count = int(columns.size());
	tree->set_columns(MAX(1, column_count));
	tree->set_column_titles_visible(column_count > 0);
	for (int c = 0; c < column_count; c++) {
		tree->set_column_title(c, columns[c].name);
		tree->set_column_title_tooltip_text(c, columns[c].declared_type.is_empty() ? TTR("No declared type.") : columns[c].declared_type);
		tree->set_column_clip_content(c, true);
		tree->set_column_custom_minimum_width(c, 80 * EDSCALE);
	}
	_load_page();
}

void DatabaseEditor::_load_page() {
	rows.clear();
	page_count = 1;
	Ref<SQLiteDatabase> connection = columns.is_empty() ? Ref<SQLiteDatabase>() : _open(database, SQLiteDatabase::OPEN_MODE_READ_ONLY, status_label);
	if (connection.is_valid()) {
		const String table_sql = _database_editor_quote(table);
		String where;
		Array params;
		if (!filter_edit->get_text().is_empty()) {
			for (const Column &column : columns) {
				where += (where.is_empty() ? " WHERE " : " OR ") + _database_editor_quote(column.name) + " LIKE ?1 ESCAPE '\\'";
			}
			params.push_back(_database_editor_like_pattern(filter_edit->get_text()));
		}
		const TypedArray<Dictionary> count = connection->query("SELECT COUNT(*) AS n FROM " + table_sql + where + ";", params);
		const int64_t total = count.is_empty() ? 0 : int64_t(Dictionary(count[0])["n"]);
		page_count = MAX(1, int((total + DATABASE_EDITOR_PAGE_SIZE - 1) / DATABASE_EDITOR_PAGE_SIZE));
		page = CLAMP(page, 0, page_count - 1);
		page_label->set_text(vformat(TTR("Page %d of %d, %d rows"), page + 1, page_count, total));

		// Keys, then the values with BLOBs left out, then the BLOB sizes: a page never loads BLOB data.
		PackedStringArray select;
		for (const String &key : key_columns) {
			select.push_back(_database_editor_quote(key));
		}
		for (const Column &column : columns) {
			const String column_sql = _database_editor_quote(column.name);
			select.push_back(vformat("CASE WHEN typeof(%s) = 'blob' THEN NULL ELSE %s END", column_sql, column_sql));
		}
		for (const Column &column : columns) {
			const String column_sql = _database_editor_quote(column.name);
			select.push_back(vformat("CASE WHEN typeof(%s) = 'blob' THEN length(%s) END", column_sql, column_sql));
		}
		// The keys break ties, so OFFSET pages neither repeat nor skip rows.
		PackedStringArray order_terms;
		if (sort_column >= 0) {
			order_terms.push_back(_database_editor_quote(columns[sort_column].name) + (sort_descending ? " DESC" : " ASC"));
		}
		for (const String &key : key_columns) {
			order_terms.push_back(_database_editor_quote(key));
		}
		const String order = order_terms.is_empty() ? String() : " ORDER BY " + String(", ").join(order_terms);
		const int limit_index = params.size() + 1;
		params.push_back(DATABASE_EDITOR_PAGE_SIZE);
		params.push_back(int64_t(page) * DATABASE_EDITOR_PAGE_SIZE);
		Ref<SQLiteStatement> statement = connection->prepare(vformat("SELECT %s FROM %s%s%s LIMIT ?%d OFFSET ?%d;", String(", ").join(select), table_sql, where, order, limit_index, limit_index + 1));
		if (statement.is_valid() && statement->bind_values(params) == OK) {
			while (statement->step() == SQLiteStatement::STEP_RESULT_ROW) {
				Array row;
				for (int i = 0; i < statement->get_column_count(); i++) {
					row.push_back(statement->get_column_value(i));
				}
				rows.push_back(row);
			}
		}
		if (connection->get_last_error() != OK) {
			status_label->set_text(connection->get_last_error_message());
		}
		statement.unref();
		connection->close();
		databases[database].read_time = FileAccess::get_modified_time(database);
	} else {
		page_label->set_text(String());
	}
	previous_button->set_disabled(page == 0);
	next_button->set_disabled(page >= page_count - 1);
	_render();
}

void DatabaseEditor::_render() {
	tree->clear();
	row_ids.clear();
	TreeItem *root = tree->create_item();
	const Color dim_color = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	const Color added_color = get_theme_color(SNAME("success_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.2);
	const Color changed_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.2);
	const Color error_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	const Ref<Font> null_font = get_theme_font(SNAME("doc_italic"), EditorStringName(EditorFonts));
	const bool editable = !key_columns.is_empty();
	const int key_count = key_columns.size();
	const int column_count = int(columns.size());
	const Array changes = _get_changes();
	const Dictionary updates = changes[0];
	const Dictionary deletes = changes[1];
	const Array inserts = changes[2];

	// New rows first, then the page.
	for (int i = 0; i < inserts.size() + rows.size(); i++) {
		const bool inserted = i < inserts.size();
		const Variant row_id = inserted ? Variant(i) : Variant(rows[i - inserts.size()].slice(0, key_count));
		const Dictionary changed = inserted ? Dictionary(inserts[i]) : Dictionary(updates.get(row_id, Dictionary()));
		const bool deleted = !inserted && deletes.has(row_id);
		const bool failed = row_id == failed_row;
		row_ids.push_back(row_id);

		TreeItem *item = tree->create_item(root);
		for (int c = 0; c < column_count; c++) {
			const String &column_name = columns[c].name;
			const bool has_change = changed.has(column_name);
			const bool is_default = inserted && !has_change;
			const Variant value = has_change ? changed[column_name] : (inserted ? Variant() : rows[i - inserts.size()][key_count + c]);
			const Variant blob_size = (has_change || inserted) ? Variant() : rows[i - inserts.size()][key_count + column_count + c];
			const bool is_blob = blob_size.get_type() != Variant::NIL;
			const String text = is_default ? String("DEFAULT") : (is_blob ? vformat("[BLOB: %d bytes]", int64_t(blob_size)) : _database_editor_cell_text(value));
			item->set_text(c, text);
			item->set_metadata(c, text);
			item->set_editable(c, editable && !deleted && !is_blob);
			if (is_default || (!is_blob && value.get_type() == Variant::NIL)) {
				// Dimmed and in italics: a text "NULL" looks different.
				item->set_custom_color(c, dim_color);
				item->set_custom_font(c, null_font);
			}
			if (failed) {
				item->set_custom_bg_color(c, error_color * Color(1, 1, 1, 0.4));
				item->set_tooltip_text(c, status_label->get_text());
			} else if (deleted) {
				item->set_custom_bg_color(c, error_color * Color(1, 1, 1, 0.2));
				item->set_tooltip_text(c, TTR("Deleted on Save."));
			} else if (inserted) {
				item->set_custom_bg_color(c, added_color);
			} else if (has_change) {
				item->set_custom_bg_color(c, changed_color);
			}
		}
		if (failed) {
			tree->scroll_to_item(item);
		}
	}
	_update_actions();
}

void DatabaseEditor::_set_pending(int p_id, const Dictionary &p_pending, const String &p_table) {
	const String path = _get_path_of(p_id); // The file may have moved since the action.
	OpenDatabase *entry = databases.getptr(path);
	ERR_FAIL_NULL(entry);
	entry->pending = p_pending.duplicate(true);
	if (path != database || p_table != table) {
		_select_table(path, p_table); // Undo and redo show the table they change.
	} else {
		failed_row = Variant();
		_render();
	}
	_update_side_bar(); // The (*) markers.
	const int64_t count = _database_editor_count_rows(entry->pending);
	status_label->set_text(count > 0 ? vformat(TTR("%d rows with unsaved changes."), count) : String());
}

void DatabaseEditor::_commit(const String &p_action, const Array &p_changes) {
	OpenDatabase *entry = _get_selected();
	ERR_FAIL_NULL(entry);
	Dictionary next = entry->pending.duplicate(true);
	if (_database_editor_is_empty(p_changes)) {
		next.erase(table);
	} else {
		next[table] = p_changes;
	}
	entry->undo_redo->create_action(p_action);
	entry->undo_redo->add_do_method(callable_mp(this, &DatabaseEditor::_set_pending).bind(entry->id, next, table));
	entry->undo_redo->add_undo_method(callable_mp(this, &DatabaseEditor::_set_pending).bind(entry->id, entry->pending.duplicate(true), table));
	entry->undo_redo->commit_action();
}

void DatabaseEditor::_set_cell(const Variant &p_row, int p_column, const Variant &p_value) {
	ERR_FAIL_INDEX(p_column, int(columns.size()));
	const Array changes = _get_changes();
	const String &column_name = columns[p_column].name;
	if (p_row.get_type() == Variant::INT) {
		Dictionary values = Array(changes[2])[int(p_row)];
		values[column_name] = p_value;
	} else {
		Dictionary changed_rows = changes[0];
		Dictionary values = changed_rows.get(p_row, Dictionary());
		values[column_name] = p_value;
		changed_rows[p_row] = values;
	}
	_commit(TTR("Edit Cell"), changes);
}

void DatabaseEditor::_cell_edited() {
	TreeItem *item = tree->get_edited();
	const int column = tree->get_edited_column();
	ERR_FAIL_NULL(item);
	ERR_FAIL_INDEX(column, int(columns.size()));
	const String text = item->get_text(column);
	if (text == String(item->get_metadata(column))) {
		return; // Unchanged; keeps NULL and DEFAULT cells as they are.
	}
	// Deferred: the tree is rebuilt, and it is still inside its own signal.
	callable_mp(this, &DatabaseEditor::_set_cell).call_deferred(row_ids[item->get_index()], column, _database_editor_parse_cell(text, columns[column].affinity));
}

void DatabaseEditor::_insert_row() {
	if (key_columns.is_empty()) {
		return;
	}
	const Array changes = _get_changes();
	Array(changes[2]).push_back(Dictionary());
	_commit(TTR("Insert Row"), changes);
}

void DatabaseEditor::_delete_rows() {
	if (key_columns.is_empty()) {
		return;
	}
	const Array changes = _get_changes();
	Dictionary deleted_rows = changes[1];
	Array new_rows = changes[2];
	Vector<int> removed_inserts;
	bool any_selected = false;
	for (TreeItem *item = tree->get_next_selected(nullptr); item; item = tree->get_next_selected(item)) {
		const Variant row_id = row_ids[item->get_index()];
		any_selected = true;
		if (row_id.get_type() == Variant::INT) {
			removed_inserts.push_back(int(row_id));
		} else if (!deleted_rows.erase(row_id)) {
			deleted_rows[row_id] = true; // Deleting a marked row again unmarks it.
		}
	}
	if (!any_selected) {
		status_label->set_text(TTR("Select the rows to delete first."));
		return;
	}
	removed_inserts.sort();
	for (int i = removed_inserts.size() - 1; i >= 0; i--) {
		new_rows.remove_at(removed_inserts[i]);
	}
	_commit(TTR("Delete Rows"), changes);
}

void DatabaseEditor::_set_null() {
	TreeItem *item = tree->get_selected();
	const int column = tree->get_selected_column();
	if (item && column >= 0 && column < int(columns.size()) && item->is_editable(column)) {
		_set_cell(row_ids[item->get_index()], column, Variant());
	}
}

void DatabaseEditor::_undo_redo(bool p_redo) {
	OpenDatabase *entry = _get_selected();
	if (entry == nullptr) {
		return;
	}
	if (p_redo) {
		entry->undo_redo->redo();
	} else {
		entry->undo_redo->undo();
	}
	_update_actions();
}

Error DatabaseEditor::_save_database(const String &p_database) {
	OpenDatabase *entry = databases.getptr(p_database);
	if (entry == nullptr || entry->pending.is_empty()) {
		return OK;
	}
	Ref<SQLiteDatabase> connection = _open(p_database, SQLiteDatabase::OPEN_MODE_READ_WRITE, status_label);
	if (connection.is_null()) {
		return ERR_CANT_OPEN;
	}
	// All tables in one transaction: a failing row leaves the file as it was.
	const bool changed_outside = entry->read_time != 0 && FileAccess::get_modified_time(p_database) != entry->read_time;
	Variant failed;
	String message;
	String failed_table;
	Error err = connection->begin_transaction();
	if (err == OK) {
		// Foreign keys are checked at COMMIT: the tables are written in any order.
		err = connection->execute("PRAGMA defer_foreign_keys = ON;");
	}
	for (const KeyValue<Variant, Variant> &E : entry->pending) {
		if (err != OK) {
			break;
		}
		const String table_name = E.key;
		err = apply_changes(connection, table_name, get_key_columns(connection, table_name), E.value, failed, message);
		if (err != OK) {
			failed_table = table_name;
		}
	}
	if (err == OK) {
		err = connection->commit();
	}
	if (err != OK) {
		if (message.is_empty()) {
			message = connection->get_last_error_message();
		}
		if (connection->is_in_transaction()) {
			connection->rollback();
		}
	}
	connection->close();

	if (err != OK) {
		// Nothing was written; the edits stay pending so they can be fixed.
		if (!failed_table.is_empty() && (p_database != database || failed_table != table)) {
			_select_table(p_database, failed_table);
			_update_side_bar();
		}
		status_label->set_text(vformat(TTR("Not saved: %s"), message));
		failed_row = failed;
		_render();
		return err;
	}
	entry->pending.clear();
	entry->undo_redo->clear_history();
	if (p_database == database) {
		_load_page();
	}
	_report_written(p_database, changed_outside ? TTR("Saved. The file was changed outside the editor since the dock read it: check the rows.") : TTR("Saved."));
	return OK;
}

void DatabaseEditor::_report_written(const String &p_database, const String &p_message) {
	OpenDatabase *entry = databases.getptr(p_database);
	if (entry) {
		entry->read_time = FileAccess::get_modified_time(p_database);
	}
	_update_side_bar();
	// The module uses no file locks: a running game reads res:// databases as unchanging files.
	status_label->set_text(EditorRunBar::get_singleton()->is_playing() ? p_message + " " + TTR("Restart the running game: it may still read the old data.") : p_message);
	// The Inspector's table overview shows the tables and their row counts.
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	const SQLiteDatabase *inspected = inspector ? Object::cast_to<SQLiteDatabase>(inspector->get_edited_object()) : nullptr;
	if (inspected && ProjectSettings::get_singleton()->globalize_path(inspected->get_path()) == p_database) {
		inspector->update_tree();
	}
}

void DatabaseEditor::_close_database(const String &p_database) {
	OpenDatabase *entry = databases.getptr(p_database);
	if (entry == nullptr) {
		return;
	}
	memdelete(entry->undo_redo);
	databases.erase(p_database);
	if (p_database == database) {
		_select_table(String(), String());
	}
	_update_side_bar();
}

void DatabaseEditor::_side_bar_selected() {
	TreeItem *item = side_bar->get_selected();
	if (updating_side_bar || item == nullptr) {
		return;
	}
	if (item->get_parent() == side_bar->get_root()) {
		_select_table(String(item->get_metadata(0)), String());
	} else {
		_select_table(String(item->get_parent()->get_metadata(0)), String(item->get_metadata(0)));
	}
}

void DatabaseEditor::_side_bar_button_clicked(TreeItem *p_item, int p_column, int p_id, MouseButton p_button) {
	if (p_button != MouseButton::LEFT || p_id != DATABASE_EDITOR_BUTTON_CLOSE) {
		return;
	}
	const String path = p_item->get_metadata(0);
	const OpenDatabase *entry = databases.getptr(path);
	if (entry && !entry->pending.is_empty()) {
		closing_database = path;
		close_prompt->set_text(vformat(TTR("\"%s\" has edits that are not saved. Save them before closing?"), entry->path));
		close_prompt->popup_centered();
	} else {
		_close_database(path);
	}
}

void DatabaseEditor::_close_saving() {
	// A failed save keeps the edits and the database: the status line says why.
	if (_save_database(closing_database) == OK) {
		_close_database(closing_database);
	}
}

void DatabaseEditor::_close_discarding() {
	close_prompt->hide();
	_close_database(closing_database);
}

void DatabaseEditor::_table_menu_pressed(int p_id) {
	switch (p_id) {
		case TABLE_MENU_CREATE: {
			if (sql_edit->get_text().strip_edges().is_empty()) {
				sql_edit->set_text("CREATE TABLE new_table (\n\tid INTEGER PRIMARY KEY,\n\tname TEXT NOT NULL DEFAULT ''\n);");
			}
			sql_status->set_text(TTR("Runs any SQL at once, in one transaction, and can't be undone: CREATE TABLE, CREATE VIEW, CREATE INDEX, ALTER TABLE, DROP and more. Don't use BEGIN, COMMIT or ROLLBACK."));
			sql_dialog->popup_centered_clamped(Size2(600, 360) * EDSCALE, 0.8);
			sql_edit->grab_focus();
		} break;
		case TABLE_MENU_RENAME: {
			if (_has_pending(table)) {
				status_label->set_text(TTR("Save or revert the edits of this table first."));
				return;
			}
			rename_edit->set_text(table);
			rename_dialog->popup_centered(Size2(360, 0) * EDSCALE);
			rename_edit->select_all();
			rename_edit->grab_focus();
		} break;
		case TABLE_MENU_DELETE: {
			if (_has_pending(table)) {
				status_label->set_text(TTR("Save or revert the edits of this table first."));
				return;
			}
			delete_dialog->set_text(view ? vformat(TTR("Delete the view \"%s\"? Its tables stay."), table) : vformat(TTR("Delete the table \"%s\" with all its rows? This can't be undone."), table));
			delete_dialog->popup_centered();
		} break;
		case TABLE_MENU_REFRESH: {
			_select_table(database, table);
			_update_side_bar();
		} break;
	}
}

void DatabaseEditor::_values_menu_pressed(int p_id) {
	switch (p_id) {
		case VALUES_MENU_INSERT_ROW: {
			_insert_row();
		} break;
		case VALUES_MENU_DELETE_ROWS: {
			_delete_rows();
		} break;
		case VALUES_MENU_SET_NULL: {
			_set_null();
		} break;
		case VALUES_MENU_UNDO: {
			_undo_redo(false);
		} break;
		case VALUES_MENU_REDO: {
			_undo_redo(true);
		} break;
	}
}

void DatabaseEditor::_save() {
	_save_database(database);
}

void DatabaseEditor::_revert() {
	OpenDatabase *entry = _get_selected();
	if (entry == nullptr || entry->pending.is_empty()) {
		return;
	}
	entry->undo_redo->create_action(TTR("Revert Edits"));
	entry->undo_redo->add_do_method(callable_mp(this, &DatabaseEditor::_set_pending).bind(entry->id, Dictionary(), table));
	entry->undo_redo->add_undo_method(callable_mp(this, &DatabaseEditor::_set_pending).bind(entry->id, entry->pending.duplicate(true), table));
	entry->undo_redo->commit_action();
}

void DatabaseEditor::_create_confirmed() {
	OpenDatabase *entry = _get_selected();
	ERR_FAIL_NULL(entry);
	// The SQL may drop or change any table: pending edits would point at rows that are gone.
	if (!entry->pending.is_empty()) {
		sql_status->set_text(TTR("Save or revert the pending edits of this database first."));
		return;
	}
	Ref<SQLiteDatabase> connection = _open(database, SQLiteDatabase::OPEN_MODE_READ_WRITE, sql_status);
	if (connection.is_null()) {
		return;
	}
	PackedStringArray before = connection->get_table_names();
	before.append_array(get_view_names(connection));
	Error err = connection->begin_transaction();
	bool ended = false; // The SQL ended the transaction itself: what ran before that is written.
	if (err == OK) {
		err = connection->execute(sql_edit->get_text());
		ended = !connection->is_in_transaction();
	}
	if (err == OK && !ended) {
		err = connection->commit();
	}
	String created;
	if (err != OK) {
		sql_status->set_text(ended ? connection->get_last_error_message() + " " + TTR("The SQL ended the transaction itself: the statements before that are written.") : connection->get_last_error_message());
		if (connection->is_in_transaction()) {
			connection->rollback();
		}
	} else {
		PackedStringArray after = connection->get_table_names();
		after.append_array(get_view_names(connection));
		for (const String &table_name : after) {
			if (!before.has(table_name)) {
				created = table_name;
				break;
			}
		}
	}
	connection->close();
	if (err != OK && !ended) {
		return; // Nothing changed; the dialog stays open with SQLite's message.
	}
	entry->undo_redo->clear_history(); // The schema may have changed under the history.
	_select_table(database, created.is_empty() ? table : created);
	if (err != OK) {
		_report_written(database, TTR("The SQL failed after it ended the transaction itself."));
		return;
	}
	sql_dialog->hide();
	sql_edit->set_text(String());
	_report_written(database, created.is_empty() ? TTR("SQL run.") : vformat(TTR("Created \"%s\"."), created));
}

void DatabaseEditor::_rename_confirmed() {
	const String new_name = rename_edit->get_text().strip_edges();
	if (new_name.is_empty() || new_name == table) {
		return;
	}
	Ref<SQLiteDatabase> connection = _open(database, SQLiteDatabase::OPEN_MODE_READ_WRITE, status_label);
	if (connection.is_null()) {
		return;
	}
	const Error err = connection->execute("ALTER TABLE " + _database_editor_quote(table) + " RENAME TO " + _database_editor_quote(new_name) + ";");
	const String message = connection->get_last_error_message();
	connection->close();
	if (err != OK) {
		status_label->set_text(vformat(TTR("Not renamed: %s"), message));
		return;
	}
	// The history holds edits under the old name.
	_get_selected()->undo_redo->clear_history();
	_select_table(database, new_name);
	_report_written(database, vformat(TTR("Renamed to \"%s\"."), new_name));
}

void DatabaseEditor::_delete_confirmed() {
	Ref<SQLiteDatabase> connection = _open(database, SQLiteDatabase::OPEN_MODE_READ_WRITE, status_label);
	if (connection.is_null()) {
		return;
	}
	const Error err = connection->execute(String(view ? "DROP VIEW " : "DROP TABLE ") + _database_editor_quote(table) + ";");
	const String message = connection->get_last_error_message();
	connection->close();
	if (err != OK) {
		status_label->set_text(vformat(TTR("Not deleted: %s"), message));
		return;
	}
	const String deleted = table;
	_get_selected()->undo_redo->clear_history();
	_select_table(database, String());
	_report_written(database, vformat(TTR("Deleted \"%s\"."), deleted));
}

void DatabaseEditor::_open_query() {
	const OpenDatabase *entry = _get_selected();
	if (entry == nullptr) {
		return;
	}
	if (query_edit->get_text().strip_edges().is_empty() && !table.is_empty()) {
		query_edit->set_text("SELECT * FROM " + _database_editor_quote(table) + " LIMIT 100;");
	}
	query_dialog->set_title(vformat(TTR("Query: %s"), entry->path.get_file()));
	query_dialog->popup_centered_clamped(Size2(800, 500) * EDSCALE, 0.8);
	query_edit->grab_focus();
}

void DatabaseEditor::_run_query() {
	query_tree->clear();
	query_status->set_text(String());
	const String sql = query_edit->get_text().strip_edges();
	if (sql.is_empty()) {
		return;
	}
	// A read-only connection: SQLite refuses every write, whatever the SQL is.
	Ref<SQLiteDatabase> connection = _open(database, SQLiteDatabase::OPEN_MODE_READ_ONLY, query_status);
	if (connection.is_null()) {
		return;
	}
	Ref<SQLiteStatement> statement = connection->prepare(sql);
	if (statement.is_null()) {
		query_status->set_text(connection->get_last_error_message());
		connection->close();
		return;
	}
	const int column_count = statement->get_column_count();
	query_tree->set_columns(MAX(1, column_count));
	for (int c = 0; c < column_count; c++) {
		query_tree->set_column_title(c, statement->get_column_name(c));
		query_tree->set_column_clip_content(c, true);
		query_tree->set_column_custom_minimum_width(c, 80 * EDSCALE);
	}
	TreeItem *root = query_tree->create_item();
	int row_count = 0;
	bool truncated = false;
	SQLiteStatement::StepResult result = statement->step();
	while (result == SQLiteStatement::STEP_RESULT_ROW) {
		if (row_count == DATABASE_EDITOR_QUERY_ROW_LIMIT) {
			truncated = true;
			break;
		}
		TreeItem *item = query_tree->create_item(root);
		for (int c = 0; c < column_count; c++) {
			item->set_text(c, _database_editor_cell_text(statement->get_column_value(c)));
		}
		row_count++;
		result = statement->step();
	}
	if (result == SQLiteStatement::STEP_RESULT_ERROR) {
		query_status->set_text(connection->get_last_error_message());
	} else {
		query_status->set_text(truncated ? vformat(TTR("The first %d rows."), DATABASE_EDITOR_QUERY_ROW_LIMIT) : vformat(TTR("%d rows."), row_count));
	}
	statement.unref();
	connection->close();
}

void DatabaseEditor::_query_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key_event = p_event;
	if (key_event.is_valid() && key_event->is_pressed() && !key_event->is_echo() && key_event->is_command_or_control_pressed() && (key_event->get_keycode() == Key::ENTER || key_event->get_keycode() == Key::KP_ENTER)) {
		_run_query();
		query_edit->accept_event();
	}
}

void DatabaseEditor::_filter_changed(const String &p_text) {
	page = 0;
	_load_page();
}

void DatabaseEditor::_change_page(int p_delta) {
	page += p_delta;
	_load_page();
}

void DatabaseEditor::_sort(int p_column, int p_mouse_button) {
	if (p_mouse_button != int(MouseButton::LEFT) || p_column >= int(columns.size())) {
		return;
	}
	sort_descending = p_column == sort_column && !sort_descending;
	if (sort_column >= 0) {
		tree->set_column_title(sort_column, columns[sort_column].name);
	}
	sort_column = p_column;
	tree->set_column_title(p_column, columns[p_column].name + String::utf8(sort_descending ? " ▼" : " ▲"));
	_load_page();
}

void DatabaseEditor::_tree_gui_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key_event = p_event;
	if (key_event.is_null() || !key_event->is_pressed() || key_event->is_echo()) {
		return; // Shortcuts match released keys too: Ctrl+S would save twice.
	}
	if (ED_IS_SHORTCUT("editor/save_scene", p_event)) {
		_save();
	} else if (ED_IS_SHORTCUT("ui_undo", p_event)) {
		_undo_redo(false);
	} else if (ED_IS_SHORTCUT("ui_redo", p_event)) {
		_undo_redo(true);
	} else if (key_event->get_keycode() == Key::KEY_DELETE) {
		_delete_rows();
	} else {
		return;
	}
	tree->accept_event();
}

void DatabaseEditor::_file_removed(const String &p_file) {
	// The file is gone: its pending edits can't be saved.
	_close_database(ProjectSettings::get_singleton()->globalize_path(p_file));
}

void DatabaseEditor::_move_database(const String &p_old_path, const String &p_new_file) {
	const OpenDatabase *entry = databases.getptr(p_old_path);
	if (entry == nullptr) {
		return;
	}
	// The undo history refers to the stable id, so it keeps working after the move.
	OpenDatabase moved = *entry;
	moved.path = p_new_file;
	databases.erase(p_old_path);
	const String new_path = ProjectSettings::get_singleton()->globalize_path(p_new_file);
	databases.insert(new_path, moved);
	if (database == p_old_path) {
		database = new_path;
	}
	_update_side_bar();
}

void DatabaseEditor::_files_moved(const String &p_old_file, const String &p_new_file) {
	_move_database(ProjectSettings::get_singleton()->globalize_path(p_old_file), p_new_file);
}

void DatabaseEditor::_folder_moved(const String &p_old_folder, const String &p_new_folder) {
	const String old_prefix = p_old_folder.ends_with("/") ? p_old_folder : p_old_folder + "/";
	const String new_prefix = p_new_folder.ends_with("/") ? p_new_folder : p_new_folder + "/";
	HashMap<String, String> moves; // Globalized old path -> new path as opened.
	for (const KeyValue<String, OpenDatabase> &E : databases) {
		if (E.value.path.begins_with(old_prefix)) {
			moves.insert(E.key, new_prefix + E.value.path.substr(old_prefix.length()));
		}
	}
	for (const KeyValue<String, String> &E : moves) {
		_move_database(E.key, E.value);
	}
}

void DatabaseEditor::_folder_removed(const String &p_folder) {
	const String prefix = p_folder.ends_with("/") ? p_folder : p_folder + "/";
	Vector<String> removed;
	for (const KeyValue<String, OpenDatabase> &E : databases) {
		if (E.value.path.begins_with(prefix)) {
			removed.push_back(E.key);
		}
	}
	for (const String &path : removed) {
		_close_database(path);
	}
}

void DatabaseEditor::_filesystem_changed() {
	// Reading every open database on each scan (every scene save) is wasted work while the dock is hidden.
	if (is_visible_in_tree()) {
		_update_side_bar();
	} else {
		side_bar_outdated = true;
	}
}

void DatabaseEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (FileSystemDock::get_singleton()) {
				FileSystemDock::get_singleton()->connect(SNAME("file_removed"), callable_mp(this, &DatabaseEditor::_file_removed));
				FileSystemDock::get_singleton()->connect(SNAME("files_moved"), callable_mp(this, &DatabaseEditor::_files_moved));
				FileSystemDock::get_singleton()->connect(SNAME("folder_moved"), callable_mp(this, &DatabaseEditor::_folder_moved));
				FileSystemDock::get_singleton()->connect(SNAME("folder_removed"), callable_mp(this, &DatabaseEditor::_folder_removed));
			}
			if (EditorFileSystem::get_singleton()) {
				// Database files removed, or tables changed, outside the editor.
				EditorFileSystem::get_singleton()->connect(SNAME("filesystem_changed"), callable_mp(this, &DatabaseEditor::_filesystem_changed));
			}
			save_button->set_tooltip_text(vformat(TTR("Write the pending edits of the database in one transaction (%s in the table)."), ED_GET_SHORTCUT("editor/save_scene")->get_as_text()));
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			_update_side_bar(); // The icons.
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (side_bar_outdated && is_visible_in_tree()) {
				_update_side_bar();
			}
		} break;
	}
}

void DatabaseEditor::edit(const String &p_path, const String &p_table) {
	const String global_path = ProjectSettings::get_singleton()->globalize_path(p_path);
	if (!databases.has(global_path)) {
		OpenDatabase entry;
		entry.path = p_path;
		entry.undo_redo = memnew(UndoRedo);
		entry.id = next_id++;
		databases.insert(global_path, entry);
	}
	// Inspecting the open database again keeps the shown table.
	if (global_path != database || (!p_table.is_empty() && p_table != table)) {
		_select_table(global_path, p_table);
	}
	_update_side_bar();
}

PackedStringArray DatabaseEditor::get_unsaved_paths() const {
	PackedStringArray paths;
	for (const KeyValue<String, OpenDatabase> &E : databases) {
		if (!E.value.pending.is_empty()) {
			paths.push_back(E.value.path);
		}
	}
	return paths;
}

bool DatabaseEditor::save_all() {
	Vector<String> paths;
	for (const KeyValue<String, OpenDatabase> &E : databases) {
		paths.push_back(E.key);
	}
	bool saved = true;
	for (const String &path : paths) {
		saved = _save_database(path) == OK && saved;
	}
	return saved;
}

DatabaseEditor::DatabaseEditor() {
	singleton = this;
	set_name(TTRC("Database"));
	set_icon_name("SQLiteDatabase");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_database_bottom_panel", TTRC("Toggle Database Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);

	VBoxContainer *main_vbox = memnew(VBoxContainer);
	add_child(main_vbox);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	main_vbox->add_child(toolbar);

	table_menu = memnew(MenuButton);
	table_menu->set_text(TTR("Table"));
	table_menu->set_switch_on_hover(true);
	PopupMenu *popup = table_menu->get_popup();
	popup->add_item(TTR("Create with SQL..."), TABLE_MENU_CREATE);
	popup->add_item(TTR("Rename..."), TABLE_MENU_RENAME);
	popup->add_item(TTR("Delete..."), TABLE_MENU_DELETE);
	popup->add_separator();
	popup->add_item(TTR("Refresh"), TABLE_MENU_REFRESH);
	popup->connect(SceneStringName(id_pressed), callable_mp(this, &DatabaseEditor::_table_menu_pressed));
	toolbar->add_child(table_menu);

	// Plain items: shortcuts in a PopupMenu would take Ctrl+Z from the rest of the editor; the table handles the keys.
	values_menu = memnew(MenuButton);
	values_menu->set_text(TTR("Values"));
	values_menu->set_switch_on_hover(true);
	popup = values_menu->get_popup();
	popup->add_item(TTR("Insert Row"), VALUES_MENU_INSERT_ROW);
	popup->add_item(TTR("Delete Rows"), VALUES_MENU_DELETE_ROWS);
	popup->add_item(TTR("Set NULL"), VALUES_MENU_SET_NULL);
	popup->add_separator();
	popup->add_item(TTR("Undo"), VALUES_MENU_UNDO);
	popup->add_item(TTR("Redo"), VALUES_MENU_REDO);
	popup->connect(SceneStringName(id_pressed), callable_mp(this, &DatabaseEditor::_values_menu_pressed));
	toolbar->add_child(values_menu);

	query_button = memnew(Button(TTR("Query...")));
	query_button->set_flat(true);
	query_button->set_tooltip_text(TTR("Run a read-only SELECT on the database."));
	query_button->connect(SceneStringName(pressed), callable_mp(this, &DatabaseEditor::_open_query));
	toolbar->add_child(query_button);

	filter_edit = memnew(LineEdit);
	filter_edit->set_placeholder(TTR("Filter rows"));
	filter_edit->set_clear_button_enabled(true);
	filter_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	filter_edit->connect(SceneStringName(text_changed), callable_mp(this, &DatabaseEditor::_filter_changed));
	toolbar->add_child(filter_edit);

	revert_button = memnew(Button(TTR("Revert")));
	revert_button->set_tooltip_text(TTR("Discard the pending edits of the database (can be undone)."));
	revert_button->connect(SceneStringName(pressed), callable_mp(this, &DatabaseEditor::_revert));
	toolbar->add_child(revert_button);
	save_button = memnew(Button(TTR("Save")));
	save_button->connect(SceneStringName(pressed), callable_mp(this, &DatabaseEditor::_save));
	toolbar->add_child(save_button);

	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(SIZE_EXPAND_FILL);
	main_vbox->add_child(split);

	side_bar = memnew(Tree);
	side_bar->set_custom_minimum_size(Size2(200 * EDSCALE, 0));
	side_bar->set_hide_root(true);
	side_bar->connect(SceneStringName(item_selected), callable_mp(this, &DatabaseEditor::_side_bar_selected));
	side_bar->connect("button_clicked", callable_mp(this, &DatabaseEditor::_side_bar_button_clicked));
	split->add_child(side_bar);

	tree = memnew(Tree);
	tree->set_h_size_flags(SIZE_EXPAND_FILL);
	tree->set_hide_root(true);
	tree->set_select_mode(Tree::SELECT_MULTI);
	tree->connect("item_edited", callable_mp(this, &DatabaseEditor::_cell_edited));
	tree->connect("column_title_clicked", callable_mp(this, &DatabaseEditor::_sort));
	tree->connect(SceneStringName(gui_input), callable_mp(this, &DatabaseEditor::_tree_gui_input));
	split->add_child(tree);
	// Shown on the empty table.
	empty_label = memnew(Label);
	empty_label->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	empty_label->set_mouse_filter(MOUSE_FILTER_IGNORE);
	empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	tree->add_child(empty_label);

	HBoxContainer *footer = memnew(HBoxContainer);
	main_vbox->add_child(footer);
	status_label = memnew(Label);
	status_label->set_h_size_flags(SIZE_EXPAND_FILL);
	status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	footer->add_child(status_label);
	previous_button = memnew(Button(TTR("Previous")));
	previous_button->connect(SceneStringName(pressed), callable_mp(this, &DatabaseEditor::_change_page).bind(-1));
	footer->add_child(previous_button);
	page_label = memnew(Label);
	footer->add_child(page_label);
	next_button = memnew(Button(TTR("Next")));
	next_button->connect(SceneStringName(pressed), callable_mp(this, &DatabaseEditor::_change_page).bind(1));
	footer->add_child(next_button);

	close_prompt = memnew(ConfirmationDialog);
	close_prompt->set_title(TTR("Unsaved Edits"));
	close_prompt->set_ok_button_text(TTR("Save"));
	close_prompt->add_button(TTR("Discard"), DisplayServer::get_singleton()->get_swap_cancel_ok(), "discard");
	close_prompt->connect(SceneStringName(confirmed), callable_mp(this, &DatabaseEditor::_close_saving));
	close_prompt->connect("custom_action", callable_mp(this, &DatabaseEditor::_close_discarding).unbind(1));
	add_child(close_prompt);

	sql_dialog = memnew(ConfirmationDialog);
	sql_dialog->set_title(TTR("Create with SQL"));
	sql_dialog->set_ok_button_text(TTR("Run"));
	sql_dialog->set_hide_on_ok(false); // Stays open on an SQL error.
	VBoxContainer *sql_vbox = memnew(VBoxContainer);
	sql_dialog->add_child(sql_vbox);
	sql_edit = memnew(TextEdit);
	sql_edit->set_custom_minimum_size(Size2(0, 160) * EDSCALE);
	sql_edit->set_v_size_flags(SIZE_EXPAND_FILL);
	sql_vbox->add_child(sql_edit);
	sql_status = memnew(Label);
	sql_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	sql_vbox->add_child(sql_status);
	sql_dialog->connect(SceneStringName(confirmed), callable_mp(this, &DatabaseEditor::_create_confirmed));
	add_child(sql_dialog);

	rename_dialog = memnew(ConfirmationDialog);
	rename_dialog->set_title(TTR("Rename Table"));
	rename_edit = memnew(LineEdit);
	rename_dialog->add_child(rename_edit);
	rename_dialog->register_text_enter(rename_edit);
	rename_dialog->connect(SceneStringName(confirmed), callable_mp(this, &DatabaseEditor::_rename_confirmed));
	add_child(rename_dialog);

	delete_dialog = memnew(ConfirmationDialog);
	delete_dialog->set_title(TTR("Delete"));
	delete_dialog->set_ok_button_text(TTR("Delete"));
	delete_dialog->connect(SceneStringName(confirmed), callable_mp(this, &DatabaseEditor::_delete_confirmed));
	add_child(delete_dialog);

	query_dialog = memnew(AcceptDialog);
	query_dialog->set_ok_button_text(TTR("Run"));
	query_dialog->set_hide_on_ok(false);
	VBoxContainer *query_vbox = memnew(VBoxContainer);
	query_dialog->add_child(query_vbox);
	query_edit = memnew(TextEdit);
	query_edit->set_custom_minimum_size(Size2(0, 100) * EDSCALE);
	query_vbox->add_child(query_edit);
	query_tree = memnew(Tree);
	query_tree->set_hide_root(true);
	query_tree->set_column_titles_visible(true);
	query_tree->set_v_size_flags(SIZE_EXPAND_FILL);
	query_vbox->add_child(query_tree);
	query_status = memnew(Label);
	query_status->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	query_vbox->add_child(query_status);
	query_dialog->connect(SceneStringName(confirmed), callable_mp(this, &DatabaseEditor::_run_query));
	query_edit->connect(SceneStringName(gui_input), callable_mp(this, &DatabaseEditor::_query_gui_input));
	query_edit->set_tooltip_text(TTR("Ctrl+Enter runs the query."));
	add_child(query_dialog);
}

DatabaseEditor::~DatabaseEditor() {
	for (KeyValue<String, OpenDatabase> &E : databases) {
		memdelete(E.value.undo_redo);
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}
