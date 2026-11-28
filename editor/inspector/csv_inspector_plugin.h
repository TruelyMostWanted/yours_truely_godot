/**************************************************************************/
/*  csv_inspector_plugin.h                                                */
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

#include "editor/inspector/data_file_inspector_plugin.h"

class DataSchema;

// The inspector view of a CSV: rows and columns in the summary, the first rows as a table whose column titles
// show the column's field of the data schema. A CSV with the header Path,Key,Value shows the tree of
// CSVTreeAdapter instead. The Dataset dock (data CSVs) or the Localization Table (translation CSVs) opens with the file.
class CSVInspectorView : public DataFileInspectorView {
	GDCLASS(CSVInspectorView, DataFileInspectorView);

	static constexpr int PREVIEW_ROWS = 100;
	static constexpr int PREVIEW_COLUMNS = 32;
	static constexpr int MAX_CELL_CHARS = 24;

protected:
	virtual void _update_preview(const Variant &p_data) override;

public:
	// "N rows, M columns".
	virtual String get_summary_text(const Variant &p_data) const override;
	// The tooltip of a column title: the column's field in the data schema (type, required, description).
	static String get_column_tooltip(const String &p_column, const Ref<DataSchema> &p_schema);
	// Characters of the longest cell per column (at most p_max_columns columns) in the header and the first
	// p_max_rows rows, capped at p_max_chars.
	static PackedInt32Array get_column_chars(const PackedStringArray &p_header, const Array &p_rows, int p_max_rows, int p_max_columns, int p_max_chars);

	CSVInspectorView(const Ref<Resource> &p_resource);
};

class EditorInspectorPluginCSV : public EditorInspectorPluginDataFile {
	GDCLASS(EditorInspectorPluginCSV, EditorInspectorPluginDataFile);

protected:
	virtual bool _is_previewed_property(Object *p_object, const String &p_path) const override;

public:
	virtual DataFileInspectorView *create_view(const Ref<Resource> &p_resource) const override;
	virtual bool can_handle(Object *p_object) override;
};
