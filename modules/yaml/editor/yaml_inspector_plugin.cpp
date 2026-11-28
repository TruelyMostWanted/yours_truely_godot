/**************************************************************************/
/*  yaml_inspector_plugin.cpp                                             */
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

#include "yaml_inspector_plugin.h"

#include "../yaml.h"

// YAMLInspectorView

int YAMLInspectorView::_get_document_count() const {
	const YAML *yaml = Object::cast_to<YAML>(resource.ptr());
	return yaml ? yaml->get_document_count() : 1;
}

String YAMLInspectorView::get_summary_text(const Variant &p_data) const {
	const int count = _get_document_count();
	return count > 1 ? vformat(TTRN("%d document", "%d documents", count), count) : DataFileInspectorView::get_summary_text(p_data);
}

Variant YAMLInspectorView::get_tree_value(const Variant &p_data) const {
	const int count = _get_document_count();
	if (count <= 1) {
		return p_data;
	}
	const YAML *yaml = Object::cast_to<YAML>(resource.ptr());
	Array documents;
	for (int i = 0; i < count; i++) {
		documents.push_back(yaml->get_document(i));
	}
	return documents;
}

void YAMLInspectorView::decorate_row(DataFileTreeAdapter::Entry &r_entry, bool p_top_level) const {
	if (p_top_level && _get_document_count() > 1) {
		// The adapter names Array items by their index.
		r_entry.key = vformat(TTR("Document %d"), r_entry.key.to_int() + 1);
		r_entry.type = TTR("Document");
	}
}

// EditorInspectorPluginYAML

DataFileInspectorView *EditorInspectorPluginYAML::create_view(const Ref<Resource> &p_resource) const {
	return memnew(YAMLInspectorView(p_resource));
}

bool EditorInspectorPluginYAML::can_handle(Object *p_object) {
	return Object::cast_to<YAML>(p_object) != nullptr;
}
