/**************************************************************************/
/*  dataset_editor.h                                                      */
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
#include "editor/docks/editor_dock.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/plugins/editor_plugin.h"

class Button;
class ConfirmationDialog;
class EditorFileDialog;
class EditorResourcePicker;
class ItemList;
class Label;
class LineEdit;
class MenuButton;
class OptionButton;
class TextEditorBase;
class Timer;
class Tree;

// Edits tabular data files in a bottom dock (#420): data CSVs (typed cells and issues from a DataSchema) and the
// table-shaped data of the other formats (#438): arrays of dictionaries in JSON, YAML and TOML (at the root or
// under a top-level key), INI sections, and the children of an XML root element with one tag (attributes as
// columns). Translation CSVs and localization catalogs keep their own table (CSVTableEditor).
// A side bar lists the open files; each file has its own undo history. Only one page of rows has tree items,
// so large files stay fast. Edits are shown in an open text editor of the same file right away.
class DatasetEditor : public EditorDock {
	GDCLASS(DatasetEditor, EditorDock);

public:
	// The shape of a table in a data file.
	enum Source {
		SOURCE_NONE,
		SOURCE_CSV, // The rows of a CSV, edited through its cell, row and column API.
		SOURCE_RECORDS, // An array of dictionaries (JSON, YAML, TOML), one row per dictionary.
		SOURCE_SECTIONS, // INI sections, one row per section, keys as columns.
		SOURCE_ELEMENTS, // The children of the XML root element with one tag, attributes as columns.
	};

	// A table found in a data file: its shape, the key (SOURCE_RECORDS under a top-level key) or tag
	// (SOURCE_ELEMENTS) it is found by, and its number of rows.
	struct Table {
		Source source = SOURCE_NONE;
		Variant key;
		int row_count = 0;
	};

	static constexpr int PAGE_SIZE = 500;
	// Larger files open as text only on double-click; "Open in Dataset" opens them anyway (#420).
	static constexpr int64_t AUTO_OPEN_SIZE = 4 * 1024 * 1024;

private:
	enum ValuesMenu {
		VALUES_ADD_ROW,
		VALUES_INSERT_ROW,
		VALUES_DELETE_ROWS,
		VALUES_INSERT_COLUMN,
		VALUES_REMOVE_COLUMN,
		VALUES_UNDO,
		VALUES_REDO,
	};

	enum TableMenu {
		TABLE_SAVE,
		TABLE_EXPORT_CSV,
		TABLE_EXPORT_JSON,
		TABLE_OPEN_TEXT,
		TABLE_CLOSE,
		TABLE_CLOSE_ALL,
	};

	// Below the scene and global histories (positive, 0) and the remote and invalid histories (-9, -99).
	static constexpr int FIRST_HISTORY_ID = -1000;

	struct OpenFile {
		Ref<Resource> resource;
		int history_id = 0;
		int page = 0;
		Variant table_key; // Of the shown table, see Table.
		Ref<DataSchema> schema; // CSV only.
		uint32_t reloaded_version = UINT32_MAX; // Version of the text editor's text right after the dock reloaded it.
	};

	static inline DatasetEditor *singleton = nullptr;

	LocalVector<OpenFile> open_files;
	int current = -1;
	int next_history_id = FIRST_HISTORY_ID;

	// The shown table, rebuilt by _update().
	Ref<CSV> csv;
	Source source = SOURCE_NONE;
	Variant table_key;
	Array records; // SOURCE_RECORDS, SOURCE_SECTIONS: field dictionaries; SOURCE_ELEMENTS: whole elements.
	PackedStringArray section_names; // SOURCE_SECTIONS.
	Array column_keys; // Field names in first-seen order (not SOURCE_CSV).
	LocalVector<Variant::Type> column_types; // Type of the first value of each column (not SOURCE_CSV).
	HashMap<int64_t, String> cell_issues; // (row << 32) | tree column -> messages.
	uint32_t rendered_hash = 0;
	bool resource_saved = false;
	bool updating = false;
	bool update_queued = false;
	bool push_text = false; // The dock changed the resource: show the change in its text editor.
	int export_format = TABLE_EXPORT_CSV;

	ItemList *file_list = nullptr;
	MenuButton *values_menu = nullptr;
	MenuButton *table_menu = nullptr;
	OptionButton *table_option = nullptr;
	Label *schema_label = nullptr;
	EditorResourcePicker *schema_picker = nullptr;
	Button *previous_button = nullptr;
	Button *next_button = nullptr;
	Label *page_label = nullptr;
	Label *status_label = nullptr;
	Label *empty_label = nullptr;
	Tree *tree = nullptr;
	Timer *validate_timer = nullptr;
	ConfirmationDialog *column_dialog = nullptr;
	LineEdit *column_name_edit = nullptr;
	EditorFileDialog *export_dialog = nullptr;

	static Variant _get_resource_data(const Resource *p_resource);
	static bool _is_records(const Variant &p_value);

	OpenFile *_get_current_file();
	Resource *_get_resource() const;
	bool _is_json() const;
	TextEditorBase *_get_text_editor() const;
	String _get_text() const;
	String _encode(const Variant &p_data) const;
	Ref<DataField> _get_field(int p_column) const;
	Dictionary _get_fields(int p_row) const;
	void _set_fields(Array &r_records, int p_row, const Dictionary &p_fields) const;
	Variant::Type _get_column_type(int p_column) const;
	Variant _build_data(const Array &p_records, const PackedStringArray &p_names) const;
	int _get_row_count() const;
	int _get_column_count() const;
	String _get_cell_text(int p_row, int p_column) const;
	Array _get_cells(int p_row, int p_column, int p_rows, int p_columns) const;
	int _get_selected_column() const;

	void _create_action(const String &p_action);
	bool _can_edit();
	void _commit_records(const String &p_action, const Array &p_records, const PackedStringArray &p_names);
	void _commit_cells(const String &p_action, int p_row, int p_column, const Array &p_cells);
	void _add_row(bool p_insert);
	void _delete_rows();
	void _rename_section(int p_row, const String &p_name);
	void _insert_column();
	void _remove_column();
	void _undo_redo(bool p_redo);
	void _copy();
	void _paste();
	void _save();
	void _export(const String &p_path);
	void _open_text();
	void _close_file(int p_index);

	void _build_view();
	void _update_file_list();
	void _resource_changed();
	void _queue_update();
	void _update();
	void _render();
	void _validate();
	void _cell_edited();
	void _values_menu_id_pressed(int p_id);
	void _table_menu_id_pressed(int p_id);
	void _table_selected(int p_index);
	void _file_selected(int p_index);
	void _file_clicked(int p_index, const Vector2 &p_position, int p_mouse_button);
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
	static DatasetEditor *get_singleton() { return singleton; }
	// The tables of a data file: a data CSV, or the table-shaped data of JSON, YAML, TOML, INI and XML.
	static Vector<Table> find_tables(const Resource *p_resource);
	static bool can_edit(const Object *p_object);
	// Opened with the file on double-click: everything can_edit() takes except INI (the INI dock edits it) and
	// files above AUTO_OPEN_SIZE.
	static bool can_open_automatically(const Object *p_object);
	// The text of a cell: strings as they are, JSON numbers without ".0" when whole, other values as JSON
	// (`p_json`) or Variant text.
	static String value_to_text(const Variant &p_value, bool p_json);
	// Reads a cell back as `p_type` (the type of the old value or of the column; NIL: taken from the text).
	// False when the text is not a value of that type.
	static bool text_to_value(const String &p_text, Variant::Type p_type, bool p_json, Variant &r_value);

	// Opens the resource in the side bar (or selects it there) and shows it.
	void edit(const Ref<Resource> &p_resource);

	DatasetEditor();
	~DatasetEditor();
};

// "Open as Text" and "Open in Dataset" in the context menu of a data file in the FileSystem dock (#420).
class EditorContextMenuPluginDataset : public EditorContextMenuPlugin {
	GDCLASS(EditorContextMenuPluginDataset, EditorContextMenuPlugin);

	void _open_text(const Dictionary &p_data);
	void _open_dataset(const Dictionary &p_data);

public:
	using EditorContextMenuPlugin::get_options;
	virtual void get_options(const OptionsData &p_data) override;
};

class DatasetEditorPlugin : public EditorPlugin {
	GDCLASS(DatasetEditorPlugin, EditorPlugin);

	DatasetEditor *dataset_editor = nullptr;
	Ref<EditorContextMenuPluginDataset> context_menu_plugin;

public:
	virtual String get_plugin_name() const override { return "Dataset"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual bool can_auto_hide() const override { return false; }
	virtual void make_visible(bool p_visible) override;

	DatasetEditorPlugin();
};
