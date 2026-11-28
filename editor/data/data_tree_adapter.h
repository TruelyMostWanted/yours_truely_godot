/**************************************************************************/
/*  data_tree_adapter.h                                                   */
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

#include "core/io/data_schema.h"
#include "core/io/resource.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"

class CSV;
class XMLElement;

// The tree model of the data formats in the editor (#463, #476): every format shows the same node kinds, the
// same paths and the same edits, while the data stays the Variant tree inside each format resource. One subclass
// per format; the YAML and TOML modules register theirs with register_adapter(). Editor only, not bound.
//
// A path is a list of segments: a key, a decimal index or a format-specific name (XML "@attribute", "Tag[1]").
// Its text form (join_path()) separates segments with "/" and escapes "~" as "~0" and "/" as "~1", as
// YAML.get_value() does. format_path() shows a path in the syntax of the file.
//
// Edits go through the format's own API (INI.set_value(), YAML.set_value(), TOML.set_value(), XMLElement,
// CSV.set_cell(), JSON.set_data()); each edit makes the resource emit `changed`.
class DataTreeAdapter : public RefCounted {
	GDCLASS(DataTreeAdapter, RefCounted);

public:
	enum NodeKind {
		NODE_KIND_NAMED, // Children by key: Dictionary, INI section, XML element, TOML table.
		NODE_KIND_INDEXED, // Children by index: Array of containers, INI/CSV list, XML element of same-tag children.
		NODE_KIND_VALUE, // A leaf: a scalar, or an Array that holds no containers.
	};

	struct Entry {
		String segment; // The last segment of the node's path.
		String key; // The name the tree shows (the segment, or "Document 2" and the like).
		String type;
		String value; // The value as one short line.
		Variant data; // The node in the format's data (internal; use get_value() for the value).
		NodeKind kind = NODE_KIND_VALUE;
		int child_count = 0;
	};

	typedef Ref<DataTreeAdapter> (*CreateFunc)(const Ref<Resource> &p_resource);

	static constexpr int MAX_VALUE_LENGTH = 200;

private:
	struct Factory {
		const char *class_name = nullptr;
		CreateFunc create = nullptr;
	};
	static constexpr int MAX_FACTORIES = 8;
	static inline Factory factories[MAX_FACTORIES];
	static inline int factory_count = 0;

	// The kind of p_node; the root (p_root) is a container whenever it is a Dictionary or an Array.
	NodeKind _get_kind_at(const Variant &p_node, bool p_root) const;
	void _find_in(const Variant &p_node, const String &p_query, int p_max, PackedStringArray &r_path, Vector<PackedStringArray> &r_paths) const;

protected:
	Ref<Resource> resource;

	// The node at p_path; false if there is none (a VALUE node has no children).
	bool _resolve(const PackedStringArray &p_path, Variant &r_node) const;
	static PackedInt32Array make_types(std::initializer_list<Variant::Type> p_types);
	// The key of p_dictionary whose text is p_segment (keys need not be Strings, as in YAML).
	static bool find_key(const Dictionary &p_dictionary, const String &p_segment, Variant &r_key);
	// Gives the values of a list (is_list()) the keys "0" .. "n-1" again, in p_values order.
	static void renumber(Dictionary p_list, const Vector<Variant> &p_values);
	// p_value as one short line (at most MAX_VALUE_LENGTH characters).
	static String short_text(const Variant &p_value);
	static PackedStringArray parent_of(const PackedStringArray &p_path);

	Entry _make_entry(const String &p_segment, const Variant &p_node, int p_depth) const;

	// The node of the empty path.
	virtual Variant _get_root() const { return Variant(); }
	// By default: a Dictionary is NAMED, an Array holding a Dictionary or Array is INDEXED, the rest is a VALUE.
	virtual NodeKind _get_kind(const Variant &p_node) const;
	virtual int _get_child_count(const Variant &p_node) const;
	// Entries of the children p_from .. p_end - 1; p_depth is the depth of the children (1 for top-level nodes).
	virtual void _get_children(const Variant &p_node, int p_depth, int p_from, int p_end, LocalVector<Entry> &r_entries) const;
	virtual bool _get_child(const Variant &p_node, const String &p_segment, Variant &r_child) const;
	// The value of a VALUE node (XML: the text of a text-only element).
	virtual Variant _get_node_value(const Variant &p_node) const { return p_node; }
	// The type and key of an entry in the terms of the format ("Section", "Table", "Document 2").
	virtual void _decorate(Entry &r_entry, int p_depth) const {}
	// A new empty container for add_container().
	virtual Variant _new_container(NodeKind p_kind);
	// Hands the data to the format after the tree from _get_root() was changed in place.
	virtual void _commit() {}

public:
	// True if the keys of p_dictionary are exactly "0" .. "n-1" (n > 0): a list in INI and path CSV.
	static bool is_list(const Dictionary &p_dictionary);
	// The values of a list (is_list()) in index order.
	static Vector<Variant> get_list_values(const Dictionary &p_list);

	// Registers the adapter of a format class (and its subclasses). For modules; the core formats are built in.
	static void register_adapter(const char *p_class_name, CreateFunc p_create);
	// The adapter of p_resource, null if the format has no tree (a CSV without the header Path,Key,Value).
	static Ref<DataTreeAdapter> create_for(const Ref<Resource> &p_resource);

	static String escape_segment(const String &p_segment);
	static String unescape_segment(const String &p_segment);
	// "a/b~1c/0" for ["a", "b/c", "0"]; the empty path is "".
	static String join_path(const PackedStringArray &p_path);
	static PackedStringArray split_path(const String &p_text);

	// Sets the format resource; create_for() calls it.
	virtual void setup(const Ref<Resource> &p_resource) { resource = p_resource; }
	Ref<Resource> get_resource() const { return resource; }

	// The entry of the node at p_path (the root with an empty path). False if there is no such node.
	bool get_entry(const PackedStringArray &p_path, Entry &r_entry) const;
	NodeKind get_kind(const PackedStringArray &p_path) const;
	int get_child_count(const PackedStringArray &p_path) const;
	// The children of p_path from index p_from, at most p_count (all with -1).
	void get_children(const PackedStringArray &p_path, int p_from, int p_count, LocalVector<Entry> &r_entries) const;
	Variant get_value(const PackedStringArray &p_path) const;
	// Paths of the nodes whose key or value contains p_query (case-insensitive; values of VALUE nodes only),
	// depth-first in document order, at most p_max.
	Vector<PackedStringArray> find(const String &p_query, int p_max) const;
	// p_path in the syntax of the file. By default a JSON Pointer ("/a/b~1c/0").
	virtual String format_path(const PackedStringArray &p_path) const;
	// The Variant types a value of p_path may take (for a type picker). Empty: any type.
	virtual PackedInt32Array get_value_types(const PackedStringArray &p_path) const { return PackedInt32Array(); }

	// Replaces the value at p_path.
	virtual Error set_value(const PackedStringArray &p_path, const Variant &p_value);
	// Adds a value to the container at p_path: under p_key, or at the end of an INDEXED container (p_key unused).
	virtual Error add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value);
	// Adds an empty NAMED or INDEXED container, like add_value().
	virtual Error add_container(const PackedStringArray &p_path, const String &p_key, NodeKind p_kind);
	// Renames the node at p_path (not an item of an INDEXED container). The order of the siblings stays.
	virtual Error rename(const PackedStringArray &p_path, const String &p_key);
	virtual Error remove(const PackedStringArray &p_path);
	// Moves the node at p_path to position p_index among its siblings.
	virtual Error move(const PackedStringArray &p_path, int p_index);
};

// JSON: the Variant tree of `data`. Edits change `data` and clear the kept text (JSON.set_data()).
class JSONTreeAdapter : public DataTreeAdapter {
	GDCLASS(JSONTreeAdapter, DataTreeAdapter);

protected:
	virtual Variant _get_root() const override;
	virtual void _commit() override;

public:
	virtual PackedInt32Array get_value_types(const PackedStringArray &p_path) const override;
};

// The base of formats whose data is a flat list of (container name, key, value): INI sections and path CSV
// rows. A container name is split at "/" into segments ("MyItems/0", "res://a.gd" is "res:", "", "a.gd") and
// written back joined with "/", verbatim: "~" and "/" are escaped only in path text (join_path()), never in a
// stored name. The adapter builds the tree once per change of the data (event: the resource's `changed` signal).
// A container whose children are "0" .. "n-1" is a list; no count is stored. A value and a container with the same
// name: the container is shown.
class FlatDataTreeAdapter : public DataTreeAdapter {
	GDCLASS(FlatDataTreeAdapter, DataTreeAdapter);

	mutable bool dirty = true;
	mutable Dictionary root;
	mutable HashSet<const void *> containers; // Dictionary::id() of the containers of `root`.

	void _resource_changed() { dirty = true; }
	void _build() const;
	void _flatten(const Dictionary &p_container, const PackedStringArray &p_path, Vector<String> &r_names, Vector<Dictionary> &r_values) const;

protected:
	// For _build_flat(): adds p_value under p_key in the container named p_container_name.
	void _add_flat(const String &p_container_name, const String &p_key, const Variant &p_value) const;
	// The name in the file of the container at p_path: its segments joined with "/".
	static String _get_container_name(const PackedStringArray &p_path);
	// True if the tree was built, so the given node is a container of it.
	bool _is_container(const Variant &p_node) const;

	// Calls _add_flat() for each value (and once with an empty key for an empty container).
	virtual void _build_flat() const {}
	// Writes the whole tree: p_names[i] is a container name, p_values[i] its values (empty: an empty container).
	virtual void _write_flat(const Vector<String> &p_names, const Vector<Dictionary> &p_values) {}

	virtual Variant _get_root() const override;
	virtual NodeKind _get_kind(const Variant &p_node) const override;
	virtual void _commit() override;

public:
	virtual void setup(const Ref<Resource> &p_resource) override;
	virtual Error add_container(const PackedStringArray &p_path, const String &p_key, NodeKind p_kind) override;
};

// INI: sections are containers, their keys values. "[MyItems/0]" is item 0 of the list MyItems; the keys before
// the first section are top-level values. Values keep their Variant type.
class INITreeAdapter : public FlatDataTreeAdapter {
	GDCLASS(INITreeAdapter, FlatDataTreeAdapter);

protected:
	virtual void _build_flat() const override;
	virtual void _write_flat(const Vector<String> &p_names, const Vector<Dictionary> &p_values) override;
	virtual void _decorate(Entry &r_entry, int p_depth) const override;

public:
	// "[This/Is/A] Test".
	virtual String format_path(const PackedStringArray &p_path) const override;
	virtual Error set_value(const PackedStringArray &p_path, const Variant &p_value) override;
	virtual Error add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) override;
	virtual Error remove(const PackedStringArray &p_path) override;
};

// CSV in path form (header Path,Key,Value): one row per value, Path is the container ("This/Is/A", empty at the
// top level). A row with a Path and no Key keeps an empty container. Values are text.
class CSVTreeAdapter : public FlatDataTreeAdapter {
	GDCLASS(CSVTreeAdapter, FlatDataTreeAdapter);

	mutable HashMap<String, int> value_rows; // join_path() of a value -> its row.

	int _get_row(const PackedStringArray &p_path) const;

protected:
	virtual void _build_flat() const override;
	virtual void _write_flat(const Vector<String> &p_names, const Vector<Dictionary> &p_values) override;

public:
	// True if p_csv has the header Path,Key,Value.
	static bool is_path_form(const Ref<CSV> &p_csv);

	// "This/Is/A,Test".
	virtual String format_path(const PackedStringArray &p_path) const override;
	virtual PackedInt32Array get_value_types(const PackedStringArray &p_path) const override;
	virtual Error set_value(const PackedStringArray &p_path, const Variant &p_value) override;
	virtual Error add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) override;
	virtual Error remove(const PackedStringArray &p_path) override;
};

// XML: the root element is the only top-level node. Attributes ("@name") are values, so is an element with only
// text. A list (segments "0" .. "n-1") is an element without attributes and text whose child elements all share
// one tag (is_list_element()); otherwise children are named by tag, repeated tags as "Tag[1]", "Tag[2]". The text
// of an element with child elements is the value "#text". Values are text.
class XMLTreeAdapter : public DataTreeAdapter {
	GDCLASS(XMLTreeAdapter, DataTreeAdapter);

	struct Child {
		String segment;
		Variant node;
		int position = -1; // Index in the element's children Array (elements), or in its attributes.
	};

	Ref<DataSchema> schema;
	mutable bool schemas_dirty = true;
	mutable HashMap<const void *, Ref<DataSchema>> element_schemas; // Element Dictionary::id() -> its schema.

	void _resource_changed() { schemas_dirty = true; }
	void _map_schemas(const Dictionary &p_element, const Ref<DataSchema> &p_schema) const;
	// is_list_element() with the schema of p_element.
	bool _is_list(const Dictionary &p_element) const;
	void _collect(const Variant &p_node, LocalVector<Child> &r_children) const;
	static String _get_item_tag(const Dictionary &p_element);
	bool _locate(const PackedStringArray &p_path, Variant &r_parent, Child &r_child) const;
	Ref<XMLElement> _view(const Dictionary &p_element) const;

protected:
	virtual Variant _get_root() const override;
	virtual NodeKind _get_kind(const Variant &p_node) const override;
	virtual int _get_child_count(const Variant &p_node) const override;
	virtual void _get_children(const Variant &p_node, int p_depth, int p_from, int p_end, LocalVector<Entry> &r_entries) const override;
	virtual bool _get_child(const Variant &p_node, const String &p_segment, Variant &r_child) const override;
	virtual Variant _get_node_value(const Variant &p_node) const override;
	virtual void _decorate(Entry &r_entry, int p_depth) const override;

public:
	// True if p_element is a list: no attributes, no text, and its child elements all share one tag. Then, in this
	// order: p_schema (the schema of p_element, as XML validation maps it) decides when it has a field for the tag
	// (type ARRAY: a list, any other type: named); else two or more children are a list; else one child is a list
	// when the element's tag is its plural (`<units><unit/></units>`: tag + "s", tag + "es", "y" -> "ies";
	// case-insensitive). So a list keeps its paths ("units/0") down to one item, and `<A><Test>42</Test></A>`
	// stays a named element.
	static bool is_list_element(const Dictionary &p_element, const Ref<DataSchema> &p_schema = Ref<DataSchema>());

	// The schema that decides lists (see is_list_element()). setup() takes the schema listed for the file
	// (project setting filesystem/data/schema_files).
	void set_schema(const Ref<DataSchema> &p_schema);
	Ref<DataSchema> get_schema() const { return schema; }
	virtual void setup(const Ref<Resource> &p_resource) override;

	// "This/Is/A/@Test".
	virtual String format_path(const PackedStringArray &p_path) const override;
	virtual PackedInt32Array get_value_types(const PackedStringArray &p_path) const override;
	virtual Error set_value(const PackedStringArray &p_path, const Variant &p_value) override;
	virtual Error add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) override;
	virtual Error add_container(const PackedStringArray &p_path, const String &p_key, NodeKind p_kind) override;
	virtual Error rename(const PackedStringArray &p_path, const String &p_key) override;
	virtual Error remove(const PackedStringArray &p_path) override;
	virtual Error move(const PackedStringArray &p_path, int p_index) override;
};
