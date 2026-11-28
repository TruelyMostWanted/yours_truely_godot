/**************************************************************************/
/*  yaml_tree_adapter.h                                                   */
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

// YAML: the Variant tree of `data`; with more than one document, the top level has one node per document
// ("Document 2"). Edits in the first document go through YAML.set_value() and erase_value(), whose paths are the
// adapter's paths; documents are added and removed with add_document() and remove_document().
class YAMLTreeAdapter : public DataTreeAdapter {
	GDCLASS(YAMLTreeAdapter, DataTreeAdapter);

	int _get_document_count() const;
	// The YAML.get_value() path of p_path in the first document; false for a path in another document.
	bool _get_yaml_path(const PackedStringArray &p_path, String &r_path) const;

protected:
	virtual Variant _get_root() const override;
	virtual void _decorate(Entry &r_entry, int p_depth) const override;
	virtual void _commit() override;

public:
	static Ref<DataTreeAdapter> create(const Ref<Resource> &p_resource);

	// The YAML.get_value() path ("a/b~1c/0"), with "(document 2)" for a path in another document.
	virtual String format_path(const PackedStringArray &p_path) const override;
	virtual Error set_value(const PackedStringArray &p_path, const Variant &p_value) override;
	virtual Error add_value(const PackedStringArray &p_path, const String &p_key, const Variant &p_value) override;
	virtual Error rename(const PackedStringArray &p_path, const String &p_key) override;
	virtual Error remove(const PackedStringArray &p_path) override;
	virtual Error move(const PackedStringArray &p_path, int p_index) override;
};
