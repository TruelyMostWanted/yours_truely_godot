/**************************************************************************/
/*  yaml_tree_adapter.cpp                                                 */
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

#include "yaml_tree_adapter.h"

#include "../yaml.h"

Ref<DataTreeAdapter> YAMLTreeAdapter::create(const Ref<Resource> &p_resource) {
	return Ref<DataTreeAdapter>(memnew(YAMLTreeAdapter));
}

int YAMLTreeAdapter::_get_document_count() const {
	const YAML *yaml = Object::cast_to<YAML>(resource.ptr());
	return yaml ? yaml->get_document_count() : 1;
}

bool YAMLTreeAdapter::_get_yaml_path(const PackedStringArray &p_path, String &r_path) const {
	if (_get_document_count() <= 1) {
		r_path = join_path(p_path);
		return true;
	}
	if (p_path.size() < 2 || p_path[0] != "0") {
		return false;
	}
	r_path = join_path(p_path.slice(1));
	return true;
}

Variant YAMLTreeAdapter::_get_root() const {
	const YAML *yaml = Object::cast_to<YAML>(resource.ptr());
	if (yaml == nullptr) {
		return Variant();
	}
	const int count = yaml->get_document_count();
	if (count <= 1) {
		return yaml->get_data();
	}
	Array documents;
	for (int i = 0; i < count; i++) {
		documents.push_back(yaml->get_document(i));
	}
	return documents;
}

void YAMLTreeAdapter::_decorate(Entry &r_entry, int p_depth) const {
	if (p_depth == 1 && _get_document_count() > 1) {
		r_entry.key = vformat(TTR("Document %d"), r_entry.segment.to_int() + 1);
		r_entry.type = TTR("Document");
	}
}

void YAMLTreeAdapter::_commit() {
	// The documents hold their containers by reference, so a change in place is already in the data.
	Ref<YAML> yaml = resource;
	ERR_FAIL_COND(yaml.is_null());
	yaml->set_data(yaml->get_data());
}

String YAMLTreeAdapter::format_path(const PackedStringArray &p_path) const {
	if (_get_document_count() <= 1 || p_path.is_empty()) {
		return join_path(p_path);
	}
	return vformat(TTR("%s (document %d)"), join_path(p_path.slice(1)), p_path[0].to_int() + 1);
}

Error YAMLTreeAdapter::set_value(const PackedStringArray &p_path, const Variant &p_value) {
	Ref<YAML> yaml = resource;
	Variant node;
	if (yaml.is_null() || p_path.is_empty() || !_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	String yaml_path;
	if (_get_yaml_path(p_path, yaml_path) && yaml->has_value(yaml_path)) {
		yaml->set_value(yaml_path, p_value);
		return OK;
	}
	if (_get_document_count() > 1 && p_path.size() == 1) {
		if (p_path[0] != "0") {
			return ERR_UNAVAILABLE; // Only the first document is replaced as a whole.
		}
		yaml->set_data(p_value);
		return OK;
	}
	return DataTreeAdapter::set_value(p_path, p_value);
}

Error YAMLTreeAdapter::add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) {
	Ref<YAML> yaml = resource;
	if (yaml.is_null()) {
		return ERR_DOES_NOT_EXIST;
	}
	if (_get_document_count() > 1 && p_path.is_empty()) {
		yaml->add_document(p_value);
		return OK;
	}
	return DataTreeAdapter::add_value(p_path, p_key, p_value);
}

Error YAMLTreeAdapter::rename(const PackedStringArray &p_path, const String &p_key) {
	if (_get_document_count() > 1 && p_path.size() == 1) {
		return ERR_INVALID_PARAMETER; // Documents are numbered.
	}
	return DataTreeAdapter::rename(p_path, p_key);
}

Error YAMLTreeAdapter::remove(const PackedStringArray &p_path) {
	Ref<YAML> yaml = resource;
	Variant node;
	if (yaml.is_null() || p_path.is_empty() || !_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (_get_document_count() > 1 && p_path.size() == 1) {
		yaml->remove_document(p_path[0].to_int());
		return OK;
	}
	String yaml_path;
	if (_get_yaml_path(p_path, yaml_path) && yaml->has_value(yaml_path)) {
		yaml->erase_value(yaml_path);
		return OK;
	}
	return DataTreeAdapter::remove(p_path);
}

Error YAMLTreeAdapter::move(const PackedStringArray &p_path, int p_index) {
	if (_get_document_count() > 1 && p_path.size() == 1) {
		return ERR_UNAVAILABLE; // Documents keep their order.
	}
	return DataTreeAdapter::move(p_path, p_index);
}
