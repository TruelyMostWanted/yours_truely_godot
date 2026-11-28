/**************************************************************************/
/*  sqlite_editor_plugin.cpp                                              */
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

#include "sqlite_editor_plugin.h"

#include "../sqlite_database.h"
#include "database_editor.h"

#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/tree.h"
#include "scene/scene_string_names.h"

enum {
	COLUMN_TABLE,
	COLUMN_COLUMNS,
	COLUMN_ROWS,
	COLUMN_MAX,
};

// Above this file size, rows are counted on demand: COUNT(*) reads the whole table.
static constexpr int64_t ROW_COUNT_SIZE_LIMIT = 16 * 1024 * 1024;

// SQLiteTableOverview

void SQLiteTableOverview::_update() {
	tree->clear();

	// An own read-only connection, closed right after reading: the file stays unchanged and unlocked.
	Ref<SQLiteDatabase> database;
	database.instantiate();
	if (database->open(path, SQLiteDatabase::OPEN_MODE_READ_ONLY) != OK) {
		summary_label->set_text(database->get_last_error_message());
		return;
	}

	const int64_t file_size = FileAccess::get_size(path);
	const bool count_on_demand = file_size > ROW_COUNT_SIZE_LIMIT;
	const PackedStringArray tables = database->get_table_names();
	TreeItem *root = tree->create_item();
	for (const String &table : tables) {
		const TypedArray<SQLiteColumn> columns = database->get_columns(table);
		PackedStringArray lines;
		for (int i = 0; i < columns.size(); i++) {
			const Ref<SQLiteColumn> column = columns[i];
			String line = column->get_name();
			if (!column->get_declared_type().is_empty()) {
				line += " " + column->get_declared_type();
			}
			if (column->get_primary_key_index() > 0) {
				line += " PRIMARY KEY";
			}
			if (column->is_not_null()) {
				line += " NOT NULL";
			}
			if (!column->get_default_value().is_empty()) {
				line += " DEFAULT " + column->get_default_value();
			}
			lines.push_back(line);
		}

		TreeItem *item = tree->create_item(root);
		item->set_text(COLUMN_TABLE, table);
		item->set_text(COLUMN_COLUMNS, itos(columns.size()));
		item->set_text(COLUMN_ROWS, count_on_demand ? String("?") : itos(database->get_row_count(table)));
		String tooltip = String("\n").join(lines);
		if (count_on_demand) {
			tooltip += "\n\n" + TTR("Double-click to count the rows.");
		}
		for (int tree_column = 0; tree_column < COLUMN_MAX; tree_column++) {
			item->set_tooltip_text(tree_column, tooltip);
		}
	}
	database->close();

	summary_label->set_text(vformat(TTR("%d tables, %s."), tables.size(), String::humanize_size(file_size)));
}

void SQLiteTableOverview::_count_rows() {
	TreeItem *item = tree->get_selected();
	if (item == nullptr) {
		return;
	}
	Ref<SQLiteDatabase> database;
	database.instantiate();
	if (database->open(path, SQLiteDatabase::OPEN_MODE_READ_ONLY) == OK) {
		item->set_text(COLUMN_ROWS, itos(database->get_row_count(item->get_text(COLUMN_TABLE))));
	}
}

void SQLiteTableOverview::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			refresh_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
			_update();
		} break;
	}
}

SQLiteTableOverview::SQLiteTableOverview(const String &p_path) {
	path = p_path;

	summary_label = memnew(Label);
	summary_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	summary_label->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	add_child(summary_label);

	// No dock button: the Database dock opens with the file (SQLiteEditorPlugin::handles(), #464).
	refresh_button = memnew(Button(TTR("Refresh")));
	refresh_button->set_tooltip_text(TTR("Read the tables of the file again."));
	refresh_button->set_h_size_flags(SIZE_SHRINK_BEGIN);
	refresh_button->connect(SceneStringName(pressed), callable_mp(this, &SQLiteTableOverview::_update));
	add_child(refresh_button);

	tree = memnew(Tree);
	tree->set_columns(COLUMN_MAX);
	tree->set_hide_root(true);
	tree->set_column_titles_visible(true);
	tree->set_column_title(COLUMN_TABLE, TTR("Table"));
	tree->set_column_title(COLUMN_COLUMNS, TTR("Columns"));
	tree->set_column_title(COLUMN_ROWS, TTR("Rows"));
	tree->set_column_expand(COLUMN_TABLE, true);
	tree->set_column_clip_content(COLUMN_TABLE, true);
	for (int column = COLUMN_COLUMNS; column < COLUMN_MAX; column++) {
		tree->set_column_expand(column, false);
		tree->set_column_custom_minimum_width(column, 72 * EDSCALE);
	}
	tree->set_custom_minimum_size(Size2(0, 200) * EDSCALE);
	tree->connect("item_activated", callable_mp(this, &SQLiteTableOverview::_count_rows));
	add_child(tree);
}

// EditorInspectorPluginSQLite

bool EditorInspectorPluginSQLite::can_handle(Object *p_object) {
	return Object::cast_to<SQLiteDatabase>(p_object) != nullptr;
}

void EditorInspectorPluginSQLite::parse_begin(Object *p_object) {
	const SQLiteDatabase *database = Object::cast_to<SQLiteDatabase>(p_object);
	ERR_FAIL_NULL(database);
	// The tables live in a database file; is_built_in() is also true without a path.
	if (!database->is_built_in() && ResourceFormatLoaderSQLite::is_database_path(database->get_path())) {
		add_custom_control(memnew(SQLiteTableOverview(database->get_path())));
		return;
	}
	Label *hint = memnew(Label);
	hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	hint->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	hint->set_text(TTR("This database has no database file yet. Save it as .db, .sqlite, .sqlite3 or .db3 to create an empty database, then select the file to see and edit its tables."));
	add_custom_control(hint);
}

// SQLiteEditorPlugin

void SQLiteEditorPlugin::edit(Object *p_object) {
	// The dock stays at the last database when something else is edited (ARCHITECTURE.md 7.3).
	if (handles(p_object)) {
		database_editor->edit(Object::cast_to<SQLiteDatabase>(p_object)->get_path());
	}
}

bool SQLiteEditorPlugin::handles(Object *p_object) const {
	// Double-click on a database file; a database without a file has no tables to edit.
	const SQLiteDatabase *database = Object::cast_to<SQLiteDatabase>(p_object);
	return database && !database->is_built_in() && ResourceFormatLoaderSQLite::is_database_path(database->get_path());
}

void SQLiteEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		database_editor->make_visible();
	}
}

String SQLiteEditorPlugin::get_unsaved_status(const String &p_for_scene) const {
	// The edits belong to database files, not to scenes: asked only when the editor quits.
	if (!p_for_scene.is_empty()) {
		return String();
	}
	const PackedStringArray paths = database_editor->get_unsaved_paths();
	return paths.is_empty() ? String() : TTR("Save the pending edits of these databases before closing?") + "\n" + String("\n").join(paths);
}

void SQLiteEditorPlugin::save_external_data() {
	// A failed save keeps the edits pending, so get_unsaved_status() asks again; the dock shows why.
	if (!database_editor->save_all()) {
		database_editor->make_visible();
	}
}

SQLiteEditorPlugin::SQLiteEditorPlugin() {
	Ref<EditorInspectorPluginSQLite> plugin;
	plugin.instantiate();
	add_inspector_plugin(plugin);

	database_editor = memnew(DatabaseEditor);
	database_editor->set_custom_minimum_size(Size2(0, 250) * EDSCALE);
	EditorDockManager::get_singleton()->add_dock(database_editor);
	database_editor->close();
}
