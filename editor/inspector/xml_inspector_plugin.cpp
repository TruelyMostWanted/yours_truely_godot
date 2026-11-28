/**************************************************************************/
/*  xml_inspector_plugin.cpp                                              */
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

#include "xml_inspector_plugin.h"

#include "core/io/xml_format.h"

String XMLInspectorView::get_counts(const Variant &p_data) {
	const Dictionary root = p_data;
	if (root.is_empty()) {
		return TTR("No root element");
	}
	const int children = Array(root.get(XML::KEY_CHILDREN, Array())).size();
	return vformat(TTRN("Root element <%s>, %d child node", "Root element <%s>, %d child nodes", children), root.get(XML::KEY_NAME, String()), children);
}

Array XMLInspectorView::get_tree_value(const Variant &p_data) {
	const Dictionary root = p_data;
	Array nodes;
	if (!root.is_empty()) {
		nodes.push_back(root);
	}
	return nodes;
}

DataFileInspectorView *EditorInspectorPluginXML::_create_view(const Ref<Resource> &p_resource) const {
	return memnew(XMLInspectorView(p_resource));
}

bool EditorInspectorPluginXML::can_handle(Object *p_object) {
	return Object::cast_to<XML>(p_object) != nullptr;
}
