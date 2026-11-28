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
// show the column's field of the data schema, Open in Localization Table Dock (translation CSVs, first) and the
// shared Open in Dataset Dock.
class CSVInspectorView : public DataFileInspectorView {
	GDCLASS(CSVInspectorView, DataFileInspectorView);

	static constexpr int PREVIEW_ROWS = 100;
	static constexpr int PREVIEW_COLUMNS = 32;
	static constexpr int MAX_CELL_CHARS = 24;

	Button *localization_button = nullptr;

	void _open_in_localization_table_dock();

protected:
	virtual void _update_dock_buttons() override;
	virtual void _update_preview(const Variant &p_data) override;

public:
	// "N rows, M columns".
	virtual String get_summary_text(const Variant &p_data) const override;
	// The tooltip of a column title: the column's field in the data schema (type, required, description).
	static String get_column_tooltip(const String &p_column, const Ref<DataSchema> &p_schema);

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
