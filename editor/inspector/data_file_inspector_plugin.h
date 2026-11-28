/**************************************************************************/
/*  data_file_inspector_plugin.h                                          */
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

#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"

class Button;
class CheckBox;
class HFlowContainer;
class ItemList;
class Label;
class LineEdit;
class Timer;
class Tree;
class TreeItem;

// What the data file view shows of Variant data: the children of a value as rows Key | Type | Value. In XML
// mode, Dictionaries are XML elements (see XML): their attributes and child nodes are their children, and an
// Array is a list of nodes. Pure logic, without UI.
class DataFileTreeAdapter {
public:
	struct Entry {
		String key;
		String type;
		String value;
		Variant data;
		int child_count = 0;
	};

	static constexpr int MAX_VALUE_LENGTH = 200;

	static Entry make_entry(const String &p_key, const Variant &p_value, bool p_xml);
	static int get_child_count(const Variant &p_value, bool p_xml);
	// The children from index p_from, at most p_count (all with -1).
	static void get_children(const Variant &p_value, bool p_xml, int p_from, int p_count, LocalVector<Entry> &r_entries);
	// Renames an entry as the view shows it (see DataFileInspectorView::decorate_row()); p_context is the view.
	typedef void (*DecorateFunc)(const void *p_context, Entry &r_entry, bool p_top_level);

	// Paths (child indices from p_root) of the entries whose key or scalar value contains p_query (case-insensitive),
	// depth-first in document order, at most p_max. With p_decorate, the keys are matched as the view shows them.
	static Vector<PackedInt32Array> find(const Variant &p_root, bool p_xml, const String &p_query, int p_max, DecorateFunc p_decorate = nullptr, const void *p_context = nullptr);
	// Path of a child that stays the same when the data is parsed again: "/"-separated keys ("/" and "\" in keys
	// escaped with "\"). XML uses the child index, because sibling elements often share their tag.
	static String make_child_path(const String &p_parent_path, const String &p_key, int p_index, bool p_xml);
	// Characters of the longest cell per column (at most p_max_columns columns) in the header and the first
	// p_max_rows rows, capped at p_max_chars.
	static PackedInt32Array get_column_chars(const PackedStringArray &p_header, const Array &p_rows, int p_max_rows, int p_max_columns, int p_max_chars);
};

// The shared inspector view of a data file (TextDataFormat or JSON): a summary with parse status and schema
// issues, a read-only preview, Open in Dataset Dock (shared by every format with a table) and Validate. The
// default preview is a Key | Type | Value tree filled on expand, with search. Each format has its own inspector
// plugin, which may create its own view (summary, preview, rows, own docks): CSVInspectorView, INIInspectorView,
// XMLInspectorView, and in their modules YAMLInspectorView and TOMLInspectorView. JSON uses this view as it is.
// The view handles TextDataFormat and JSON documents (parse error, kept text, validation), because JSON is
// Godot's class and not a TextDataFormat.
class DataFileInspectorView : public VBoxContainer {
	GDCLASS(DataFileInspectorView, VBoxContainer);

	static constexpr int MAX_MATCHES = 100;
	static constexpr int MAX_ISSUES = 100;
	static constexpr int AUTO_VALIDATE_SIZE = 256 * 1024;
	static constexpr double UPDATE_DELAY = 0.3;

	Variant tree_value;

	Label *summary_label = nullptr;
	Label *status_label = nullptr;
	HFlowContainer *button_row = nullptr;
	Timer *search_timer = nullptr;
	Timer *update_timer = nullptr;
	ItemList *issue_list = nullptr;
	CheckBox *raw_check = nullptr;

	static void _decorate_found(const void *p_context, DataFileTreeAdapter::Entry &r_entry, bool p_top_level);
	String _get_parsed_text() const;
	TreeItem *_create_item(TreeItem *p_parent, const DataFileTreeAdapter::Entry &p_entry, int p_index, bool p_lazy);
	void _fill(TreeItem *p_parent, int p_from);
	String _get_item_path(TreeItem *p_item) const;
	void _collect_expanded(TreeItem *p_item, HashSet<String> &r_paths) const;
	void _restore(TreeItem *p_item, const HashSet<String> &p_expanded, const String &p_selected);
	void _update_tree();
	void _update_dataset_button();
	void _queue_update();
	void _update();
	void _validate(bool p_force);
	void _expand(TreeItem *p_item);
	void _item_collapsed(TreeItem *p_item);
	void _expand_deferred(ObjectID p_item_id);
	void _item_selected();
	void _open_text(int p_line);
	void _open_in_dataset_dock();
	void _issue_activated(int p_index);
	void _raw_toggled(bool p_pressed);

protected:
	static constexpr int CHILD_PAGE = 200;

	Ref<Resource> resource;
	bool xml = false; // The tree shows XML elements (see DataFileTreeAdapter).
	Button *dataset_button = nullptr; // Visible when the file has a table (DatasetEditor::find_tables()).
	LineEdit *search = nullptr;
	Tree *tree = nullptr;
	Label *more_label = nullptr;

	int _get_row_height() const;
	// Adds a button for a dock of the format, before Open in Dataset Dock. For the constructor of a format's view.
	Button *_add_dock_button(const String &p_text, const String &p_tooltip, const Callable &p_pressed);

	// Per format: shows or hides the format's own dock buttons (_add_dock_button()) for the current data.
	virtual void _update_dock_buttons() {}
	// Per format: the preview of `data`, the tree by default.
	virtual void _update_preview(const Variant &p_data);

	void _notification(int p_what);

public:
	// Per format: the text after "<class>: " in the summary. By default the type of `data` and the size of a
	// container ("Dictionary, 3 keys").
	virtual String get_summary_text(const Variant &p_data) const;
	// Per format: the value whose children are the top-level rows of the tree.
	virtual Variant get_tree_value(const Variant &p_data) const { return p_data; }
	// Per format: the key or type of a tree row in the terms of the format (INI sections, TOML tables). Search
	// matches the renamed keys too.
	virtual void decorate_row(DataFileTreeAdapter::Entry &r_entry, bool p_top_level) const {}

	DataFileInspectorView(const Ref<Resource> &p_resource);
};

// The shared part of the inspector plugins of the data formats: adds the format's view in place of the default
// editor of `data`. Each format overrides can_handle() and, for a view of its own, create_view().
class EditorInspectorPluginDataFile : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginDataFile, EditorInspectorPlugin);

	bool view_added = false;

	void _add_view(Object *p_object);

protected:
	// The stored properties that the view shows (`data`; CSV also `header`).
	virtual bool _is_previewed_property(Object *p_object, const String &p_path) const { return p_path == "data"; }

public:
	// The view of the format (the caller owns it).
	virtual DataFileInspectorView *create_view(const Ref<Resource> &p_resource) const;

	// Shows the default editors of the previewed properties as well (editor session, all data files).
	static inline bool show_raw_properties = false;

	virtual void parse_begin(Object *p_object) override;
	virtual void parse_category(Object *p_object, const String &p_category) override;
	virtual bool parse_property(Object *p_object, const Variant::Type p_type, const String &p_path, const PropertyHint p_hint, const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage, const bool p_wide = false) override;
};

// JSON: the shared value tree.
class EditorInspectorPluginJSON : public EditorInspectorPluginDataFile {
	GDCLASS(EditorInspectorPluginJSON, EditorInspectorPluginDataFile);

public:
	virtual bool can_handle(Object *p_object) override;
};

// Adds the button "Generate from Resource…" to the inspector of a DataSchema: fills the fields from the
// stored properties of a Resource class (DataSchema.create_from_resource()).
class EditorInspectorPluginDataSchema : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginDataSchema, EditorInspectorPlugin);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};

// Registers the inspector plugins of the core data formats (CSV, INI, XML, JSON) and of DataSchema. The YAML and
// TOML modules register theirs.
class DataFileInspectorEditorPlugin : public EditorPlugin {
	GDCLASS(DataFileInspectorEditorPlugin, EditorPlugin);

public:
	virtual String get_plugin_name() const override { return "DataFileInspector"; }

	DataFileInspectorEditorPlugin();
};
