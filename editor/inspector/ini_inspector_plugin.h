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

// The inspector view of an INI file: sections and keys in the summary, the tree of INITreeAdapter (keys before the
// first section at the top, sections and numbered sections as containers). The INI dock opens with the file.
class INIInspectorView : public DataFileInspectorView {
	GDCLASS(INIInspectorView, DataFileInspectorView);

public:
	// "N sections, M keys" of INI data (section -> key -> value).
	virtual String get_summary_text(const Variant &p_data) const override;

	INIInspectorView(const Ref<Resource> &p_resource);
};

class EditorInspectorPluginINI : public EditorInspectorPluginDataFile {
	GDCLASS(EditorInspectorPluginINI, EditorInspectorPluginDataFile);

public:
	virtual DataFileInspectorView *create_view(const Ref<Resource> &p_resource) const override;
	virtual bool can_handle(Object *p_object) override;
};
