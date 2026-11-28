/**************************************************************************/
/*  dataset_editor.cpp                                                    */
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

#include "dataset_editor.h"

#include "core/input/input_event.h"
#include "core/io/file_access.h"
#include "core/io/ini_format.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/text_data_format.h"
#include "core/io/xml_format.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/undo_redo.h"
#include "core/templates/hashfuncs.h"
#include "core/variant/variant_utility.h"
#include "editor/data/csv_table_editor.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_toaster.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/script/data_schema_lookup.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "servers/display/display_server.h"

static int64_t _dataset_cell_key(int p_row, int p_tree_column) {
	return ((int64_t)p_row << 32) | (uint32_t)p_tree_column;
}

static Ref<SerializerOptions> _dataset_tsv_options() {
	Ref<SerializerOptions> options;
	options.instantiate();
	options->set_csv_delimiter("\t");
	options->set_csv_headers(false);
	return options;
}

// The text editor holds "\n" line breaks; a kept text read from a CRLF file has "\r\n".
static String _dataset_lf(const String &p_text) {
	return p_text.replace("\r\n", "\n");
}

Variant DatasetEditor::_get_resource_data(const Resource *p_resource) {
	if (const JSON *json = Object::cast_to<JSON>(p_resource)) {
		return json->get_data();
	}
	if (const TextDataFormat *format = Object::cast_to<TextDataFormat>(p_resource)) {
		return format->get_data();
	}
	return Variant();
}

String DatasetEditor::_get_error(const Resource *p_resource) {
	if (const JSON *json = Object::cast_to<JSON>(p_resource)) {
		return json->get_error_message();
	}
	if (const TextDataFormat *format = Object::cast_to<TextDataFormat>(p_resource)) {
		return format->get_error_message();
	}
	return String();
}

bool DatasetEditor::_is_records(const Variant &p_value) {
	// A non-empty array of dictionaries, at least one with a field.
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}
	const Array values = p_value;
	bool has_field = false;
	for (int idx = 0; idx < values.size(); idx++) {
		if (values[idx].get_type() != Variant::DICTIONARY) {
			return false;
		}
		has_field = has_field || !Dictionary(values[idx]).is_empty();
	}
	return has_field;
}

bool DatasetEditor::_has_shared_key(const Array &p_records) {
	// At least two records and a key that every record has: a list of unrelated objects is no table to open on
	// double-click ("Open in Dataset Dock" still opens it).
	if (p_records.size() < 2) {
		return false;
	}
	const Dictionary first = p_records[0];
	for (const KeyValue<Variant, Variant> &kv : first) {
		bool shared = true;
		for (int idx = 1; idx < p_records.size() && shared; idx++) {
			shared = Dictionary(p_records[idx]).has(kv.key);
		}
		if (shared) {
			return true;
		}
	}
	return false;
}

String DatasetEditor::value_to_text(const Variant &p_value, bool p_json) {
	switch (p_value.get_type()) {
		case Variant::NIL:
			return p_json ? "null" : String();
		case Variant::STRING:
		case Variant::STRING_NAME:
			return p_value;
		case Variant::BOOL:
			return bool(p_value) ? "true" : "false";
		case Variant::INT:
			return itos((int64_t)p_value);
		case Variant::FLOAT: {
			// JSON has one number type: whole numbers read as 3, not 3.0.
			const double number = p_value;
			if (p_json && number == Math::floor(number) && Math::abs(number) < 1e15) {
				return itos((int64_t)number);
			}
			return p_value;
		}
		default:
			return p_json ? JSON::stringify(p_value, "", false, true) : VariantUtilityFunctions::var_to_str(p_value);
	}
}

bool DatasetEditor::text_to_value(const String &p_text, Variant::Type p_type, bool p_json, Variant &r_value) {
	const String text = p_text.strip_edges();
	const String lower = text.to_lower();
	switch (p_type) {
		case Variant::STRING:
			r_value = p_text;
			return true;
		case Variant::STRING_NAME:
			r_value = StringName(p_text);
			return true;
		case Variant::BOOL:
			if (lower == "true" || lower == "1" || lower == "false" || lower == "0") {
				r_value = lower == "true" || lower == "1";
				return true;
			}
			return false;
		case Variant::INT:
			if (!text.is_valid_int()) {
				return false;
			}
			r_value = text.to_int();
			return true;
		case Variant::FLOAT:
			if (!text.is_valid_float()) {
				return false;
			}
			r_value = text.to_float();
			return true;
		case Variant::NIL: {
			// No value in the column yet: the type is taken from the text.
			if (p_json && lower == "null") {
				r_value = Variant();
			} else if (lower == "true" || lower == "false") {
				r_value = lower == "true";
			} else if (text.is_valid_int()) {
				r_value = p_json ? Variant((double)text.to_int()) : Variant(text.to_int());
			} else if (text.is_valid_float()) {
				r_value = text.to_float();
			} else {
				r_value = p_text;
			}
			return true;
		}
		default: {
			if (p_json) {
				Ref<JSON> parser;
				parser.instantiate();
				if (parser->parse(p_text) != OK) {
					return false;
				}
				r_value = parser->get_data();
			} else {
				r_value = VariantUtilityFunctions::str_to_var(p_text);
			}
			return r_value.get_type() == p_type;
		}
	}
}

Vector<DatasetEditor::Table> DatasetEditor::find_tables(const Resource *p_resource) {
	Vector<Table> tables;
	if (const CSV *data_csv = Object::cast_to<CSV>(p_resource)) {
		Table table;
		table.source = SOURCE_CSV;
		table.row_count = data_csv->get_row_count();
		tables.push_back(table);
		return tables;
	}
	if (!Object::cast_to<JSON>(p_resource) && !Object::cast_to<TextDataFormat>(p_resource)) {
		return tables;
	}
	const Variant file_data = _get_resource_data(p_resource);
	if (Object::cast_to<INI>(p_resource)) {
		const Dictionary sections = file_data;
		if (!sections.is_empty()) {
			Table table;
			table.source = SOURCE_SECTIONS;
			table.row_count = sections.size();
			tables.push_back(table);
		}
		return tables;
	}
	if (Object::cast_to<XML>(p_resource)) {
		// One table per child tag of the root element that has attributes in at least one child.
		const Dictionary root = file_data;
		const Array children = root.get(XML::KEY_CHILDREN, Array());
		Vector<String> tags;
		Dictionary counts;
		Dictionary with_attributes;
		for (int idx = 0; idx < children.size(); idx++) {
			if (children[idx].get_type() != Variant::DICTIONARY) {
				continue; // Text and comments between the elements.
			}
			const Dictionary element = children[idx];
			const String tag = element.get(XML::KEY_NAME, String());
			if (!counts.has(tag)) {
				tags.push_back(tag);
				counts[tag] = 0;
			}
			counts[tag] = int(counts[tag]) + 1;
			if (!Dictionary(element.get(XML::KEY_ATTRIBUTES, Dictionary())).is_empty()) {
				with_attributes[tag] = true;
			}
		}
		for (const String &tag : tags) {
			if (with_attributes.has(tag)) {
				Table table;
				table.source = SOURCE_ELEMENTS;
				table.key = tag;
				table.row_count = counts[tag];
				tables.push_back(table);
			}
		}
		return tables;
	}
	// YAML (a module, so called by name): with several documents, a table edit would encode the first document
	// only and the others would be lost.
	if (p_resource->has_method(SNAME("get_document_count")) && int(const_cast<Resource *>(p_resource)->call(SNAME("get_document_count"))) > 1) {
		return tables;
	}
	// JSON, YAML, TOML: an array of dictionaries at the root or under a top-level key (TOML arrays of tables).
	if (_is_records(file_data)) {
		Table table;
		table.source = SOURCE_RECORDS;
		table.row_count = Array(file_data).size();
		tables.push_back(table);
	} else if (file_data.get_type() == Variant::DICTIONARY) {
		const Dictionary root = file_data;
		for (const KeyValue<Variant, Variant> &kv : root) {
			if (_is_records(kv.value)) {
				Table table;
				table.source = SOURCE_RECORDS;
				table.key = kv.key;
				table.row_count = Array(kv.value).size();
				tables.push_back(table);
			}
		}
	}
	return tables;
}

bool DatasetEditor::can_edit(const Object *p_object) {
	const Resource *resource = Object::cast_to<Resource>(p_object);
	return resource && !find_tables(resource).is_empty();
}

bool DatasetEditor::can_open_automatically(const Object *p_object) {
	const Resource *resource = Object::cast_to<Resource>(p_object);
	if (!resource || Object::cast_to<INI>(resource)) {
		return false;
	}
	const String path = resource->get_path();
	if (path.is_resource_file() && FileAccess::exists(path) && FileAccess::get_size(path) > AUTO_OPEN_SIZE) {
		return false;
	}
	if (const CSV *data_csv = Object::cast_to<CSV>(resource)) {
		return !CSVTableEditor::is_translation_csv(data_csv);
	}
	const Variant file_data = _get_resource_data(resource);
	for (const Table &table : find_tables(resource)) {
		if (table.source == SOURCE_ELEMENTS && table.row_count >= 2) {
			return true;
		}
		if (table.source == SOURCE_RECORDS && _has_shared_key(table.key.get_type() == Variant::NIL ? Array(file_data) : Array(Dictionary(file_data).get(table.key, Array())))) {
			return true;
		}
	}
	return false;
}

bool DatasetEditor::has_empty_table(Source p_source, const Variant &p_key, const Variant &p_file_data) {
	switch (p_source) {
		case SOURCE_RECORDS: {
			const Variant value = p_key.get_type() == Variant::NIL ? p_file_data : (p_file_data.get_type() == Variant::DICTIONARY ? Dictionary(p_file_data).get(p_key, Variant()) : Variant());
			return value.get_type() == Variant::ARRAY && Array(value).is_empty();
		}
		case SOURCE_SECTIONS:
			return p_file_data.get_type() == Variant::DICTIONARY && Dictionary(p_file_data).is_empty();
		case SOURCE_ELEMENTS:
			// The root element is still there; its children of the tag are gone.
			return p_file_data.get_type() == Variant::DICTIONARY && !Dictionary(p_file_data).is_empty();
		default:
			return false;
	}
}

void DatasetEditor::read_rows(Source p_source, const Variant &p_key, const Variant &p_file_data, Array &r_records, PackedStringArray &r_names) {
	// New arrays, never clear(): `r_records` may share its array with the data of a file (Array is a reference).
	r_records = Array();
	r_names.clear();
	if (p_source == SOURCE_RECORDS) {
		const Variant value = p_key.get_type() == Variant::NIL ? p_file_data : (p_file_data.get_type() == Variant::DICTIONARY ? Dictionary(p_file_data).get(p_key, Array()) : Variant());
		if (value.get_type() == Variant::ARRAY) {
			r_records = value;
		}
	} else if (p_source == SOURCE_SECTIONS) {
		const Dictionary sections = p_file_data;
		for (const KeyValue<Variant, Variant> &kv : sections) {
			r_names.push_back(kv.key);
			r_records.push_back(kv.value);
		}
	} else if (p_source == SOURCE_ELEMENTS) {
		const Array children = Dictionary(p_file_data).get(XML::KEY_CHILDREN, Array());
		for (int idx = 0; idx < children.size(); idx++) {
			if (children[idx].get_type() == Variant::DICTIONARY && Dictionary(children[idx]).get(XML::KEY_NAME, String()) == p_key) {
				r_records.push_back(children[idx]);
			}
		}
	}
}

Variant DatasetEditor::build_data(Source p_source, const Variant &p_key, const Variant &p_file_data, const Array &p_records, const PackedStringArray &p_names) {
	switch (p_source) {
		case SOURCE_RECORDS: {
			if (p_key.get_type() == Variant::NIL) {
				return p_records;
			}
			Dictionary root = Dictionary(p_file_data).duplicate();
			root[p_key] = p_records;
			return root;
		}
		case SOURCE_SECTIONS: {
			Dictionary sections;
			for (int idx = 0; idx < p_records.size(); idx++) {
				sections[p_names[idx]] = p_records[idx];
			}
			return sections;
		}
		case SOURCE_ELEMENTS: {
			// The rows fill the places of the elements with the tag in order; new rows follow the last one.
			Dictionary root = Dictionary(p_file_data).duplicate();
			const Array children = root.get(XML::KEY_CHILDREN, Array());
			Array next_children;
			int next = 0;
			int insert_at = -1;
			for (int idx = 0; idx < children.size(); idx++) {
				const bool is_row = children[idx].get_type() == Variant::DICTIONARY && Dictionary(children[idx]).get(XML::KEY_NAME, String()) == p_key;
				if (!is_row) {
					next_children.push_back(children[idx]);
					continue;
				}
				if (next < p_records.size()) {
					next_children.push_back(p_records[next++]);
				}
				insert_at = next_children.size();
			}
			if (insert_at < 0) {
				insert_at = next_children.size();
			}
			for (; next < p_records.size(); next++) {
				next_children.insert(insert_at++, p_records[next]);
			}
			root[XML::KEY_CHILDREN] = next_children;
			return root;
		}
		default:
			return p_file_data;
	}
}

Dictionary DatasetEditor::get_fields(Source p_source, const Variant &p_record) {
	if (p_source == SOURCE_ELEMENTS) {
		return Dictionary(p_record).get(XML::KEY_ATTRIBUTES, Dictionary());
	}
	return p_record;
}

String DatasetEditor::encode(const Resource *p_resource, const Variant &p_data) {
	if (Object::cast_to<JSON>(p_resource)) {
		// Full precision: numbers the edit did not touch keep their value.
		return JSON::stringify(p_data, "\t", false, true);
	}
	const TextDataFormat *format = Object::cast_to<TextDataFormat>(p_resource);
	ERR_FAIL_NULL_V(format, String());
	// A new instance encodes the next data with the options of the file, so the file is only changed by the action.
	// By class name: a script on the resource is not copied, a scripted subclass encodes like its class.
	Ref<TextDataFormat> encoder = Ref<TextDataFormat>(Object::cast_to<TextDataFormat>(ClassDB::instantiate(format->get_class_name())));
	ERR_FAIL_COND_V(encoder.is_null(), String());
	encoder->set_options(format->get_options());
	encoder->set_data(p_data);
	return encoder->encode_to_text();
}

DatasetEditor::OpenFile *DatasetEditor::_get_current_file() {
	return current >= 0 && current < (int)open_files.size() ? &open_files[current] : nullptr;
}

Resource *DatasetEditor::_get_resource() const {
	return current >= 0 && current < (int)open_files.size() ? open_files[current].resource.ptr() : nullptr;
}

bool DatasetEditor::_is_json() const {
	return Object::cast_to<JSON>(_get_resource()) != nullptr;
}

bool DatasetEditor::_is_unsaved(const OpenFile &p_file) const {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	return undo_redo->has_history(p_file.history_id) && undo_redo->is_history_unsaved(p_file.history_id);
}

TextEditorBase *DatasetEditor::_get_text_editor() const {
	Resource *resource = _get_resource();
	if (!resource || !ScriptEditor::get_singleton()) {
		return nullptr;
	}
	return Object::cast_to<TextEditorBase>(ScriptEditor::get_singleton()->get_script_container()->get_resource_editor(Ref<Resource>(resource)));
}

String DatasetEditor::_get_text() const {
	const Resource *resource = _get_resource();
	if (const JSON *json = Object::cast_to<JSON>(resource)) {
		const String text = json->get_parsed_text();
		return text.is_empty() ? encode(resource, json->get_data()) : text;
	}
	if (const TextDataFormat *format = Object::cast_to<TextDataFormat>(resource)) {
		const String text = format->get_parsed_text();
		return text.is_empty() ? format->encode_to_text() : text;
	}
	return String();
}

Ref<DataField> DatasetEditor::_get_field(int p_column) const {
	const Ref<DataSchema> schema = current >= 0 && current < (int)open_files.size() ? open_files[current].schema : Ref<DataSchema>();
	if (schema.is_null() || csv.is_null()) {
		return Ref<DataField>();
	}
	const PackedStringArray header = csv->get_header();
	if (!header.is_empty()) {
		return p_column < header.size() ? schema->get_field(header[p_column]) : Ref<DataField>();
	}
	const TypedArray<DataField> fields = schema->get_fields();
	return p_column < fields.size() ? Ref<DataField>(fields[p_column]) : Ref<DataField>();
}

Variant::Type DatasetEditor::_get_column_type(int p_column) const {
	if (source == SOURCE_ELEMENTS) {
		return Variant::STRING; // XML attributes are text.
	}
	const Variant::Type type = p_column < (int)column_types.size() ? column_types[p_column] : Variant::NIL;
	return type == MIXED_TYPE ? Variant::NIL : type;
}

Variant DatasetEditor::_make_record(const Dictionary &p_fields) const {
	if (source != SOURCE_ELEMENTS) {
		return p_fields;
	}
	Dictionary element;
	element[XML::KEY_NAME] = table_key;
	element[XML::KEY_ATTRIBUTES] = p_fields;
	element[XML::KEY_CHILDREN] = Array();
	element[XML::KEY_TEXT] = String();
	return element;
}

int DatasetEditor::_get_row_count() const {
	if (source == SOURCE_CSV) {
		return csv->get_row_count();
	}
	return records.size();
}

int DatasetEditor::_get_column_count() const {
	if (source == SOURCE_CSV) {
		return csv->get_column_count();
	}
	return column_keys.size();
}

String DatasetEditor::_get_cell_text(int p_row, int p_column) const {
	if (source == SOURCE_CSV) {
		const PackedStringArray cells = csv->get_row(p_row);
		return p_column < cells.size() ? cells[p_column] : String();
	}
	const Dictionary fields = get_fields(source, records[p_row]);
	const Variant &key = column_keys[p_column];
	return fields.has(key) ? value_to_text(fields[key], _is_json()) : String();
}

Array DatasetEditor::_get_cells(int p_row, int p_column, int p_rows, int p_columns) const {
	if (source == SOURCE_CSV) {
		return csv->get_cells(p_row, p_column, p_rows, p_columns);
	}
	Array cells;
	for (int row = p_row; row < p_row + p_rows && row < _get_row_count(); row++) {
		PackedStringArray line;
		for (int column = p_column; column < p_column + p_columns && column < _get_column_count(); column++) {
			line.push_back(_get_cell_text(row, column));
		}
		cells.push_back(line);
	}
	return cells;
}

int DatasetEditor::_get_selected_column() const {
	const TreeItem *item = table_view->get_selected();
	const int tree_column = table_view->get_selected_column();
	return item && tree_column >= 1 ? tree_column - 1 : -1;
}

void DatasetEditor::_create_action(const String &p_action) {
	// Each file has its own history: undo in the dock never undoes an edit of another file or of the scene.
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(p_action, _get_current_file()->history_id);
	undo_redo->force_fixed_history();
}

bool DatasetEditor::_can_edit() {
	OpenFile *file = _get_current_file();
	if (!file || source == SOURCE_NONE) {
		return false;
	}
	TextEditorBase *text_editor = _get_text_editor();
	const TextEdit *text_edit = text_editor ? Object::cast_to<TextEdit>(text_editor->get_base_editor()) : nullptr;
	if (source == SOURCE_CSV) {
		if (text_edit) {
			// Takes over text typed since the text editor's last idle parse, so the table edits the current text.
			const bool in_sync = csv->get_parsed_text().is_empty() ? text_edit->get_version() == file->reloaded_version : text_edit->get_text() == _dataset_lf(csv->get_parsed_text());
			if (!in_sync) {
				csv->parse(text_edit->get_text(), true);
			}
		}
	} else if (text_edit && text_edit->get_text() != _dataset_lf(_get_text())) {
		JSON *json = Object::cast_to<JSON>(file->resource.ptr());
		TextDataFormat *format = Object::cast_to<TextDataFormat>(file->resource.ptr());
		if (json) {
			json->parse(text_edit->get_text(), true);
			json->emit_changed();
		} else if (format) {
			format->parse(text_edit->get_text(), true);
		}
		_build_view();
	}
	const String error = _get_error(file->resource.ptr());
	if (!error.is_empty() || source == SOURCE_NONE) {
		status_label->set_text(vformat(TTR("Fix the text first: %s"), error));
		return false;
	}
	return true;
}

void DatasetEditor::_commit_records(const String &p_action, const Array &p_records, const PackedStringArray &p_names) {
	OpenFile *file = _get_current_file();
	Resource *resource = file->resource.ptr();
	const Variant file_data = _get_resource_data(resource);
	const Variant next_data = build_data(source, table_key, file_data, p_records, p_names);
	// Compared as data, not as text: an edit that changes nothing (a refused value, the same value again) must not
	// rewrite the file in the encoder's style.
	if (next_data == file_data) {
		_render();
		return;
	}
	const String new_text = encode(resource, next_data);
	const String old_text = _get_text();
	// The text is the state: undo gives back the exact text, with its comments and formatting. Neighboring
	// actions share their texts (copy-on-write), so each edit keeps about one copy of the file in memory.
	_create_action(p_action);
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->add_do_method(resource, "parse", new_text, true);
	undo_redo->add_undo_method(resource, "parse", old_text, true);
	if (_is_json()) {
		// JSON.parse() does not emit `changed`.
		undo_redo->add_do_method(resource, "emit_changed");
		undo_redo->add_undo_method(resource, "emit_changed");
	}
	push_text = true;
	undo_redo->commit_action();
	if (!file->encoder_hint_shown) {
		// Set after the commit: `changed` clears the status line only in the deferred update.
		file->encoder_hint_shown = true;
		EditorToaster::get_singleton()->popup_str(TTR("The Dataset dock writes the whole file in the format's own style: comments and formatting of the text can change. Undo restores the exact text."), EditorToaster::SEVERITY_INFO);
	}
}

void DatasetEditor::_commit_cells(const String &p_action, int p_row, int p_column, const Array &p_cells) {
	if (!_can_edit()) {
		_render();
		return;
	}
	if (source == SOURCE_CSV) {
		const int row_count = csv->get_row_count();
		ERR_FAIL_INDEX(p_row, row_count + 1);
		int width = 0;
		for (int idx = 0; idx < p_cells.size(); idx++) {
			width = MAX(width, (int)PackedStringArray(p_cells[idx]).size());
		}
		_create_action(p_action);
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->add_do_method(csv.ptr(), "set_cells", p_row, p_column, p_cells);
		undo_redo->add_undo_method(csv.ptr(), "set_cells", p_row, p_column, csv->get_cells(p_row, p_column, p_cells.size(), width));
		for (int row = row_count; row < p_row + p_cells.size(); row++) {
			undo_redo->add_undo_method(csv.ptr(), "remove_row", row_count);
		}
		undo_redo->commit_action();
		return;
	}

	const bool json = _is_json();
	Array next = records.duplicate();
	PackedStringArray names = section_names;
	bool refused = false;
	for (int idx = 0; idx < p_cells.size(); idx++) {
		const int row = p_row + idx;
		if (row >= next.size()) {
			// Like a CSV: pasted rows past the end become new rows.
			next.push_back(_make_record(Dictionary()));
			if (source == SOURCE_SECTIONS) {
				String section_name = "new_section";
				for (int number = 2; names.has(section_name); number++) {
					section_name = vformat("new_section_%d", number);
				}
				names.push_back(section_name);
			}
		}
		const PackedStringArray line = p_cells[idx];
		Dictionary fields = get_fields(source, next[row]).duplicate();
		for (int cell = 0; cell < line.size() && p_column + cell < column_keys.size(); cell++) {
			const Variant &key = column_keys[p_column + cell];
			const bool present = fields.has(key);
			const Variant::Type type = present ? fields[key].get_type() : _get_column_type(p_column + cell);
			const bool text_type = type == Variant::STRING || type == Variant::STRING_NAME;
			if (line[cell].is_empty() && !text_type) {
				// An emptied cell unsets the field; a missing field stays missing. Null stays null.
				if (present && type != Variant::NIL) {
					fields.erase(key);
				}
				continue;
			}
			if (!present && line[cell].is_empty()) {
				continue; // Only keys that a record has (or that get a value) are written.
			}
			Variant value;
			if (!text_to_value(line[cell], type, json, value)) {
				status_label->set_text(vformat(TTR("\"%s\" is not a valid %s value."), line[cell], Variant::get_type_name(type)));
				refused = true;
				continue;
			}
			fields[key] = value;
		}
		if (source == SOURCE_ELEMENTS) {
			Dictionary element = Dictionary(next[row]).duplicate();
			element[XML::KEY_ATTRIBUTES] = fields;
			next[row] = element;
		} else {
			next[row] = fields;
		}
	}
	if (refused) {
		// The tree still shows the refused text.
		callable_mp(this, &DatasetEditor::_render).call_deferred();
	}
	_commit_records(p_action, next, names);
}

void DatasetEditor::_add_row(bool p_insert) {
	OpenFile *file = _get_current_file();
	if (!file || !_can_edit()) {
		return;
	}
	const TreeItem *selected = table_view->get_selected();
	const int row = p_insert && selected ? (int)selected->get_metadata(0) : _get_row_count();
	file->page = row / DATASET_PAGE_ROWS;
	if (source == SOURCE_CSV) {
		if (csv->get_column_count() == 0) {
			// An empty row of an empty CSV writes no text, so nothing would be added.
			status_label->set_text(TTR("The CSV has no columns yet. Insert a column first (Values > Insert Column)."));
			return;
		}
		PackedStringArray cells;
		cells.resize(csv->get_column_count());
		_create_action(TTR("Add Row"));
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->add_do_method(csv.ptr(), "insert_row", row, cells);
		undo_redo->add_undo_method(csv.ptr(), "remove_row", row);
		undo_redo->commit_action();
		return;
	}

	// Every column gets the default value of its type, so the new row is complete.
	Dictionary fields;
	for (int column = 0; column < column_keys.size(); column++) {
		const Variant::Type type = _get_column_type(column);
		Variant value = String();
		if (type != Variant::NIL && type != Variant::STRING) {
			Callable::CallError error;
			Variant::construct(type, value, nullptr, 0, error);
		}
		fields[column_keys[column]] = value;
	}
	Array next = records.duplicate();
	PackedStringArray names = section_names;
	if (source == SOURCE_SECTIONS) {
		String section_name = "new_section";
		for (int number = 2; names.has(section_name); number++) {
			section_name = vformat("new_section_%d", number);
		}
		names.insert(row, section_name);
	}
	next.insert(row, _make_record(fields));
	_commit_records(source == SOURCE_SECTIONS ? TTR("Add Section") : TTR("Add Row"), next, names);
}

void DatasetEditor::_delete_rows() {
	if (!_can_edit()) {
		return;
	}
	Vector<int> rows;
	for (TreeItem *item = table_view->get_next_selected(nullptr); item; item = table_view->get_next_selected(item)) {
		if (!rows.has((int)item->get_metadata(0))) {
			rows.push_back((int)item->get_metadata(0));
		}
	}
	if (rows.is_empty()) {
		status_label->set_text(TTR("Select a cell of each row to delete."));
		return;
	}
	rows.sort();
	if (source == SOURCE_CSV) {
		_create_action(TTR("Delete Rows"));
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		for (int idx = rows.size() - 1; idx >= 0; idx--) {
			undo_redo->add_do_method(csv.ptr(), "remove_row", rows[idx]);
		}
		for (int row : rows) {
			undo_redo->add_undo_method(csv.ptr(), "insert_row", row, csv->get_row(row));
		}
		undo_redo->commit_action();
		return;
	}
	Array next = records.duplicate();
	PackedStringArray names = section_names;
	for (int idx = rows.size() - 1; idx >= 0; idx--) {
		next.remove_at(rows[idx]);
		if (source == SOURCE_SECTIONS) {
			names.remove_at(rows[idx]);
		}
	}
	_commit_records(source == SOURCE_SECTIONS ? TTR("Delete Sections") : TTR("Delete Rows"), next, names);
}

void DatasetEditor::_rename_section(int p_row, const String &p_name) {
	const String section_name = p_name.strip_edges();
	if (!_can_edit() || p_row >= section_names.size() || section_name == section_names[p_row]) {
		_render();
		return;
	}
	if (section_name.is_empty() || section_names.has(section_name)) {
		status_label->set_text(section_name.is_empty() ? TTR("A section name can't be empty.") : vformat(TTR("The section \"%s\" exists already."), section_name));
		callable_mp(this, &DatasetEditor::_render).call_deferred();
		return;
	}
	PackedStringArray names = section_names;
	names.set(p_row, section_name);
	_commit_records(TTR("Rename Section"), records, names);
}

void DatasetEditor::_insert_column() {
	if (!_can_edit()) {
		return;
	}
	const String column_name = column_name_edit->get_text().strip_edges();
	const int selected = _get_selected_column();
	const int column = selected >= 0 ? selected : _get_column_count();
	if (source == SOURCE_CSV) {
		const PackedStringArray header = csv->get_header();
		// Without a header the column needs no name, unless it is the first column of an empty CSV.
		if (column_name.is_empty() && (!header.is_empty() || csv->get_row_count() == 0)) {
			status_label->set_text(TTR("Type a name for the column."));
			return;
		}
		if (!column_name.is_empty() && header.has(column_name)) {
			status_label->set_text(vformat(TTR("The column \"%s\" exists already."), column_name));
			return;
		}
		_create_action(TTR("Insert Column"));
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->add_do_method(csv.ptr(), "insert_column", column, column_name, String());
		undo_redo->add_undo_method(csv.ptr(), "remove_column", column);
		undo_redo->commit_action();
		return;
	}
	if (column_name.is_empty() || column_keys.has(column_name)) {
		status_label->set_text(column_name.is_empty() ? TTR("Type a name for the column.") : vformat(TTR("The column \"%s\" exists already."), column_name));
		return;
	}
	if (records.is_empty()) {
		// No row to hold the field: the column is remembered for the next new row.
		OpenFile *file = _get_current_file();
		file->remembered_keys.insert(column, column_name);
		file->remembered_types.insert(column, Variant::STRING);
		rendered_hash = 0;
		_update();
		return;
	}
	// In every row, before the field of the selected column (or at the end); the order of the fields is kept.
	const Variant before = column < column_keys.size() ? column_keys[column] : Variant();
	Array next = records.duplicate();
	for (int row = 0; row < records.size(); row++) {
		const Dictionary fields = get_fields(source, records[row]);
		Dictionary next_fields;
		bool inserted = false;
		for (const KeyValue<Variant, Variant> &kv : fields) {
			if (!inserted && before.get_type() != Variant::NIL && kv.key == before) {
				next_fields[column_name] = String();
				inserted = true;
			}
			next_fields[kv.key] = kv.value;
		}
		if (!inserted) {
			next_fields[column_name] = String();
		}
		if (source == SOURCE_ELEMENTS) {
			Dictionary element = Dictionary(next[row]).duplicate();
			element[XML::KEY_ATTRIBUTES] = next_fields;
			next[row] = element;
		} else {
			next[row] = next_fields;
		}
	}
	_commit_records(TTR("Insert Column"), next, section_names);
}

void DatasetEditor::_remove_column() {
	const int column = _get_selected_column();
	if (column < 0 || column >= _get_column_count()) {
		status_label->set_text(TTR("Select a cell of the column to remove."));
		return;
	}
	if (!_can_edit()) {
		return;
	}
	if (source == SOURCE_CSV) {
		const PackedStringArray header = csv->get_header();
		_create_action(TTR("Remove Column"));
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->add_do_method(csv.ptr(), "remove_column", column);
		undo_redo->add_undo_method(csv.ptr(), "insert_column", column, column < header.size() ? header[column] : String(), String());
		undo_redo->add_undo_method(csv.ptr(), "set_cells", 0, column, csv->get_cells(0, column, csv->get_row_count(), 1));
		undo_redo->commit_action();
		return;
	}
	const Variant key = column_keys[column];
	Array next = records.duplicate();
	for (int row = 0; row < records.size(); row++) {
		Dictionary fields = get_fields(source, records[row]).duplicate();
		fields.erase(key);
		if (source == SOURCE_ELEMENTS) {
			Dictionary element = Dictionary(next[row]).duplicate();
			element[XML::KEY_ATTRIBUTES] = fields;
			next[row] = element;
		} else {
			next[row] = fields;
		}
	}
	_commit_records(TTR("Remove Column"), next, section_names);
}

void DatasetEditor::_undo_redo(bool p_redo) {
	const OpenFile *file = _get_current_file();
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (!file || !undo_redo->has_history(file->history_id)) {
		return;
	}
	const UndoRedo *history = undo_redo->get_history_undo_redo(file->history_id);
	if (p_redo ? !history->has_redo() : !history->has_undo()) {
		status_label->set_text(p_redo ? TTR("Nothing to redo.") : TTR("Nothing to undo."));
		return;
	}
	push_text = true;
	if (p_redo) {
		undo_redo->redo_history(file->history_id);
	} else {
		undo_redo->undo_history(file->history_id);
	}
}

void DatasetEditor::_copy() {
	// The bounding box of the selected cells, as tab-separated text like spreadsheets copy it.
	int first_row = INT_MAX;
	int last_row = -1;
	int first_column = INT_MAX;
	int last_column = -1;
	for (TreeItem *item = table_view->get_next_selected(nullptr); item; item = table_view->get_next_selected(item)) {
		for (int column = 1; column < table_view->get_columns(); column++) {
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
		const String text = CSV::stringify(_get_cells(first_row, first_column, last_row - first_row + 1, last_column - first_column + 1), _dataset_tsv_options());
		DisplayServer::get_singleton()->clipboard_set(text.trim_suffix("\n").trim_suffix("\r"));
	}
}

void DatasetEditor::_paste() {
	const TreeItem *item = table_view->get_selected();
	const int column = _get_selected_column();
	if (!item || column < 0) {
		return;
	}
	Array cells = CSV::parse_string(DisplayServer::get_singleton()->clipboard_get(), _dataset_tsv_options());
	const int width = _get_column_count() - column;
	if (width > 0) {
		// Like spreadsheets: a block wider than the table is clipped at the last column.
		for (int idx = 0; idx < cells.size(); idx++) {
			PackedStringArray line = cells[idx];
			if (line.size() > width) {
				line.resize(width);
				cells[idx] = line;
			}
		}
	}
	if (!cells.is_empty()) {
		_commit_cells(TTR("Paste Cells"), (int)item->get_metadata(0), column, cells);
	}
}

void DatasetEditor::_save() {
	Resource *resource = _get_resource();
	if (!resource) {
		return;
	}
	// A pending validation would replace the message of the save.
	if (!validate_timer->is_stopped()) {
		validate_timer->stop();
		_validate();
	}
	if (!_can_edit()) {
		return;
	}
	resource_saved = false;
	EditorNode::get_singleton()->save_resource(Ref<Resource>(resource));
	TextEditorBase *text_editor = _get_text_editor();
	if (resource_saved && text_editor) {
		text_editor->tag_saved_version();
	}
}

void DatasetEditor::_export(const String &p_path) {
	if (source == SOURCE_NONE) {
		return;
	}
	const int row_count = _get_row_count();
	const int column_count = _get_column_count();
	PackedStringArray header;
	if (source == SOURCE_CSV) {
		header = csv->get_header();
	} else {
		for (int column = 0; column < column_count; column++) {
			header.push_back(String(column_keys[column]));
		}
	}
	String text;
	if (export_format == TABLE_EXPORT_CSV) {
		Array rows;
		if (!header.is_empty()) {
			if (source == SOURCE_SECTIONS) {
				header.insert(0, "section");
			}
			rows.push_back(header);
		}
		for (int row = 0; row < row_count; row++) {
			const Array line_cells = _get_cells(row, 0, 1, column_count);
			PackedStringArray line = line_cells.is_empty() ? PackedStringArray() : PackedStringArray(line_cells[0]);
			if (source == SOURCE_SECTIONS) {
				line.insert(0, section_names[row]);
			}
			rows.push_back(line);
		}
		text = CSV::stringify(rows);
	} else if (source == SOURCE_SECTIONS) {
		// Section name -> keys, like the INI file.
		text = JSON::stringify(build_data(source, table_key, Variant(), records, section_names), "\t", false, true);
	} else {
		// One object per row; CSV cells stay text, the values of the other formats keep their types.
		Array rows;
		for (int row = 0; row < row_count; row++) {
			if (source != SOURCE_CSV) {
				rows.push_back(get_fields(source, records[row]));
				continue;
			}
			Dictionary fields;
			const PackedStringArray cells = csv->get_row(row);
			for (int column = 0; column < column_count; column++) {
				fields[column < header.size() ? header[column] : vformat("column_%d", column + 1)] = column < cells.size() ? cells[column] : String();
			}
			rows.push_back(fields);
		}
		text = JSON::stringify(rows, "\t", false, true);
	}
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	if (file.is_null()) {
		status_label->set_text(vformat(TTR("Can't write \"%s\"."), p_path));
		return;
	}
	file->store_string(text);
	file.unref();
	EditorFileSystem::get_singleton()->update_file(p_path);
	status_label->set_text(vformat(TTR("Exported %d rows to \"%s\"."), row_count, p_path));
}

void DatasetEditor::_open_text() {
	Resource *resource = _get_resource();
	if (resource && ScriptEditor::get_singleton() && ScriptEditor::get_singleton()->edit(Ref<Resource>(resource), true)) {
		EditorInterface::get_singleton()->set_main_screen_editor("Script");
	}
}

void DatasetEditor::_close_file(int p_index) {
	ERR_FAIL_INDEX(p_index, (int)open_files.size());
	OpenFile &file = open_files[p_index];
	file.resource->disconnect_changed(callable_mp(this, &DatasetEditor::_resource_changed));
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	if (undo_redo->has_history(file.history_id)) {
		undo_redo->discard_history(file.history_id);
	}
	open_files.remove_at(p_index);
	if (p_index < current || current >= (int)open_files.size()) {
		current--;
	}
	rendered_hash = 0;
	cell_issues.clear();
	_update();
	_validate();
}

void DatasetEditor::_request_close(const Vector<int> &p_indices) {
	pending_close.clear();
	PackedStringArray unsaved;
	for (int index : p_indices) {
		ERR_CONTINUE(index < 0 || index >= (int)open_files.size());
		pending_close.push_back(open_files[index].resource);
		if (_is_unsaved(open_files[index])) {
			unsaved.push_back(open_files[index].resource->get_path().get_file());
		}
	}
	if (unsaved.is_empty()) {
		_close_pending(false);
		return;
	}
	close_dialog->set_text(vformat(TTR("Save changes to the following file(s) before closing?\n%s"), String("\n").join(unsaved)));
	close_dialog->popup_centered();
}

void DatasetEditor::_close_pending(bool p_save) {
	for (const Ref<Resource> &resource : pending_close) {
		for (uint32_t idx = 0; idx < open_files.size(); idx++) {
			if (open_files[idx].resource != resource) {
				continue;
			}
			if (p_save && _is_unsaved(open_files[idx])) {
				EditorNode::get_singleton()->save_resource(resource);
			}
			_close_file(idx);
			break;
		}
	}
	pending_close.clear();
}

void DatasetEditor::_close_dialog_action(const StringName &p_action) {
	if (p_action == StringName("discard")) {
		close_dialog->hide();
		_close_pending(false);
	}
}

void DatasetEditor::_build_view() {
	csv.unref();
	source = SOURCE_NONE;
	table_key = Variant();
	records = Array(); // Not clear(): the array is the one in the data of the shown file.
	section_names.clear();
	column_keys = Array();
	column_types.clear();
	table_picker->clear();
	OpenFile *file = _get_current_file();
	if (!file) {
		table_picker->hide();
		return;
	}
	const Variant file_data = _get_resource_data(file->resource.ptr());
	Vector<Table> tables = find_tables(file->resource.ptr());
	// A table whose last row was deleted stays open: rows can be added again and undo stays reachable.
	bool shown = false;
	for (const Table &table : tables) {
		shown = shown || (table.source == file->source && table.key == file->table_key);
	}
	if (!shown && file->source != SOURCE_NONE && file->source != SOURCE_CSV && has_empty_table(file->source, file->table_key, file_data)) {
		Table table;
		table.source = file->source;
		table.key = file->table_key;
		tables.push_back(table);
	}
	int selected = 0;
	for (int idx = 0; idx < tables.size(); idx++) {
		const Table &table = tables[idx];
		String label;
		if (table.source == SOURCE_ELEMENTS) {
			label = vformat("<%s>", table.key);
		} else if (table.source == SOURCE_RECORDS) {
			label = table.key.get_type() == Variant::NIL ? TTR("Root Array") : String(table.key);
		} else {
			label = table.source == SOURCE_SECTIONS ? TTR("Sections") : TTR("Rows");
		}
		table_picker->add_item(vformat("%s (%d)", label, table.row_count));
		table_picker->set_item_metadata(idx, table.key);
		if (table.source == file->source && table.key == file->table_key) {
			selected = idx;
		}
	}
	table_picker->set_visible(tables.size() > 1);
	if (tables.is_empty()) {
		return;
	}
	table_picker->select(selected);
	source = tables[selected].source;
	table_key = tables[selected].key;
	file->source = source;
	file->table_key = table_key;
	if (source == SOURCE_CSV) {
		csv = Ref<CSV>(Object::cast_to<CSV>(file->resource.ptr()));
		return;
	}

	read_rows(source, table_key, file_data, records, section_names);
	// The columns: every field name in first-seen order, typed by its values. A row writes only its own fields.
	Dictionary column_index;
	for (int row = 0; row < records.size(); row++) {
		const Dictionary fields = get_fields(source, records[row]);
		for (const KeyValue<Variant, Variant> &kv : fields) {
			const Variant::Type type = kv.value.get_type();
			if (!column_index.has(kv.key)) {
				column_index[kv.key] = column_keys.size();
				column_keys.push_back(kv.key);
				column_types.push_back(type);
				continue;
			}
			Variant::Type &column_type = column_types[int(column_index[kv.key])];
			if (column_type == Variant::NIL) {
				column_type = type;
			} else if (type != Variant::NIL && type != column_type) {
				column_type = MIXED_TYPE;
			}
		}
	}
	if (records.is_empty()) {
		column_keys = file->remembered_keys.duplicate();
		for (int idx = 0; idx < file->remembered_types.size(); idx++) {
			column_types.push_back((Variant::Type)file->remembered_types[idx]);
		}
	} else {
		file->remembered_keys = column_keys.duplicate();
		file->remembered_types.clear();
		for (const Variant::Type type : column_types) {
			file->remembered_types.push_back(type);
		}
	}
}

void DatasetEditor::_update_side_bar() {
	side_bar->clear();
	for (uint32_t idx = 0; idx < open_files.size(); idx++) {
		const OpenFile &file = open_files[idx];
		const String path = file.resource->get_path();
		String file_title = path.is_empty() ? vformat(TTR("[unsaved %s]"), file.resource->get_class()) : path.get_file();
		if (_is_unsaved(file)) {
			file_title += "(*)";
		}
		side_bar->add_item(file_title, EditorNode::get_singleton()->get_object_icon(file.resource.ptr(), "File"));
		side_bar->set_item_tooltip(side_bar->get_item_count() - 1, path);
	}
	if (current >= 0) {
		side_bar->select(current);
	}
}

void DatasetEditor::_resource_changed() {
	// A message of an earlier action is stale now; the validation that follows writes the status again.
	status_label->set_text(String());
	_queue_update();
}

void DatasetEditor::_queue_update() {
	if (!update_queued) {
		update_queued = true;
		callable_mp(this, &DatasetEditor::_update).call_deferred();
	}
}

void DatasetEditor::_json_text_check() {
	// JSON.parse() of the text editor emits no `changed`: the table catches up when the mouse comes back.
	if (_is_json()) {
		_queue_update();
	}
}

void DatasetEditor::_update() {
	update_queued = false;
	_update_side_bar();
	OpenFile *file = _get_current_file();
	if (file) {
		csv = Ref<CSV>(Object::cast_to<CSV>(file->resource.ptr()));
		// The table, undo/redo or a script changed the data: show it in the text editor, so neither side
		// overwrites the other. A CSV changed through its API has no kept text.
		if (push_text || (csv.is_valid() && csv->get_parsed_text().is_empty())) {
			TextEditorBase *text_editor = _get_text_editor();
			const TextEdit *text_edit = text_editor ? Object::cast_to<TextEdit>(text_editor->get_base_editor()) : nullptr;
			// Reloading the same text would parse it again, emit `changed` and queue the next update: a loop.
			if (text_edit && text_edit->get_text() != _dataset_lf(_get_text())) {
				text_editor->reload_text();
				file->reloaded_version = text_edit->get_version();
			}
		}
	}
	push_text = false;
	_build_view();

	const bool has_table = source != SOURCE_NONE;
	table_view->set_visible(has_table);
	empty_label->set_visible(!has_table);
	// Undo and Redo stay reachable while a file is open, also when its text can't be parsed.
	values_menu->set_disabled(!file);
	PopupMenu *values_popup = values_menu->get_popup();
	for (int id : { VALUES_ADD_ROW, VALUES_INSERT_ROW, VALUES_DELETE_ROWS, VALUES_INSERT_COLUMN, VALUES_REMOVE_COLUMN }) {
		values_popup->set_item_disabled(values_popup->get_item_index(id), !has_table);
	}
	table_menu->set_disabled(!has_table);
	file_menu->set_disabled(!file);
	schema_label->set_visible(source == SOURCE_CSV);
	schema_picker->set_visible(source == SOURCE_CSV);
	if (!file) {
		empty_label->set_text(TTR("Open a CSV file, or a JSON, YAML, TOML, INI or XML file with table-shaped data: double-click it in the FileSystem dock, or use \"Open in Dataset Dock\" in its context menu."));
		page_label->set_text(String());
		previous_button->set_disabled(true);
		next_button->set_disabled(true);
		table_view->clear();
		rendered_hash = 0;
		return;
	}
	if (!has_table) {
		// A parse error leaves no data; the text editor shows where.
		const String error = _get_error(file->resource.ptr());
		empty_label->set_text(error.is_empty() ? TTR("This file has no table-shaped data: an array of dictionaries (JSON, YAML, TOML), INI sections, or XML elements with attributes under the root element.") : vformat(TTR("The text can't be parsed: %s"), error));
		page_label->set_text(String());
		previous_button->set_disabled(true);
		next_button->set_disabled(true);
		rendered_hash = 0;
		return;
	}
	schema_picker->set_edited_resource(file->schema);
	// Rendered only when the data changed: a re-parse of the same text must not close an open cell editor.
	const uint32_t view_hash = hash_murmur3_one_32(file->page, hash_murmur3_one_32(table_key.hash(), hash_murmur3_one_32(column_keys.hash(), _get_resource_data(file->resource.ptr()).hash())));
	if (view_hash != rendered_hash) {
		rendered_hash = view_hash;
		_render();
		if (source == SOURCE_CSV && validate_timer->is_inside_tree()) {
			validate_timer->start();
		}
	}
}

void DatasetEditor::_render() {
	OpenFile *file = _get_current_file();
	if (!file || source == SOURCE_NONE) {
		table_view->clear();
		return;
	}
	updating = true;
	const int row_count = _get_row_count();
	const int column_count = MAX(_get_column_count(), 1);
	const int page_count = MAX(1, (row_count + DATASET_PAGE_ROWS - 1) / DATASET_PAGE_ROWS);
	file->page = CLAMP(file->page, 0, page_count - 1);
	const int first = file->page * DATASET_PAGE_ROWS;
	const int last = MIN(first + DATASET_PAGE_ROWS, row_count);
	const bool json = _is_json();

	if (table_view->get_columns() != column_count + 1) {
		table_view->clear();
		table_view->set_columns(column_count + 1);
	}
	table_view->set_column_title(0, source == SOURCE_SECTIONS ? TTR("Section") : String("#"));
	table_view->set_column_title_tooltip_text(0, source == SOURCE_SECTIONS ? TTR("Double-click to rename the section.") : String());
	table_view->set_column_expand(0, false);
	table_view->set_column_custom_minimum_width(0, (source == SOURCE_SECTIONS ? 140 : 60) * EDSCALE);
	const PackedStringArray header = source == SOURCE_CSV ? csv->get_header() : PackedStringArray();
	for (int column = 0; column < column_count; column++) {
		String column_title;
		String tooltip;
		if (source == SOURCE_CSV) {
			const Ref<DataField> field = _get_field(column);
			column_title = column < header.size() ? header[column] : itos(column + 1);
			tooltip = field.is_valid() ? vformat("%s\n%s", Variant::get_type_name(field->get_value_type()), field->get_description()).strip_edges() : String();
		} else if (column < column_keys.size()) {
			column_title = String(column_keys[column]);
			const Variant::Type type = column < (int)column_types.size() ? column_types[column] : Variant::NIL;
			tooltip = type == MIXED_TYPE ? TTR("Mixed types") : (type == Variant::NIL ? TTR("Any type") : Variant::get_type_name(type));
		}
		table_view->set_column_title(column + 1, column_title);
		table_view->set_column_title_tooltip_text(column + 1, tooltip);
		table_view->set_column_clip_content(column + 1, true);
	}
	_update_column_widths();

	TreeItem *root = table_view->get_root() ? table_view->get_root() : table_view->create_item();
	TreeItem *item = root->get_first_child();
	const Color issue_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.35);
	const Color missing_color = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.15);
	for (int row = first; row < last; row++) {
		if (!item) {
			item = table_view->create_item(root);
		}
		item->set_metadata(0, row);
		item->set_cell_mode(0, TreeItem::CELL_MODE_STRING);
		item->set_text(0, source == SOURCE_SECTIONS ? section_names[row] : itos(row + 1));
		item->set_selectable(0, source == SOURCE_SECTIONS);
		item->set_editable(0, source == SOURCE_SECTIONS);
		const PackedStringArray cells = source == SOURCE_CSV ? csv->get_row(row) : PackedStringArray();
		const Dictionary fields = source == SOURCE_CSV ? Dictionary() : get_fields(source, records[row]);
		for (int column = 0; column <= column_count; column++) {
			const String *issue = cell_issues.getptr(_dataset_cell_key(row, column));
			item->set_tooltip_text(column, issue ? *issue : String());
			if (issue) {
				item->set_custom_bg_color(column, issue_color);
			} else {
				item->clear_custom_bg_color(column);
			}
			if (column == 0) {
				continue;
			}
			item->set_editable(column, true);
			if (source != SOURCE_CSV) {
				if (column - 1 >= column_keys.size()) {
					item->set_cell_mode(column, TreeItem::CELL_MODE_STRING);
					item->set_text(column, String());
					continue;
				}
				const Variant &key = column_keys[column - 1];
				if (!fields.has(key)) {
					item->set_cell_mode(column, TreeItem::CELL_MODE_STRING);
					item->set_text(column, String());
					item->set_custom_bg_color(column, missing_color);
					item->set_tooltip_text(column, TTR("Not set in this row. Type a value to add the field."));
				} else if (fields[key].get_type() == Variant::BOOL) {
					item->set_cell_mode(column, TreeItem::CELL_MODE_CHECK);
					item->set_checked(column, fields[key]);
					item->set_text(column, String());
				} else {
					item->set_cell_mode(column, TreeItem::CELL_MODE_STRING);
					item->set_text(column, value_to_text(fields[key], json));
				}
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
		}
		item = item->get_next();
	}
	while (item) {
		TreeItem *next = item->get_next();
		memdelete(item);
		item = next;
	}

	page_label->set_text(row_count == 0 ? TTR("No rows") : vformat(TTR("Rows %d-%d of %d"), first + 1, last, row_count));
	previous_button->set_disabled(file->page == 0);
	next_button->set_disabled(file->page >= page_count - 1);
	updating = false;
}

void DatasetEditor::_validate() {
	cell_issues.clear();
	const OpenFile *file = _get_current_file();
	if (!file || source != SOURCE_CSV || file->schema.is_null()) {
		if (source == SOURCE_CSV) {
			status_label->set_text(csv->get_error_message());
		}
		_render();
		return;
	}
	const Ref<DataValidation> result = csv->validate(file->schema);
	ERR_FAIL_COND(result.is_null());
	int issue_count = 0;
	String first_issue;
	for (int idx = 0; idx < result->get_issue_count(); idx++) {
		const int row = result->get_issue_row(idx);
		const int column = result->get_issue_column(idx);
		if (row != DataValidation::POSITION_NONE) {
			const int64_t key = _dataset_cell_key(row, column == DataValidation::POSITION_NONE ? 0 : column + 1);
			const String *previous = cell_issues.getptr(key);
			cell_issues[key] = previous ? *previous + "\n" + result->get_issue_message(idx) : result->get_issue_message(idx);
		}
		first_issue = issue_count == 0 ? result->get_issue_message(idx) : first_issue;
		issue_count++;
	}
	status_label->set_text(issue_count == 0 ? TTR("No issues.") : vformat(TTR("%d issue(s). First: %s"), issue_count, first_issue));
	_render();
}

void DatasetEditor::_cell_edited() {
	if (updating) {
		return;
	}
	TreeItem *item = table_view->get_edited();
	const int tree_column = table_view->get_edited_column();
	ERR_FAIL_COND(!item || tree_column < 0);
	const int row = item->get_metadata(0);
	if (tree_column == 0) {
		_rename_section(row, item->get_text(0));
		return;
	}
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
	_commit_cells(TTR("Set Cell"), row, tree_column - 1, cells);
}

void DatasetEditor::_values_menu_id_pressed(int p_id) {
	switch (p_id) {
		case VALUES_ADD_ROW:
			_add_row(false);
			break;
		case VALUES_INSERT_ROW:
			_add_row(true);
			break;
		case VALUES_DELETE_ROWS:
			_delete_rows();
			break;
		case VALUES_INSERT_COLUMN:
			column_name_edit->clear();
			column_dialog->popup_centered(Size2(320, 0) * EDSCALE);
			column_name_edit->grab_focus();
			break;
		case VALUES_REMOVE_COLUMN:
			_remove_column();
			break;
		case VALUES_UNDO:
			_undo_redo(false);
			break;
		case VALUES_REDO:
			_undo_redo(true);
			break;
	}
}

void DatasetEditor::_table_menu_id_pressed(int p_id) {
	const Resource *resource = _get_resource();
	if (!resource) {
		return;
	}
	export_format = p_id;
	const String extension = p_id == TABLE_EXPORT_CSV ? "csv" : "json";
	export_dialog->clear_filters();
	export_dialog->add_filter("*." + extension, p_id == TABLE_EXPORT_CSV ? TTR("CSV File") : TTR("JSON File"));
	const String path = resource->get_path();
	if (path.is_resource_file()) {
		export_dialog->set_current_path(path.get_basename() + "_export." + extension);
	}
	export_dialog->set_title(p_id == TABLE_EXPORT_CSV ? TTR("Export as CSV") : TTR("Export as JSON"));
	export_dialog->popup_file_dialog();
}

void DatasetEditor::_file_menu_id_pressed(int p_id) {
	if (current < 0) {
		return;
	}
	switch (p_id) {
		case FILE_SAVE:
			_save();
			break;
		case FILE_OPEN_TEXT:
			_open_text();
			break;
		case FILE_SHOW_IN_FILESYSTEM: {
			const String path = open_files[current].resource->get_path();
			if (path.is_resource_file() && FileSystemDock::get_singleton()) {
				FileSystemDock::get_singleton()->navigate_to_path(path);
			}
		} break;
		case FILE_CLOSE:
			_request_close({ current });
			break;
		case FILE_CLOSE_OTHERS:
		case FILE_CLOSE_ALL: {
			Vector<int> indices;
			for (int idx = 0; idx < (int)open_files.size(); idx++) {
				if (p_id == FILE_CLOSE_ALL || idx != current) {
					indices.push_back(idx);
				}
			}
			_request_close(indices);
		} break;
	}
}

void DatasetEditor::_table_selected(int p_index) {
	OpenFile *file = _get_current_file();
	if (!file) {
		return;
	}
	file->table_key = table_picker->get_item_metadata(p_index);
	file->source = SOURCE_NONE; // The first table with the key.
	for (const Table &table : find_tables(file->resource.ptr())) {
		if (table.key == file->table_key) {
			file->source = table.source;
			break;
		}
	}
	file->page = 0;
	cell_issues.clear();
	rendered_hash = 0;
	_update();
}

void DatasetEditor::_file_selected(int p_index) {
	if (p_index == current) {
		return;
	}
	current = p_index;
	cell_issues.clear();
	rendered_hash = 0;
	_update();
	_validate();
}

void DatasetEditor::_file_clicked(int p_index, const Vector2 &p_position, int p_mouse_button) {
	if (p_mouse_button == (int)MouseButton::MIDDLE) {
		_request_close({ p_index });
	} else if (p_mouse_button == (int)MouseButton::RIGHT) {
		_file_selected(p_index);
		side_bar_menu->set_position(side_bar->get_screen_position() + p_position);
		side_bar_menu->reset_size();
		side_bar_menu->popup();
	}
}

void DatasetEditor::_resource_saved(Object *p_resource) {
	for (uint32_t idx = 0; idx < open_files.size(); idx++) {
		if (open_files[idx].resource.ptr() != p_resource) {
			continue;
		}
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		if (undo_redo->has_history(open_files[idx].history_id)) {
			undo_redo->set_history_as_saved(open_files[idx].history_id);
		}
		if ((int)idx == current) {
			resource_saved = true;
			status_label->set_text(vformat(TTR("Saved \"%s\"."), open_files[idx].resource->get_path()));
		}
		_update_side_bar();
	}
}

void DatasetEditor::_update_column_widths() {
	// Readable columns: about seven fill the table, further columns scroll horizontally (#346).
	const int width = MAX(int(120 * EDSCALE), int((table_view->get_size().x - 80 * EDSCALE) / 7));
	for (int column = 1; column < table_view->get_columns(); column++) {
		table_view->set_column_custom_minimum_width(column, width);
	}
}

void DatasetEditor::_change_page(int p_delta) {
	OpenFile *file = _get_current_file();
	if (file) {
		file->page += p_delta;
		rendered_hash = 0;
		_render();
	}
}

void DatasetEditor::_schema_changed(const Ref<Resource> &p_schema) {
	OpenFile *file = _get_current_file();
	if (file) {
		file->schema = p_schema;
		_validate();
	}
}

void DatasetEditor::_tree_gui_input(const Ref<InputEvent> &p_event) {
	if (p_event->is_action_pressed(SNAME("ui_copy"), false, true)) {
		_copy();
		table_view->accept_event();
		return;
	}
	if (p_event->is_action_pressed(SNAME("ui_paste"), false, true)) {
		_paste();
		table_view->accept_event();
		return;
	}
	const Ref<InputEventKey> key_event = p_event;
	if (key_event.is_null() || !key_event->is_pressed() || key_event->is_echo()) {
		return; // Shortcuts match released keys too: Ctrl+S would save twice.
	}
	if (ED_IS_SHORTCUT("editor/save_scene", p_event)) {
		_save();
		table_view->accept_event();
	} else if (p_event->is_action_pressed(SNAME("ui_redo"), false, true)) {
		// Before ui_undo: Ctrl+Shift+Z also matches Ctrl+Z without exact matching.
		_undo_redo(true);
		table_view->accept_event();
	} else if (p_event->is_action_pressed(SNAME("ui_undo"), false, true)) {
		_undo_redo(false);
		table_view->accept_event();
	} else if (key_event->get_keycode() == Key::KEY_DELETE) {
		_delete_rows();
		table_view->accept_event();
	}
}

void DatasetEditor::_file_moved(const String &p_old_file, const String &p_new_file) {
	_update_side_bar(); // The resources have their new paths (also for moved folders).
}

void DatasetEditor::_file_removed(const String &p_file) {
	for (int idx = (int)open_files.size() - 1; idx >= 0; idx--) {
		if (open_files[idx].resource->get_path() == p_file) {
			_close_file(idx);
		}
	}
}

void DatasetEditor::_folder_removed(const String &p_folder) {
	const String folder = p_folder.ends_with("/") ? p_folder : p_folder + "/";
	for (int idx = (int)open_files.size() - 1; idx >= 0; idx--) {
		if (open_files[idx].resource->get_path().begins_with(folder)) {
			_close_file(idx);
		}
	}
}

void DatasetEditor::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (FileSystemDock::get_singleton()) {
			FileSystemDock::get_singleton()->connect(SNAME("files_moved"), callable_mp(this, &DatasetEditor::_file_moved));
			FileSystemDock::get_singleton()->connect(SNAME("folder_moved"), callable_mp(this, &DatasetEditor::_file_moved));
			FileSystemDock::get_singleton()->connect(SNAME("file_removed"), callable_mp(this, &DatasetEditor::_file_removed));
			FileSystemDock::get_singleton()->connect(SNAME("folder_removed"), callable_mp(this, &DatasetEditor::_folder_removed));
		}
		EditorNode::get_singleton()->connect(SNAME("resource_saved"), callable_mp(this, &DatasetEditor::_resource_saved));
	} else if (p_what == NOTIFICATION_THEME_CHANGED) {
		rendered_hash = 0;
		if (source != SOURCE_NONE) {
			_render();
		}
	}
}

void DatasetEditor::edit(const Ref<Resource> &p_resource) {
	if (p_resource.is_null()) {
		return;
	}
	int index = -1;
	for (uint32_t idx = 0; idx < open_files.size(); idx++) {
		if (open_files[idx].resource == p_resource) {
			index = idx;
			break;
		}
	}
	if (index < 0) {
		if (!can_edit(p_resource.ptr())) {
			return;
		}
		OpenFile file;
		file.resource = p_resource;
		file.history_id = next_history_id--;
		if (Object::cast_to<CSV>(p_resource.ptr())) {
			file.schema = EditorDataSchemaLookup::find_schema(p_resource->get_path());
		}
		p_resource->connect_changed(callable_mp(this, &DatasetEditor::_resource_changed));
		open_files.push_back(file);
		index = open_files.size() - 1;
	}
	if (index != current) {
		current = index;
		cell_issues.clear();
		rendered_hash = 0;
	}
	_update();
	_validate();
	const String path = p_resource->get_path();
	if (path.is_resource_file() && FileAccess::exists(path) && FileAccess::get_size(path) > AUTO_OPEN_SIZE) {
		status_label->set_text(vformat(TTR("Large file (%s): each edit encodes and parses the whole file and keeps a copy of the text for undo."), String::humanize_size(FileAccess::get_size(path))));
	}
}

DatasetEditor::DatasetEditor() {
	singleton = this;
	set_name(TTRC("Dataset"));
	set_icon_name("Grid");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_dataset_bottom_panel", TTRC("Toggle Dataset Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);

	HSplitContainer *split = memnew(HSplitContainer);
	add_child(split);

	side_bar = memnew(ItemList);
	side_bar->set_custom_minimum_size(Size2(160, 0) * EDSCALE);
	side_bar->set_allow_rmb_select(true);
	side_bar->set_tooltip_text(TTR("Open dataset files. Each file has its own undo history. Middle-click closes a file, right-click shows more."));
	side_bar->connect(SceneStringName(item_selected), callable_mp(this, &DatasetEditor::_file_selected));
	side_bar->connect("item_clicked", callable_mp(this, &DatasetEditor::_file_clicked));
	split->add_child(side_bar);

	VBoxContainer *main_vbox = memnew(VBoxContainer);
	main_vbox->set_h_size_flags(SIZE_EXPAND_FILL);
	split->add_child(main_vbox);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	main_vbox->add_child(toolbar);

	// File (Godot's menu for file actions), Values and Table (#420).
	file_menu = memnew(MenuButton);
	file_menu->set_text(TTR("File"));
	file_menu->set_flat(false);
	file_menu->set_theme_type_variation("FlatMenuButton");
	side_bar_menu = memnew(PopupMenu);
	add_child(side_bar_menu);
	for (PopupMenu *popup : { file_menu->get_popup(), side_bar_menu }) {
		popup->add_item(TTR("Save"), FILE_SAVE);
		popup->add_item(TTR("Open as Text"), FILE_OPEN_TEXT);
		popup->add_item(TTR("Show in FileSystem"), FILE_SHOW_IN_FILESYSTEM);
		popup->add_separator();
		popup->add_item(TTR("Close"), FILE_CLOSE);
		popup->add_item(TTR("Close Others"), FILE_CLOSE_OTHERS);
		popup->add_item(TTR("Close All"), FILE_CLOSE_ALL);
		popup->connect(SceneStringName(id_pressed), callable_mp(this, &DatasetEditor::_file_menu_id_pressed));
	}
	toolbar->add_child(file_menu);

	values_menu = memnew(MenuButton);
	values_menu->set_text(TTR("Values"));
	values_menu->set_flat(false);
	values_menu->set_theme_type_variation("FlatMenuButton");
	PopupMenu *values_popup = values_menu->get_popup();
	values_popup->add_item(TTR("Add Row"), VALUES_ADD_ROW);
	values_popup->add_item(TTR("Insert Row Above"), VALUES_INSERT_ROW);
	values_popup->add_item(TTR("Delete Rows"), VALUES_DELETE_ROWS);
	values_popup->set_item_tooltip(-1, TTR("Delete the rows of the selected cells (Delete)."));
	values_popup->add_separator();
	values_popup->add_item(TTR("Insert Column..."), VALUES_INSERT_COLUMN);
	values_popup->set_item_tooltip(-1, TTR("Insert a column left of the selected cell's column, or at the end."));
	values_popup->add_item(TTR("Remove Column"), VALUES_REMOVE_COLUMN);
	values_popup->set_item_tooltip(-1, TTR("Remove the column of the selected cell."));
	values_popup->add_separator();
	values_popup->add_item(TTR("Undo"), VALUES_UNDO);
	values_popup->set_item_tooltip(-1, TTR("Undo the last edit of this file (Ctrl+Z in the table)."));
	values_popup->add_item(TTR("Redo"), VALUES_REDO);
	values_popup->connect(SceneStringName(id_pressed), callable_mp(this, &DatasetEditor::_values_menu_id_pressed));
	toolbar->add_child(values_menu);

	table_menu = memnew(MenuButton);
	table_menu->set_text(TTR("Table"));
	table_menu->set_flat(false);
	table_menu->set_theme_type_variation("FlatMenuButton");
	table_menu->get_popup()->add_item(TTR("Export as CSV..."), TABLE_EXPORT_CSV);
	table_menu->get_popup()->add_item(TTR("Export as JSON..."), TABLE_EXPORT_JSON);
	table_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &DatasetEditor::_table_menu_id_pressed));
	toolbar->add_child(table_menu);

	table_picker = memnew(OptionButton);
	table_picker->set_tooltip_text(TTR("The table of the file to show: an array of dictionaries under a top-level key, or the XML elements of one tag."));
	table_picker->connect(SceneStringName(item_selected), callable_mp(this, &DatasetEditor::_table_selected));
	table_picker->hide();
	toolbar->add_child(table_picker);

	schema_label = memnew(Label(TTR("Schema:")));
	toolbar->add_child(schema_label);
	schema_picker = memnew(EditorResourcePicker);
	schema_picker->set_base_type("DataSchema");
	schema_picker->set_custom_minimum_size(Size2(200 * EDSCALE, 0));
	schema_picker->set_tooltip_text(TTR("Types the cells and checks them. Found by the schema's file patterns in the project setting \"filesystem/data/schema_files\"; a schema picked here is not saved."));
	schema_picker->connect("resource_changed", callable_mp(this, &DatasetEditor::_schema_changed));
	toolbar->add_child(schema_picker);

	Control *spacer = memnew(Control);
	spacer->set_h_size_flags(SIZE_EXPAND_FILL);
	toolbar->add_child(spacer);
	previous_button = memnew(Button(TTR("Previous")));
	previous_button->connect(SceneStringName(pressed), callable_mp(this, &DatasetEditor::_change_page).bind(-1));
	toolbar->add_child(previous_button);
	page_label = memnew(Label);
	toolbar->add_child(page_label);
	next_button = memnew(Button(TTR("Next")));
	next_button->connect(SceneStringName(pressed), callable_mp(this, &DatasetEditor::_change_page).bind(1));
	toolbar->add_child(next_button);

	table_view = memnew(Tree);
	table_view->set_v_size_flags(SIZE_EXPAND_FILL);
	table_view->set_hide_root(true);
	table_view->set_column_titles_visible(true);
	table_view->set_select_mode(Tree::SELECT_MULTI);
	table_view->connect("item_edited", callable_mp(this, &DatasetEditor::_cell_edited));
	table_view->connect(SceneStringName(gui_input), callable_mp(this, &DatasetEditor::_tree_gui_input));
	table_view->connect(SceneStringName(mouse_entered), callable_mp(this, &DatasetEditor::_json_text_check));
	table_view->connect(SceneStringName(resized), callable_mp(this, &DatasetEditor::_update_column_widths));
	table_view->hide();
	main_vbox->add_child(table_view);

	empty_label = memnew(Label);
	empty_label->set_v_size_flags(SIZE_EXPAND_FILL);
	empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	empty_label->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
	main_vbox->add_child(empty_label);

	status_label = memnew(Label);
	status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	status_label->set_h_size_flags(SIZE_EXPAND_FILL);
	main_vbox->add_child(status_label);

	// Validation runs once after a burst of edits, not per edit (large files).
	validate_timer = memnew(Timer);
	validate_timer->set_one_shot(true);
	validate_timer->set_wait_time(0.5);
	validate_timer->connect("timeout", callable_mp(this, &DatasetEditor::_validate));
	add_child(validate_timer);

	column_dialog = memnew(ConfirmationDialog);
	column_dialog->set_title(TTR("Insert Column"));
	VBoxContainer *column_vbox = memnew(VBoxContainer);
	column_dialog->add_child(column_vbox);
	column_vbox->add_child(memnew(Label(TTR("Column name:"))));
	column_name_edit = memnew(LineEdit);
	column_vbox->add_child(column_name_edit);
	column_dialog->register_text_enter(column_name_edit);
	column_dialog->connect(SceneStringName(confirmed), callable_mp(this, &DatasetEditor::_insert_column));
	add_child(column_dialog);

	// Like the script editor: unsaved changes are saved, discarded or the close is canceled.
	close_dialog = memnew(ConfirmationDialog);
	close_dialog->set_title(TTR("Unsaved Changes"));
	close_dialog->set_ok_button_text(TTR("Save"));
	close_dialog->add_button(TTR("Discard"), DisplayServer::get_singleton()->get_swap_cancel_ok(), "discard");
	close_dialog->connect(SceneStringName(confirmed), callable_mp(this, &DatasetEditor::_close_pending).bind(true));
	close_dialog->connect("custom_action", callable_mp(this, &DatasetEditor::_close_dialog_action));
	add_child(close_dialog);

	export_dialog = memnew(EditorFileDialog);
	export_dialog->set_file_mode(FileDialog::FILE_MODE_SAVE_FILE);
	export_dialog->set_access(FileDialog::ACCESS_RESOURCES);
	export_dialog->connect("file_selected", callable_mp(this, &DatasetEditor::_export));
	add_child(export_dialog);

	_update();
}

DatasetEditor::~DatasetEditor() {
	for (const OpenFile &file : open_files) {
		if (file.resource.is_valid()) {
			file.resource->disconnect_changed(callable_mp(this, &DatasetEditor::_resource_changed));
		}
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

void EditorContextMenuPluginDataset::_open_text(const Dictionary &p_data) {
	const PackedStringArray selected = p_data.get("selected_files", PackedStringArray());
	ERR_FAIL_COND(selected.size() != 1);
	const Ref<Resource> resource = ResourceLoader::load(selected[0]);
	if (resource.is_valid() && ScriptEditor::get_singleton() && ScriptEditor::get_singleton()->edit(resource, true)) {
		EditorInterface::get_singleton()->set_main_screen_editor("Script");
	}
}

void EditorContextMenuPluginDataset::_open_dataset(const Dictionary &p_data) {
	const PackedStringArray selected = p_data.get("selected_files", PackedStringArray());
	ERR_FAIL_COND(selected.size() != 1);
	// Any table, also of INI files and translation CSVs: the item names the dock it opens.
	const Ref<Resource> resource = ResourceLoader::load(selected[0]);
	DatasetEditor *dataset_editor = DatasetEditor::get_singleton();
	if (dataset_editor && DatasetEditor::can_edit(resource.ptr())) {
		dataset_editor->edit(resource);
		dataset_editor->make_visible();
		return;
	}
	String message = vformat(TTR("\"%s\" has no table-shaped data."), selected[0].get_file());
	const JSON *json = Object::cast_to<JSON>(resource.ptr());
	const TextDataFormat *format = Object::cast_to<TextDataFormat>(resource.ptr());
	const String error = json ? json->get_error_message() : (format ? format->get_error_message() : String());
	if (resource.is_null() || !error.is_empty()) {
		message = vformat(TTR("\"%s\" can't be parsed: %s"), selected[0].get_file(), error);
	}
	EditorToaster::get_singleton()->popup_str(message, EditorToaster::SEVERITY_WARNING);
}

void EditorContextMenuPluginDataset::get_options(const OptionsData &p_data) {
	EditorContextMenuPlugin::get_options(p_data);
	const PackedStringArray selected = p_data.get("selected_files", PackedStringArray());
	if (selected.size() != 1) {
		return;
	}
	// By the type the file loads as, without loading it (a large file opens only when the item is used).
	const String type = ResourceLoader::get_resource_type(selected[0]);
	if (type != "JSON" && !ClassDB::is_parent_class(type, "TextDataFormat")) {
		return;
	}
	Control *gui_base = EditorNode::get_singleton()->get_gui_base();
	add_context_menu_item(TTR("Open as Text"), callable_mp(this, &EditorContextMenuPluginDataset::_open_text), gui_base->get_editor_theme_icon(SNAME("TextFile")));
	add_context_menu_item(TTR("Open in Dataset Dock"), callable_mp(this, &EditorContextMenuPluginDataset::_open_dataset), gui_base->get_editor_theme_icon(SNAME("Grid")));
}

void DatasetEditorPlugin::edit(Object *p_object) {
	Resource *resource = Object::cast_to<Resource>(p_object);
	if (resource) {
		dataset_editor->edit(Ref<Resource>(resource));
	}
}

bool DatasetEditorPlugin::handles(Object *p_object) const {
	return DatasetEditor::can_open_automatically(p_object);
}

void DatasetEditorPlugin::make_visible(bool p_visible) {
	// Opens only: the open files stay in the side bar while other objects are edited (ARCHITECTURE.md 7.3).
	if (p_visible) {
		dataset_editor->make_visible();
	}
}

DatasetEditorPlugin::DatasetEditorPlugin() {
	dataset_editor = memnew(DatasetEditor);
	dataset_editor->set_custom_minimum_size(Size2(0, 250) * EDSCALE);
	EditorDockManager::get_singleton()->add_dock(dataset_editor);
	dataset_editor->close();

	context_menu_plugin.instantiate();
	add_context_menu_plugin(EditorContextMenuPlugin::CONTEXT_SLOT_FILESYSTEM, context_menu_plugin);
}
