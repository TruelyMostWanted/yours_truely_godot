/**************************************************************************/
/*  csv_table_editor.h                                                    */
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

#include "core/io/csv_format.h"
#include "core/io/data_schema.h"
#include "core/string/localization_catalog.h"
#include "core/templates/hash_set.h"
#include "editor/docks/editor_dock.h"
#include "editor/plugins/editor_plugin.h"

class Button;
class EditorLocaleDialog;
class EditorResourcePicker;
class Label;
class LineEdit;
class PopupMenu;
class TextEditorBase;
class TextFile;
class Timer;
class Tree;

// The "Localization Table" bottom dock: edits translation CSVs and localization catalogs as a table.
// Only one page of rows has tree items, so large files stay fast. Data CSVs open in the Dataset dock
// (DatasetEditor, #436). Translation CSVs (a translation header or import, or opened as TextFile) get the
// issues of CSV.validate_translation(); a TextFile CSV without a translation header keeps typed cells from a
// DataSchema. Edits go through EditorUndoRedoManager and are shown in an open
// text editor of the same file right away. Localization catalogs (#346) use the same table: one row per key, one
// column per entry field (key, context, plural key or type, locales, notes), plural forms as child rows.
class CSVTableEditor : public EditorDock {
	GDCLASS(CSVTableEditor, EditorDock);

	static constexpr int CSV_TABLE_PAGE_ROWS = 500;
	static inline CSVTableEditor *singleton = nullptr;

	Ref<CSV> csv;
	Ref<TextFile> text_file; // Translation CSVs: the TextFile of the script editor, kept in sync with `csv`.
	String synced_text; // Text of `text_file` that `csv` was parsed from or written to.
	uint32_t reloaded_version = UINT32_MAX; // Version of the text editor's text right after the table reloaded it.
	Ref<DataSchema> schema;
	bool translation_mode = false;
	Ref<LocalizationCatalog> catalog;
	PackedStringArray catalog_fields; // Tree column - 1 -> entry field (`_key`, `_context`, `_plural`, locale, `_<note>`).
	uint64_t catalog_modified_time = 0; // Of the catalog file when loaded or saved by the table.
	HashMap<int64_t, String> cell_issues; // (row << 32) | tree column -> messages.
	HashSet<int64_t> missing_cells; // Keys of `cell_issues` that are missing translations: gaps, not errors (#346).
	bool resource_saved = false; // Set by _resource_saved() during an in-place save.
	int page = 0;
	bool updating = false;
	bool update_queued = false;
	String edited_path; // Path of the edited file, followed when it is moved.

	Label *file_label = nullptr;
	EditorResourcePicker *schema_picker = nullptr;
	Button *previous_button = nullptr;
	Button *next_button = nullptr;
	Label *page_label = nullptr;
	Label *status_label = nullptr;
	Button *add_button = nullptr;
	Button *insert_button = nullptr;
	Button *remove_button = nullptr;
	Button *add_locale_button = nullptr;
	Button *remove_locale_button = nullptr;
	EditorLocaleDialog *locale_dialog = nullptr;
	PopupMenu *header_menu = nullptr; // Locale column header context menu.
	LineEdit *column_edit = nullptr; // Name of a note column.
	Button *add_column_button = nullptr;
	Tree *tree = nullptr;
	Timer *validate_timer = nullptr;

	TextEditorBase *_get_text_editor() const;
	Ref<DataField> _get_field(int p_column) const;
	void _sync_from_text_file();
	bool _can_edit();
	void _commit_cells(const String &p_action, int p_row, int p_column, const Array &p_cells, int p_form = 0);
	Array _get_cells(int p_row, int p_column, int p_rows, int p_columns) const;

	void _update_catalog_fields();
	String _get_catalog_cell(const Dictionary &p_entry, int p_field, int p_form) const;
	void _commit_catalog_cells(const String &p_action, int p_row, int p_field, const Array &p_cells, int p_form);
	void _render_catalog();
	void _add_catalog_column();
	void _add_catalog_locale(const String &p_locale);
	void _remove_catalog_locale(const String &p_locale);
	String _get_locale_of_column(int p_tree_column) const;
	void _remove_selected_locale();
	void _column_title_clicked(int p_column, int p_mouse_button);
	void _header_menu_id_pressed(int p_id);
	bool _can_drop_resource(const Point2 &p_point, const Variant &p_data) const;
	void _drop_resource(const Point2 &p_point, const Variant &p_data);
	void _filesystem_changed();

	void _csv_changed();
	void _update();
	void _render();
	void _validate();
	void _cell_edited();
	void _add_row(bool p_insert);
	void _remove_rows();
	void _copy();
	void _paste();
	void _save();
	void _resource_saved(Object *p_resource);
	void _update_column_widths();
	void _change_page(int p_delta);
	void _schema_changed(const Ref<Resource> &p_schema);
	void _tree_gui_input(const Ref<InputEvent> &p_event);
	void _file_moved(const String &p_old_file, const String &p_new_file);
	void _file_removed(const String &p_file);

protected:
	void _notification(int p_what);

public:
	static CSVTableEditor *get_singleton() { return singleton; }
	// A CSV imported as translation, or whose header columns after the first are all locales, notes (`_`) or
	// `?context`/`?plural` with at least one known language. Other CSVs are data for the Dataset dock.
	static bool is_translation_csv(const CSV *p_csv);
	// LocalizationCatalog resources, translation CSVs, and CSVs opened as TextFile.
	static bool can_edit(const Object *p_object);

	void edit(const Ref<Resource> &p_resource);

	CSVTableEditor();
	~CSVTableEditor();
};

class CSVTableEditorPlugin : public EditorPlugin {
	GDCLASS(CSVTableEditorPlugin, EditorPlugin);

	CSVTableEditor *table_editor = nullptr;

public:
	virtual String get_plugin_name() const override { return "CSVTable"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	// The dock stays open while other objects are edited; it closes with its close button.
	virtual bool can_auto_hide() const override { return false; }
	virtual void make_visible(bool p_visible) override;

	CSVTableEditorPlugin();
};
