/**************************************************************************/
/*  data_tree_adapter.cpp                                                 */
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

#include "data_tree_adapter.h"

#include "core/core_string_names.h"
#include "core/io/csv_format.h"
#include "core/io/ini_format.h"
#include "core/io/json.h"
#include "core/io/xml_format.h"
#include "core/object/callable_mp.h"

// The index of p_segment in an Array of p_size items, -1 if it is not one.
static int _index_of(const String &p_segment, int p_size) {
	if (!p_segment.is_valid_int()) {
		return -1;
	}
	const int64_t index = p_segment.to_int();
	return index >= 0 && index < p_size ? (int)index : -1;
}

// DataTreeAdapter

void DataTreeAdapter::register_adapter(const char *p_class_name, CreateFunc p_create) {
	for (int i = 0; i < factory_count; i++) {
		if (String(factories[i].class_name) == p_class_name) {
			factories[i].create = p_create;
			return;
		}
	}
	ERR_FAIL_COND_MSG(factory_count >= MAX_FACTORIES, "Too many data tree adapters.");
	factories[factory_count].class_name = p_class_name;
	factories[factory_count].create = p_create;
	factory_count++;
}

Ref<DataTreeAdapter> DataTreeAdapter::create_for(const Ref<Resource> &p_resource) {
	Ref<DataTreeAdapter> adapter;
	Object *object = p_resource.ptr();
	if (object == nullptr) {
		return adapter;
	}
	if (Object::cast_to<JSON>(object)) {
		adapter = Ref<DataTreeAdapter>(memnew(JSONTreeAdapter));
	} else if (Object::cast_to<INI>(object)) {
		adapter = Ref<DataTreeAdapter>(memnew(INITreeAdapter));
	} else if (Object::cast_to<XML>(object)) {
		adapter = Ref<DataTreeAdapter>(memnew(XMLTreeAdapter));
	} else if (Object::cast_to<CSV>(object)) {
		if (CSVTreeAdapter::is_path_form(Ref<CSV>(p_resource))) {
			adapter = Ref<DataTreeAdapter>(memnew(CSVTreeAdapter));
		}
	} else {
		for (int i = 0; i < factory_count; i++) {
			if (object->is_class(factories[i].class_name)) {
				adapter = factories[i].create(p_resource);
				break;
			}
		}
	}
	if (adapter.is_valid()) {
		adapter->setup(p_resource);
	}
	return adapter;
}

String DataTreeAdapter::escape_segment(const String &p_segment) {
	return p_segment.replace("~", "~0").replace("/", "~1");
}

String DataTreeAdapter::unescape_segment(const String &p_segment) {
	return p_segment.replace("~1", "/").replace("~0", "~");
}

String DataTreeAdapter::join_path(const PackedStringArray &p_path) {
	String text;
	for (int i = 0; i < p_path.size(); i++) {
		text += (i > 0 ? "/" : "") + escape_segment(p_path[i]);
	}
	return text;
}

PackedStringArray DataTreeAdapter::split_path(const String &p_text) {
	// Empty segments are skipped, as in YAML.get_value(); a leading "/" (JSON Pointer) is allowed.
	PackedStringArray path;
	for (const String &part : p_text.split("/", false)) {
		path.push_back(unescape_segment(part));
	}
	return path;
}

PackedInt32Array DataTreeAdapter::make_types(std::initializer_list<Variant::Type> p_types) {
	PackedInt32Array types;
	for (const Variant::Type type : p_types) {
		types.push_back(type);
	}
	return types;
}

bool DataTreeAdapter::find_key(const Dictionary &p_dictionary, const String &p_segment, Variant &r_key) {
	if (p_dictionary.has(p_segment)) {
		r_key = p_segment;
		return true;
	}
	for (const KeyValue<Variant, Variant> &kv : p_dictionary) {
		if (String(kv.key) == p_segment) {
			r_key = kv.key;
			return true;
		}
	}
	return false;
}

bool DataTreeAdapter::is_list(const Dictionary &p_dictionary) {
	const int size = p_dictionary.size();
	if (size == 0) {
		return false;
	}
	for (const KeyValue<Variant, Variant> &kv : p_dictionary) {
		if (kv.key.get_type() != Variant::STRING || _index_of(kv.key, size) < 0 || itos(String(kv.key).to_int()) != String(kv.key)) {
			return false;
		}
	}
	return true;
}

Vector<Variant> DataTreeAdapter::get_list_values(const Dictionary &p_list) {
	Vector<Variant> values;
	values.resize(p_list.size());
	for (const KeyValue<Variant, Variant> &kv : p_list) {
		values.write[String(kv.key).to_int()] = kv.value;
	}
	return values;
}

void DataTreeAdapter::renumber(Dictionary p_list, const Vector<Variant> &p_values) {
	p_list.clear();
	for (int i = 0; i < p_values.size(); i++) {
		p_list[itos(i)] = p_values[i];
	}
}

String DataTreeAdapter::short_text(const Variant &p_value) {
	String text;
	switch (p_value.get_type()) {
		case Variant::NIL: {
			text = "null";
		} break;
		case Variant::ARRAY: {
			// Item by item, so a big Array is not turned into text as a whole.
			const Array array = p_value;
			text = "[";
			for (int i = 0; i < array.size() && text.length() <= MAX_VALUE_LENGTH; i++) {
				text += (i > 0 ? ", " : "") + short_text(array[i]);
			}
			text += "]";
		} break;
		case Variant::DICTIONARY: {
			const Dictionary dictionary = p_value;
			text = "{";
			int index = 0;
			for (const KeyValue<Variant, Variant> &kv : dictionary) {
				if (text.length() > MAX_VALUE_LENGTH) {
					break;
				}
				text += (index++ > 0 ? ", " : "") + short_text(kv.key) + ": " + short_text(kv.value);
			}
			text += "}";
		} break;
		default: {
			text = String(p_value);
		} break;
	}
	text = text.replace("\n", " ");
	if (text.length() > MAX_VALUE_LENGTH) {
		text = text.left(MAX_VALUE_LENGTH) + "...";
	}
	return text;
}

PackedStringArray DataTreeAdapter::parent_of(const PackedStringArray &p_path) {
	PackedStringArray parent = p_path;
	if (!parent.is_empty()) {
		parent.resize(parent.size() - 1);
	}
	return parent;
}

DataTreeAdapter::Entry DataTreeAdapter::_make_entry(const String &p_segment, const Variant &p_node, int p_depth) const {
	Entry entry;
	entry.segment = p_segment;
	entry.key = p_segment;
	entry.data = p_node;
	entry.kind = _get_kind(p_node);
	if (entry.kind == NODE_KIND_VALUE) {
		const Variant value = _get_node_value(p_node);
		entry.type = Variant::get_type_name(value.get_type());
		entry.value = short_text(value);
	} else {
		entry.child_count = _get_child_count(p_node);
		if (entry.kind == NODE_KIND_NAMED) {
			entry.type = "Dictionary";
			entry.value = vformat(TTRN("%d key", "%d keys", entry.child_count), entry.child_count);
		} else {
			entry.type = p_node.get_type() == Variant::ARRAY ? String("Array") : TTR("List");
			entry.value = vformat(TTRN("%d item", "%d items", entry.child_count), entry.child_count);
		}
	}
	_decorate(entry, p_depth);
	return entry;
}

DataTreeAdapter::NodeKind DataTreeAdapter::_get_kind(const Variant &p_node) const {
	if (p_node.get_type() == Variant::DICTIONARY) {
		return NODE_KIND_NAMED;
	}
	if (p_node.get_type() == Variant::ARRAY) {
		// Only an Array that holds containers is a node of the tree; an Array of scalars is one value.
		const Array array = p_node;
		for (const Variant &item : array) {
			if (item.get_type() == Variant::DICTIONARY || item.get_type() == Variant::ARRAY) {
				return NODE_KIND_INDEXED;
			}
		}
	}
	return NODE_KIND_VALUE;
}

int DataTreeAdapter::_get_child_count(const Variant &p_node) const {
	if (p_node.get_type() == Variant::DICTIONARY) {
		return Dictionary(p_node).size();
	}
	if (p_node.get_type() == Variant::ARRAY) {
		return Array(p_node).size();
	}
	return 0;
}

void DataTreeAdapter::_get_children(const Variant &p_node, int p_depth, int p_from, int p_end, LocalVector<Entry> &r_entries) const {
	if (p_node.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_node;
		int index = 0;
		for (const KeyValue<Variant, Variant> &kv : dictionary) {
			if (index >= p_end) {
				break;
			}
			if (index >= p_from) {
				r_entries.push_back(_make_entry(String(kv.key), kv.value, p_depth));
			}
			index++;
		}
	} else if (p_node.get_type() == Variant::ARRAY) {
		const Array array = p_node;
		for (int i = p_from; i < MIN(p_end, array.size()); i++) {
			r_entries.push_back(_make_entry(itos(i), array[i], p_depth));
		}
	}
}

bool DataTreeAdapter::_get_child(const Variant &p_node, const String &p_segment, Variant &r_child) const {
	if (p_node.get_type() == Variant::DICTIONARY) {
		const Dictionary dictionary = p_node;
		Variant key;
		if (!find_key(dictionary, p_segment, key)) {
			return false;
		}
		r_child = dictionary[key];
		return true;
	}
	if (p_node.get_type() == Variant::ARRAY) {
		const Array array = p_node;
		const int index = _index_of(p_segment, array.size());
		if (index < 0) {
			return false;
		}
		r_child = array[index];
		return true;
	}
	return false;
}

Variant DataTreeAdapter::_new_container(NodeKind p_kind) {
	return p_kind == NODE_KIND_INDEXED ? Variant(Array()) : Variant(Dictionary());
}

bool DataTreeAdapter::_resolve(const PackedStringArray &p_path, Variant &r_node) const {
	Variant node = _get_root();
	for (const String &segment : p_path) {
		Variant child;
		if (_get_kind(node) == NODE_KIND_VALUE || !_get_child(node, segment, child)) {
			return false;
		}
		node = child;
	}
	r_node = node;
	return true;
}

bool DataTreeAdapter::get_entry(const PackedStringArray &p_path, Entry &r_entry) const {
	Variant node;
	if (!_resolve(p_path, node)) {
		return false;
	}
	r_entry = _make_entry(p_path.is_empty() ? String() : p_path[p_path.size() - 1], node, p_path.size());
	return true;
}

DataTreeAdapter::NodeKind DataTreeAdapter::get_kind(const PackedStringArray &p_path) const {
	Variant node;
	return _resolve(p_path, node) ? _get_kind(node) : NODE_KIND_VALUE;
}

int DataTreeAdapter::get_child_count(const PackedStringArray &p_path) const {
	Variant node;
	if (!_resolve(p_path, node) || _get_kind(node) == NODE_KIND_VALUE) {
		return 0;
	}
	return _get_child_count(node);
}

void DataTreeAdapter::get_children(const PackedStringArray &p_path, int p_from, int p_count, LocalVector<Entry> &r_entries) const {
	Variant node;
	if (!_resolve(p_path, node) || _get_kind(node) == NODE_KIND_VALUE) {
		return;
	}
	const int total = _get_child_count(node);
	const int end = p_count < 0 ? total : MIN(total, p_from + p_count);
	if (p_from < 0 || p_from >= end) {
		return;
	}
	_get_children(node, p_path.size() + 1, p_from, end, r_entries);
}

Variant DataTreeAdapter::get_value(const PackedStringArray &p_path) const {
	Variant node;
	return _resolve(p_path, node) ? _get_node_value(node) : Variant();
}

void DataTreeAdapter::_find_in(const Variant &p_node, const String &p_query, int p_max, PackedStringArray &r_path, Vector<PackedStringArray> &r_paths) const {
	LocalVector<Entry> entries;
	_get_children(p_node, r_path.size() + 1, 0, _get_child_count(p_node), entries);
	for (const Entry &entry : entries) {
		if (r_paths.size() >= p_max) {
			return;
		}
		r_path.push_back(entry.segment);
		// Only values: containers show their size.
		if (entry.key.findn(p_query) != -1 || (entry.kind == NODE_KIND_VALUE && entry.value.findn(p_query) != -1)) {
			r_paths.push_back(r_path);
		}
		if (entry.child_count > 0) {
			_find_in(entry.data, p_query, p_max, r_path, r_paths);
		}
		r_path.resize(r_path.size() - 1);
	}
}

Vector<PackedStringArray> DataTreeAdapter::find(const String &p_query, int p_max) const {
	Vector<PackedStringArray> paths;
	const Variant root = _get_root();
	if (!p_query.is_empty() && p_max > 0 && _get_kind(root) != NODE_KIND_VALUE) {
		PackedStringArray path;
		_find_in(root, p_query, p_max, path, paths);
	}
	return paths;
}

String DataTreeAdapter::format_path(const PackedStringArray &p_path) const {
	return "/" + join_path(p_path);
}

Error DataTreeAdapter::set_value(const PackedStringArray &p_path, const Variant &p_value) {
	Variant parent;
	if (p_path.is_empty() || !_resolve(parent_of(p_path), parent)) {
		return ERR_DOES_NOT_EXIST;
	}
	const String segment = p_path[p_path.size() - 1];
	if (parent.get_type() == Variant::DICTIONARY) {
		Dictionary dictionary = parent;
		Variant key;
		if (!find_key(dictionary, segment, key)) {
			return ERR_DOES_NOT_EXIST;
		}
		dictionary[key] = p_value;
	} else if (parent.get_type() == Variant::ARRAY) {
		Array array = parent;
		const int index = _index_of(segment, array.size());
		if (index < 0) {
			return ERR_DOES_NOT_EXIST;
		}
		array[index] = p_value;
	} else {
		return ERR_DOES_NOT_EXIST;
	}
	_commit();
	return OK;
}

Error DataTreeAdapter::add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) {
	Variant node;
	if (!_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	const NodeKind kind = _get_kind(node);
	if (node.get_type() == Variant::DICTIONARY && kind != NODE_KIND_VALUE) {
		Dictionary dictionary = node;
		if (kind == NODE_KIND_INDEXED) {
			dictionary[itos(dictionary.size())] = p_value;
		} else {
			Variant key;
			if (p_key.is_empty()) {
				return ERR_INVALID_PARAMETER;
			}
			if (find_key(dictionary, p_key, key)) {
				return ERR_ALREADY_EXISTS;
			}
			dictionary[p_key] = p_value;
		}
	} else if (node.get_type() == Variant::ARRAY) {
		Array array = node;
		array.push_back(p_value);
	} else {
		return ERR_INVALID_PARAMETER;
	}
	_commit();
	return OK;
}

Error DataTreeAdapter::add_container(const PackedStringArray &p_path, const String &p_key, NodeKind p_kind) {
	if (p_kind == NODE_KIND_VALUE) {
		return ERR_INVALID_PARAMETER;
	}
	return add_value(p_path, p_key, _new_container(p_kind));
}

Error DataTreeAdapter::rename(const PackedStringArray &p_path, const String &p_key) {
	Variant parent;
	if (p_path.is_empty() || !_resolve(parent_of(p_path), parent)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (p_key.is_empty() || parent.get_type() != Variant::DICTIONARY || _get_kind(parent) != NODE_KIND_NAMED) {
		return ERR_INVALID_PARAMETER;
	}
	Dictionary dictionary = parent;
	Variant key;
	Variant existing;
	if (!find_key(dictionary, p_path[p_path.size() - 1], key)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (find_key(dictionary, p_key, existing)) {
		return existing == key ? OK : ERR_ALREADY_EXISTS;
	}
	// In place, so the order of the keys stays.
	const Dictionary old = dictionary.duplicate();
	dictionary.clear();
	for (const KeyValue<Variant, Variant> &kv : old) {
		dictionary[kv.key == key ? Variant(p_key) : kv.key] = kv.value;
	}
	_commit();
	return OK;
}

Error DataTreeAdapter::remove(const PackedStringArray &p_path) {
	Variant parent;
	if (p_path.is_empty() || !_resolve(parent_of(p_path), parent)) {
		return ERR_DOES_NOT_EXIST;
	}
	const String segment = p_path[p_path.size() - 1];
	if (parent.get_type() == Variant::DICTIONARY) {
		Dictionary dictionary = parent;
		Variant key;
		if (!find_key(dictionary, segment, key)) {
			return ERR_DOES_NOT_EXIST;
		}
		if (_get_kind(parent) == NODE_KIND_INDEXED) {
			Vector<Variant> values = get_list_values(dictionary);
			values.remove_at(segment.to_int());
			renumber(dictionary, values);
		} else {
			dictionary.erase(key);
		}
	} else if (parent.get_type() == Variant::ARRAY) {
		Array array = parent;
		const int index = _index_of(segment, array.size());
		if (index < 0) {
			return ERR_DOES_NOT_EXIST;
		}
		array.remove_at(index);
	} else {
		return ERR_DOES_NOT_EXIST;
	}
	_commit();
	return OK;
}

Error DataTreeAdapter::move(const PackedStringArray &p_path, int p_index) {
	Variant parent;
	if (p_path.is_empty() || !_resolve(parent_of(p_path), parent)) {
		return ERR_DOES_NOT_EXIST;
	}
	const String segment = p_path[p_path.size() - 1];
	if (parent.get_type() == Variant::DICTIONARY) {
		Dictionary dictionary = parent;
		Variant key;
		if (!find_key(dictionary, segment, key)) {
			return ERR_DOES_NOT_EXIST;
		}
		if (_get_kind(parent) == NODE_KIND_INDEXED) {
			Vector<Variant> values = get_list_values(dictionary);
			const Variant item = values[segment.to_int()];
			values.remove_at(segment.to_int());
			values.insert(CLAMP(p_index, 0, values.size()), item);
			renumber(dictionary, values);
		} else {
			Array keys = dictionary.keys();
			keys.erase(key);
			keys.insert(CLAMP(p_index, 0, keys.size()), key);
			const Dictionary old = dictionary.duplicate();
			dictionary.clear();
			for (const Variant &each_key : keys) {
				dictionary[each_key] = old[each_key];
			}
		}
	} else if (parent.get_type() == Variant::ARRAY) {
		Array array = parent;
		const int index = _index_of(segment, array.size());
		if (index < 0) {
			return ERR_DOES_NOT_EXIST;
		}
		const Variant item = array[index];
		array.remove_at(index);
		array.insert(CLAMP(p_index, 0, array.size()), item);
	} else {
		return ERR_DOES_NOT_EXIST;
	}
	_commit();
	return OK;
}

// JSONTreeAdapter

Variant JSONTreeAdapter::_get_root() const {
	const Ref<JSON> json = resource;
	return json.is_valid() ? json->get_data() : Variant();
}

void JSONTreeAdapter::_commit() {
	Ref<JSON> json = resource;
	ERR_FAIL_COND(json.is_null());
	json->set_data(json->get_data()); // Drops the kept text, so a save writes the edit.
	json->emit_changed();
}

PackedInt32Array JSONTreeAdapter::get_value_types(const PackedStringArray &p_path) const {
	return make_types({ Variant::NIL, Variant::BOOL, Variant::FLOAT, Variant::STRING, Variant::ARRAY, Variant::DICTIONARY });
}

// FlatDataTreeAdapter

void FlatDataTreeAdapter::setup(const Ref<Resource> &p_resource) {
	DataTreeAdapter::setup(p_resource);
	dirty = true;
	// Event-driven: the tree is built again on the next read after the data changed.
	const Callable on_changed = callable_mp(this, &FlatDataTreeAdapter::_resource_changed);
	if (p_resource.is_valid() && !p_resource->is_connected(CoreStringName(changed), on_changed)) {
		p_resource->connect(CoreStringName(changed), on_changed);
	}
}

void FlatDataTreeAdapter::_build() const {
	if (!dirty) {
		return;
	}
	dirty = false;
	root = Dictionary();
	container_names.clear();
	container_names.insert(root.id(), String());
	_build_flat();
}

void FlatDataTreeAdapter::_add_flat(const String &p_container_name, const String &p_key, const Variant &p_value) const {
	Dictionary container = root;
	String prefix;
	for (const String &part : p_container_name.split("/", false)) {
		prefix += (prefix.is_empty() ? "" : "/") + part;
		const String segment = unescape_segment(part);
		const Variant child = container.get(segment, Variant());
		if (_is_container(child)) {
			container = child;
			continue;
		}
		// A container wins over a value of the same name.
		Dictionary created;
		container_names.insert(created.id(), prefix);
		container[segment] = created;
		container = created;
	}
	if (!p_container_name.is_empty()) {
		container_names[container.id()] = p_container_name;
	}
	if (!p_key.is_empty() && !_is_container(container.get(p_key, Variant()))) {
		container[p_key] = p_value;
	}
}

bool FlatDataTreeAdapter::_is_container(const Variant &p_node) const {
	return p_node.get_type() == Variant::DICTIONARY && container_names.has(Dictionary(p_node).id());
}

String FlatDataTreeAdapter::_get_container_name(const PackedStringArray &p_path) const {
	Variant node;
	if (p_path.is_empty() || !_resolve(p_path, node) || !_is_container(node)) {
		return String();
	}
	const String *name = container_names.getptr(Dictionary(node).id());
	return name && !name->is_empty() ? *name : join_path(p_path);
}

Variant FlatDataTreeAdapter::_get_root() const {
	_build();
	return root;
}

DataTreeAdapter::NodeKind FlatDataTreeAdapter::_get_kind(const Variant &p_node) const {
	if (!_is_container(p_node)) {
		return NODE_KIND_VALUE; // Arrays and Dictionaries in a value are one value.
	}
	return is_list(p_node) ? NODE_KIND_INDEXED : NODE_KIND_NAMED;
}

Variant FlatDataTreeAdapter::_new_container(NodeKind p_kind) {
	// A list is a container with the keys "0" .. "n-1"; until its first item it is a named one.
	Dictionary created;
	container_names.insert(created.id(), String());
	return created;
}

void FlatDataTreeAdapter::_flatten(const Dictionary &p_container, const PackedStringArray &p_path, Vector<String> &r_names, Vector<Dictionary> &r_values) const {
	Dictionary values;
	bool has_containers = false;
	for (const KeyValue<Variant, Variant> &kv : p_container) {
		if (_is_container(kv.value)) {
			has_containers = true;
		} else {
			values[kv.key] = kv.value;
		}
	}
	// An empty container is written once with no values; the top level only if it has values.
	if (!values.is_empty() || (!has_containers && !p_path.is_empty())) {
		r_names.push_back(join_path(p_path));
		r_values.push_back(values);
	}
	for (const KeyValue<Variant, Variant> &kv : p_container) {
		if (_is_container(kv.value)) {
			PackedStringArray path = p_path;
			path.push_back(String(kv.key));
			_flatten(kv.value, path, r_names, r_values);
		}
	}
}

void FlatDataTreeAdapter::_commit() {
	_build();
	Vector<String> names;
	Vector<Dictionary> values;
	_flatten(root, PackedStringArray(), names, values);
	dirty = true;
	_write_flat(names, values);
}

// INITreeAdapter

void INITreeAdapter::_build_flat() const {
	const Ref<INI> ini = resource;
	if (ini.is_null() || ini->get_data().get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary sections = ini->get_data();
	for (const KeyValue<Variant, Variant> &section : sections) {
		if (section.value.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary keys = section.value;
		if (keys.is_empty()) {
			_add_flat(section.key, String(), Variant());
		}
		for (const KeyValue<Variant, Variant> &kv : keys) {
			_add_flat(section.key, kv.key, kv.value);
		}
	}
}

void INITreeAdapter::_write_flat(const Vector<String> &p_names, const Vector<Dictionary> &p_values) {
	Ref<INI> ini = resource;
	ERR_FAIL_COND(ini.is_null());
	Dictionary sections;
	for (int i = 0; i < p_names.size(); i++) {
		sections[p_names[i]] = p_values[i];
	}
	ini->set_data(sections);
}

void INITreeAdapter::_decorate(Entry &r_entry, int p_depth) const {
	if (r_entry.kind == NODE_KIND_NAMED) {
		r_entry.type = TTR("Section");
	} else if (r_entry.kind == NODE_KIND_INDEXED) {
		r_entry.type = TTR("List");
	}
}

String INITreeAdapter::format_path(const PackedStringArray &p_path) const {
	if (p_path.is_empty()) {
		return String();
	}
	if (get_kind(p_path) != NODE_KIND_VALUE) {
		return "[" + _get_container_name(p_path) + "]";
	}
	const String section = _get_container_name(parent_of(p_path));
	const String key = p_path[p_path.size() - 1];
	return section.is_empty() ? key : "[" + section + "] " + key;
}

Error INITreeAdapter::set_value(const PackedStringArray &p_path, const Variant &p_value) {
	Variant node;
	if (p_path.is_empty() || !_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (p_value.get_type() == Variant::NIL) {
		return ERR_INVALID_PARAMETER; // INI erases a key set to null.
	}
	if (_is_container(node)) {
		return DataTreeAdapter::set_value(p_path, p_value);
	}
	Ref<INI> ini = resource;
	ini->set_value(_get_container_name(parent_of(p_path)), p_path[p_path.size() - 1], p_value);
	return OK;
}

Error INITreeAdapter::remove(const PackedStringArray &p_path) {
	Variant node;
	Variant parent;
	if (p_path.is_empty() || !_resolve(p_path, node) || !_resolve(parent_of(p_path), parent)) {
		return ERR_DOES_NOT_EXIST;
	}
	// INI erases a section with its last key; a list renumbers its items. Both go through the whole tree.
	if (_is_container(node) || _get_kind(parent) == NODE_KIND_INDEXED || Dictionary(parent).size() == 1) {
		return DataTreeAdapter::remove(p_path);
	}
	Ref<INI> ini = resource;
	ini->erase_section_key(_get_container_name(parent_of(p_path)), p_path[p_path.size() - 1]);
	return OK;
}

// CSVTreeAdapter

bool CSVTreeAdapter::is_path_form(const Ref<CSV> &p_csv) {
	if (p_csv.is_null()) {
		return false;
	}
	const PackedStringArray header = p_csv->get_header();
	return header.size() == 3 && header[0].strip_edges().to_lower() == "path" && header[1].strip_edges().to_lower() == "key" && header[2].strip_edges().to_lower() == "value";
}

void CSVTreeAdapter::_build_flat() const {
	value_rows.clear();
	const Ref<CSV> csv = resource;
	if (csv.is_null()) {
		return;
	}
	const Array rows = csv->get_data();
	for (int i = 0; i < rows.size(); i++) {
		const PackedStringArray cells = rows[i];
		const String container_name = cells.size() > 0 ? cells[0].strip_edges() : String();
		const String key = cells.size() > 1 ? cells[1] : String();
		_add_flat(container_name, key, cells.size() > 2 ? cells[2] : String());
		if (!key.is_empty()) {
			PackedStringArray path = split_path(container_name);
			path.push_back(key);
			value_rows[join_path(path)] = i;
		}
	}
}

void CSVTreeAdapter::_write_flat(const Vector<String> &p_names, const Vector<Dictionary> &p_values) {
	Ref<CSV> csv = resource;
	ERR_FAIL_COND(csv.is_null());
	Array rows;
	for (int i = 0; i < p_names.size(); i++) {
		if (p_values[i].is_empty()) {
			rows.push_back(PackedStringArray({ p_names[i], String(), String() }));
		}
		for (const KeyValue<Variant, Variant> &kv : p_values[i]) {
			rows.push_back(PackedStringArray({ p_names[i], String(kv.key), String(kv.value) }));
		}
	}
	csv->set_data(rows);
}

int CSVTreeAdapter::_get_row(const PackedStringArray &p_path) const {
	Variant node;
	if (!_resolve(p_path, node) || _is_container(node)) {
		return -1;
	}
	const int *row = value_rows.getptr(join_path(p_path));
	return row ? *row : -1;
}

String CSVTreeAdapter::format_path(const PackedStringArray &p_path) const {
	if (p_path.is_empty() || get_kind(p_path) != NODE_KIND_VALUE) {
		return _get_container_name(p_path);
	}
	return _get_container_name(parent_of(p_path)) + "," + p_path[p_path.size() - 1];
}

PackedInt32Array CSVTreeAdapter::get_value_types(const PackedStringArray &p_path) const {
	return make_types({ Variant::STRING });
}

Error CSVTreeAdapter::set_value(const PackedStringArray &p_path, const Variant &p_value) {
	const int row = _get_row(p_path);
	if (row < 0) {
		return DataTreeAdapter::set_value(p_path, p_value);
	}
	Ref<CSV> csv = resource;
	csv->set_cell(row, 2, String(p_value));
	return OK;
}

Error CSVTreeAdapter::remove(const PackedStringArray &p_path) {
	const int row = _get_row(p_path);
	Variant parent;
	if (row < 0 || !_resolve(parent_of(p_path), parent) || _get_kind(parent) == NODE_KIND_INDEXED || Dictionary(parent).size() == 1) {
		return DataTreeAdapter::remove(p_path);
	}
	Ref<CSV> csv = resource;
	csv->remove_row(row);
	return OK;
}

// XMLTreeAdapter

Variant XMLTreeAdapter::_get_root() const {
	// The document: an Array with the root element, empty without one.
	Array document;
	const Ref<XML> xml = resource;
	if (xml.is_valid() && xml->get_data().get_type() == Variant::DICTIONARY && !Dictionary(xml->get_data()).is_empty()) {
		document.push_back(xml->get_data());
	}
	return document;
}

bool XMLTreeAdapter::_is_list(const Dictionary &p_element) {
	if (!Dictionary(p_element.get(XML::KEY_ATTRIBUTES, Dictionary())).is_empty() || !String(p_element.get(XML::KEY_TEXT, String())).strip_edges().is_empty()) {
		return false;
	}
	const Array children = p_element.get(XML::KEY_CHILDREN, Array());
	String tag;
	int count = 0;
	for (const Variant &child : children) {
		if (child.get_type() != Variant::DICTIONARY) {
			continue;
		}
		const String child_tag = Dictionary(child).get(XML::KEY_NAME, String());
		if (count > 0 && child_tag != tag) {
			return false;
		}
		tag = child_tag;
		count++;
	}
	return count >= 2;
}

String XMLTreeAdapter::_get_item_tag(const Dictionary &p_element) {
	const Array children = p_element.get(XML::KEY_CHILDREN, Array());
	for (const Variant &child : children) {
		if (child.get_type() == Variant::DICTIONARY) {
			return Dictionary(child).get(XML::KEY_NAME, String());
		}
	}
	return "item";
}

void XMLTreeAdapter::_collect(const Variant &p_node, LocalVector<Child> &r_children) {
	if (p_node.get_type() == Variant::ARRAY) {
		const Array document = p_node;
		for (int i = 0; i < document.size(); i++) {
			r_children.push_back({ String(Dictionary(document[i]).get(XML::KEY_NAME, String())), document[i], i });
		}
		return;
	}
	if (p_node.get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary element = p_node;
	const Array children = element.get(XML::KEY_CHILDREN, Array());
	if (_is_list(element)) {
		int index = 0;
		for (int i = 0; i < children.size(); i++) {
			if (children[i].get_type() == Variant::DICTIONARY) {
				r_children.push_back({ itos(index++), children[i], i });
			}
		}
		return;
	}
	const Dictionary attributes = element.get(XML::KEY_ATTRIBUTES, Dictionary());
	int position = 0;
	for (const KeyValue<Variant, Variant> &kv : attributes) {
		r_children.push_back({ "@" + String(kv.key), kv.value, position++ });
	}
	const String text = element.get(XML::KEY_TEXT, String());
	if (!text.strip_edges().is_empty()) {
		r_children.push_back({ "#text", text, -1 });
	}
	HashMap<String, int> seen;
	for (int i = 0; i < children.size(); i++) {
		if (children[i].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const String tag = Dictionary(children[i]).get(XML::KEY_NAME, String());
		int *count = seen.getptr(tag);
		r_children.push_back({ count ? vformat("%s[%d]", tag, *count) : tag, children[i], i });
		if (count) {
			(*count)++;
		} else {
			seen.insert(tag, 1);
		}
	}
}

DataTreeAdapter::NodeKind XMLTreeAdapter::_get_kind(const Variant &p_node) const {
	if (p_node.get_type() == Variant::ARRAY) {
		return NODE_KIND_NAMED; // The document.
	}
	if (p_node.get_type() != Variant::DICTIONARY) {
		return NODE_KIND_VALUE; // An attribute or "#text".
	}
	const Dictionary element = p_node;
	if (_is_list(element)) {
		return NODE_KIND_INDEXED;
	}
	if (!Dictionary(element.get(XML::KEY_ATTRIBUTES, Dictionary())).is_empty()) {
		return NODE_KIND_NAMED;
	}
	const Array children = element.get(XML::KEY_CHILDREN, Array());
	for (const Variant &child : children) {
		if (child.get_type() == Variant::DICTIONARY) {
			return NODE_KIND_NAMED;
		}
	}
	return NODE_KIND_VALUE; // Only text.
}

int XMLTreeAdapter::_get_child_count(const Variant &p_node) const {
	LocalVector<Child> children;
	_collect(p_node, children);
	return children.size();
}

void XMLTreeAdapter::_get_children(const Variant &p_node, int p_depth, int p_from, int p_end, LocalVector<Entry> &r_entries) const {
	LocalVector<Child> children;
	_collect(p_node, children);
	for (int i = p_from; i < MIN(p_end, (int)children.size()); i++) {
		r_entries.push_back(_make_entry(children[i].segment, children[i].node, p_depth));
	}
}

bool XMLTreeAdapter::_get_child(const Variant &p_node, const String &p_segment, Variant &r_child) const {
	LocalVector<Child> children;
	_collect(p_node, children);
	for (const Child &child : children) {
		if (child.segment == p_segment) {
			r_child = child.node;
			return true;
		}
	}
	return false;
}

Variant XMLTreeAdapter::_get_node_value(const Variant &p_node) const {
	if (p_node.get_type() == Variant::DICTIONARY) {
		return Dictionary(p_node).get(XML::KEY_TEXT, String());
	}
	return p_node;
}

void XMLTreeAdapter::_decorate(Entry &r_entry, int p_depth) const {
	if (r_entry.segment.begins_with("@")) {
		r_entry.type = TTR("Attribute");
	} else if (r_entry.kind == NODE_KIND_VALUE) {
		r_entry.type = TTR("Text");
	} else if (r_entry.data.get_type() == Variant::DICTIONARY) {
		r_entry.type = TTR("Element");
		const Dictionary attributes = Dictionary(r_entry.data).get(XML::KEY_ATTRIBUTES, Dictionary());
		if (!attributes.is_empty()) {
			PackedStringArray parts;
			for (const KeyValue<Variant, Variant> &kv : attributes) {
				parts.push_back(vformat("%s=\"%s\"", kv.key, kv.value));
			}
			r_entry.value = short_text(String(" ").join(parts));
		}
	}
}

bool XMLTreeAdapter::_locate(const PackedStringArray &p_path, Variant &r_parent, Child &r_child) const {
	if (p_path.is_empty() || !_resolve(parent_of(p_path), r_parent)) {
		return false;
	}
	LocalVector<Child> children;
	_collect(r_parent, children);
	for (const Child &child : children) {
		if (child.segment == p_path[p_path.size() - 1]) {
			r_child = child;
			return true;
		}
	}
	return false;
}

Ref<XMLElement> XMLTreeAdapter::_view(const Dictionary &p_element) const {
	return XMLElement::create_view(p_element, resource.is_valid() ? resource->get_instance_id() : ObjectID());
}

String XMLTreeAdapter::format_path(const PackedStringArray &p_path) const {
	return join_path(p_path);
}

PackedInt32Array XMLTreeAdapter::get_value_types(const PackedStringArray &p_path) const {
	return make_types({ Variant::STRING });
}

Error XMLTreeAdapter::set_value(const PackedStringArray &p_path, const Variant &p_value) {
	Variant parent;
	Child child;
	if (!_locate(p_path, parent, child)) {
		return ERR_DOES_NOT_EXIST;
	}
	const String text = p_value;
	if (child.segment.begins_with("@")) {
		_view(parent)->set_attribute(child.segment.substr(1), text);
	} else if (child.segment == "#text") {
		_view(parent)->set_text(text);
	} else if (child.node.get_type() == Variant::DICTIONARY && _get_kind(child.node) == NODE_KIND_VALUE) {
		_view(child.node)->set_text(text);
	} else {
		return ERR_INVALID_PARAMETER;
	}
	return OK;
}

Error XMLTreeAdapter::add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) {
	Variant node;
	if (!_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (node.get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_PARAMETER;
	}
	const Dictionary element = node;
	const String text = p_value;
	if (_is_list(element)) {
		// A new item copies the tag of its siblings.
		Ref<XMLElement> item;
		item.instantiate();
		item->set_name(_get_item_tag(element));
		item->set_text(text);
		_view(element)->add_child(item);
		return OK;
	}
	// New values become attributes.
	const String attribute = p_key.trim_prefix("@");
	if (attribute.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	Ref<XMLElement> view = _view(element);
	if (view->has_attribute(attribute)) {
		return ERR_ALREADY_EXISTS;
	}
	view->set_attribute(attribute, text);
	return OK;
}

Error XMLTreeAdapter::add_container(const PackedStringArray &p_path, const String &p_key, NodeKind p_kind) {
	Variant node;
	if (!_resolve(p_path, node)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (p_kind == NODE_KIND_VALUE) {
		return ERR_INVALID_PARAMETER;
	}
	if (node.get_type() == Variant::ARRAY) {
		// The document has one root element.
		if (!Array(node).is_empty()) {
			return ERR_ALREADY_EXISTS;
		}
		if (p_key.is_empty()) {
			return ERR_INVALID_PARAMETER;
		}
		Ref<XML> xml = resource;
		xml->create_root(p_key);
		return OK;
	}
	if (node.get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_PARAMETER;
	}
	const Dictionary element = node;
	const String tag = _is_list(element) ? _get_item_tag(element) : p_key;
	if (tag.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	Ref<XMLElement> child;
	child.instantiate();
	child->set_name(tag);
	_view(element)->add_child(child);
	return OK;
}

Error XMLTreeAdapter::rename(const PackedStringArray &p_path, const String &p_key) {
	Variant parent;
	Child child;
	if (!_locate(p_path, parent, child)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (p_key.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	if (child.segment.begins_with("@")) {
		const String old_name = child.segment.substr(1);
		const String new_name = p_key.trim_prefix("@");
		Dictionary attributes = Dictionary(parent).get(XML::KEY_ATTRIBUTES, Dictionary());
		if (attributes.has(new_name)) {
			return new_name == old_name ? OK : ERR_ALREADY_EXISTS;
		}
		// In place, so the order of the attributes stays.
		const Dictionary old = attributes.duplicate();
		attributes.clear();
		for (const KeyValue<Variant, Variant> &kv : old) {
			attributes[String(kv.key) == old_name ? Variant(new_name) : kv.key] = kv.value;
		}
		Ref<XML> xml = resource;
		xml->notify_tree_changed();
		return OK;
	}
	if (child.node.get_type() == Variant::DICTIONARY) {
		_view(child.node)->set_name(p_key);
		return OK;
	}
	return ERR_INVALID_PARAMETER; // "#text".
}

Error XMLTreeAdapter::remove(const PackedStringArray &p_path) {
	Variant parent;
	Child child;
	if (!_locate(p_path, parent, child)) {
		return ERR_DOES_NOT_EXIST;
	}
	Ref<XML> xml = resource;
	if (parent.get_type() == Variant::ARRAY) {
		xml->clear(); // The root element.
	} else if (child.segment.begins_with("@")) {
		_view(parent)->remove_attribute(child.segment.substr(1));
	} else if (child.segment == "#text") {
		_view(parent)->set_text(String());
	} else {
		_view(parent)->remove_child(XMLElement::create_view(child.node, ObjectID()));
	}
	return OK;
}

Error XMLTreeAdapter::move(const PackedStringArray &p_path, int p_index) {
	// p_index counts the siblings of the same kind: attributes among attributes, elements among elements.
	Variant parent;
	Child child;
	if (!_locate(p_path, parent, child)) {
		return ERR_DOES_NOT_EXIST;
	}
	if (parent.get_type() != Variant::DICTIONARY || child.segment == "#text") {
		return ERR_INVALID_PARAMETER;
	}
	Dictionary element = parent;
	if (child.segment.begins_with("@")) {
		Dictionary attributes = element.get(XML::KEY_ATTRIBUTES, Dictionary());
		Array names = attributes.keys();
		const Variant moved_name = names[child.position];
		names.remove_at(child.position);
		names.insert(CLAMP(p_index, 0, names.size()), moved_name);
		const Dictionary old = attributes.duplicate();
		attributes.clear();
		for (const Variant &each_name : names) {
			attributes[each_name] = old[each_name];
		}
	} else {
		Array children = element.get(XML::KEY_CHILDREN, Array());
		const Variant moved = children[child.position];
		children.remove_at(child.position);
		int target = children.size();
		int index = 0;
		for (int i = 0; i < children.size(); i++) {
			if (children[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			if (index == p_index) {
				target = i;
				break;
			}
			index++;
		}
		children.insert(target, moved);
	}
	Ref<XML> xml = resource;
	xml->notify_tree_changed();
	return OK;
}
