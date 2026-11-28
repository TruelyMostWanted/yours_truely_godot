/**************************************************************************/
/*  toml_tree_adapter.h                                                   */
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

#include "editor/data/data_tree_adapter.h"

// TOML: tables are NAMED nodes, an array of tables is an INDEXED one. Edits of keys in tables go through
// TOML.set_value(), erase_table() and erase_table_key(); inside an array of tables through `data`. TOML has no
// null, so no value is set to null.
class TOMLTreeAdapter : public DataTreeAdapter {
	GDCLASS(TOMLTreeAdapter, DataTreeAdapter);

	// The TOML.get_value() table path of the table at p_path; false if a part of it is not a table.
	bool _get_table_path(const PackedStringArray &p_path, String &r_table_path) const;

protected:
	virtual Variant _get_root() const override;
	virtual void _decorate(Entry &r_entry, int p_depth) const override;
	virtual void _commit() override;

public:
	// A non-empty Array of Dictionaries only, as `[[name]]` writes it.
	static bool is_array_of_tables(const Variant &p_value);
	static Ref<DataTreeAdapter> create(const Ref<Resource> &p_resource);

	// TOML dotted keys: `server."host name"`, `unit[0].name`.
	virtual String format_path(const PackedStringArray &p_path) const override;
	virtual PackedInt32Array get_value_types(const PackedStringArray &p_path) const override;
	virtual Error set_value(const PackedStringArray &p_path, const Variant &p_value) override;
	virtual Error add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) override;
	virtual Error remove(const PackedStringArray &p_path) override;
};
