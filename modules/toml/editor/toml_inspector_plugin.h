/**************************************************************************/
/*  toml_inspector_plugin.h                                               */
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

// The inspector view of a TOML file: top-level tables and keys in the summary, and a value tree in TOML terms
// (a Dictionary is a "Table", an Array of Dictionaries an "Array of Tables", as `[[name]]` writes it).
class TOMLInspectorView : public DataFileInspectorView {
	GDCLASS(TOMLInspectorView, DataFileInspectorView);

protected:
	virtual String _get_counts(const Variant &p_data) const override { return get_counts(p_data); }
	virtual void _decorate_entry(DataFileTreeAdapter::Entry &r_entry, bool p_top_level) const override { decorate_entry(r_entry); }

public:
	// A non-empty Array of Dictionaries only.
	static bool is_array_of_tables(const Variant &p_value);
	// "N tables, M keys" at the top level of TOML data.
	static String get_counts(const Variant &p_data);
	static void decorate_entry(DataFileTreeAdapter::Entry &r_entry);

	TOMLInspectorView(const Ref<Resource> &p_resource) :
			DataFileInspectorView(p_resource) {}
};

class EditorInspectorPluginTOML : public EditorInspectorPluginDataFile {
	GDCLASS(EditorInspectorPluginTOML, EditorInspectorPluginDataFile);

protected:
	virtual DataFileInspectorView *_create_view(const Ref<Resource> &p_resource) const override;

public:
	virtual bool can_handle(Object *p_object) override;
};
