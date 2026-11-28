/**************************************************************************/
/*  data_file_inspector_plugin.cpp                                        */
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

#include "data_file_inspector_plugin.h"

#include "core/core_string_names.h"
#include "core/io/data_schema.h"
#include "core/io/json.h"
#include "core/io/text_data_format.h"
#include "core/io/xml_format.h"
#include "core/object/callable_mp.h"
#include "editor/data/dataset_editor.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/create_dialog.h"
#include "editor/inspector/csv_inspector_plugin.h"
#include "editor/inspector/ini_inspector_plugin.h"
#include "editor/inspector/xml_inspector_plugin.h"
#include "editor/script/data_schema_lookup.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "scene/resources/font.h"
#include "scene/scene_string_names.h"

enum {
	COLUMN_KEY,
	COLUMN_TYPE,
	COLUMN_VALUE,
};

// Metadata of a tree item: column 0 holds its Variant, column 1 its child index in the parent's value.
// A placeholder child (not filled yet) has index -1, a "more" item -1 - (index of the next child).
static constexpr int PLACEHOLDER_INDEX = -1;

// DataFileTreeAdapter

DataFileTreeAdapter::Entry DataFileTreeAdapter::make_entry(const String &p_key, const Variant &p_value, bool p_xml) {
	Entry entry;
	entry.key = p_key;
	entry.data = p_value;
	entry.child_count = get_child_count(p_value, p_xml);
	if (p_xml && p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary element = p_value;
		entry.key = String(element.get(XML::KEY_NAME, String()));
		entry.type = "Element";
		entry.value = String(element.get(XML::KEY_TEXT, String()));
		if (entry.value.is_empty()) {
			const Dictionary attributes = element.get(XML::KEY_ATTRIBUTES, Dictionary());
			PackedStringArray parts;
			for (const KeyValue<Variant, Variant> &kv : attributes) {
				parts.push_back(vformat("%s=\"%s\"", kv.key, kv.value));
			}
			entry.value = String(" ").join(parts);
		}
	} else if (p_value.get_type() == Variant::DICTIONARY) {
		entry.type = "Dictionary";
		entry.value = vformat(TTRN("%d key", "%d keys", entry.child_count), entry.child_count);
		return entry;
	} else if (p_value.is_array()) {
		entry.type = Variant::get_type_name(p_value.get_type());
		entry.value = vformat(TTRN("%d item", "%d items", entry.child_count), entry.child_count);
		return entry;
	} else {
		entry.type = p_xml ? (p_key.begins_with("@") ? "Attribute" : "Text") : Variant::get_type_name(p_value.get_type());
		entry.value = p_value.get_type() == Variant::NIL ? String("null") : String(p_value);
	}
	entry.value = entry.value.replace("\n", " ");
	if (entry.value.length() > MAX_VALUE_LENGTH) {
		entry.value = entry.value.left(MAX_VALUE_LENGTH) + "...";
	}
	return entry;
}

int DataFileTreeAdapter::get_child_count(const Variant &p_value, bool p_xml) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		if (!p_xml) {
			return dictionary.size();
		}
		const Dictionary attributes = dictionary.get(XML::KEY_ATTRIBUTES, Dictionary());
		const Array nodes = dictionary.get(XML::KEY_CHILDREN, Array());
		return attributes.size() + nodes.size();
	}
	if (p_value.is_array()) {
		const Array array = p_value;
		return array.size();
	}
	return 0;
}

void DataFileTreeAdapter::get_children(const Variant &p_value, bool p_xml, int p_from, int p_count, LocalVector<Entry> &r_entries) {
	const int total = get_child_count(p_value, p_xml);
	const int end = p_count < 0 ? total : MIN(total, p_from + p_count);
	if (p_from < 0 || p_from >= end) {
		return;
	}

	Array nodes; // Array items, or the child nodes of an XML element after its attributes.
	int node_offset = 0;
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_value;
		const Dictionary pairs = p_xml ? Dictionary(dictionary.get(XML::KEY_ATTRIBUTES, Dictionary())) : dictionary;
		int index = 0;
		for (const KeyValue<Variant, Variant> &kv : pairs) {
			if (index >= end) {
				break;
			}
			if (index >= p_from) {
				r_entries.push_back(make_entry(p_xml ? "@" + String(kv.key) : String(kv.key), kv.value, p_xml));
			}
			index++;
		}
		if (!p_xml) {
			return;
		}
		nodes = dictionary.get(XML::KEY_CHILDREN, Array());
		node_offset = pairs.size();
	} else {
		nodes = p_value;
	}

	for (int i = MAX(p_from, node_offset); i < end; i++) {
		const Variant &node = nodes[i - node_offset];
		// In XML, element nodes take their tag as key; other nodes are text.
		const String key = p_xml ? (node.get_type() == Variant::DICTIONARY ? String() : String("#text")) : itos(i);
		r_entries.push_back(make_entry(key, node, p_xml));
	}
}

static void _find_in(const Variant &p_value, bool p_xml, const String &p_query, int p_max, DataFileTreeAdapter::DecorateFunc p_decorate, const void *p_context, PackedInt32Array &r_path, Vector<PackedInt32Array> &r_paths) {
	LocalVector<DataFileTreeAdapter::Entry> entries;
	DataFileTreeAdapter::get_children(p_value, p_xml, 0, -1, entries);
	const bool top_level = r_path.is_empty();
	for (uint32_t i = 0; i < entries.size() && r_paths.size() < p_max; i++) {
		DataFileTreeAdapter::Entry &entry = entries[i];
		if (p_decorate) {
			p_decorate(p_context, entry, top_level);
		}
		// Only scalar values: containers show their size, XML elements repeat their text and attributes.
		const bool scalar = !entry.data.is_array() && entry.data.get_type() != Variant::DICTIONARY;
		r_path.push_back(i);
		if (entry.key.findn(p_query) != -1 || (scalar && entry.value.findn(p_query) != -1)) {
			r_paths.push_back(r_path);
		}
		if (entry.child_count > 0) {
			_find_in(entry.data, p_xml, p_query, p_max, p_decorate, p_context, r_path, r_paths);
		}
		r_path.resize(r_path.size() - 1);
	}
}

Vector<PackedInt32Array> DataFileTreeAdapter::find(const Variant &p_root, bool p_xml, const String &p_query, int p_max, DecorateFunc p_decorate, const void *p_context) {
	Vector<PackedInt32Array> paths;
	if (!p_query.is_empty() && p_max > 0) {
		PackedInt32Array path;
		_find_in(p_root, p_xml, p_query, p_max, p_decorate, p_context, path, paths);
	}
	return paths;
}

String DataFileTreeAdapter::make_child_path(const String &p_parent_path, const String &p_key, int p_index, bool p_xml) {
	const String segment = p_xml ? itos(p_index) : p_key.replace("\\", "\\\\").replace("/", "\\/");
	return p_parent_path + "/" + segment;
}

PackedInt32Array DataFileTreeAdapter::get_column_chars(const PackedStringArray &p_header, const Array &p_rows, int p_max_rows, int p_max_columns, int p_max_chars) {
	const int row_count = MIN(p_rows.size(), p_max_rows);
	int column_count = p_header.size();
	for (int i = 0; i < row_count; i++) {
		column_count = MAX(column_count, (int)PackedStringArray(p_rows[i]).size());
	}

	PackedInt32Array chars;
	chars.resize_initialized(MIN(column_count, p_max_columns));
	int *w = chars.ptrw();
	for (int i = -1; i < row_count; i++) {
		const PackedStringArray cells = i < 0 ? p_header : PackedStringArray(p_rows[i]);
		for (int column = 0; column < MIN(cells.size(), chars.size()); column++) {
			w[column] = MIN(MAX(w[column], cells[column].length()), p_max_chars);
		}
	}
	return chars;
}

// DataFileInspectorView

void DataFileInspectorView::_decorate_found(const void *p_context, DataFileTreeAdapter::Entry &r_entry, bool p_top_level) {
	static_cast<const DataFileInspectorView *>(p_context)->decorate_row(r_entry, p_top_level);
}

String DataFileInspectorView::_get_parsed_text() const {
	const Ref<JSON> json = resource;
	if (json.is_valid()) {
		return json->get_parsed_text();
	}
	const Ref<TextDataFormat> format = resource;
	return format.is_valid() ? format->get_parsed_text() : String();
}

TreeItem *DataFileInspectorView::_create_item(TreeItem *p_parent, const DataFileTreeAdapter::Entry &p_entry, int p_index, bool p_lazy) {
	DataFileTreeAdapter::Entry entry = p_entry;
	decorate_row(entry, p_parent == tree->get_root());
	TreeItem *item = tree->create_item(p_parent);
	item->set_text(COLUMN_KEY, entry.key);
	item->set_text(COLUMN_TYPE, entry.type);
	item->set_text(COLUMN_VALUE, entry.value);
	item->set_tooltip_text(COLUMN_VALUE, entry.value);
	item->set_metadata(0, entry.data);
	item->set_metadata(1, p_index);
	if (p_lazy && entry.child_count > 0) {
		tree->create_item(item)->set_metadata(1, PLACEHOLDER_INDEX);
		item->set_collapsed(true);
	}
	return item;
}

void DataFileInspectorView::_fill(TreeItem *p_parent, int p_from) {
	const Variant value = p_parent->get_metadata(0);
	LocalVector<DataFileTreeAdapter::Entry> entries;
	DataFileTreeAdapter::get_children(value, xml, p_from, CHILD_PAGE, entries);
	for (uint32_t i = 0; i < entries.size(); i++) {
		_create_item(p_parent, entries[i], p_from + (int)i, true);
	}
	const int next = p_from + (int)entries.size();
	const int rest = DataFileTreeAdapter::get_child_count(value, xml) - next;
	if (rest > 0) {
		TreeItem *more = tree->create_item(p_parent);
		more->set_text(COLUMN_KEY, vformat(TTR("%d more..."), rest));
		more->set_tooltip_text(COLUMN_KEY, vformat(TTR("Select to show the next %d."), MIN(rest, CHILD_PAGE)));
		more->set_metadata(1, -1 - next);
	}
}

String DataFileInspectorView::_get_item_path(TreeItem *p_item) const {
	// Tree items: the key in column 0 and the child index in metadata 1. CSV rows: the row number in column 0.
	TreeItem *parent = p_item->get_parent();
	if (parent == nullptr) {
		return String();
	}
	return DataFileTreeAdapter::make_child_path(_get_item_path(parent), p_item->get_text(0), p_item->get_metadata(1), xml);
}

void DataFileInspectorView::_collect_expanded(TreeItem *p_item, HashSet<String> &r_paths) const {
	for (TreeItem *child = p_item->get_first_child(); child; child = child->get_next()) {
		TreeItem *first = child->get_first_child();
		if (!child->is_collapsed() && first && int(first->get_metadata(1)) != PLACEHOLDER_INDEX) {
			r_paths.insert(_get_item_path(child));
			_collect_expanded(child, r_paths);
		}
	}
}

void DataFileInspectorView::_restore(TreeItem *p_item, const HashSet<String> &p_expanded, const String &p_selected) {
	for (TreeItem *child = p_item->get_first_child(); child; child = child->get_next()) {
		if (int(child->get_metadata(1)) < PLACEHOLDER_INDEX) {
			continue; // A "more" item.
		}
		const String path = _get_item_path(child);
		if (path == p_selected) {
			child->select(0);
		}
		if (child->is_collapsed() && p_expanded.has(path)) {
			child->set_collapsed(false);
			_expand(child); // Now, to restore the expanded children too.
		}
		if (!child->is_collapsed()) {
			_restore(child, p_expanded, p_selected);
		}
	}
}

String DataFileInspectorView::get_summary_text(const Variant &p_data) const {
	const DataFileTreeAdapter::Entry entry = DataFileTreeAdapter::make_entry(String(), p_data, false);
	return entry.data.is_array() || entry.data.get_type() == Variant::DICTIONARY ? entry.type + ", " + entry.value : entry.type;
}

int DataFileInspectorView::_get_row_height() const {
	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Tree"));
	const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Tree"));
	return (int)(font.is_valid() ? font->get_height(font_size) + get_theme_constant(SNAME("v_separation"), SNAME("Tree")) : 24 * EDSCALE);
}

void DataFileInspectorView::_update_preview(const Variant &p_data) {
	tree_value = get_tree_value(p_data);
	_update_tree();
}

void DataFileInspectorView::_update_tree() {
	tree->clear();
	tree->set_columns(3);
	tree->set_hide_folding(false);
	tree->set_column_title(COLUMN_KEY, TTR("Key"));
	tree->set_column_title(COLUMN_TYPE, TTR("Type"));
	tree->set_column_title(COLUMN_VALUE, TTR("Value"));
	for (int column = COLUMN_KEY; column <= COLUMN_VALUE; column++) {
		tree->set_column_title_alignment(column, HORIZONTAL_ALIGNMENT_LEFT);
		tree->set_column_expand(column, column != COLUMN_TYPE);
		tree->set_column_clip_content(column, true);
	}
	tree->set_column_custom_minimum_width(COLUMN_TYPE, (int)(90 * EDSCALE));
	tree->set_column_expand_ratio(COLUMN_VALUE, 2);

	TreeItem *root = tree->create_item();
	root->set_metadata(0, tree_value);
	const String query = search->get_text().strip_edges();
	if (query.is_empty()) {
		_fill(root, 0);
		more_label->hide();
	} else {
		// Only the matching paths, expanded; clearing the search goes back to the lazy tree.
		const Vector<PackedInt32Array> paths = DataFileTreeAdapter::find(tree_value, xml, query, MAX_MATCHES, &_decorate_found, this);
		const Color match_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
		for (const PackedInt32Array &path : paths) {
			TreeItem *item = root;
			for (int i = 0; i < path.size() && item; i++) {
				TreeItem *child = item->get_first_child();
				while (child && int(child->get_metadata(1)) != path[i]) {
					child = child->get_next();
				}
				if (child == nullptr) {
					LocalVector<DataFileTreeAdapter::Entry> entries;
					DataFileTreeAdapter::get_children(item->get_metadata(0), xml, path[i], 1, entries);
					child = entries.is_empty() ? nullptr : _create_item(item, entries[0], path[i], false);
				}
				item = child;
			}
			if (item) {
				item->set_custom_color(COLUMN_KEY, match_color);
			}
		}
		more_label->set_text(paths.size() >= MAX_MATCHES ? vformat(TTR("The first %d matches."), MAX_MATCHES) : vformat(TTRN("%d match.", "%d matches.", (int)paths.size()), paths.size()));
		more_label->show();
	}

	const int top_level_count = MIN(DataFileTreeAdapter::get_child_count(tree_value, xml), CHILD_PAGE);
	tree->set_custom_minimum_size(Size2(0, (CLAMP(top_level_count, 3, 14) + 2) * _get_row_height()));
}

void DataFileInspectorView::_update_dataset_button() {
	// The Dataset dock is shared by every format with a table (#420, #436); formats add their own docks with
	// _add_dock_button(). "Open as Text" is in the FileSystem context menu. Runs once per change of the data
	// (debounced), like the preview; see the unit test "Dataset button cost" for the time on large files.
	const Vector<DatasetEditor::Table> tables = DatasetEditor::find_tables(resource.ptr());
	dataset_button->set_visible(!tables.is_empty());
	String tooltip = TTR("Edit the rows in the Dataset dock.");
	for (const DatasetEditor::Table &table : tables) {
		String table_name = String(table.key);
		if (table.source == DatasetEditor::SOURCE_SECTIONS) {
			table_name = TTR("Sections");
		} else if (table.key.get_type() == Variant::NIL) {
			table_name = TTR("Whole file");
		} else if (table.source == DatasetEditor::SOURCE_ELEMENTS) {
			table_name = "<" + table_name + ">";
		}
		tooltip += "\n" + vformat(TTRN("%s: %d row", "%s: %d rows", table.row_count), table_name, table.row_count);
	}
	dataset_button->set_tooltip_text(tooltip);
}

Button *DataFileInspectorView::_add_dock_button(const String &p_text, const String &p_tooltip, const Callable &p_pressed) {
	Button *button = memnew(Button(p_text));
	button->set_tooltip_text(p_tooltip);
	button->connect(SceneStringName(pressed), p_pressed);
	button_row->add_child(button);
	button_row->move_child(button, dataset_button->get_index());
	return button;
}

void DataFileInspectorView::_queue_update() {
	// Once after a burst of changes (the text editor parses the file again while typing).
	update_timer->start();
}

void DataFileInspectorView::_update() {
	const Ref<TextDataFormat> format = resource;
	const Ref<JSON> json = resource;
	ERR_FAIL_COND(format.is_null() && json.is_null());

	// Keep what the user opened, selected and scrolled to across the rebuild.
	HashSet<String> expanded;
	String selected = "-"; // Not a path.
	const real_t scroll = tree->get_scroll().y;
	if (tree->get_root()) {
		_collect_expanded(tree->get_root(), expanded);
		if (tree->get_selected()) {
			selected = _get_item_path(tree->get_selected());
		}
	}

	const Variant file_data = format.is_valid() ? format->get_data() : json->get_data();
	summary_label->set_text(vformat("%s: %s", resource->get_class(), get_summary_text(file_data)));
	_update_dataset_button();
	_update_dock_buttons();
	_update_preview(file_data);

	TreeItem *root = tree->get_root();
	if (root && root->get_first_child()) {
		_restore(root, expanded, selected);
		tree->scroll_to_item(root->get_first_child()); // Updates the scroll bar range to the new items.
		tree->get_vscroll_bar()->set_value(scroll);
	}
	_validate(false);
}

void DataFileInspectorView::_validate(bool p_force) {
	issue_list->clear();
	const Ref<TextDataFormat> format = resource;
	const Ref<JSON> json = resource;
	const Color error_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	const Color success_color = get_theme_color(SNAME("success_color"), EditorStringName(Editor));

	const String error = format.is_valid() ? format->get_error_message() : json->get_error_message();
	if (!error.is_empty()) {
		// JSON counts lines from 0 and has no column.
		const int line = format.is_valid() ? format->get_error_line() : json->get_error_line() + 1;
		const int column = format.is_valid() ? format->get_error_column() : 0;
		const String at = column > 0 ? vformat(TTR("line %d, column %d"), line, column) : vformat(TTR("line %d"), line);
		// The message once, in the list (activating it opens the line); the status only says what failed.
		status_label->set_text(TTR("The text can't be parsed."));
		status_label->add_theme_color_override(SceneStringName(font_color), error_color);
		issue_list->add_item(line > 0 ? vformat(TTR("Parse error at %s: %s"), at, error) : vformat(TTR("Parse error: %s"), error));
		issue_list->set_item_metadata(0, line);
		issue_list->show();
		return;
	}

	const Ref<DataSchema> schema = EditorDataSchemaLookup::find_schema(resource->get_path());
	status_label->remove_theme_color_override(SceneStringName(font_color));
	issue_list->hide();
	if (schema.is_null()) {
		status_label->set_text(TTR("Parsed. No data schema is listed for this file (project setting filesystem/data/schema_files)."));
		return;
	}
	const String schema_name = schema->get_path().is_empty() ? schema->get_name() : schema->get_path().get_file();
	const String text = _get_parsed_text();
	if (!p_force && text.length() > AUTO_VALIDATE_SIZE) {
		status_label->set_text(vformat(TTR("Parsed. Schema %s: press Validate to check this large file."), schema_name));
		return;
	}

	Ref<DataValidationOptions> options;
	options.instantiate();
	options->set_keep_positions(true);
	options->set_max_issues(MAX_ISSUES);
	Ref<DataValidation> validation;
	if (format.is_valid()) {
		validation = format->validate(schema, options);
	} else {
		validation = schema->validate(json->get_data(), options);
		validation->resolve_lines(text);
	}
	ERR_FAIL_COND(validation.is_null());

	const int count = validation->get_issue_count();
	if (count == 0) {
		status_label->set_text(vformat(TTR("Parsed. Matches schema %s."), schema_name));
		status_label->add_theme_color_override(SceneStringName(font_color), success_color);
		return;
	}
	status_label->set_text(vformat(TTRN("Parsed. Schema %s: %s issue.", "Parsed. Schema %s: %s issues.", count), schema_name, count >= MAX_ISSUES ? itos(count) + "+" : itos(count)));
	status_label->add_theme_color_override(SceneStringName(font_color), error_color);
	for (int i = 0; i < count; i++) {
		const int line = validation->get_issue_line(i);
		issue_list->add_item(line > 0 ? vformat(TTR("Line %d: %s"), line, validation->get_issue_message(i)) : validation->get_issue_message(i));
		issue_list->set_item_metadata(i, line);
	}
	issue_list->show();
}

void DataFileInspectorView::_expand(TreeItem *p_item) {
	// Replaces the placeholder child of an expanded item with the first page of its children.
	TreeItem *first = p_item->get_first_child();
	if (!p_item->is_collapsed() && first && int(first->get_metadata(1)) == PLACEHOLDER_INDEX) {
		memdelete(first);
		_fill(p_item, 0);
	}
}

void DataFileInspectorView::_item_collapsed(TreeItem *p_item) {
	// Deferred: a click on the fold arrow blocks creating and removing items until the mouse event is handled.
	// By ID, because a rebuild of the tree in between frees the item.
	callable_mp(this, &DataFileInspectorView::_expand_deferred).call_deferred(p_item->get_instance_id());
}

void DataFileInspectorView::_expand_deferred(ObjectID p_item_id) {
	TreeItem *item = ObjectDB::get_instance<TreeItem>(p_item_id);
	if (item) {
		_expand(item);
	}
}

void DataFileInspectorView::_item_selected() {
	TreeItem *item = tree->get_selected();
	if (item == nullptr) {
		return;
	}
	const int index = item->get_metadata(1);
	if (index < PLACEHOLDER_INDEX) {
		TreeItem *parent = item->get_parent();
		memdelete(item);
		_fill(parent, -1 - index);
	}
}

void DataFileInspectorView::_open_text(int p_line) {
	if (ScriptEditor::get_singleton() && ScriptEditor::get_singleton()->edit(resource, p_line - 1, 0, true)) {
		EditorInterface::get_singleton()->set_main_screen_editor("Script");
	}
}

void DataFileInspectorView::_open_in_dataset_dock() {
	DatasetEditor *dataset_editor = DatasetEditor::get_singleton();
	if (dataset_editor && DatasetEditor::can_edit(resource.ptr())) {
		dataset_editor->edit(resource);
		dataset_editor->make_visible();
	}
}

void DataFileInspectorView::_issue_activated(int p_index) {
	_open_text(issue_list->get_item_metadata(p_index));
}

void DataFileInspectorView::_raw_toggled(bool p_pressed) {
	EditorInspectorPluginDataFile::show_raw_properties = p_pressed;
	resource->notify_property_list_changed();
}

void DataFileInspectorView::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			resource->connect(CoreStringName(changed), callable_mp(this, &DataFileInspectorView::_queue_update));
		} break;

		case NOTIFICATION_EXIT_TREE: {
			resource->disconnect(CoreStringName(changed), callable_mp(this, &DataFileInspectorView::_queue_update));
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			search->set_right_icon(get_editor_theme_icon(SNAME("Search")));
			more_label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
			_update();
		} break;
	}
}

DataFileInspectorView::DataFileInspectorView(const Ref<Resource> &p_resource) {
	resource = p_resource;

	summary_label = memnew(Label);
	summary_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	summary_label->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	add_child(summary_label);

	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	add_child(status_label);

	// Wraps in a narrow Inspector (a format may add its own dock buttons before Open in Dataset Dock).
	button_row = memnew(HFlowContainer);
	add_child(button_row);

	dataset_button = memnew(Button(TTR("Open in Dataset Dock")));
	dataset_button->hide();
	dataset_button->connect(SceneStringName(pressed), callable_mp(this, &DataFileInspectorView::_open_in_dataset_dock));
	button_row->add_child(dataset_button);

	Button *validate_button = memnew(Button(TTR("Validate")));
	validate_button->set_tooltip_text(TTR("Check the data against the data schema listed for this file (project setting filesystem/data/schema_files)."));
	validate_button->connect(SceneStringName(pressed), callable_mp(this, &DataFileInspectorView::_validate).bind(true));
	button_row->add_child(validate_button);

	issue_list = memnew(ItemList);
	issue_list->set_auto_height(true);
	issue_list->set_max_columns(1);
	issue_list->set_tooltip_text(TTR("Double-click an issue to open its line in the script editor."));
	issue_list->connect("item_activated", callable_mp(this, &DataFileInspectorView::_issue_activated));
	issue_list->hide();
	add_child(issue_list);

	search = memnew(LineEdit);
	search->set_placeholder(TTR("Search keys and values"));
	search->set_clear_button_enabled(true);
	add_child(search);

	search_timer = memnew(Timer);
	search_timer->set_one_shot(true);
	search_timer->set_wait_time(0.3);
	search_timer->connect("timeout", callable_mp(this, &DataFileInspectorView::_update_tree));
	add_child(search_timer);

	update_timer = memnew(Timer);
	update_timer->set_one_shot(true);
	update_timer->set_wait_time(UPDATE_DELAY);
	update_timer->connect("timeout", callable_mp(this, &DataFileInspectorView::_update));
	add_child(update_timer);
	search->connect(SceneStringName(text_changed), callable_mp(search_timer, &Timer::start).bind(-1).unbind(1));

	tree = memnew(Tree);
	tree->set_hide_root(true);
	tree->set_column_titles_visible(true);
	tree->set_select_mode(Tree::SELECT_ROW);
	tree->connect("item_collapsed", callable_mp(this, &DataFileInspectorView::_item_collapsed));
	// Deferred: the selected "more" item is removed.
	tree->connect(SceneStringName(item_selected), callable_mp(this, &DataFileInspectorView::_item_selected), CONNECT_DEFERRED);
	add_child(tree);

	more_label = memnew(Label);
	more_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	more_label->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	more_label->hide();
	add_child(more_label);

	raw_check = memnew(CheckBox(TTR("Raw Properties")));
	raw_check->set_pressed(EditorInspectorPluginDataFile::show_raw_properties);
	raw_check->set_tooltip_text(TTR("Also show the default editors of the stored properties that the view shows."));
	raw_check->connect(SceneStringName(toggled), callable_mp(this, &DataFileInspectorView::_raw_toggled));
	add_child(raw_check);
}

// EditorInspectorPluginDataFile

DataFileInspectorView *EditorInspectorPluginDataFile::create_view(const Ref<Resource> &p_resource) const {
	return memnew(DataFileInspectorView(p_resource));
}

void EditorInspectorPluginDataFile::_add_view(Object *p_object) {
	view_added = true;
	add_custom_control(create_view(Ref<Resource>(Object::cast_to<Resource>(p_object))));
}

void EditorInspectorPluginDataFile::parse_begin(Object *p_object) {
	view_added = false;
}

void EditorInspectorPluginDataFile::parse_category(Object *p_object, const String &p_category) {
	// Below the first category (the format class, or TextDataFormat if the format has no own properties).
	if (!view_added) {
		_add_view(p_object);
	}
}

bool EditorInspectorPluginDataFile::parse_property(Object *p_object, const Variant::Type p_type, const String &p_path, const PropertyHint p_hint, const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage, const bool p_wide) {
	const bool previewed = _is_previewed_property(p_object, p_path);
	if (previewed && !view_added) {
		// Inspectors without categories (sub-inspectors).
		_add_view(p_object);
	}
	return previewed && !show_raw_properties;
}

// EditorInspectorPluginJSON

bool EditorInspectorPluginJSON::can_handle(Object *p_object) {
	return Object::cast_to<JSON>(p_object) != nullptr;
}

// EditorInspectorPluginDataSchema

// A Button created in the inspector cannot be a GDCLASS of its own (unregistered classes have no signals), so the
// class picker calls a static function bound to the schema.
static void _generate_data_schema(const Ref<DataSchema> &p_schema, Object *p_dialog) {
	CreateDialog *dialog = Object::cast_to<CreateDialog>(p_dialog);
	ERR_FAIL_NULL(dialog);
	Ref<Resource> sample = Object::cast_to<Resource>(dialog->instantiate_selected().get_validated_object());
	if (sample.is_null()) {
		return;
	}
	const Ref<DataSchema> generated = DataSchema::create_from_resource(sample);
	if (generated.is_null() || generated->get_fields().is_empty()) {
		EditorNode::get_singleton()->show_warning(vformat(TTR("%s has no stored properties to describe."), dialog->get_selected_type()));
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Generate Data Schema from Resource"), UndoRedo::MERGE_DISABLE, p_schema.ptr());
	undo_redo->add_do_property(p_schema.ptr(), "fields", generated->get_fields());
	undo_redo->add_undo_property(p_schema.ptr(), "fields", p_schema->get_fields().duplicate());
	undo_redo->add_do_property(p_schema.ptr(), "extra_fields", generated->get_extra_fields());
	undo_redo->add_undo_property(p_schema.ptr(), "extra_fields", p_schema->get_extra_fields());
	undo_redo->commit_action();
}

static Button *_make_data_schema_generate_button(const Ref<DataSchema> &p_schema) {
	Button *button = memnew(Button(TTR("Generate from Resource...")));
	button->set_tooltip_text(TTR("Replace the fields with one field per stored property of a Resource class, as the text formats write them."));
	CreateDialog *dialog = memnew(CreateDialog);
	dialog->set_base_type("Resource");
	dialog->connect("create", callable_mp_static(&_generate_data_schema).bind(p_schema, dialog));
	button->add_child(dialog);
	button->connect(SceneStringName(pressed), callable_mp(dialog, &CreateDialog::popup_create).bind(true, false, String(), String()));
	return button;
}

bool EditorInspectorPluginDataSchema::can_handle(Object *p_object) {
	return Object::cast_to<DataSchema>(p_object) != nullptr;
}

void EditorInspectorPluginDataSchema::parse_begin(Object *p_object) {
	add_custom_control(_make_data_schema_generate_button(Ref<DataSchema>(Object::cast_to<DataSchema>(p_object))));
}

// DataFileInspectorEditorPlugin

DataFileInspectorEditorPlugin::DataFileInspectorEditorPlugin() {
	Ref<EditorInspectorPluginCSV> csv_plugin;
	csv_plugin.instantiate();
	add_inspector_plugin(csv_plugin);
	Ref<EditorInspectorPluginINI> ini_plugin;
	ini_plugin.instantiate();
	add_inspector_plugin(ini_plugin);
	Ref<EditorInspectorPluginXML> xml_plugin;
	xml_plugin.instantiate();
	add_inspector_plugin(xml_plugin);
	Ref<EditorInspectorPluginJSON> json_plugin;
	json_plugin.instantiate();
	add_inspector_plugin(json_plugin);
	Ref<EditorInspectorPluginDataSchema> schema_plugin;
	schema_plugin.instantiate();
	add_inspector_plugin(schema_plugin);
}
