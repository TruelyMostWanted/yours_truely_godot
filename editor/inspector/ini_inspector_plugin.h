/**************************************************************************/
/*  ini_inspector_plugin.h                                                */
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

// The inspector view of an INI file: sections and keys in the summary, a tree of sections (type "Section",
// keys without a section under "(no section)") with their typed keys, and the INI and Dataset docks.
class INIInspectorView : public DataFileInspectorView {
	GDCLASS(INIInspectorView, DataFileInspectorView);

protected:
	virtual String _get_counts(const Variant &p_data) const override { return get_counts(p_data); }
	virtual void _decorate_entry(DataFileTreeAdapter::Entry &r_entry, bool p_top_level) const override { decorate_entry(r_entry, p_top_level); }
	virtual bool _can_open_in(Dock p_dock) const override { return p_dock == DOCK_INI; }

public:
	// "N sections, M keys" of INI data (section -> key -> value).
	static String get_counts(const Variant &p_data);
	// Top-level rows are sections.
	static void decorate_entry(DataFileTreeAdapter::Entry &r_entry, bool p_top_level);

	INIInspectorView(const Ref<Resource> &p_resource) :
			DataFileInspectorView(p_resource) {}
};

class EditorInspectorPluginINI : public EditorInspectorPluginDataFile {
	GDCLASS(EditorInspectorPluginINI, EditorInspectorPluginDataFile);

protected:
	virtual DataFileInspectorView *_create_view(const Ref<Resource> &p_resource) const override;

public:
	virtual bool can_handle(Object *p_object) override;
};
