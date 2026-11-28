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

// Metadata of a tree item: column 0 holds its path (DataTreeAdapter), column 1 its child index in the parent.
// A placeholder child (not filled yet) has index -1, a "more" item -1 - (index of the next child).
static constexpr int PLACEHOLDER_INDEX = -1;

// DataFileInspectorView

String DataFileInspectorView::_get_parsed_text() const {
	const Ref<JSON> json = resource;
	if (json.is_valid()) {
		return json->get_parsed_text();
	}
	const Ref<TextDataFormat> format = resource;
	return format.is_valid() ? format->get_parsed_text() : String();
}

TreeItem *DataFileInspectorView::_create_item(TreeItem *p_parent, const DataTreeAdapter::Entry &p_entry, int p_index, bool p_lazy) {
	PackedStringArray path = p_parent->get_metadata(0);
	path.push_back(p_entry.segment);
	TreeItem *item = tree->create_item(p_parent);
	item->set_text(COLUMN_KEY, p_entry.key);
	item->set_text(COLUMN_TYPE, p_entry.type);
	item->set_text(COLUMN_VALUE, p_entry.value);
	item->set_tooltip_text(COLUMN_KEY, adapter->format_path(path));
	item->set_tooltip_text(COLUMN_VALUE, p_entry.value);
	item->set_metadata(0, path);
	item->set_metadata(1, p_index);
	if (p_lazy && p_entry.child_count > 0) {
		tree->create_item(item)->set_metadata(1, PLACEHOLDER_INDEX);
		item->set_collapsed(true);
	}
	return item;
}

void DataFileInspectorView::_fill(TreeItem *p_parent, int p_from) {
	const PackedStringArray path = p_parent->get_metadata(0);
	LocalVector<DataTreeAdapter::Entry> entries;
	adapter->get_children(path, p_from, CHILD_PAGE, entries);
	for (uint32_t i = 0; i < entries.size(); i++) {
		_create_item(p_parent, entries[i], p_from + (int)i, true);
	}
	const int next = p_from + (int)entries.size();
	const int rest = adapter->get_child_count(path) - next;
	if (rest > 0) {
		TreeItem *more = tree->create_item(p_parent);
		more->set_text(COLUMN_KEY, vformat(TTR("%d more..."), rest));
		more->set_tooltip_text(COLUMN_KEY, vformat(TTR("Select to show the next %d."), MIN(rest, CHILD_PAGE)));
		more->set_metadata(0, path);
		more->set_metadata(1, -1 - next);
	}
}

String DataFileInspectorView::_get_item_path(TreeItem *p_item) const {
	// A path stays the same when the data is parsed again (keys, not row numbers).
	return DataTreeAdapter::join_path(p_item->get_metadata(0));
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
	const String type = Variant::get_type_name(p_data.get_type());
	if (p_data.get_type() == Variant::DICTIONARY) {
		const int count = Dictionary(p_data).size();
		return type + ", " + vformat(TTRN("%d key", "%d keys", count), count);
	}
	if (p_data.get_type() == Variant::ARRAY) {
		const int count = Array(p_data).size();
		return type + ", " + vformat(TTRN("%d item", "%d items", count), count);
	}
	return type;
}

int DataFileInspectorView::_get_row_height() const {
	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Tree"));
	const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Tree"));
	return (int)(font.is_valid() ? font->get_height(font_size) + get_theme_constant(SNAME("v_separation"), SNAME("Tree")) : 24 * EDSCALE);
}

void DataFileInspectorView::_update_preview(const Variant &p_data) {
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
	if (adapter.is_null()) {
		more_label->hide();
		return;
	}

	TreeItem *root = tree->create_item();
	root->set_metadata(0, PackedStringArray());
	const String query = search->get_text().strip_edges();
	if (query.is_empty()) {
		_fill(root, 0);
		more_label->hide();
	} else {
		// Only the matching paths, expanded; clearing the search goes back to the lazy tree.
		const Vector<PackedStringArray> paths = adapter->find(query, MAX_MATCHES);
		const Color match_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
		for (const PackedStringArray &path : paths) {
			TreeItem *item = root;
			PackedStringArray prefix;
			for (int i = 0; i < path.size() && item; i++) {
				prefix.push_back(path[i]);
				TreeItem *child = item->get_first_child();
				while (child && PackedStringArray(child->get_metadata(0)) != prefix) {
					child = child->get_next();
				}
				if (child == nullptr) {
					DataTreeAdapter::Entry entry;
					child = adapter->get_entry(prefix, entry) ? _create_item(item, entry, 0, false) : nullptr;
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

	const int top_level_count = MIN(adapter->get_child_count(PackedStringArray()), CHILD_PAGE);
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
	adapter = DataTreeAdapter::create_for(resource); // Again: a CSV gets a tree when its header becomes Path,Key,Value.
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
	adapter = DataTreeAdapter::create_for(resource);

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
