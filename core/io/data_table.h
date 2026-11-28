/**************************************************************************/
/*  data_table.h                                                          */
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
#include "core/object/gdvirtual.gen.h"
#include "core/templates/local_vector.h"
#include "core/variant/binder_common.h"

class CSV;

// A table whose columns are the fields of a DataSchema. Cells are stored per column (Structure of Arrays): a
// packed array for int, float, String and bool columns, a Variant array for every other type. A cell that a
// row does not have is absent (not null); the absent rows are a sorted list per column, so a column without
// absent cells costs nothing extra. The protected virtuals are the storage: scripts, GDExtensions and C++
// subclasses override them to expose the data of another source (a file, a database) without copying it.
class DataTable : public Resource {
	GDCLASS(DataTable, Resource);

public:
	enum SortOrder {
		SORT_ORDER_ASCENDING,
		SORT_ORDER_DESCENDING,
	};

private:
	struct Column {
		Ref<DataField> field;
		Variant::Type type = Variant::NIL;
		Vector<int64_t> ints;
		Vector<double> floats;
		Vector<String> strings;
		Vector<uint8_t> bools;
		Vector<Variant> values;
		LocalVector<int32_t> absent_rows;

		// Calls `p_function` with the array that stores the cells of this column type.
		template <typename F>
		void visit(F &&p_function) {
			switch (type) {
				case Variant::INT:
					p_function(ints);
					break;
				case Variant::FLOAT:
					p_function(floats);
					break;
				case Variant::STRING:
					p_function(strings);
					break;
				case Variant::BOOL:
					p_function(bools);
					break;
				default:
					p_function(values);
					break;
			}
		}
		Variant get(int p_row) const;
		void set(int p_row, const Variant &p_value);
		Variant get_data() const;
	};

	// The present cells of one column, kept while the column is rebuilt.
	struct ColumnCells {
		StringName name;
		Ref<DataField> field;
		LocalVector<int32_t> rows;
		LocalVector<Variant> values;
		bool restored = false;
	};

	Ref<DataSchema> schema;
	LocalVector<Column> columns;
	int row_count = 0;
	bool updating_schema = false;

	void _schema_changed();
	void _insert_field(int p_column, const Ref<DataField> &p_field);
	Ref<DataValidation> _sync_columns();
	ColumnCells _take_cells(int p_column) const;
	void _restore_cells(int p_column, const ColumnCells &p_cells, DataValidation *r_validation);
	void _write_row(int p_row, const Dictionary &p_values, bool p_erase_missing);
	void _emit_columns_changed();
	void _set_data(const Array &p_data);
	Array _get_data() const;

protected:
	static void _bind_methods();

	// Storage. The defaults keep the cells in memory and call a script or GDExtension override of the same name
	// first. Indices are checked before; `_set_cell()` gets a value of the column type; `_insert_rows()` and
	// `_insert_column()` add absent cells; the column of `_insert_column()` and `_remove_column()` is already
	// (still) in `columns` and the schema.
	virtual int _get_row_count() const;
	virtual Variant _get_cell(int p_row, int p_column) const;
	virtual bool _has_cell(int p_row, int p_column) const;
	virtual void _set_cell(int p_row, int p_column, const Variant &p_value);
	virtual void _erase_cell(int p_row, int p_column);
	virtual void _insert_rows(int p_row, int p_count);
	virtual void _remove_rows(int p_row, int p_count);
	virtual void _insert_column(int p_column);
	virtual void _remove_column(int p_column);
	// The column as get_column() returns it. C++ subclasses with their own storage return _build_column().
	virtual Variant _get_column(int p_column) const;
	Variant _build_column(int p_column) const;

	GDVIRTUAL0RC(int, _get_row_count)
	GDVIRTUAL2RC(Variant, _get_cell, int, int)
	GDVIRTUAL2RC(bool, _has_cell, int, int)
	GDVIRTUAL3(_set_cell, int, int, Variant)
	GDVIRTUAL2(_erase_cell, int, int)
	GDVIRTUAL2(_insert_rows, int, int)
	GDVIRTUAL2(_remove_rows, int, int)
	GDVIRTUAL1(_insert_column, int)
	GDVIRTUAL1(_remove_column, int)

public:
	void set_schema(const Ref<DataSchema> &p_schema);
	Ref<DataSchema> get_schema() const { return schema; }

	int get_column_count() const { return columns.size(); }
	StringName get_column_name(int p_column) const;
	Variant::Type get_column_type(int p_column) const;
	int find_column(const StringName &p_name) const;
	void add_column(const StringName &p_name, Variant::Type p_type = Variant::NIL, int p_at = -1);
	void remove_column(int p_column);
	void rename_column(int p_column, const StringName &p_name);
	// Converts the cells with DataField.coerce(); cells that do not convert become absent and are reported.
	Ref<DataValidation> set_column_type(int p_column, Variant::Type p_type);

	int get_row_count() const;
	void add_row(const Dictionary &p_values = Dictionary());
	void insert_row(int p_row, const Dictionary &p_values = Dictionary());
	void remove_rows(int p_row, int p_count = 1);
	Dictionary get_row(int p_row) const;
	void set_row(int p_row, const Dictionary &p_values);

	Variant get_cell(int p_row, int p_column) const;
	void set_cell(int p_row, int p_column, const Variant &p_value);
	bool has_cell(int p_row, int p_column) const;
	void erase_cell(int p_row, int p_column);
	Variant get_column(int p_column) const;

	PackedInt32Array find_rows(int p_column, const Variant &p_value) const;
	void sort_rows(int p_column, SortOrder p_order = SORT_ORDER_ASCENDING);
	Ref<DataValidation> validate(const Ref<DataValidationOptions> &p_options = Ref<DataValidationOptions>()) const;

	static Ref<DataTable> create_from_records(const Array &p_records);
	Array to_records() const;
	static Ref<DataTable> create_from_csv(const Ref<CSV> &p_csv, const Ref<DataSchema> &p_schema = Ref<DataSchema>());
	Ref<CSV> to_csv() const;

	DataTable();
};

VARIANT_ENUM_CAST(DataTable::SortOrder);
