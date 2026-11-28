/**************************************************************************/
/*  ini_editor.cpp                                                        */
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

#include "ini_editor.h"

#include "core/input/input_event.h"
#include "core/io/resource_text_serializer.h"
#include "core/object/callable_mp.h"
#include "core/variant/variant_utility.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"

// The types offered in the Type column; a value of another type adds its own.
static const Variant::Type INI_EDITOR_VALUE_TYPES[] = { Variant::BOOL, Variant::INT, Variant::FLOAT, Variant::STRING, Variant::VECTOR2, Variant::VECTOR3, Variant::COLOR, Variant::ARRAY, Variant::DICTIONARY };

enum INIEditorColumn {
	INI_EDITOR_COLUMN_KEY,
	INI_EDITOR_COLUMN_TYPE,
	INI_EDITOR_COLUMN_VALUE,
};

// What one line of INI text is, by the rules of ResourceTextSerializer::parse_ini_sections(): one item per line,
// `;` and `#` start a comment line, keys before the first header belong to the section "".
struct INIEditorTextLine {
	enum Kind {
		KIND_OTHER, // Comment or blank line.
		KIND_HEADER,
		KIND_KEY,
	};
	Kind kind = KIND_OTHER;
	String section; // The section of the line; a header's own name.
	String key;
};

static LocalVector<INIEditorTextLine> _ini_editor_scan(const Vector<String> &p_lines) {
	LocalVector<INIEditorTextLine> result;
	result.resize(p_lines.size());
	String current;
	for (int i = 0; i < p_lines.size(); i++) {
		String line = p_lines[i].strip_edges();
		if (i == 0 && !line.is_empty() && line[0] == 0xFEFF) {
			line = line.substr(1).strip_edges();
		}
		INIEditorTextLine &info = result[i];
		if (!line.is_empty() && line[0] == '[') {
			current = line.substr(1, line.length() - 2).strip_edges();
			info.kind = INIEditorTextLine::KIND_HEADER;
		} else if (!line.is_empty() && line[0] != ';' && line[0] != '#') {
			info.kind = INIEditorTextLine::KIND_KEY;
			info.key = line.substr(0, MAX(line.find_char('='), 0)).strip_edges();
			if (info.key.begins_with("\"")) {
				const Variant parsed = ResourceTextSerializer::ini_text_to_value(info.key);
				if (parsed.get_type() == Variant::STRING) {
					info.key = parsed;
				}
			}
		}
		info.section = current;
	}
	return result;
}

// Replaces the text of a line and keeps its "\r" (CRLF files).
static void _ini_editor_set_line(Vector<String> &r_lines, int p_index, const String &p_text) {
	r_lines.write[p_index] = p_text + (r_lines[p_index].ends_with("\r") ? "\r" : "");
}

static String _ini_editor_get_unique_name(const PackedStringArray &p_names, const String &p_base) {
	String result = p_base;
	for (int i = 2; p_names.has(result); i++) {
		result = p_base + "_" + itos(i);
	}
	return result;
}

// Text of a value in a cell: Strings as they are, every other type in Variant syntax.
static String _ini_editor_value_to_text(const Variant &p_value) {
	return p_value.get_type() == Variant::STRING ? String(p_value) : VariantUtilityFunctions::var_to_str(p_value);
}

// A String typed in a plain file or a cell becomes the target type when it is written in that type's syntax.
static Variant _ini_editor_convert(const Variant &p_value, Variant::Type p_type) {
	if (p_value.get_type() == Variant::STRING && p_type != Variant::STRING) {
		const Variant parsed = VariantUtilityFunctions::str_to_var(p_value);
		if (parsed.get_type() == p_type) {
			return parsed;
		}
	}
	return VariantUtilityFunctions::type_convert(p_value, p_type);
}

TextEditorBase *INIEditor::_get_text_editor() const {
	return Object::cast_to<TextEditorBase>(ScriptEditor::get_singleton()->get_script_container()->get_resource_editor(ini));
}

bool INIEditor::_can_edit() {
	if (ini.is_null()) {
		return false;
	}
	TextEditorBase *text_editor = _get_text_editor();
	const TextEdit *text_edit = text_editor ? Object::cast_to<TextEdit>(text_editor->get_base_editor()) : nullptr;
	if (text_edit) {
		// Takes over text typed since the text editor's last idle parse, so the dock edits the current text.
		const bool in_sync = ini->get_parsed_text().is_empty() ? text_edit->get_version() == reloaded_version : text_edit->get_text() == ini->get_parsed_text();
		if (!in_sync) {
			ini->parse(text_edit->get_text(), true);
		}
	}
	if (!ini->get_error_message().is_empty()) {
		status_label->set_text(vformat(TTR("Fix the INI text first (line %d): %s"), ini->get_error_line(), ini->get_error_message()));
		return false;
	}
	return true;
}

String INIEditor::_get_text() const {
	const String text = ini->get_parsed_text();
	return text.is_empty() ? ini->encode_to_text() : text;
}

void INIEditor::_edit_text(const String &p_action, TextChange p_change, const String &p_key, const String &p_to, const Variant &p_value) {
	const String old_text = _get_text();
	Vector<String> lines = old_text.split("\n");
	const LocalVector<INIEditorTextLine> info = _ini_editor_scan(lines);
	const String cr = old_text.contains("\r\n") ? "\r" : "";
	const bool plain = ResourceTextSerializer::get_options(ini->get_options()).ini_plain_values;
	// Where appended lines go: before the empty element of a final line break.
	const int end = lines.size() > 0 && lines[lines.size() - 1].is_empty() ? lines.size() - 1 : lines.size();
	String select = section;

	switch (p_change) {
		case TEXT_CHANGE_SET_VALUE: {
			// The last line of a repeated key wins when the file is read.
			for (int i = (int)info.size() - 1; i >= 0; i--) {
				if (info[i].kind == INIEditorTextLine::KIND_KEY && info[i].section == section && info[i].key == p_key) {
					const String line = lines[i].trim_suffix("\r");
					const int equal = line.find_char('=');
					const bool space = line.substr(equal + 1).begins_with(" ");
					_ini_editor_set_line(lines, i, line.substr(0, equal + 1) + (space ? " " : "") + ResourceTextSerializer::ini_value_to_text(p_value, plain));
					break;
				}
			}
		} break;
		case TEXT_CHANGE_RENAME_KEY: {
			for (uint32_t i = 0; i < info.size(); i++) {
				if (info[i].kind == INIEditorTextLine::KIND_KEY && info[i].section == section && info[i].key == p_key) {
					// Keeps the indentation and the spaces around `=`.
					const String line = lines[i].trim_suffix("\r");
					const int start = line.length() - line.strip_edges(true, false).length();
					const int equal = line.find_char('=');
					const String spaces = line.substr(start, equal - start);
					_ini_editor_set_line(lines, i, line.substr(0, start) + p_to.property_name_encode() + spaces.substr(spaces.strip_edges(false, true).length()) + line.substr(equal));
				}
			}
		} break;
		case TEXT_CHANGE_REMOVE_KEYS: {
			const PackedStringArray removed = p_value;
			for (int i = (int)info.size() - 1; i >= 0; i--) {
				if (info[i].kind == INIEditorTextLine::KIND_KEY && info[i].section == section && removed.has(info[i].key)) {
					lines.remove_at(i);
				}
			}
		} break;
		case TEXT_CHANGE_ADD_KEY: {
			// After the section's last key or header; keys without a section before the first header.
			int at = -1;
			int first_header = -1;
			for (uint32_t i = 0; i < info.size(); i++) {
				if (info[i].kind == INIEditorTextLine::KIND_HEADER && first_header < 0) {
					first_header = i;
				}
				if (info[i].kind != INIEditorTextLine::KIND_OTHER && info[i].section == section && (info[i].kind == INIEditorTextLine::KIND_KEY || !section.is_empty())) {
					at = i + 1;
				}
			}
			if (at < 0) {
				at = first_header >= 0 ? first_header : end;
				if (first_header >= 0) {
					lines.insert(at, cr); // A blank line before the first header.
				}
			}
			lines.insert(at, p_key.property_name_encode() + "=" + ResourceTextSerializer::ini_value_to_text(p_value, plain) + cr);
		} break;
		case TEXT_CHANGE_ADD_SECTION: {
			int at = end;
			if (at > 0 && !lines[at - 1].strip_edges().is_empty()) {
				lines.insert(at++, cr);
			}
			lines.insert(at, "[" + p_to + "]" + cr);
			if (at == lines.size() - 1) {
				lines.push_back(String()); // The file ends with a line break.
			}
			select = p_to;
		} break;
		case TEXT_CHANGE_RENAME_SECTION: {
			for (uint32_t i = 0; i < info.size(); i++) {
				if (info[i].kind == INIEditorTextLine::KIND_HEADER && info[i].section == section) {
					const String line = lines[i].trim_suffix("\r");
					_ini_editor_set_line(lines, i, line.substr(0, line.find_char('[')) + "[" + p_to + "]");
				}
			}
			select = p_to;
		} break;
		case TEXT_CHANGE_REMOVE_SECTION: {
			// Every block of the section with its comments; for "" only its keys.
			for (int i = (int)info.size() - 1; i >= 0; i--) {
				if (info[i].section == section && (!section.is_empty() || info[i].kind == INIEditorTextLine::KIND_KEY)) {
					lines.remove_at(i);
				}
			}
		} break;
	}

	const String new_text = String("\n").join(lines);
	if (new_text == old_text) {
		return;
	}
	// The text is committed, not the data: comments, blank lines and formatting stay, and undo restores the exact text.
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action);
	undo_redo->add_do_method(ini.ptr(), "parse", new_text, true);
	undo_redo->add_undo_method(ini.ptr(), "parse", old_text, true);
	previous_section = section;
	section = select;
	undo_redo->commit_action(); // `changed` renders the dock deferred, not while a tree emits a signal.
}

void INIEditor::_ini_changed() {
	status_label->set_text(String());
	if (!update_queued) {
		update_queued = true;
		callable_mp(this, &INIEditor::_update).call_deferred();
	}
}

String INIEditor::_get_render_key() const {
	// The kept text, or the data with types when a script changed it.
	const String text = ini->get_parsed_text();
	return text.is_empty() ? VariantUtilityFunctions::var_to_str(ini->get_data()) : text;
}

void INIEditor::_update() {
	update_queued = false;
	if (ini.is_null()) {
		return;
	}
	// Show a change of the dock, undo/redo or a script in the text editor, so neither side overwrites the other.
	// Text the text editor parsed itself is equal and stays as it is.
	TextEditorBase *text_editor = _get_text_editor();
	const TextEdit *text_edit = text_editor ? Object::cast_to<TextEdit>(text_editor->get_base_editor()) : nullptr;
	if (text_edit && text_edit->get_text() != _get_text()) {
		text_editor->reload_text();
		reloaded_version = text_edit->get_version();
	}
	// The text editor parses the reloaded text again: same text, and a render would close an open cell editor.
	if (_get_render_key() != rendered_key) {
		_render();
	}
}

void INIEditor::_render() {
	section_tree->clear();
	TreeItem *root = section_tree->create_item();
	const Dictionary file_data = ini.is_valid() ? Dictionary(ini->get_data()) : Dictionary();
	rendered_key = ini.is_valid() ? _get_render_key() : String();
	if (!file_data.has(section) && file_data.has(previous_section)) {
		section = previous_section; // Undo of a rename or removal selects the section again.
	}
	if (!file_data.has(section) && !file_data.is_empty()) {
		section = file_data.get_key_at_index(0);
	}
	section_tree->set_block_signals(true); // select() emits item_selected; the keys are rendered once below.
	for (const KeyValue<Variant, Variant> &kv : file_data) {
		const String name = kv.key;
		TreeItem *item = section_tree->create_item(root);
		item->set_metadata(0, name);
		if (name.is_empty()) {
			item->set_text(0, TTR("(no section)"));
			item->set_tooltip_text(0, TTR("Keys before the first section header."));
			item->set_custom_color(0, get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
		} else {
			item->set_text(0, name);
			item->set_editable(0, true);
		}
		if (name == section) {
			item->select(0);
		}
	}
	section_tree->set_block_signals(false);
	_section_selected();
	if (edit_section || !edit_key.is_empty()) {
		callable_mp(this, &INIEditor::_edit_added).call_deferred();
	}
}

void INIEditor::_edit_added() {
	// After the last render of the action: a later render would close the cell editor again.
	if (edit_section && section_tree->get_selected()) {
		section_tree->edit_selected(true);
	}
	for (TreeItem *item = key_tree->get_root()->get_first_child(); item && !edit_key.is_empty(); item = item->get_next()) {
		if (String(item->get_metadata(INI_EDITOR_COLUMN_KEY)) == edit_key) {
			key_tree->set_selected(item, INI_EDITOR_COLUMN_KEY);
			key_tree->edit_selected(true);
		}
	}
	edit_section = false;
	edit_key = String();
}

void INIEditor::_section_selected() {
	const TreeItem *selected = section_tree->get_selected();
	if (selected) {
		section = selected->get_metadata(0);
	}
	key_tree->clear();
	const Dictionary file_data = ini.is_valid() ? Dictionary(ini->get_data()) : Dictionary();
	const Dictionary keys = file_data.get(section, Dictionary());
	remove_section_button->set_disabled(!file_data.has(section));
	add_section_button->set_disabled(ini.is_null());
	add_key_button->set_disabled(ini.is_null());
	remove_key_button->set_disabled(keys.is_empty());
	if (ini.is_null()) {
		empty_label->set_text(TTR("Double-click an INI file (.ini, .cfg) in the FileSystem dock to open it here."));
	} else if (file_data.is_empty()) {
		empty_label->set_text(TTR("The file has no keys yet. Add a section or a key."));
	} else if (keys.is_empty()) {
		empty_label->set_text(vformat(TTR("The section \"%s\" has no keys yet. Add one with Add Key."), section));
	}
	empty_label->set_visible(keys.is_empty());

	const Ref<SerializerOptions> options = ini.is_valid() ? ini->get_options() : Ref<SerializerOptions>();
	const bool plain = options.is_valid() && options->get_ini_value_style() == SerializerOptions::INI_VALUE_STYLE_PLAIN;
	TreeItem *root = key_tree->create_item();
	for (const KeyValue<Variant, Variant> &kv : keys) {
		TreeItem *item = key_tree->create_item(root);
		item->set_metadata(INI_EDITOR_COLUMN_KEY, kv.key);
		item->set_text(INI_EDITOR_COLUMN_KEY, kv.key);
		item->set_editable(INI_EDITOR_COLUMN_KEY, true);

		const Variant::Type type = kv.value.get_type();
		String type_names;
		int type_index = -1;
		for (int i = 0; i < (int)std_size(INI_EDITOR_VALUE_TYPES); i++) {
			type_names += (i > 0 ? "," : "") + Variant::get_type_name(INI_EDITOR_VALUE_TYPES[i]);
			if (INI_EDITOR_VALUE_TYPES[i] == type) {
				type_index = i;
			}
		}
		if (type_index < 0) {
			type_index = (int)std_size(INI_EDITOR_VALUE_TYPES);
			type_names += "," + Variant::get_type_name(type);
		}
		item->set_cell_mode(INI_EDITOR_COLUMN_TYPE, TreeItem::CELL_MODE_RANGE);
		item->set_text(INI_EDITOR_COLUMN_TYPE, type_names);
		item->set_range(INI_EDITOR_COLUMN_TYPE, type_index);
		// Plain files store every value as text.
		item->set_editable(INI_EDITOR_COLUMN_TYPE, !plain);
		if (plain) {
			item->set_tooltip_text(INI_EDITOR_COLUMN_TYPE, TTR("Plain INI files store every value as text (SerializerOptions.ini_value_style)."));
		}

		if (type == Variant::BOOL) {
			item->set_cell_mode(INI_EDITOR_COLUMN_VALUE, TreeItem::CELL_MODE_CHECK);
			item->set_checked(INI_EDITOR_COLUMN_VALUE, kv.value);
			item->set_text(INI_EDITOR_COLUMN_VALUE, kv.value ? "true" : "false");
		} else {
			const String text = _ini_editor_value_to_text(kv.value);
			item->set_text(INI_EDITOR_COLUMN_VALUE, text);
			item->set_tooltip_text(INI_EDITOR_COLUMN_VALUE, text);
		}
		item->set_editable(INI_EDITOR_COLUMN_VALUE, true);
	}
}

void INIEditor::_section_edited() {
	TreeItem *item = section_tree->get_edited();
	const String from = item->get_metadata(0);
	const String to = item->get_text(0).strip_edges();
	if (to == from || !_can_edit()) {
		callable_mp(this, &INIEditor::_render).call_deferred(); // Restores the name; not while the tree is emitting.
		return;
	}
	if (to.is_empty() || to.contains_char(']') || to.contains_char('\n')) {
		status_label->set_text(TTR("A section name can't be empty or contain \"]\"."));
	} else if (ini->has_section(to)) {
		status_label->set_text(vformat(TTR("A section named \"%s\" already exists."), to));
	} else {
		section = from;
		_edit_text(TTR("Rename INI Section"), TEXT_CHANGE_RENAME_SECTION, String(), to);
		return;
	}
	callable_mp(this, &INIEditor::_render).call_deferred();
}

void INIEditor::_key_edited() {
	TreeItem *item = key_tree->get_edited();
	const int column = key_tree->get_edited_column();
	const String key = item->get_metadata(INI_EDITOR_COLUMN_KEY);
	if (!_can_edit() || !ini->has_section_key(section, key)) {
		callable_mp(this, &INIEditor::_render).call_deferred();
		return;
	}
	const Variant value = ini->get_value(section, key);
	String error;
	if (column == INI_EDITOR_COLUMN_KEY) {
		const String to = item->get_text(INI_EDITOR_COLUMN_KEY).strip_edges();
		if (to == key) {
			return;
		}
		if (to.is_empty() || to.contains_char('=') || to.contains_char('\n')) {
			error = TTR("A key name can't be empty or contain \"=\".");
		} else if (to[0] == ';' || to[0] == '#' || to[0] == '[' || to[0] == '"') {
			// The line would be read as a comment, a section header or a quoted name.
			error = TTR("A key name can't start with \";\", \"#\", \"[\" or a quote.");
		} else if (ini->has_section_key(section, to)) {
			error = vformat(TTR("The key \"%s\" already exists in this section."), to);
		} else {
			_edit_text(TTR("Rename INI Key"), TEXT_CHANGE_RENAME_KEY, key, to);
			return;
		}
	} else if (column == INI_EDITOR_COLUMN_TYPE) {
		const Variant::Type type = Variant::get_type_by_name(item->get_text(INI_EDITOR_COLUMN_TYPE).get_slicec(',', (int)item->get_range(INI_EDITOR_COLUMN_TYPE)));
		if (type == value.get_type()) {
			return;
		}
		const Variant converted = _ini_editor_convert(value, type);
		_edit_text(TTR("Change INI Value Type"), TEXT_CHANGE_SET_VALUE, key, String(), converted);
		const String from_text = _ini_editor_value_to_text(value);
		const String to_text = _ini_editor_value_to_text(converted);
		if (from_text != to_text) {
			status_label->set_text(vformat(TTR("Converted \"%s\" to %s."), from_text, to_text)); // A lossy conversion is visible.
		}
		return;
	} else {
		const String text = item->get_text(INI_EDITOR_COLUMN_VALUE);
		Variant new_value;
		switch (value.get_type()) {
			case Variant::BOOL:
				new_value = item->is_checked(INI_EDITOR_COLUMN_VALUE);
				break;
			case Variant::STRING:
				new_value = text;
				break;
			case Variant::INT:
				if (text.strip_edges().is_valid_int()) {
					new_value = text.strip_edges().to_int();
				}
				break;
			case Variant::FLOAT:
				if (text.strip_edges().is_valid_float()) {
					new_value = text.strip_edges().to_float();
				}
				break;
			default: {
				const Variant parsed = VariantUtilityFunctions::str_to_var(text);
				if (parsed.get_type() == value.get_type()) {
					new_value = parsed;
				}
			} break;
		}
		if (new_value.get_type() == Variant::NIL) {
			error = vformat(TTR("\"%s\" is not a valid %s value."), text, Variant::get_type_name(value.get_type()));
		} else if (new_value != value || new_value.get_type() != value.get_type()) {
			_edit_text(TTR("Set INI Value"), TEXT_CHANGE_SET_VALUE, key, String(), new_value);
			return;
		}
	}
	callable_mp(this, &INIEditor::_render).call_deferred(); // Restores the cell.
	status_label->set_text(error);
}

void INIEditor::_add_section() {
	if (!_can_edit()) {
		return;
	}
	edit_section = true;
	_edit_text(TTR("Add INI Section"), TEXT_CHANGE_ADD_SECTION, String(), _ini_editor_get_unique_name(ini->get_sections(), "new_section"));
}

void INIEditor::_remove_section() {
	if (!_can_edit() || !ini->has_section(section)) {
		return;
	}
	_edit_text(TTR("Remove INI Section"), TEXT_CHANGE_REMOVE_SECTION);
}

void INIEditor::_add_key() {
	if (!_can_edit()) {
		return;
	}
	const PackedStringArray keys = ini->has_section(section) ? ini->get_section_keys(section) : PackedStringArray();
	edit_key = _ini_editor_get_unique_name(keys, "new_key");
	_edit_text(TTR("Add INI Key"), TEXT_CHANGE_ADD_KEY, edit_key, String(), String());
}

void INIEditor::_remove_keys() {
	if (!_can_edit() || !ini->has_section(section)) {
		return;
	}
	// The section stays, also when it has no keys left.
	PackedStringArray keys;
	for (TreeItem *item = key_tree->get_next_selected(nullptr); item; item = key_tree->get_next_selected(item)) {
		keys.push_back(item->get_metadata(INI_EDITOR_COLUMN_KEY));
	}
	if (!keys.is_empty()) {
		_edit_text(TTR("Remove INI Keys"), TEXT_CHANGE_REMOVE_KEYS, String(), String(), keys);
	}
}

void INIEditor::_save() {
	if (!_can_edit()) {
		return;
	}
	EditorNode::get_singleton()->save_resource(ini);
	TextEditorBase *text_editor = _get_text_editor();
	if (text_editor) {
		text_editor->tag_saved_version();
	}
}

void INIEditor::_resource_saved(Object *p_resource) {
	if (p_resource && p_resource == ini.ptr()) {
		status_label->set_text(vformat(TTR("Saved \"%s\"."), ini->get_path()));
	}
}

void INIEditor::_tree_gui_input(const Ref<InputEvent> &p_event, bool p_sections) {
	Tree *tree = p_sections ? section_tree : key_tree;
	const Ref<InputEventKey> key_event = p_event;
	if (key_event.is_null() || !key_event->is_pressed() || key_event->is_echo()) {
		return; // Shortcuts match released keys too: Ctrl+S would save twice.
	}
	if (ED_IS_SHORTCUT("editor/save_scene", p_event)) {
		_save();
		tree->accept_event();
	} else if (key_event->get_keycode() == Key::KEY_DELETE) {
		if (p_sections) {
			_remove_section();
		} else {
			_remove_keys();
		}
		tree->accept_event();
	} else if (key_event->get_keycode() == Key::F2 && tree->get_selected()) {
		// "(no section)" can't be renamed.
		if (tree->get_selected()->is_editable(0)) {
			tree->set_selected(tree->get_selected(), 0);
			tree->edit_selected();
		}
		tree->accept_event();
	}
}

void INIEditor::_file_removed(const String &p_file) {
	if (ini.is_valid() && p_file == ini->get_path()) {
		edit(Ref<INI>());
		close();
	}
}

void INIEditor::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (FileSystemDock::get_singleton()) {
			FileSystemDock::get_singleton()->connect(SNAME("file_removed"), callable_mp(this, &INIEditor::_file_removed));
		}
		EditorNode::get_singleton()->connect(SNAME("resource_saved"), callable_mp(this, &INIEditor::_resource_saved));
		save_button->set_tooltip_text(vformat(TTR("Save the file (%s in the dock)."), ED_GET_SHORTCUT("editor/save_scene")->get_as_text()));
	} else if (p_what == NOTIFICATION_THEME_CHANGED && ini.is_valid()) {
		_render(); // The color of "(no section)".
	}
}

void INIEditor::edit(const Ref<INI> &p_ini) {
	if (ini == p_ini) {
		return;
	}
	if (ini.is_valid()) {
		ini->disconnect_changed(callable_mp(this, &INIEditor::_ini_changed));
	}
	ini = p_ini;
	section = String();
	reloaded_version = UINT32_MAX;
	status_label->set_text(String());
	file_label->set_text(ini.is_valid() ? ini->get_path().get_file() : String());
	if (ini.is_valid()) {
		ini->connect_changed(callable_mp(this, &INIEditor::_ini_changed));
		if (!ini->get_error_message().is_empty()) {
			status_label->set_text(vformat(TTR("Fix the INI text first (line %d): %s"), ini->get_error_line(), ini->get_error_message()));
		}
	}
	_render();
}

INIEditor::INIEditor() {
	singleton = this;
	set_name(TTRC("INI"));
	set_icon_name("TextFile");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_ini_bottom_panel", TTRC("Toggle INI Dock")));
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

	add_section_button = memnew(Button(TTR("Add Section")));
	add_section_button->set_tooltip_text(TTR("Add a section at the end of the file and name it."));
	add_section_button->connect(SceneStringName(pressed), callable_mp(this, &INIEditor::_add_section));
	toolbar->add_child(add_section_button);
	remove_section_button = memnew(Button(TTR("Remove Section")));
	remove_section_button->set_tooltip_text(TTR("Remove the selected section with all its keys (Delete in the section list)."));
	remove_section_button->connect(SceneStringName(pressed), callable_mp(this, &INIEditor::_remove_section));
	toolbar->add_child(remove_section_button);
	add_key_button = memnew(Button(TTR("Add Key")));
	add_key_button->set_tooltip_text(TTR("Add a key with an empty String value to the selected section."));
	add_key_button->connect(SceneStringName(pressed), callable_mp(this, &INIEditor::_add_key));
	toolbar->add_child(add_key_button);
	remove_key_button = memnew(Button(TTR("Remove Keys")));
	remove_key_button->set_tooltip_text(TTR("Remove the selected keys (Delete in the key table)."));
	remove_key_button->connect(SceneStringName(pressed), callable_mp(this, &INIEditor::_remove_keys));
	toolbar->add_child(remove_key_button);
	save_button = memnew(Button(TTR("Save")));
	save_button->connect(SceneStringName(pressed), callable_mp(this, &INIEditor::_save));
	toolbar->add_child(save_button);

	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(SIZE_EXPAND_FILL);
	main_vbox->add_child(split);

	section_tree = memnew(Tree);
	section_tree->set_custom_minimum_size(Size2(180 * EDSCALE, 0));
	section_tree->set_hide_root(true);
	section_tree->set_column_titles_visible(true);
	section_tree->set_column_title(0, TTR("Sections"));
	section_tree->connect(SceneStringName(item_selected), callable_mp(this, &INIEditor::_section_selected));
	section_tree->connect("item_edited", callable_mp(this, &INIEditor::_section_edited));
	section_tree->connect(SceneStringName(gui_input), callable_mp(this, &INIEditor::_tree_gui_input).bind(true));
	split->add_child(section_tree);

	key_tree = memnew(Tree);
	key_tree->set_h_size_flags(SIZE_EXPAND_FILL);
	key_tree->set_columns(3);
	key_tree->set_hide_root(true);
	key_tree->set_column_titles_visible(true);
	key_tree->set_select_mode(Tree::SELECT_MULTI);
	key_tree->set_column_title(INI_EDITOR_COLUMN_KEY, TTR("Key"));
	key_tree->set_column_title(INI_EDITOR_COLUMN_TYPE, TTR("Type"));
	key_tree->set_column_title(INI_EDITOR_COLUMN_VALUE, TTR("Value"));
	key_tree->set_column_expand_ratio(INI_EDITOR_COLUMN_KEY, 1);
	key_tree->set_column_expand(INI_EDITOR_COLUMN_TYPE, false);
	key_tree->set_column_custom_minimum_width(INI_EDITOR_COLUMN_TYPE, 120 * EDSCALE);
	key_tree->set_column_expand_ratio(INI_EDITOR_COLUMN_VALUE, 2);
	key_tree->set_column_clip_content(INI_EDITOR_COLUMN_VALUE, true);
	key_tree->connect("item_edited", callable_mp(this, &INIEditor::_key_edited));
	key_tree->connect(SceneStringName(gui_input), callable_mp(this, &INIEditor::_tree_gui_input).bind(false));
	split->add_child(key_tree);
	// Shown on the empty table, which stays visible so a new key is edited right away.
	empty_label = memnew(Label);
	empty_label->set_anchors_and_offsets_preset(PRESET_FULL_RECT);
	empty_label->set_mouse_filter(MOUSE_FILTER_IGNORE);
	empty_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	empty_label->set_vertical_alignment(VERTICAL_ALIGNMENT_CENTER);
	empty_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	key_tree->add_child(empty_label);

	status_label = memnew(Label);
	status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	status_label->set_h_size_flags(SIZE_EXPAND_FILL);
	main_vbox->add_child(status_label);
}

INIEditor::~INIEditor() {
	if (singleton == this) {
		singleton = nullptr;
	}
}

bool INIEditor::can_edit(const Object *p_object) {
	const INI *ini_resource = Object::cast_to<INI>(p_object);
	return ini_resource && ini_resource->is_recognized_path(ini_resource->get_path());
}

void INIEditorPlugin::edit(Object *p_object) {
	ini_editor->edit(Ref<INI>(Object::cast_to<INI>(p_object)));
}

bool INIEditorPlugin::handles(Object *p_object) const {
	return INIEditor::can_edit(p_object);
}

void INIEditorPlugin::make_visible(bool p_visible) {
	// Opens only: the dock stays at the last INI file while other objects are edited (ARCHITECTURE.md 7.3).
	if (p_visible) {
		ini_editor->make_visible();
	}
}

INIEditorPlugin::INIEditorPlugin() {
	ini_editor = memnew(INIEditor);
	ini_editor->set_custom_minimum_size(Size2(0, 250) * EDSCALE);
	EditorDockManager::get_singleton()->add_dock(ini_editor);
	ini_editor->close();
}
