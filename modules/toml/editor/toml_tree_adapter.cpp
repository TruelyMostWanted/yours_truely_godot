/**************************************************************************/
/*  toml_tree_adapter.cpp                                                 */
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

#include "toml_tree_adapter.h"

#include "../toml.h"

bool TOMLTreeAdapter::is_array_of_tables(const Variant &p_value) {
	if (p_value.get_type() != Variant::ARRAY) {
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

Ref<DataTreeAdapter> TOMLTreeAdapter::create(const Ref<Resource> &p_resource) {
	return Ref<DataTreeAdapter>(memnew(TOMLTreeAdapter));
}

bool TOMLTreeAdapter::_get_table_path(const PackedStringArray &p_path, String &r_table_path) const {
	Variant node = _get_root();
	r_table_path = String();
	for (const String &segment : p_path) {
		if (node.get_type() != Variant::DICTIONARY) {
			return false;
		}
		const Dictionary table = node;
		if (!table.has(segment)) {
			return false;
		}
		node = table[segment];
		r_table_path += (r_table_path.is_empty() ? "" : ".") + TOML::escape_table_name(segment);
	}
	return node.get_type() == Variant::DICTIONARY;
}

Variant TOMLTreeAdapter::_get_root() const {
	const Ref<TOML> toml = resource;
	return toml.is_valid() ? toml->get_data() : Variant();
}

void TOMLTreeAdapter::_decorate(Entry &r_entry, int p_depth) const {
	// The parsed data does not tell `[server]` from an inline table, so both are tables.
	if (r_entry.data.get_type() == Variant::DICTIONARY) {
		r_entry.type = TTR("Table");
	} else if (is_array_of_tables(r_entry.data)) {
		r_entry.type = TTR("Array of Tables");
	}
}

void TOMLTreeAdapter::_commit() {
	Ref<TOML> toml = resource;
	ERR_FAIL_COND(toml.is_null());
	toml->set_data(toml->get_data());
}

String TOMLTreeAdapter::format_path(const PackedStringArray &p_path) const {
	String text;
	Variant node = _get_root();
	for (const String &segment : p_path) {
		if (node.get_type() == Variant::ARRAY) {
			text += "[" + segment + "]";
		} else {
			bool bare = !segment.is_empty();
			for (int i = 0; i < segment.length() && bare; i++) {
				const char32_t c = segment[i];
				bare = is_ascii_alphanumeric_char(c) || c == U'_' || c == U'-';
			}
			text += (text.is_empty() ? "" : ".") + (bare ? segment : "\"" + segment.c_escape() + "\"");
		}
		Variant child;
		if (!_get_child(node, segment, child)) {
			break;
		}
		node = child;
	}
	return text;
}

PackedInt32Array TOMLTreeAdapter::get_value_types(const PackedStringArray &p_path) const {
	return make_types({ Variant::BOOL, Variant::INT, Variant::FLOAT, Variant::STRING, Variant::ARRAY, Variant::DICTIONARY });
}

Error TOMLTreeAdapter::set_value(const PackedStringArray &p_path, const Variant &p_value) {
	Variant node;
	if (p_path.is_empty() || !_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (p_value.get_type() == Variant::NIL) {
		return ERR_INVALID_PARAMETER; // TOML has no null.
	}
	String table_path;
	if (!_get_table_path(parent_of(p_path), table_path)) {
		return DataTreeAdapter::set_value(p_path, p_value); // Inside an array of tables.
	}
	Ref<TOML> toml = resource;
	toml->set_value(table_path, p_path[p_path.size() - 1], p_value);
	return OK;
}

Error TOMLTreeAdapter::add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) {
		return ERR_INVALID_PARAMETER;
	}
	String table_path;
	if (p_key.is_empty() || !_get_table_path(p_path, table_path)) {
		return DataTreeAdapter::add_value(p_path, p_key, p_value);
	}
	Ref<TOML> toml = resource;
	if (toml->has_table_key(table_path, p_key)) {
		return ERR_ALREADY_EXISTS;
	}
	toml->set_value(table_path, p_key, p_value);
	return OK;
}

Error TOMLTreeAdapter::remove(const PackedStringArray &p_path) {
	Variant node;
	if (p_path.is_empty() || !_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	String table_path;
	if (!_get_table_path(parent_of(p_path), table_path)) {
		return DataTreeAdapter::remove(p_path);
	}
	Ref<TOML> toml = resource;
	const String key = p_path[p_path.size() - 1];
	if (node.get_type() == Variant::DICTIONARY) {
		toml->erase_table((table_path.is_empty() ? String() : table_path + ".") + TOML::escape_table_name(key));
	} else {
		toml->erase_table_key(table_path, key);
	}
	return OK;
}
