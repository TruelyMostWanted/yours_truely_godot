/**************************************************************************/
/*  csv_inspector_plugin.cpp                                              */
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

#include "csv_inspector_plugin.h"

#include "core/io/csv_format.h"
#include "core/io/data_field.h"
#include "core/io/data_schema.h"
#include "core/object/callable_mp.h"
#include "editor/data/csv_table_editor.h"
#include "editor/editor_string_names.h"
#include "editor/script/data_schema_lookup.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/tree.h"
#include "scene/resources/font.h"
#include "scene/scene_string_names.h"

// CSVInspectorView

String CSVInspectorView::get_column_tooltip(const String &p_column, const Ref<DataSchema> &p_schema) {
	if (p_schema.is_null()) {
		return p_column;
	}
	const Ref<DataField> field = p_schema->get_field(p_column);
	if (field.is_null()) {
		return vformat(TTR("%s: not in the data schema."), p_column);
	}
	String tooltip = vformat("%s: %s", p_column, Variant::get_type_name(field->get_value_type()));
	if (field->is_required()) {
		tooltip += ", " + TTR("required");
	}
	if (!field->get_description().is_empty()) {
		tooltip += "\n" + field->get_description();
	}
	return tooltip;
}

String CSVInspectorView::get_summary_text(const Variant &p_data) const {
	const Ref<CSV> csv = resource;
	const int rows = csv->get_row_count();
	const int columns = csv->get_column_count();
	return vformat(TTRN("%d row", "%d rows", rows), rows) + ", " + vformat(TTRN("%d column", "%d columns", columns), columns);
}

void CSVInspectorView::_update_dock_buttons() {
	localization_button->set_visible(CSVTableEditor::can_edit(resource.ptr()));
}

void CSVInspectorView::_open_in_localization_table_dock() {
	CSVTableEditor *table_editor = CSVTableEditor::get_singleton();
	if (table_editor && CSVTableEditor::can_edit(resource.ptr())) {
		table_editor->edit(resource);
		table_editor->make_visible();
	}
}

void CSVInspectorView::_update_preview(const Variant &p_data) {
	const Ref<CSV> csv = resource;
	const Array rows = p_data;
	const PackedStringArray header = csv->get_header();
	const PackedInt32Array chars = DataFileTreeAdapter::get_column_chars(header, rows, PREVIEW_ROWS, PREVIEW_COLUMNS, MAX_CELL_CHARS);
	const int row_count = MIN(rows.size(), PREVIEW_ROWS);
	const Ref<DataSchema> schema = header.is_empty() ? Ref<DataSchema>() : EditorDataSchemaLookup::find_schema(resource->get_path());
	tree->clear();

	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Tree"));
	const int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Tree"));
	const real_t char_width = font.is_valid() ? font->get_char_size('0', font_size).width : 8 * EDSCALE;
	const int padding = (int)(16 * EDSCALE);

	tree->set_columns((int)chars.size() + 1);
	tree->set_hide_folding(true);
	tree->set_column_title(0, "#");
	tree->set_column_expand(0, false);
	tree->set_column_custom_minimum_width(0, (int)(itos(MAX(row_count, 1)).length() * char_width) + padding);
	for (int column = 0; column < chars.size(); column++) {
		const int tree_column = column + 1;
		const String title = column < header.size() ? header[column] : itos(tree_column);
		tree->set_column_title(tree_column, title);
		tree->set_column_title_tooltip_text(tree_column, column < header.size() ? get_column_tooltip(title, schema) : title);
		tree->set_column_title_alignment(tree_column, HORIZONTAL_ALIGNMENT_LEFT);
		tree->set_column_expand(tree_column, false);
		tree->set_column_clip_content(tree_column, true);
		tree->set_column_custom_minimum_width(tree_column, (int)(MAX(chars[column], 2) * char_width) + padding);
	}

	TreeItem *root = tree->create_item();
	const Color dim = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));
	for (int i = 0; i < row_count; i++) {
		TreeItem *item = tree->create_item(root);
		item->set_text(0, itos(i + 1));
		item->set_custom_color(0, dim);
		const PackedStringArray cells = rows[i];
		for (int column = 0; column < MIN(cells.size(), chars.size()); column++) {
			item->set_text(column + 1, cells[column]);
			item->set_tooltip_text(column + 1, cells[column]);
		}
	}

	PackedStringArray more;
	if (rows.size() > row_count) {
		more.push_back(vformat(TTR("%d more rows"), rows.size() - row_count));
	}
	const int column_count = csv->get_column_count();
	if (column_count > chars.size()) {
		more.push_back(vformat(TTR("%d more columns"), column_count - chars.size()));
	}
	String more_text = more.is_empty() ? String() : String(", ").join(more) + ".";
	if (!more.is_empty() && dataset_button->is_visible()) {
		more_text += " " + TTR("Open in Dataset Dock to see all.");
	}
	more_label->set_text(more_text);
	more_label->set_visible(!more.is_empty());

	tree->set_custom_minimum_size(Size2(0, (MIN(row_count, 12) + 2) * _get_row_height()));
}

CSVInspectorView::CSVInspectorView(const Ref<Resource> &p_resource) :
		DataFileInspectorView(p_resource) {
	search->hide(); // The table shows rows, not a tree to search.
	localization_button = _add_dock_button(TTR("Open in Localization Table Dock"), TTR("Edit the translations in the Localization Table dock."), callable_mp(this, &CSVInspectorView::_open_in_localization_table_dock));
	localization_button->hide();
}

// EditorInspectorPluginCSV

DataFileInspectorView *EditorInspectorPluginCSV::create_view(const Ref<Resource> &p_resource) const {
	return memnew(CSVInspectorView(p_resource));
}

bool EditorInspectorPluginCSV::_is_previewed_property(Object *p_object, const String &p_path) const {
	return p_path == "data" || p_path == "header";
}

bool EditorInspectorPluginCSV::can_handle(Object *p_object) {
	return Object::cast_to<CSV>(p_object) != nullptr;
}
