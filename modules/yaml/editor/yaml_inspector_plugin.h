/**************************************************************************/
/*  yaml_inspector_plugin.h                                               */
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

class YAML;

// The inspector view of a YAML file: the value tree of `data`; a file with several documents ("---") shows every
// document as a top-level row "Document N" and their number in the summary.
class YAMLInspectorView : public DataFileInspectorView {
	GDCLASS(YAMLInspectorView, DataFileInspectorView);

	int _get_document_count() const;

protected:
	virtual String _get_counts(const Variant &p_data) const override;
	virtual Variant _get_tree_value(const Variant &p_data) const override;
	virtual void _decorate_entry(DataFileTreeAdapter::Entry &r_entry, bool p_top_level) const override;

public:
	// The value tree: `data`, or with several documents an Array of all documents.
	static Variant get_tree_value(const YAML *p_yaml);
	// With several documents, a top-level row is a document.
	static void decorate_entry(DataFileTreeAdapter::Entry &r_entry, bool p_top_level, int p_document_count);

	YAMLInspectorView(const Ref<Resource> &p_resource) :
			DataFileInspectorView(p_resource) {}
};

class EditorInspectorPluginYAML : public EditorInspectorPluginDataFile {
	GDCLASS(EditorInspectorPluginYAML, EditorInspectorPluginDataFile);

protected:
	virtual DataFileInspectorView *_create_view(const Ref<Resource> &p_resource) const override;

public:
	virtual bool can_handle(Object *p_object) override;
};
