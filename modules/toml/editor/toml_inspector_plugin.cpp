/**************************************************************************/
/*  toml_inspector_plugin.cpp                                             */
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

#include "toml_inspector_plugin.h"

#include "../toml.h"

// TOMLInspectorView

bool TOMLInspectorView::is_array_of_tables(const Variant &p_value) {
	if (!p_value.is_array()) {
		return false;
	}
	const Array items = p_value;
	if (items.is_empty()) {
		return false;
	}
	for (const Variant &item : items) {
		if (item.get_type() != Variant::DICTIONARY) {
			return false;
		}
	}
	return true;
}

String TOMLInspectorView::get_counts(const Variant &p_data) {
	const Dictionary root = p_data;
	int table_count = 0;
	for (const KeyValue<Variant, Variant> &kv : root) {
		if (kv.value.get_type() == Variant::DICTIONARY || is_array_of_tables(kv.value)) {
			table_count++;
		}
	}
	const int key_count = root.size() - table_count;
	return vformat(TTRN("%d table", "%d tables", table_count), table_count) + ", " + vformat(TTRN("%d key", "%d keys", key_count), key_count);
}

void TOMLInspectorView::decorate_entry(DataFileTreeAdapter::Entry &r_entry) {
	if (r_entry.data.get_type() == Variant::DICTIONARY) {
		r_entry.type = TTR("Table");
	} else if (is_array_of_tables(r_entry.data)) {
		r_entry.type = TTR("Array of Tables");
	}
}

// EditorInspectorPluginTOML

DataFileInspectorView *EditorInspectorPluginTOML::_create_view(const Ref<Resource> &p_resource) const {
	return memnew(TOMLInspectorView(p_resource));
}

bool EditorInspectorPluginTOML::can_handle(Object *p_object) {
	return Object::cast_to<TOML>(p_object) != nullptr;
}
