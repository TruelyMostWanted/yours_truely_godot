/**************************************************************************/
/*  csv_table_editor.cpp                                                  */
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

#include "csv_table_editor.h"

#include "core/config/project_settings.h"
#include "core/input/input_event.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/string/plural_rules.h"
#include "core/string/translation_server.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/script/data_schema_lookup.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/themes/editor_scale.h"
#include "editor/translations/editor_locale_dialog.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "scene/resources/text_file.h"
#include "servers/display/display_server.h"

// The row of column issues in `cell_issues` (shown in the column header).
static constexpr int HEADER_ROW = INT32_MAX;

static int64_t _cell_key(int p_row, int p_tree_column) {
	return ((int64_t)p_row << 32) | (uint32_t)p_tree_column;
}

static Ref<SerializerOptions> _tsv_options() {
	Ref<SerializerOptions> options;
	options.instantiate();
	options->set_csv_delimiter("\t");
	options->set_csv_headers(false);
	return options;
}

// The entry of a catalog row with its key and context (get_entry() lists them only for keys with a context).
static Dictionary _get_catalog_entry(const Ref<LocalizationCatalog> &p_catalog, int p_row) {
	const StringName key = p_catalog->get_key(p_row);
	const StringName context = p_catalog->get_key_context(p_row);
	Dictionary entry = p_catalog->get_entry(key, context);
	entry["_key"] = String(key);
	entry["_context"] = String(context);
	return entry;
}

static bool _is_forms(const Variant &p_value) {
	return p_value.get_type() == Variant::ARRAY || p_value.get_type() == Variant::PACKED_STRING_ARRAY;
}

TextEditorBase *CSVTableEditor::_get_text_editor() const {
	const Ref<Resource> edited = text_file.is_valid() ? Ref<Resource>(text_file) : Ref<Resource>(csv);
	return Object::cast_to<TextEditorBase>(ScriptEditor::get_singleton()->get_script_container()->get_resource_editor(edited));
}

Ref<DataField> CSVTableEditor::_get_field(int p_column) const {
	if (schema.is_null()) {
		return Ref<DataField>();
	}
	const PackedStringArray header = csv->get_header();
	if (!header.is_empty()) {
		return p_column < header.size() ? schema->get_field(header[p_column]) : Ref<DataField>();
	}
	const TypedArray<DataField> fields = schema->get_fields();
	return p_column < fields.size() ? Ref<DataField>(fields[p_column]) : Ref<DataField>();
}

void CSVTableEditor::_sync_from_text_file() {
	if (text_file.is_valid() && text_file->get_text() != synced_text) {
		synced_text = text_file->get_text();
		csv->parse(synced_text, true);
	}
}

bool CSVTableEditor::_can_edit() {
	if (catalog.is_valid()) {
		return true; // Edited as a resource, no text editor to keep in sync.
	}
	TextEditorBase *text_editor = _get_text_editor();
	const TextEdit *text_edit = text_editor ? Object::cast_to<TextEdit>(text_editor->get_base_editor()) : nullptr;
	if (text_file.is_valid()) {
		// A TextFile is only updated when its text editor saves: unsaved text must not be overwritten.
		if (text_edit && text_edit->get_text() != text_file->get_text()) {
			status_label->set_text(TTR("The text editor has unsaved changes of this file. Save or discard them before editing the table."));
			return false;
		}
		_sync_from_text_file();
	} else if (text_edit) {
		// Takes over text typed since the text editor's last idle parse, so the table edits the current text.
		const bool in_sync = csv->get_parsed_text().is_empty() ? text_edit->get_version() == reloaded_version : text_edit->get_text() == csv->get_parsed_text();
		if (!in_sync) {
			csv->parse(text_edit->get_text(), true);
		}
	}
	if (!csv->get_error_message().is_empty()) {
		status_label->set_text(vformat(TTR("Fix the CSV text first: %s"), csv->get_error_message()));
		return false;
	}
	return true;
}

void CSVTableEditor::_commit_cells(const String &p_action, int p_row, int p_column, const Array &p_cells, int p_form) {
	if (catalog.is_valid()) {
		_commit_catalog_cells(p_action, p_row, p_column, p_cells, p_form);
		return;
	}
	if (!_can_edit()) {
		_render();
		return;
	}
	const int row_count = csv->get_row_count();
	ERR_FAIL_INDEX(p_row, row_count + 1);
	int width = 0;
	for (int i = 0; i < p_cells.size(); i++) {
		width = MAX(width, (int)PackedStringArray(p_cells[i]).size());
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action);
	undo_redo->add_do_method(csv.ptr(), "set_cells", p_row, p_column, p_cells);
	undo_redo->add_undo_method(csv.ptr(), "set_cells", p_row, p_column, csv->get_cells(p_row, p_column, p_cells.size(), width));
	for (int i = row_count; i < p_row + p_cells.size(); i++) {
		undo_redo->add_undo_method(csv.ptr(), "remove_row", row_count);
	}
	undo_redo->commit_action();
}

Array CSVTableEditor::_get_cells(int p_row, int p_column, int p_rows, int p_columns) const {
	if (catalog.is_null()) {
		return csv->get_cells(p_row, p_column, p_rows, p_columns);
	}
	Array cells;
	for (int row = p_row; row < p_row + p_rows && row < catalog->get_key_count(); row++) {
		const Dictionary entry = _get_catalog_entry(catalog, row);
		PackedStringArray line;
		for (int field = p_column; field < p_column + p_columns && field < catalog_fields.size(); field++) {
			line.push_back(_get_catalog_cell(entry, field, 0));
		}
		cells.push_back(line);
	}
	return cells;
}

void CSVTableEditor::_update_catalog_fields() {
	catalog_fields.clear();
	catalog_fields.push_back("_key");
	catalog_fields.push_back("_context");
	catalog_fields.push_back(catalog->is_class("TextLocalizationCatalog") ? "_plural" : "_type");
	catalog_fields.append_array(catalog->get_locales());
	for (const String &note_column : catalog->get_note_columns()) {
		catalog_fields.push_back("_" + note_column);
	}
}

String CSVTableEditor::_get_catalog_cell(const Dictionary &p_entry, int p_field, int p_form) const {
	const Variant value = p_entry.get(catalog_fields[p_field], String());
	if (_is_forms(value)) {
		const PackedStringArray forms = value;
		return p_form < forms.size() ? forms[p_form] : String();
	}
	return p_form == 0 ? String(value) : String();
}

void CSVTableEditor::_commit_catalog_cells(const String &p_action, int p_row, int p_field, const Array &p_cells, int p_form) {
	// Collected first: an action is only created when something changes.
	struct Change {
		String old_key;
		String old_context;
		String key;
		String context;
		Dictionary old_entry;
		Dictionary entry;
	};
	LocalVector<Change> changes;
	bool refused = false;
	for (int i = 0; i < p_cells.size() && p_row + i < catalog->get_key_count(); i++) {
		const PackedStringArray cells = p_cells[i];
		const Dictionary old_entry = _get_catalog_entry(catalog, p_row + i);
		Dictionary entry = old_entry.duplicate(true);
		for (int c = 0; c < cells.size() && p_field + c < catalog_fields.size(); c++) {
			const String &field = catalog_fields[p_field + c];
			const Variant value = entry.get(field, Variant());
			if (_is_forms(value) || p_form > 0) {
				// A plural form: the cell of a child row, or the first form of a key with plural forms.
				PackedStringArray forms;
				if (value.get_type() == Variant::STRING) {
					forms.push_back(value);
				} else if (_is_forms(value)) {
					forms = value;
				}
				if (forms.size() <= p_form) {
					forms.resize(p_form + 1);
				}
				forms.set(p_form, cells[c]);
				entry[field] = forms;
			} else if (cells[c].is_empty()) {
				entry.erase(field);
			} else {
				entry[field] = cells[c];
			}
		}
		Change change;
		change.old_entry = old_entry;
		change.old_key = old_entry.get("_key", String());
		change.old_context = old_entry.get("_context", String());
		// A cleared key or context cell erased its field: read with a default, not as NIL ("<null>").
		change.key = entry.get("_key", String());
		change.context = entry.get("_context", String());
		if (change.key.is_empty()) {
			status_label->set_text(TTR("A key can't be empty."));
			refused = true;
			continue;
		}
		if ((change.key != change.old_key || change.context != change.old_context) && catalog->has_key(change.key, change.context)) {
			status_label->set_text(vformat(TTR("The key \"%s\" with this context exists already."), change.key));
			refused = true;
			continue;
		}
		change.entry = entry;
		changes.push_back(change);
	}
	if (refused) {
		// The tree still shows the refused text.
		callable_mp(this, &CSVTableEditor::_render).call_deferred();
	}
	if (changes.is_empty()) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action);
	for (const Change &change : changes) {
		if (change.key == change.old_key && change.context == change.old_context) {
			undo_redo->add_do_method(catalog.ptr(), "set_entry", change.key, change.entry, change.context);
			undo_redo->add_undo_method(catalog.ptr(), "set_entry", change.key, change.old_entry, change.context);
			continue;
		}
		// Renamed in place: the row keeps its position (and page).
		undo_redo->add_do_method(catalog.ptr(), "rename_key", change.old_key, change.key, change.old_context, change.context);
		undo_redo->add_do_method(catalog.ptr(), "set_entry", change.key, change.entry, change.context);
		undo_redo->add_undo_method(catalog.ptr(), "set_entry", change.key, change.old_entry, change.context);
		undo_redo->add_undo_method(catalog.ptr(), "rename_key", change.key, change.old_key, change.context, change.old_context);
	}
	undo_redo->commit_action();
}

void CSVTableEditor::_render_catalog() {
	updating = true;
	const int row_count = catalog->get_key_count();
	const int column_count = catalog_fields.size();
	const int page_count = MAX(1, (row_count + PAGE_SIZE - 1) / PAGE_SIZE);
	page = CLAMP(page, 0, page_count - 1);
	const int first = page * PAGE_SIZE;
	const int last = MIN(first + PAGE_SIZE, row_count);

	if (tree->get_columns() != column_count + 1) {
		tree->clear();
		tree->set_columns(column_count + 1);
	}
	tree->set_column_title(0, "#");
	tree->set_column_expand(0, false);
	tree->set_column_custom_minimum_width(0, 60 * EDSCALE);
	for (int field = 0; field < column_count; field++) {
		const String &name = catalog_fields[field];
		String column_title;
		String tooltip;
		if (field == 0) {
			column_title = TTR("Key");
		} else if (field == 1) {
			column_title = TTR("Context");
		} else if (field == 2) {
			column_title = name == "_plural" ? TTR("Plural Key") : TTR("Type");
			tooltip = name == "_plural" ? TTR("The plural key (msgid_plural). Keys with plural forms show one child row per form.") : TTR("The resource type the localized paths must have.");
		} else if (name.begins_with("_")) {
			column_title = vformat(TTR("Note: %s"), name.substr(1));
			tooltip = TTR("Translator note: for translators only, not used at runtime.");
		} else {
			column_title = name;
			tooltip = vformat(TTR("%s: %d of %d translated"), TranslationServer::get_singleton()->get_locale_name(name), catalog->get_translated_count(name), row_count);
		}
		// Column issues without a row, such as an invalid locale (#403).
		const String *column_issue = cell_issues.getptr(_cell_key(HEADER_ROW, field + 1));
		if (column_issue) {
			column_title = "(!) " + column_title;
			tooltip = *column_issue + "\n" + tooltip;
		}
		tree->set_column_title(field + 1, column_title);
		tree->set_column_title_tooltip_text(field + 1, tooltip);
		tree->set_column_clip_content(field + 1, true);
	}
	_update_column_widths();

	// The most plural forms of the locales (their plural rules).
	int plural_form_count = 1;
	const Ref<TextLocalizationCatalog> text_catalog = catalog;
	if (text_catalog.is_valid()) {
		for (const String &locale : text_catalog->get_locales()) {
			const PluralRules *rules = LocalizationCatalog::get_plural_rules(text_catalog->get_plural_rule(locale), locale);
			plural_form_count = MAX(plural_form_count, rules ? rules->get_nplurals() : 2);
		}
	}

	TreeItem *root = tree->get_root() ? tree->get_root() : tree->create_item();
	TreeItem *item = root->get_first_child();
	const Color issue_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.35);
	const Color missing_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.2);
	for (int row = first; row < last; row++) {
		if (!item) {
			item = tree->create_item(root);
		}
		while (TreeItem *child = item->get_first_child()) {
			memdelete(child);
		}
		const Dictionary entry = _get_catalog_entry(catalog, row);
		// A key with a plural key gets the form rows its locales need, so the forms can be filled in (#403).
		int form_count = String(entry.get("_plural", String())).is_empty() ? 1 : plural_form_count;
		for (const KeyValue<Variant, Variant> &kv : entry) {
			if (_is_forms(kv.value)) {
				form_count = MAX(form_count, (int)PackedStringArray(kv.value).size());
			}
		}
		for (int form = 0; form < form_count; form++) {
			TreeItem *form_item = form == 0 ? item : tree->create_item(item);
			form_item->set_text(0, form == 0 ? itos(row + 1) : vformat(TTR("form %d"), form));
			form_item->set_metadata(0, row);
			form_item->set_metadata(1, form);
			form_item->set_selectable(0, false);
			for (int column = 0; column <= column_count; column++) {
				const String *issue = form == 0 ? cell_issues.getptr(_cell_key(row, column)) : nullptr;
				form_item->set_tooltip_text(column, issue ? *issue : String());
				if (issue) {
					form_item->set_custom_bg_color(column, missing_cells.has(_cell_key(row, column)) ? missing_color : issue_color);
				} else {
					form_item->clear_custom_bg_color(column);
				}
				if (column == 0) {
					continue;
				}
				form_item->set_cell_mode(column, TreeItem::CELL_MODE_STRING);
				form_item->set_text(column, _get_catalog_cell(entry, column - 1, form));
				// Child rows hold plural forms: only the locale columns.
				form_item->set_editable(column, form == 0 || (column > 3 && !catalog_fields[column - 1].begins_with("_")));
			}
		}
		item = item->get_next();
	}
	while (item) {
		TreeItem *next = item->get_next();
		memdelete(item);
		item = next;
	}

	page_label->set_text(row_count == 0 ? TTR("No keys") : vformat(TTR("Keys %d-%d of %d"), first + 1, last, row_count));
	previous_button->set_disabled(page == 0);
	next_button->set_disabled(page >= page_count - 1);
	updating = false;
}

void CSVTableEditor::_add_catalog_column() {
	// A translator note column; the `_` is optional. Locales come from the locale dialog.
	const String name = column_edit->get_text().strip_edges().trim_prefix("_");
	if (catalog.is_null() || name.is_empty()) {
		return;
	}
	if (catalog->get_note_columns().has(name) || name == "key" || name == "context" || name == "plural" || name == "type") {
		status_label->set_text(vformat(TTR("\"%s\" is a note column or a reserved name already."), name));
		return;
	}
	// Note columns exist through the notes of keys: added with an empty note of the first key.
	if (catalog->get_key_count() == 0) {
		status_label->set_text(TTR("Add a key before adding a note column."));
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Add Note Column"));
	undo_redo->add_do_method(catalog.ptr(), "set_note", catalog->get_key(0), String(), catalog->get_key_context(0), name);
	undo_redo->add_undo_property(catalog.ptr(), "data", catalog->get("data"));
	undo_redo->commit_action();
	column_edit->clear();
}

void CSVTableEditor::_add_catalog_locale(const String &p_locale) {
	if (catalog.is_null() || p_locale.is_empty()) {
		return;
	}
	if (catalog->has_locale(p_locale)) {
		status_label->set_text(vformat(TTR("The catalog has the locale \"%s\" already."), p_locale));
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Add Locale"));
	undo_redo->add_do_method(catalog.ptr(), "add_locale", p_locale);
	undo_redo->add_undo_method(catalog.ptr(), "remove_locale", p_locale);
	undo_redo->commit_action();
}

void CSVTableEditor::_remove_catalog_locale(const String &p_locale) {
	if (catalog.is_null() || !catalog->has_locale(p_locale)) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(vformat(TTR("Remove Locale %s"), p_locale));
	undo_redo->add_do_method(catalog.ptr(), "remove_locale", p_locale);
	// The whole catalog: its texts and plural forms of the locale come back in their place.
	undo_redo->add_undo_property(catalog.ptr(), "data", catalog->get("data"));
	undo_redo->commit_action();
}

String CSVTableEditor::_get_locale_of_column(int p_tree_column) const {
	const int field = p_tree_column - 1;
	if (catalog.is_null() || field < 3 || field >= catalog_fields.size() || catalog_fields[field].begins_with("_")) {
		return String();
	}
	return catalog_fields[field];
}

void CSVTableEditor::_remove_selected_locale() {
	const String locale = _get_locale_of_column(tree->get_selected_column());
	if (locale.is_empty()) {
		status_label->set_text(TTR("Select a cell of the locale column to remove."));
		return;
	}
	_remove_catalog_locale(locale);
}

void CSVTableEditor::_column_title_clicked(int p_column, int p_mouse_button) {
	const String locale = _get_locale_of_column(p_column);
	if (p_mouse_button != (int)MouseButton::RIGHT || locale.is_empty()) {
		return;
	}
	header_menu->clear();
	header_menu->add_item(vformat(TTR("Remove Locale \"%s\" from Catalog"), locale));
	header_menu->set_item_metadata(0, locale);
	header_menu->set_position(get_screen_position() + get_local_mouse_position());
	header_menu->reset_size();
	header_menu->popup();
}

void CSVTableEditor::_header_menu_id_pressed(int p_id) {
	_remove_catalog_locale(header_menu->get_item_metadata(0));
}

bool CSVTableEditor::_can_drop_resource(const Point2 &p_point, const Variant &p_data) const {
	// Resource catalogs: a resource dropped on a key or locale cell writes its path (#391).
	if (catalog.is_null() || !catalog->is_class("ResourceLocalizationCatalog")) {
		return false;
	}
	TreeItem *item = tree->get_item_at_position(p_point);
	const int column = tree->get_column_at_position(p_point);
	if (!item || column < 1 || !item->is_editable(column)) {
		return false;
	}
	const Dictionary drop_data = p_data;
	const String type = drop_data.get("type", String());
	return (type == "files" && !PackedStringArray(drop_data.get("files", PackedStringArray())).is_empty()) || (type == "resource" && Ref<Resource>(drop_data.get("resource", Variant())).is_valid());
}

void CSVTableEditor::_drop_resource(const Point2 &p_point, const Variant &p_data) {
	if (!_can_drop_resource(p_point, p_data)) {
		return;
	}
	const Dictionary drop_data = p_data;
	String path;
	if (String(drop_data.get("type", String())) == "files") {
		path = PackedStringArray(drop_data["files"])[0];
	} else {
		path = Ref<Resource>(drop_data["resource"])->get_path();
	}
	if (path.is_empty() || !path.is_resource_file()) {
		status_label->set_text(TTR("Only a resource saved in a file has a path."));
		return;
	}
	const TreeItem *item = tree->get_item_at_position(p_point);
	Array cells;
	cells.push_back(PackedStringArray({ path }));
	_commit_cells(TTR("Drop Resource Path"), (int)item->get_metadata(0), tree->get_column_at_position(p_point) - 1, cells, (int)item->get_metadata(1));
}

void CSVTableEditor::_csv_changed() {
	// A message of an earlier action is stale now; the validation that follows writes the status again (#346).
	status_label->set_text(String());
	if (!update_queued) {
		update_queued = true;
		callable_mp(this, &CSVTableEditor::_update).call_deferred();
	}
}

void CSVTableEditor::_update() {
	update_queued = false;
	if (catalog.is_valid()) {
		_update_catalog_fields();
		_render();
		if (validate_timer->is_inside_tree()) {
			validate_timer->start();
		}
		return;
	}
	if (csv.is_null()) {
		return;
	}
	// No kept text: the data was changed by the table, undo/redo or a script, not by parsing the text.
	// Show the change in the text editor, so neither side overwrites the other.
	if (csv->get_parsed_text().is_empty()) {
		const String text = csv->encode_to_text();
		if (text_file.is_valid()) {
			synced_text = text;
			text_file->set_text(synced_text);
		}
		TextEditorBase *text_editor = _get_text_editor();
		const TextEdit *text_edit = text_editor ? Object::cast_to<TextEdit>(text_editor->get_base_editor()) : nullptr;
		// An empty file has no kept text either. Reloading the same text would validate (parse) it again,
		// which emits `changed` and queues the next update: an endless loop.
		if (text_edit && text_edit->get_text() != text) {
			text_editor->reload_text();
			reloaded_version = text_edit->get_version();
		}
	}
	_render();
	if (validate_timer->is_inside_tree()) {
		validate_timer->start();
	}
}

void CSVTableEditor::_render() {
	if (catalog.is_valid()) {
		_render_catalog();
		return;
	}
	if (csv.is_null()) {
		tree->clear();
		return;
	}
	updating = true;
	const int row_count = csv->get_row_count();
	const int column_count = MAX(csv->get_column_count(), 1);
	const int page_count = MAX(1, (row_count + PAGE_SIZE - 1) / PAGE_SIZE);
	page = CLAMP(page, 0, page_count - 1);
	const int first = page * PAGE_SIZE;
	const int last = MIN(first + PAGE_SIZE, row_count);

	const PackedStringArray header = csv->get_header();
	if (tree->get_columns() != column_count + 1) {
		tree->clear();
		tree->set_columns(column_count + 1);
	}
	tree->set_column_title(0, "#");
	tree->set_column_expand(0, false);
	tree->set_column_custom_minimum_width(0, 60 * EDSCALE);
	for (int column = 0; column < column_count; column++) {
		const Ref<DataField> field = _get_field(column);
		tree->set_column_title(column + 1, column < header.size() ? header[column] : itos(column + 1));
		tree->set_column_title_tooltip_text(column + 1, field.is_valid() ? vformat("%s\n%s", Variant::get_type_name(field->get_value_type()), field->get_description()).strip_edges() : String());
		tree->set_column_clip_content(column + 1, true);
	}
	_update_column_widths();

	TreeItem *root = tree->get_root() ? tree->get_root() : tree->create_item();
	TreeItem *item = root->get_first_child();
	const Array rows = csv->get_data();
	const bool editable = csv->get_error_message().is_empty();
	const Color issue_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.35);
	const Color missing_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.2);
	for (int row = first; row < last; row++) {
		if (!item) {
			item = tree->create_item(root);
		}
		const PackedStringArray cells = rows[row];
		item->set_text(0, itos(row + 1));
		item->set_metadata(0, row);
		item->set_selectable(0, false);
		for (int column = 0; column <= column_count; column++) {
			const String *issue = cell_issues.getptr(_cell_key(row, column));
			item->set_tooltip_text(column, issue ? *issue : String());
			if (issue) {
				item->set_custom_bg_color(column, missing_cells.has(_cell_key(row, column)) ? missing_color : issue_color);
			} else {
				item->clear_custom_bg_color(column);
			}
			if (column == 0) {
				continue;
			}
			const String text = column - 1 < cells.size() ? cells[column - 1] : String();
			const Ref<DataField> field = _get_field(column - 1);
			const String lower = text.to_lower();
			// Another value in a bool column stays a text cell, so one click does not overwrite it.
			if (field.is_valid() && field->get_value_type() == Variant::BOOL && (lower.is_empty() || lower == "true" || lower == "false" || lower == "1" || lower == "0")) {
				item->set_cell_mode(column, TreeItem::CELL_MODE_CHECK);
				item->set_checked(column, lower == "true" || lower == "1");
			} else if (field.is_valid() && field->get_hint() == PROPERTY_HINT_ENUM && (field->get_value_type() != Variant::STRING || field->get_hint_string().split(",").has(text))) {
				// Like the inspector: "Name:value" options for numbers, the option index for strings.
				// A string that is no option stays a text cell below, so the wrong value is visible.
				item->set_cell_mode(column, TreeItem::CELL_MODE_RANGE);
				item->set_text(column, field->get_hint_string());
				item->set_range(column, field->get_value_type() == Variant::STRING ? (int)field->get_hint_string().split(",").find(text) : text.to_int());
			} else {
				item->set_cell_mode(column, TreeItem::CELL_MODE_STRING);
				item->set_text(column, text);
			}
			item->set_editable(column, editable);
		}
		item = item->get_next();
	}
	while (item) {
		TreeItem *next = item->get_next();
		memdelete(item);
		item = next;
	}

	page_label->set_text(row_count == 0 ? TTR("No rows") : vformat(TTR("Rows %d-%d of %d"), first + 1, last, row_count));
	previous_button->set_disabled(page == 0);
	next_button->set_disabled(page >= page_count - 1);
	updating = false;
}

void CSVTableEditor::_validate() {
	if (csv.is_null() && catalog.is_null()) {
		return;
	}
	cell_issues.clear();
	missing_cells.clear();
	Ref<DataValidation> result;
	if (catalog.is_valid()) {
		result = catalog->validate();
	} else if (translation_mode) {
		result = csv->validate_translation();
	} else if (schema.is_valid()) {
		result = csv->validate(schema);
	}
	if (result.is_null()) {
		status_label->set_text(csv->get_error_message());
		_render();
		return;
	}

	HashMap<int, int> missing; // Column -> missing translations.
	// Missing translations are gaps, counted per locale below, not issues (#346). In data CSVs they stay issues.
	const bool gaps = catalog.is_valid() || translation_mode;
	int issue_count = 0;
	String first_issue;
	for (int i = 0; i < result->get_issue_count(); i++) {
		const int row = result->get_issue_row(i);
		const int column = result->get_issue_column(i);
		if (row == DataValidation::POSITION_NONE && catalog.is_valid() && catalog_fields.has(result->get_issue_field(i))) {
			// A column issue (an invalid locale): shown in the column header (#403).
			const int64_t key = _cell_key(HEADER_ROW, (int)catalog_fields.find(result->get_issue_field(i)) + 1);
			const String *previous = cell_issues.getptr(key);
			cell_issues[key] = previous ? *previous + "\n" + result->get_issue_message(i) : result->get_issue_message(i);
		} else if (row != DataValidation::POSITION_NONE) {
			// Catalog issues name their locale as field; key issues have none (the row number column).
			const int tree_column = catalog.is_valid() ? (int)catalog_fields.find(result->get_issue_field(i)) + 1 : (column == DataValidation::POSITION_NONE ? 0 : column + 1);
			const int64_t key = _cell_key(row, tree_column);
			const String *previous = cell_issues.getptr(key);
			cell_issues[key] = previous ? *previous + "\n" + result->get_issue_message(i) : result->get_issue_message(i);
			if (gaps && result->get_issue_kind(i) == DataValidation::ISSUE_KIND_EMPTY_REQUIRED) {
				missing_cells.insert(key);
			}
		}
		if (result->get_issue_kind(i) == DataValidation::ISSUE_KIND_EMPTY_REQUIRED) {
			missing[column]++;
		}
		if (!gaps || result->get_issue_kind(i) != DataValidation::ISSUE_KIND_EMPTY_REQUIRED) {
			first_issue = issue_count == 0 ? result->get_issue_message(i) : first_issue;
			issue_count++;
		}
	}

	String status = issue_count == 0 ? TTR("No issues.") : vformat(TTR("%d issue(s). First: %s"), issue_count, first_issue);
	if (catalog.is_valid()) {
		// Counted per locale, not limited like the issues.
		PackedStringArray counts;
		for (const String &locale : catalog->get_locales()) {
			counts.push_back(vformat("%s: %d", locale, catalog->get_key_count() - catalog->get_translated_count(locale)));
		}
		if (!counts.is_empty()) {
			status = vformat(TTR("Missing translations: %s. %s"), String(", ").join(counts), status);
		}
	} else if (translation_mode) {
		CSV::TranslationHeader columns;
		Ref<DataValidation> header_issues;
		header_issues.instantiate();
		CSV::check_translation_header(csv->get_header(), columns, header_issues.ptr());
		PackedStringArray counts;
		for (const KeyValue<int, String> &E : columns.locale_columns) {
			counts.push_back(vformat("%s: %d", E.value, missing.has(E.key) ? missing[E.key] : 0));
		}
		if (!counts.is_empty()) {
			status = vformat(TTR("Missing translations: %s. %s"), String(", ").join(counts), status);
		}
	}
	status_label->set_text(status);
	_render();
}

void CSVTableEditor::_cell_edited() {
	if (updating) {
		return;
	}
	TreeItem *item = tree->get_edited();
	const int tree_column = tree->get_edited_column();
	ERR_FAIL_COND(!item || tree_column < 1);
	String value;
	if (item->get_cell_mode(tree_column) == TreeItem::CELL_MODE_CHECK) {
		value = item->is_checked(tree_column) ? "true" : "false";
	} else if (item->get_cell_mode(tree_column) == TreeItem::CELL_MODE_RANGE) {
		const int option = (int)item->get_range(tree_column);
		const Ref<DataField> field = _get_field(tree_column - 1);
		value = field.is_valid() && field->get_value_type() == Variant::STRING ? field->get_hint_string().get_slicec(',', option) : itos(option);
	} else {
		value = item->get_text(tree_column);
	}
	Array cells;
	cells.push_back(PackedStringArray({ value }));
	_commit_cells(TTR("Set CSV Cell"), (int)item->get_metadata(0), tree_column - 1, cells, (int)item->get_metadata(1));
}

void CSVTableEditor::_add_row(bool p_insert) {
	if (catalog.is_valid()) {
		// Keys are always appended (a catalog has no row positions to insert at).
		String key = "new_key";
		for (int i = 2; catalog->has_key(key); i++) {
			key = vformat("new_key_%d", i);
		}
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Add Localization Key"));
		undo_redo->add_do_method(catalog.ptr(), "add_key", key);
		undo_redo->add_undo_method(catalog.ptr(), "remove_key", key);
		undo_redo->commit_action();
		page = (catalog->get_key_count() - 1) / PAGE_SIZE;
		return;
	}
	if (csv.is_null() || !_can_edit()) {
		return;
	}
	if (csv->get_column_count() == 0) {
		// An empty row of an empty CSV writes no text, so nothing would be added.
		status_label->set_text(TTR("The CSV has no columns yet. Type the header row in the text editor first."));
		return;
	}
	TreeItem *selected = tree->get_selected();
	const int row = p_insert && selected ? (int)selected->get_metadata(0) : csv->get_row_count();
	PackedStringArray cells;
	cells.resize(csv->get_column_count());
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Add CSV Row"));
	undo_redo->add_do_method(csv.ptr(), "insert_row", row, cells);
	undo_redo->add_undo_method(csv.ptr(), "remove_row", row);
	undo_redo->commit_action();
	page = row / PAGE_SIZE;
}

void CSVTableEditor::_remove_rows() {
	if ((csv.is_null() && catalog.is_null()) || !_can_edit()) {
		return;
	}
	Vector<int> rows;
	for (TreeItem *item = tree->get_next_selected(nullptr); item; item = tree->get_next_selected(item)) {
		if (!rows.has((int)item->get_metadata(0))) {
			rows.push_back((int)item->get_metadata(0)); // Child rows (plural forms) share the row of their key.
		}
	}
	if (rows.is_empty()) {
		return;
	}
	rows.sort();
	if (catalog.is_valid()) {
		// Undo adds the keys again at the end.
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Remove Localization Keys"));
		for (int row : rows) {
			const StringName key = catalog->get_key(row);
			const StringName context = catalog->get_key_context(row);
			undo_redo->add_do_method(catalog.ptr(), "remove_key", key, context);
			undo_redo->add_undo_method(catalog.ptr(), "set_entry", key, catalog->get_entry(key, context), context);
		}
		undo_redo->commit_action();
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Remove CSV Rows"));
	for (int i = rows.size() - 1; i >= 0; i--) {
		undo_redo->add_do_method(csv.ptr(), "remove_row", rows[i]);
	}
	for (int row : rows) {
		undo_redo->add_undo_method(csv.ptr(), "insert_row", row, csv->get_row(row));
	}
	undo_redo->commit_action();
}

void CSVTableEditor::_copy() {
	// The bounding box of the selected cells, as tab-separated text like spreadsheets copy it.
	int first_row = INT_MAX;
	int last_row = -1;
	int first_column = INT_MAX;
	int last_column = -1;
	for (TreeItem *item = tree->get_next_selected(nullptr); item; item = tree->get_next_selected(item)) {
		for (int column = 1; column < tree->get_columns(); column++) {
			if (item->is_selected(column)) {
				const int row = (int)item->get_metadata(0);
				first_row = MIN(first_row, row);
				last_row = MAX(last_row, row);
				first_column = MIN(first_column, column - 1);
				last_column = MAX(last_column, column - 1);
			}
		}
	}
	if (last_row >= 0) {
		// Without the line break after the last row, so one copied cell pastes as plain text elsewhere.
		const String text = CSV::stringify(_get_cells(first_row, first_column, last_row - first_row + 1, last_column - first_column + 1), _tsv_options());
		DisplayServer::get_singleton()->clipboard_set(text.trim_suffix("\n").trim_suffix("\r"));
	}
}

void CSVTableEditor::_paste() {
	TreeItem *item = tree->get_selected();
	const int tree_column = tree->get_selected_column();
	if ((csv.is_null() && catalog.is_null()) || !item || tree_column < 1) {
		return;
	}
	Array cells = CSV::parse_string(DisplayServer::get_singleton()->clipboard_get(), _tsv_options());
	const int width = csv.is_valid() ? csv->get_column_count() - (tree_column - 1) : 0;
	if (width > 0) {
		// Like spreadsheets: a block wider than the table is clipped at the last column.
		for (int i = 0; i < cells.size(); i++) {
			PackedStringArray row = cells[i];
			if (row.size() > width) {
				row.resize(width);
				cells[i] = row;
			}
		}
	}
	if (!cells.is_empty()) {
		_commit_cells(TTR("Paste CSV Cells"), (int)item->get_metadata(0), tree_column - 1, cells, (int)item->get_metadata(1));
	}
}

void CSVTableEditor::_save() {
	// A pending validation would replace the message of the save.
	if (!validate_timer->is_stopped()) {
		validate_timer->stop();
		_validate();
	}
	if (catalog.is_valid()) {
		const String path = catalog->get_path();
		if (!path.is_resource_file()) {
			// Built-in or new: saved with the scene or a save dialog, reported by _resource_saved().
			EditorNode::get_singleton()->save_resource(catalog);
			return;
		}
		// In place (#346): a catalog file is never imported, a CSV with "Keep File" included.
		resource_saved = false;
		EditorNode::get_singleton()->save_resource_in_path(catalog, path);
		if (!resource_saved) {
			status_label->set_text(vformat(TTR("Can't save \"%s\"."), path));
		}
		return;
	}
	if (csv.is_null() || !_can_edit()) {
		return;
	}
	TextEditorBase *text_editor = _get_text_editor();
	if (text_file.is_valid()) {
		const String path = text_file->get_path();
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		if (file.is_null()) {
			status_label->set_text(vformat(TTR("Can't save \"%s\"."), path));
			return;
		}
		file->store_string(text_file->get_text());
		file.unref();
		const uint64_t modified_time = FileAccess::get_modified_time(path);
		text_file->set_last_modified_time(modified_time);
		if (text_editor) {
			text_editor->edited_file_data.last_modified_time = modified_time; // No "changed on disk" prompt.
		}
		EditorFileSystem::get_singleton()->update_file(path);
		status_label->set_text(vformat(TTR("Saved \"%s\"."), path));
	} else {
		EditorNode::get_singleton()->save_resource(csv);
	}
	if (text_editor) {
		text_editor->tag_saved_version();
	}
}

void CSVTableEditor::_resource_saved(Object *p_resource) {
	if (!p_resource || (p_resource != catalog.ptr() && p_resource != csv.ptr())) {
		return;
	}
	resource_saved = true;
	const String path = Object::cast_to<Resource>(p_resource)->get_path();
	String message = vformat(TTR("Saved \"%s\"."), path);
	if (catalog.is_valid()) {
		catalog_modified_time = FileAccess::get_modified_time(path);
		edited_path = path;
		file_label->set_text(path.get_file()); // A new catalog has its path now.
		if (path.get_extension().to_lower() == LocalizationCatalog::BINARY_EXTENSION && !catalog->get_note_columns().is_empty()) {
			message += " " + TTR("Translator notes are not stored in .lcat: save the catalog as CSV, JSON or another text format to keep them.");
		}
	}
	status_label->set_text(message);
}

void CSVTableEditor::_update_column_widths() {
	// Readable columns: Key, Context and five locales fill the table, further columns scroll horizontally (#346).
	const int width = MAX(int(120 * EDSCALE), int((tree->get_size().x - 80 * EDSCALE) / 7));
	for (int column = 1; column < tree->get_columns(); column++) {
		tree->set_column_custom_minimum_width(column, width);
	}
}

void CSVTableEditor::_filesystem_changed() {
	const String path = catalog.is_valid() ? catalog->get_path() : edited_path;
	if (!path.is_resource_file()) {
		return;
	}
	if (!FileAccess::exists(path)) {
		// Deleted or moved outside of the editor: Save would write the old file again (#403).
		edit(Ref<Resource>());
		status_label->set_text(vformat(TTR("\"%s\" was deleted or moved. The table is closed."), path));
		return;
	}
	if (catalog.is_null()) {
		return;
	}
	file_label->set_text(path.get_file()); // Moved in the FileSystem dock: the catalog has the new path.
	const uint64_t modified_time = FileAccess::get_modified_time(path);
	if (modified_time != catalog_modified_time) {
		// Changed outside of the table: the file wins.
		catalog_modified_time = modified_time;
		catalog->reload_from_file();
	}
}

void CSVTableEditor::_change_page(int p_delta) {
	page += p_delta;
	_render();
}

void CSVTableEditor::_schema_changed(const Ref<Resource> &p_schema) {
	schema = p_schema;
	_validate();
}

void CSVTableEditor::_tree_gui_input(const Ref<InputEvent> &p_event) {
	if (p_event->is_action_pressed(SNAME("ui_copy"), false, true)) {
		_copy();
		tree->accept_event();
	} else if (p_event->is_action_pressed(SNAME("ui_paste"), false, true)) {
		_paste();
		tree->accept_event();
	} else if (catalog.is_valid()) {
		// Delete removes the keys of the selected cells (#391).
		const Ref<InputEventKey> key_event = p_event;
		if (key_event.is_valid() && key_event->is_pressed() && !key_event->is_echo() && key_event->get_keycode() == Key::KEY_DELETE) {
			_remove_rows();
			tree->accept_event();
		}
	}
}

bool CSVTableEditor::is_translation_csv(const CSV *p_csv) {
	// A file imported as translation is one.
	const String path = p_csv->get_path();
	if (path.is_resource_file() && FileAccess::exists(path + ".import")) {
		Ref<ConfigFile> import_config;
		import_config.instantiate();
		if (import_config->load(path + ".import") == OK && String(import_config->get_value("remap", "importer", String())) == "csv_translation") {
			return true;
		}
	}
	// Otherwise the header decides: every column after the key column is a locale, a note (`_`) or `?context` /
	// `?plural`. One other column ("hp") makes it a data CSV, also when another column looks like a locale ("id").
	CSV::TranslationHeader translation_header;
	Ref<DataValidation> header_issues;
	header_issues.instantiate();
	CSV::check_translation_header(p_csv->get_header(), translation_header, header_issues.ptr());
	return translation_header.is_translation_table();
}

bool CSVTableEditor::can_edit(const Object *p_object) {
	if (Object::cast_to<LocalizationCatalog>(p_object)) {
		return true;
	}
	// Data CSVs open in the Dataset dock (#436).
	if (const CSV *csv_resource = Object::cast_to<CSV>(p_object)) {
		return is_translation_csv(csv_resource);
	}
	const TextFile *text_file = Object::cast_to<TextFile>(p_object);
	return text_file && text_file->get_path().get_extension().to_lower() == "csv";
}

void CSVTableEditor::_inspector_object_changed() {
	// Opened with "Open in Localization Table", the dock is not closed by the editor when another file is inspected.
	// A catalog stays open until another table object replaces it (#391).
	const Resource *inspected = Object::cast_to<Resource>(InspectorDock::get_inspector_singleton()->get_edited_object());
	if (catalog.is_null() && inspected && !inspected->get_path().is_empty() && !can_edit(inspected)) {
		edit(Ref<Resource>());
		close();
	}
}

void CSVTableEditor::_file_moved(const String &p_old_file, const String &p_new_file) {
	if (!edited_path.is_empty() && p_old_file == edited_path) {
		edited_path = p_new_file;
		file_label->set_text(p_new_file.get_file());
	}
}

void CSVTableEditor::_file_removed(const String &p_file) {
	if (!edited_path.is_empty() && p_file == edited_path) {
		edit(Ref<Resource>());
		close();
	}
}

void CSVTableEditor::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (InspectorDock::get_singleton()) {
			InspectorDock::get_inspector_singleton()->connect(SNAME("edited_object_changed"), callable_mp(this, &CSVTableEditor::_inspector_object_changed));
		}
		if (FileSystemDock::get_singleton()) {
			FileSystemDock::get_singleton()->connect(SNAME("files_moved"), callable_mp(this, &CSVTableEditor::_file_moved));
			FileSystemDock::get_singleton()->connect(SNAME("file_removed"), callable_mp(this, &CSVTableEditor::_file_removed));
		}
		EditorNode::get_singleton()->connect(SNAME("resource_saved"), callable_mp(this, &CSVTableEditor::_resource_saved));
	}
}

void CSVTableEditor::edit(const Ref<Resource> &p_resource) {
	edited_path = p_resource.is_valid() ? p_resource->get_path() : String();
	if (csv.is_valid()) {
		csv->disconnect_changed(callable_mp(this, &CSVTableEditor::_csv_changed));
	}
	if (catalog.is_valid()) {
		catalog->disconnect_changed(callable_mp(this, &CSVTableEditor::_csv_changed));
	}
	catalog = p_resource;
	// Catalog mode: keys and locales instead of rows and columns (#391); keys are always appended.
	const bool catalog_mode = catalog.is_valid();
	add_button->set_text(catalog_mode ? TTR("Add Key") : TTR("Add Row"));
	insert_button->set_visible(!catalog_mode);
	remove_button->set_text(catalog_mode ? TTR("Remove Keys") : TTR("Remove Rows"));
	remove_button->set_tooltip_text(catalog_mode ? TTR("Remove the keys of the selected cells (Delete).") : TTR("Remove the rows of the selected cells."));
	add_locale_button->set_visible(catalog_mode);
	remove_locale_button->set_visible(catalog_mode);
	column_edit->set_visible(catalog_mode);
	add_column_button->set_visible(catalog_mode);
	tree->set_drop_mode_flags(catalog_mode && catalog->is_class("ResourceLocalizationCatalog") ? Tree::DROP_MODE_ON_ITEM : Tree::DROP_MODE_DISABLED);
	if (catalog.is_valid()) {
		csv.unref();
		text_file.unref();
		schema.unref();
		cell_issues.clear();
		missing_cells.clear();
		page = 0;
		translation_mode = false;
		catalog->connect_changed(callable_mp(this, &CSVTableEditor::_csv_changed));
		schema_picker->set_visible(false);
		file_label->set_text(catalog->get_path().get_file());
		catalog_modified_time = catalog->get_path().is_resource_file() ? FileAccess::get_modified_time(catalog->get_path()) : 0;
		tree->clear();
		_update_catalog_fields();
		_validate();
		return;
	}
	text_file = p_resource;
	csv = p_resource;
	schema.unref();
	cell_issues.clear();
	missing_cells.clear();
	page = 0;
	if (text_file.is_valid()) {
		// Translation CSVs open as TextFile: parse them with the delimiter of their import settings.
		Ref<ConfigFile> import_config;
		import_config.instantiate();
		Ref<SerializerOptions> options;
		options.instantiate();
		if (import_config->load(text_file->get_path() + ".import") == OK) {
			const int delimiter = (int)import_config->get_value("params", "delimiter", 0);
			options->set_csv_delimiter(delimiter == 1 ? ";" : (delimiter == 2 ? "\t" : ","));
		}
		csv.instantiate();
		csv->set_options(options);
		synced_text = String();
		_sync_from_text_file();
	}
	if (csv.is_null()) {
		file_label->set_text(String());
		tree->clear();
		return;
	}
	csv->connect_changed(callable_mp(this, &CSVTableEditor::_csv_changed));

	const String path = p_resource->get_path();
	// The header decides, not the importer: a data CSV with a Keep File or CSV Translation import has no translations to check.
	CSV::TranslationHeader translation_header;
	Ref<DataValidation> header_issues;
	header_issues.instantiate();
	CSV::check_translation_header(csv->get_header(), translation_header, header_issues.ptr());
	translation_mode = translation_header.has_known_language;
	if (!translation_mode) {
		schema = EditorDataSchemaLookup::find_schema(path);
	}
	schema_picker->set_visible(!translation_mode);
	schema_picker->set_edited_resource(schema);
	file_label->set_text(path.get_file());
	_validate();
}

CSVTableEditor::CSVTableEditor() {
	singleton = this;
	set_name(TTRC("Localization Table"));
	set_icon_name("TextFile");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_csv_table_bottom_panel", TTRC("Toggle Localization Table Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);

	VBoxContainer *main_vbox = memnew(VBoxContainer);
	add_child(main_vbox);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	main_vbox->add_child(toolbar);
	file_label = memnew(Label);
	toolbar->add_child(file_label);

	add_button = memnew(Button(TTR("Add Row")));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_add_row).bind(false));
	toolbar->add_child(add_button);
	insert_button = memnew(Button(TTR("Insert Row")));
	insert_button->set_tooltip_text(TTR("Insert an empty row above the selected row."));
	insert_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_add_row).bind(true));
	toolbar->add_child(insert_button);
	remove_button = memnew(Button(TTR("Remove Rows")));
	remove_button->set_tooltip_text(TTR("Remove the rows of the selected cells."));
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_remove_rows));
	toolbar->add_child(remove_button);
	Button *save_button = memnew(Button(TTR("Save")));
	save_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_save));
	toolbar->add_child(save_button);

	// Catalog mode: locales from the editor's locale dialog, translator note columns by name (#391).
	locale_dialog = memnew(EditorLocaleDialog);
	locale_dialog->connect("locale_selected", callable_mp(this, &CSVTableEditor::_add_catalog_locale));
	add_child(locale_dialog);
	add_locale_button = memnew(Button(TTR("Add Locale...")));
	add_locale_button->set_tooltip_text(TTR("Pick a language and optionally a script and country; the locale becomes a column."));
	add_locale_button->set_visible(false);
	add_locale_button->connect(SceneStringName(pressed), callable_mp(locale_dialog, &EditorLocaleDialog::popup_locale_dialog));
	toolbar->add_child(add_locale_button);
	remove_locale_button = memnew(Button(TTR("Remove Locale Column")));
	remove_locale_button->set_tooltip_text(TTR("Remove the locale of the selected cell's column from the catalog. Also in the context menu of a locale column header."));
	remove_locale_button->set_visible(false);
	remove_locale_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_remove_selected_locale));
	toolbar->add_child(remove_locale_button);
	header_menu = memnew(PopupMenu);
	header_menu->connect(SceneStringName(id_pressed), callable_mp(this, &CSVTableEditor::_header_menu_id_pressed));
	add_child(header_menu);

	column_edit = memnew(LineEdit);
	column_edit->set_placeholder(TTR("Note column"));
	column_edit->set_tooltip_text(TTR("The name of a translator note column, such as \"max_length\"."));
	column_edit->set_custom_minimum_size(Size2(100 * EDSCALE, 0));
	column_edit->set_visible(false);
	column_edit->connect(SceneStringName(text_submitted), callable_mp(this, &CSVTableEditor::_add_catalog_column).unbind(1));
	toolbar->add_child(column_edit);
	add_column_button = memnew(Button(TTR("Add Note Column")));
	add_column_button->set_visible(false);
	add_column_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_add_catalog_column));
	toolbar->add_child(add_column_button);

	toolbar->add_child(memnew(Label(TTR("Schema:"))));
	schema_picker = memnew(EditorResourcePicker);
	schema_picker->set_base_type("DataSchema");
	schema_picker->set_custom_minimum_size(Size2(200 * EDSCALE, 0));
	schema_picker->set_tooltip_text(TTR("Types the cells and checks them. Found by the schema's file patterns in the project setting \"filesystem/data/schema_files\"; a schema picked here is not saved."));
	schema_picker->connect("resource_changed", callable_mp(this, &CSVTableEditor::_schema_changed));
	toolbar->add_child(schema_picker);

	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(SIZE_EXPAND_FILL);
	toolbar->add_child(spacer);
	previous_button = memnew(Button(TTR("Previous")));
	previous_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_change_page).bind(-1));
	toolbar->add_child(previous_button);
	page_label = memnew(Label);
	toolbar->add_child(page_label);
	next_button = memnew(Button(TTR("Next")));
	next_button->connect(SceneStringName(pressed), callable_mp(this, &CSVTableEditor::_change_page).bind(1));
	toolbar->add_child(next_button);

	tree = memnew(Tree);
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->set_hide_root(true);
	tree->set_column_titles_visible(true);
	tree->set_select_mode(Tree::SELECT_MULTI);
	tree->connect("item_edited", callable_mp(this, &CSVTableEditor::_cell_edited));
	tree->connect(SceneStringName(gui_input), callable_mp(this, &CSVTableEditor::_tree_gui_input));
	tree->connect(SceneStringName(mouse_entered), callable_mp(this, &CSVTableEditor::_sync_from_text_file));
	tree->connect("column_title_clicked", callable_mp(this, &CSVTableEditor::_column_title_clicked));
	tree->connect(SceneStringName(resized), callable_mp(this, &CSVTableEditor::_update_column_widths));
	tree->set_drag_forwarding(Callable(), callable_mp(this, &CSVTableEditor::_can_drop_resource), callable_mp(this, &CSVTableEditor::_drop_resource));
	main_vbox->add_child(tree);

	status_label = memnew(Label);
	status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	status_label->set_h_size_flags(SIZE_EXPAND_FILL);
	main_vbox->add_child(status_label);

	// Validation runs once after a burst of edits, not per edit (large files).
	validate_timer = memnew(Timer);
	validate_timer->set_one_shot(true);
	validate_timer->set_wait_time(0.5);
	validate_timer->connect("timeout", callable_mp(this, &CSVTableEditor::_validate));
	add_child(validate_timer);

	// An open catalog follows external changes of its file (#403).
	EditorFileSystem::get_singleton()->connect("filesystem_changed", callable_mp(this, &CSVTableEditor::_filesystem_changed));
}

CSVTableEditor::~CSVTableEditor() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

void CSVTableEditorPlugin::edit(Object *p_object) {
	table_editor->edit(Ref<Resource>(Object::cast_to<Resource>(p_object)));
}

bool CSVTableEditorPlugin::handles(Object *p_object) const {
	return CSVTableEditor::can_edit(p_object);
}

bool CSVTableEditorPlugin::can_auto_hide() const {
	// An open catalog keeps the dock while other objects are edited (like #396); it closes with the dock.
	return !table_editor->is_editing_catalog();
}

void CSVTableEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		table_editor->make_visible();
	} else {
		table_editor->close();
	}
}

CSVTableEditorPlugin::CSVTableEditorPlugin() {
	table_editor = memnew(CSVTableEditor);
	table_editor->set_custom_minimum_size(Size2(0, 250) * EDSCALE);
	EditorDockManager::get_singleton()->add_dock(table_editor);
	table_editor->close();
}
