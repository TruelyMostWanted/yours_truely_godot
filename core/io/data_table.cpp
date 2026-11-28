/**************************************************************************/
/*  data_table.cpp                                                        */
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

#include "data_table.h"

#include "core/core_string_names.h"
#include "core/io/csv_format.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"
#include "core/templates/sort_array.h"

// Inserts `p_count` default values at `p_at`.
template <typename T>
static void _data_table_insert_cells(Vector<T> &r_data, int p_at, int p_count) {
	const int old_size = r_data.size();
	r_data.resize(old_size + p_count);
	T *w = r_data.ptrw();
	for (int i = old_size - 1; i >= p_at; i--) {
		w[i + p_count] = w[i];
	}
	for (int i = 0; i < p_count; i++) {
		w[p_at + i] = T();
	}
}

template <typename T>
static void _data_table_remove_cells(Vector<T> &r_data, int p_at, int p_count) {
	const int old_size = r_data.size();
	T *w = r_data.ptrw();
	for (int i = p_at + p_count; i < old_size; i++) {
		w[i - p_count] = w[i];
	}
	r_data.resize(old_size - p_count);
}

template <typename T>
static void _data_table_reset_cell(Vector<T> &r_data, int p_row) {
	r_data.write[p_row] = T();
}

// Absent cells of a column: one bit per row, allocated with the first absent cell (empty: every cell present),
// plus the number of set bits. Bits past the last row are always 0.
static bool _data_table_get_bit(const LocalVector<uint64_t> &p_bits, int p_row) {
	return (p_bits[p_row >> 6] >> (p_row & 63)) & 1;
}

static void _data_table_set_bit(LocalVector<uint64_t> &r_bits, int p_row, bool p_value) {
	const uint64_t mask = uint64_t(1) << (p_row & 63);
	if (p_value) {
		r_bits[p_row >> 6] |= mask;
	} else {
		r_bits[p_row >> 6] &= ~mask;
	}
}

// Grows the bitset to hold `p_rows` rows; new words are 0.
static void _data_table_reserve_bits(LocalVector<uint64_t> &r_bits, int p_rows) {
	const uint32_t words = uint32_t((p_rows + 63) / 64);
	if (r_bits.size() < words) {
		r_bits.resize_initialized(words);
	}
}

// Inserts `p_count` absent rows at `p_at` into a column of `p_old_rows` rows. O(p_count) when appending.
static void _data_table_insert_absent(LocalVector<uint64_t> &r_bits, int &r_absent_count, int p_at, int p_count, int p_old_rows) {
	_data_table_reserve_bits(r_bits, p_old_rows + p_count);
	if (r_absent_count > 0) {
		for (int row = p_old_rows - 1; row >= p_at; row--) {
			_data_table_set_bit(r_bits, row + p_count, _data_table_get_bit(r_bits, row));
		}
	}
	for (int row = p_at; row < p_at + p_count; row++) {
		_data_table_set_bit(r_bits, row, true);
	}
	r_absent_count += p_count;
}

static void _data_table_remove_absent(LocalVector<uint64_t> &r_bits, int &r_absent_count, int p_at, int p_count, int p_old_rows) {
	if (r_absent_count == 0) {
		return;
	}
	for (int row = p_at; row < p_at + p_count; row++) {
		if (_data_table_get_bit(r_bits, row)) {
			r_absent_count--;
		}
	}
	for (int row = p_at; row < p_old_rows - p_count; row++) {
		_data_table_set_bit(r_bits, row, _data_table_get_bit(r_bits, row + p_count));
	}
	for (int row = MAX(p_at, p_old_rows - p_count); row < p_old_rows; row++) {
		_data_table_set_bit(r_bits, row, false);
	}
}

// DataField.coerce(), and any data value as text in a String column.
static bool _data_table_convert(Variant::Type p_type, const Variant &p_value, Variant &r_value) {
	if (DataField::coerce(p_type, p_value, r_value)) {
		return true;
	}
	if (p_type == Variant::STRING && DataField::is_data_type(p_value.get_type())) {
		r_value = DataField::value_to_text(p_value);
		return true;
	}
	return false;
}

// Orders values of one type by `<`, other values by type; used by sort_rows().
static bool _data_table_less(const Variant &p_a, const Variant &p_b) {
	Variant result;
	bool valid = false;
	Variant::evaluate(Variant::OP_LESS, p_a, p_b, result, valid);
	if (valid) {
		return result.booleanize();
	}
	return p_a.get_type() < p_b.get_type();
}

struct DataTableRowOrder {
	const LocalVector<Variant> *keys = nullptr;
	const LocalVector<uint8_t> *present = nullptr;
	bool descending = false;

	// Absent cells last in both orders; equal keys keep their row order (stable).
	bool operator()(int32_t p_a, int32_t p_b) const {
		const bool has_a = (*present)[p_a] != 0;
		const bool has_b = (*present)[p_b] != 0;
		if (has_a != has_b) {
			return has_a;
		}
		if (has_a) {
			const Variant &key_a = (*keys)[p_a];
			const Variant &key_b = (*keys)[p_b];
			if (_data_table_less(key_a, key_b)) {
				return !descending;
			}
			if (_data_table_less(key_b, key_a)) {
				return descending;
			}
		}
		return p_a < p_b;
	}
};

Variant DataTable::Column::get(int p_row) const {
	switch (type) {
		case Variant::INT:
			return ints[p_row];
		case Variant::FLOAT:
			return floats[p_row];
		case Variant::STRING:
			return strings[p_row];
		case Variant::BOOL:
			return bools[p_row] != 0;
		default:
			return values[p_row];
	}
}

void DataTable::Column::set(int p_row, const Variant &p_value) {
	switch (type) {
		case Variant::INT:
			ints.write[p_row] = p_value.operator int64_t();
			break;
		case Variant::FLOAT:
			floats.write[p_row] = p_value.operator double();
			break;
		case Variant::STRING:
			strings.write[p_row] = p_value.operator String();
			break;
		case Variant::BOOL:
			bools.write[p_row] = p_value.operator bool() ? 1 : 0;
			break;
		default:
			values.write[p_row] = p_value;
			break;
	}
}

Variant DataTable::Column::get_data() const {
	switch (type) {
		case Variant::INT:
			return ints;
		case Variant::FLOAT:
			return floats;
		case Variant::STRING:
			return strings;
		case Variant::BOOL:
			return bools;
		default: {
			Array result;
			result.resize(values.size());
			for (int i = 0; i < values.size(); i++) {
				result[i] = values[i];
			}
			return result;
		}
	}
}

// Storage.

int DataTable::_get_row_count() const {
	int ret = 0;
	if (GDVIRTUAL_CALL(_get_row_count, ret)) {
		return ret;
	}
	return row_count;
}

Variant DataTable::_get_cell(int p_row, int p_column) const {
	Variant ret;
	if (GDVIRTUAL_CALL(_get_cell, p_row, p_column, ret)) {
		return ret;
	}
	return columns[p_column].get(p_row);
}

bool DataTable::_has_cell(int p_row, int p_column) const {
	bool ret = false;
	if (GDVIRTUAL_CALL(_has_cell, p_row, p_column, ret)) {
		return ret;
	}
	const Column &column = columns[p_column];
	return column.absent_count == 0 || !_data_table_get_bit(column.absent_bits, p_row);
}

void DataTable::_set_cell(int p_row, int p_column, const Variant &p_value) {
	if (GDVIRTUAL_CALL(_set_cell, p_row, p_column, p_value)) {
		return;
	}
	Column &column = columns[p_column];
	column.set(p_row, p_value);
	if (column.absent_count > 0 && _data_table_get_bit(column.absent_bits, p_row)) {
		_data_table_set_bit(column.absent_bits, p_row, false);
		column.absent_count--;
	}
}

void DataTable::_erase_cell(int p_row, int p_column) {
	if (GDVIRTUAL_CALL(_erase_cell, p_row, p_column)) {
		return;
	}
	Column &column = columns[p_column];
	column.visit([&](auto &r_data) { _data_table_reset_cell(r_data, p_row); });
	_data_table_reserve_bits(column.absent_bits, row_count);
	if (!_data_table_get_bit(column.absent_bits, p_row)) {
		_data_table_set_bit(column.absent_bits, p_row, true);
		column.absent_count++;
	}
}

void DataTable::_insert_rows(int p_row, int p_count) {
	if (GDVIRTUAL_CALL(_insert_rows, p_row, p_count)) {
		return;
	}
	for (Column &column : columns) {
		column.visit([&](auto &r_data) { _data_table_insert_cells(r_data, p_row, p_count); });
		_data_table_insert_absent(column.absent_bits, column.absent_count, p_row, p_count, row_count);
	}
	row_count += p_count;
}

void DataTable::_remove_rows(int p_row, int p_count) {
	if (GDVIRTUAL_CALL(_remove_rows, p_row, p_count)) {
		return;
	}
	for (Column &column : columns) {
		column.visit([&](auto &r_data) { _data_table_remove_cells(r_data, p_row, p_count); });
		_data_table_remove_absent(column.absent_bits, column.absent_count, p_row, p_count, row_count);
	}
	row_count -= p_count;
}

void DataTable::_insert_column(int p_column) {
	if (GDVIRTUAL_CALL(_insert_column, p_column)) {
		return;
	}
	Column &column = columns[p_column];
	column.visit([&](auto &r_data) { r_data.resize_initialized(row_count); });
	column.absent_bits.clear();
	_data_table_reserve_bits(column.absent_bits, row_count);
	for (int row = 0; row < row_count; row++) {
		_data_table_set_bit(column.absent_bits, row, true);
	}
	column.absent_count = row_count;
}

void DataTable::_remove_column(int p_column) {
	if (GDVIRTUAL_CALL(_remove_column, p_column)) {
		return;
	}
	Column &column = columns[p_column];
	column.ints.clear();
	column.floats.clear();
	column.strings.clear();
	column.bools.clear();
	column.values.clear();
	column.absent_bits.clear();
	column.absent_count = 0;
}

Variant DataTable::_build_column(int p_column) const {
	Column column;
	column.type = columns[p_column].type;
	const int rows = get_row_count();
	column.visit([&](auto &r_data) { r_data.resize_initialized(rows); });
	for (int row = 0; row < rows; row++) {
		if (_has_cell(row, p_column)) {
			column.set(row, _get_cell(row, p_column));
		}
	}
	return column.get_data();
}

// Columns.

void DataTable::_schema_changed() {
	if (updating_schema) {
		return;
	}
	const Ref<DataValidation> validation = _sync_columns();
	if (!validation->is_valid()) {
		WARN_PRINT(vformat("%d cells of the table do not convert to the new type of their column; they are absent now.", validation->get_issue_count()));
	}
}

// A field edited directly (in the Inspector, or through DataSchema.get_field()) retypes or renames its column.
void DataTable::_field_changed() {
	if (updating_schema) {
		return;
	}
	for (const Column &column : columns) {
		if (column.field.is_valid() && column.field->get_value_type() != column.type) {
			_schema_changed();
			return;
		}
	}
	_emit_columns_changed();
}

void DataTable::_watch_field(const Ref<DataField> &p_field, bool p_watch) {
	if (p_field.is_null()) {
		return;
	}
	const Callable callable = callable_mp(this, &DataTable::_field_changed);
	const bool connected = p_field->is_connected(CoreStringName(changed), callable);
	if (p_watch && !connected) {
		p_field->connect_changed(callable);
	} else if (!p_watch && connected) {
		p_field->disconnect_changed(callable);
	}
}

void DataTable::_emit_columns_changed() {
	emit_signal(SNAME("columns_changed"));
	emit_changed();
}

void DataTable::_insert_field(int p_column, const Ref<DataField> &p_field) {
	if (schema.is_null()) {
		// The table's own schema, created with the first column (the property default stays null).
		Ref<DataSchema> own;
		own.instantiate();
		updating_schema = true;
		set_schema(own);
		updating_schema = false;
	}
	TypedArray<DataField> fields = schema->get_fields();
	fields.insert(p_column, p_field);
	updating_schema = true;
	schema->set_fields(fields);
	updating_schema = false;
	Column column;
	column.field = p_field;
	column.type = p_field->get_value_type();
	columns.insert(p_column, column);
	_insert_column(p_column);
	_watch_field(p_field, true);
}

DataTable::ColumnCells DataTable::_take_cells(int p_column) const {
	ColumnCells cells;
	cells.field = columns[p_column].field;
	cells.name = get_column_name(p_column);
	const int rows = get_row_count();
	for (int row = 0; row < rows; row++) {
		if (_has_cell(row, p_column)) {
			cells.rows.push_back(row);
			cells.values.push_back(_get_cell(row, p_column));
		}
	}
	return cells;
}

void DataTable::_restore_cells(int p_column, const ColumnCells &p_cells, DataValidation *r_validation) {
	const Variant::Type column_type = columns[p_column].type;
	for (uint32_t i = 0; i < p_cells.rows.size(); i++) {
		Variant typed;
		if (_data_table_convert(column_type, p_cells.values[i], typed)) {
			_set_cell(p_cells.rows[i], p_column, typed);
		} else {
			r_validation->add_issue(DataValidation::ISSUE_KIND_TYPE_MISMATCH, vformat("The value %s does not convert to %s.", DataField::value_to_text(p_cells.values[i]), Variant::get_type_name(column_type)), p_cells.name, String(), p_cells.rows[i], p_column);
		}
	}
}

// Brings the columns in line with the schema's fields: retypes columns whose field type changed, and rebuilds
// all columns if fields were added, removed or moved (cells follow their field, or a field of the same name).
Ref<DataValidation> DataTable::_sync_columns() {
	Ref<DataValidation> validation;
	validation.instantiate();
	const TypedArray<DataField> fields = schema.is_valid() ? schema->get_fields() : TypedArray<DataField>();
	bool same_fields = (int)columns.size() == fields.size();
	for (int i = 0; same_fields && i < fields.size(); i++) {
		same_fields = columns[i].field == Ref<DataField>(fields[i]);
	}
	if (same_fields) {
		bool retyped = false;
		for (int i = 0; i < get_column_count(); i++) {
			const Ref<DataField> &field = columns[i].field;
			const Variant::Type field_type = field.is_valid() ? field->get_value_type() : Variant::NIL;
			if (field_type == columns[i].type) {
				continue;
			}
			const ColumnCells cells = _take_cells(i);
			_remove_column(i);
			columns[i].type = field_type;
			_insert_column(i);
			_restore_cells(i, cells, validation.ptr());
			retyped = true;
		}
		if (retyped) {
			_emit_columns_changed();
		}
		return validation;
	}

	LocalVector<ColumnCells> old_cells;
	for (int i = 0; i < get_column_count(); i++) {
		old_cells.push_back(_take_cells(i));
	}
	while (!columns.is_empty()) {
		const int last = get_column_count() - 1;
		_remove_column(last);
		_watch_field(columns[last].field, false);
		columns.remove_at(last);
	}
	for (int i = 0; i < fields.size(); i++) {
		Column column;
		column.field = fields[i];
		column.type = column.field.is_valid() ? column.field->get_value_type() : Variant::NIL;
		columns.push_back(column);
		_insert_column(i);
		_watch_field(column.field, true);
		ColumnCells *source = nullptr;
		for (ColumnCells &cells : old_cells) {
			if (!cells.restored && cells.field == column.field) {
				source = &cells;
				break;
			}
		}
		for (ColumnCells &cells : old_cells) {
			if (source == nullptr && column.field.is_valid() && !cells.restored && cells.name == column.field->get_field_name()) {
				source = &cells;
			}
		}
		if (source) {
			source->restored = true;
			_restore_cells(i, *source, validation.ptr());
		}
	}
	_emit_columns_changed();
	return validation;
}

void DataTable::set_schema(const Ref<DataSchema> &p_schema) {
	if (p_schema == schema) {
		return;
	}
	if (schema.is_valid()) {
		schema->disconnect_changed(callable_mp(this, &DataTable::_schema_changed));
	}
	schema = p_schema;
	if (schema.is_valid()) {
		schema->connect_changed(callable_mp(this, &DataTable::_schema_changed));
	}
	_schema_changed();
}

StringName DataTable::get_column_name(int p_column) const {
	ERR_FAIL_INDEX_V(p_column, get_column_count(), StringName());
	const Ref<DataField> &field = columns[p_column].field;
	return field.is_valid() ? field->get_field_name() : StringName();
}

Variant::Type DataTable::get_column_type(int p_column) const {
	ERR_FAIL_INDEX_V(p_column, get_column_count(), Variant::NIL);
	return columns[p_column].type;
}

int DataTable::find_column(const StringName &p_name) const {
	for (int i = 0; i < get_column_count(); i++) {
		if (get_column_name(i) == p_name) {
			return i;
		}
	}
	return -1;
}

void DataTable::add_column(const StringName &p_name, Variant::Type p_type, int p_at) {
	ERR_FAIL_COND_MSG(p_name == StringName(), "A column needs a name.");
	ERR_FAIL_COND_MSG(find_column(p_name) != -1, vformat("The table already has a column \"%s\".", p_name));
	ERR_FAIL_COND_MSG(!DataField::is_data_type(p_type), vformat("%s is not a data type.", Variant::get_type_name(p_type)));
	const int at = p_at < 0 ? get_column_count() : p_at;
	ERR_FAIL_INDEX(at, get_column_count() + 1);
	Ref<DataField> field;
	field.instantiate();
	field->set_field_name(p_name);
	field->set_value_type(p_type);
	_insert_field(at, field);
	_emit_columns_changed();
}

void DataTable::remove_column(int p_column) {
	ERR_FAIL_INDEX(p_column, get_column_count());
	_remove_column(p_column);
	_watch_field(columns[p_column].field, false);
	columns.remove_at(p_column);
	updating_schema = true;
	schema->remove_field(p_column);
	updating_schema = false;
	_emit_columns_changed();
}

void DataTable::rename_column(int p_column, const StringName &p_name) {
	ERR_FAIL_INDEX(p_column, get_column_count());
	ERR_FAIL_COND_MSG(p_name == StringName(), "A column needs a name.");
	ERR_FAIL_COND_MSG(columns[p_column].field.is_null(), "The column has no field in the schema.");
	const int existing = find_column(p_name);
	ERR_FAIL_COND_MSG(existing != -1 && existing != p_column, vformat("The table already has a column \"%s\".", p_name));
	updating_schema = true;
	columns[p_column].field->set_field_name(p_name);
	updating_schema = false;
	_emit_columns_changed();
}

Ref<DataValidation> DataTable::set_column_type(int p_column, Variant::Type p_type) {
	ERR_FAIL_INDEX_V(p_column, get_column_count(), Ref<DataValidation>());
	ERR_FAIL_COND_V_MSG(columns[p_column].field.is_null(), Ref<DataValidation>(), "The column has no field in the schema.");
	ERR_FAIL_COND_V_MSG(!DataField::is_data_type(p_type), Ref<DataValidation>(), vformat("%s is not a data type.", Variant::get_type_name(p_type)));
	updating_schema = true;
	columns[p_column].field->set_value_type(p_type);
	updating_schema = false;
	return _sync_columns();
}

// Rows.

int DataTable::get_row_count() const {
	return _get_row_count();
}

// Sets the cells of the row that `p_values` has (keys are column names); the others are erased if
// `p_erase_missing` is set.
void DataTable::_write_row(int p_row, const Dictionary &p_values, bool p_erase_missing) {
	int matched = 0;
	for (int column = 0; column < get_column_count(); column++) {
		const Variant *value = p_values.getptr(String(get_column_name(column)));
		Variant typed;
		if (value != nullptr) {
			matched++;
			if (_data_table_convert(columns[column].type, *value, typed)) {
				_set_cell(p_row, column, typed);
				continue;
			}
			ERR_PRINT(vformat("The value %s does not convert to the type %s of column \"%s\".", DataField::value_to_text(*value), Variant::get_type_name(columns[column].type), get_column_name(column)));
		}
		if (p_erase_missing && _has_cell(p_row, column)) {
			_erase_cell(p_row, column);
		}
	}
	ERR_FAIL_COND_MSG(matched < p_values.size(), "Some keys of the row are not columns of the table; they were ignored.");
}

void DataTable::add_row(const Dictionary &p_values) {
	insert_row(get_row_count(), p_values);
}

void DataTable::insert_row(int p_row, const Dictionary &p_values) {
	ERR_FAIL_INDEX(p_row, get_row_count() + 1);
	_insert_rows(p_row, 1);
	_write_row(p_row, p_values, false);
	emit_signal(SNAME("rows_inserted"), p_row, 1);
	emit_changed();
}

void DataTable::remove_rows(int p_row, int p_count) {
	ERR_FAIL_COND(p_count < 0);
	ERR_FAIL_COND_MSG(p_row < 0 || p_row + p_count > get_row_count(), vformat("Rows %d to %d are out of range (%d rows).", p_row, p_row + p_count - 1, get_row_count()));
	if (p_count == 0) {
		return;
	}
	_remove_rows(p_row, p_count);
	emit_signal(SNAME("rows_removed"), p_row, p_count);
	emit_changed();
}

Dictionary DataTable::get_row(int p_row) const {
	ERR_FAIL_INDEX_V(p_row, get_row_count(), Dictionary());
	Dictionary row;
	for (int column = 0; column < get_column_count(); column++) {
		if (_has_cell(p_row, column)) {
			row[String(get_column_name(column))] = _get_cell(p_row, column);
		}
	}
	return row;
}

void DataTable::set_row(int p_row, const Dictionary &p_values) {
	ERR_FAIL_INDEX(p_row, get_row_count());
	_write_row(p_row, p_values, true);
	emit_signal(SNAME("cells_changed"), p_row, 0, 1, get_column_count());
	emit_changed();
}

// Cells.

Variant DataTable::get_cell(int p_row, int p_column) const {
	ERR_FAIL_INDEX_V(p_row, get_row_count(), Variant());
	ERR_FAIL_INDEX_V(p_column, get_column_count(), Variant());
	if (!_has_cell(p_row, p_column)) {
		return Variant();
	}
	return _get_cell(p_row, p_column);
}

void DataTable::set_cell(int p_row, int p_column, const Variant &p_value) {
	ERR_FAIL_INDEX(p_row, get_row_count());
	ERR_FAIL_INDEX(p_column, get_column_count());
	Variant typed;
	ERR_FAIL_COND_MSG(!_data_table_convert(columns[p_column].type, p_value, typed), vformat("The value %s does not convert to the type %s of column \"%s\".", DataField::value_to_text(p_value), Variant::get_type_name(columns[p_column].type), get_column_name(p_column)));
	_set_cell(p_row, p_column, typed);
	emit_signal(SNAME("cells_changed"), p_row, p_column, 1, 1);
	emit_changed();
}

bool DataTable::has_cell(int p_row, int p_column) const {
	ERR_FAIL_INDEX_V(p_row, get_row_count(), false);
	ERR_FAIL_INDEX_V(p_column, get_column_count(), false);
	return _has_cell(p_row, p_column);
}

void DataTable::erase_cell(int p_row, int p_column) {
	ERR_FAIL_INDEX(p_row, get_row_count());
	ERR_FAIL_INDEX(p_column, get_column_count());
	if (!_has_cell(p_row, p_column)) {
		return;
	}
	_erase_cell(p_row, p_column);
	emit_signal(SNAME("cells_changed"), p_row, p_column, 1, 1);
	emit_changed();
}

Variant DataTable::get_column(int p_column) const {
	ERR_FAIL_INDEX_V(p_column, get_column_count(), Variant());
	// The member storage holds the cells unless a script or a C++ subclass keeps its own rows.
	if (!GDVIRTUAL_IS_OVERRIDDEN(_get_cell) && _get_row_count() == row_count) {
		return columns[p_column].get_data();
	}
	return _build_column(p_column);
}

PackedInt32Array DataTable::find_rows(int p_column, const Variant &p_value) const {
	PackedInt32Array result;
	ERR_FAIL_INDEX_V(p_column, get_column_count(), result);
	Variant typed;
	if (!_data_table_convert(columns[p_column].type, p_value, typed)) {
		return result;
	}
	const int rows = get_row_count();
	for (int row = 0; row < rows; row++) {
		if (_has_cell(row, p_column) && _get_cell(row, p_column) == typed) {
			result.push_back(row);
		}
	}
	return result;
}

void DataTable::sort_rows(int p_column, SortOrder p_order) {
	ERR_FAIL_INDEX(p_column, get_column_count());
	ERR_FAIL_INDEX(p_order, SORT_ORDER_DESCENDING + 1);
	const int rows = get_row_count();
	const int column_count = get_column_count();
	if (rows < 2) {
		return;
	}
	// Snapshot of all cells, row by row, through the storage virtuals.
	LocalVector<Variant> cells;
	LocalVector<uint8_t> present;
	cells.resize(rows * column_count);
	present.resize(rows * column_count);
	LocalVector<Variant> keys;
	LocalVector<uint8_t> key_present;
	keys.resize(rows);
	key_present.resize(rows);
	for (int row = 0; row < rows; row++) {
		for (int column = 0; column < column_count; column++) {
			const int index = row * column_count + column;
			present[index] = _has_cell(row, column) ? 1 : 0;
			if (present[index]) {
				cells[index] = _get_cell(row, column);
			}
		}
		key_present[row] = present[row * column_count + p_column];
		keys[row] = cells[row * column_count + p_column];
	}
	LocalVector<int32_t> order;
	order.resize(rows);
	for (int row = 0; row < rows; row++) {
		order[row] = row;
	}
	SortArray<int32_t, DataTableRowOrder> sorter;
	sorter.compare.keys = &keys;
	sorter.compare.present = &key_present;
	sorter.compare.descending = p_order == SORT_ORDER_DESCENDING;
	sorter.sort(order.ptr(), rows);

	// Rewrite in place through _set_cell() and _erase_cell(), so sources keep their rows (no remove and insert).
	for (int row = 0; row < rows; row++) {
		const int source = order[row] * column_count;
		for (int column = 0; column < column_count; column++) {
			if (present[source + column]) {
				_set_cell(row, column, cells[source + column]);
			} else if (present[row * column_count + column]) {
				_erase_cell(row, column);
			}
		}
	}
	emit_signal(SNAME("cells_changed"), 0, 0, rows, column_count);
	emit_changed();
}

Ref<DataValidation> DataTable::validate(const Ref<DataValidationOptions> &p_options) const {
	if (schema.is_null()) {
		Ref<DataValidation> result;
		result.instantiate();
		return result;
	}
	return schema->validate(to_records(), p_options);
}

// Conversion.

Ref<DataTable> DataTable::create_from_records(const Array &p_records) {
	Ref<DataTable> table;
	table.instantiate();
	// Columns: the union of the keys in first-seen order. A column has the type of its values if all agree,
	// else it holds any Variant (so int and float values keep their type in a round trip).
	LocalVector<StringName> names;
	LocalVector<Variant::Type> types;
	HashMap<StringName, uint32_t> indices;
	for (const Variant &record : p_records) {
		ERR_CONTINUE_MSG(record.get_type() != Variant::DICTIONARY, "Every record must be a Dictionary.");
		const Dictionary values = record;
		for (const KeyValue<Variant, Variant> &entry : values) {
			const StringName key = String(entry.key);
			Variant::Type value_type = entry.value.get_type();
			if (!DataField::is_data_type(value_type)) {
				value_type = Variant::NIL;
			}
			const uint32_t *index = indices.getptr(key);
			if (index == nullptr) {
				indices.insert(key, names.size());
				names.push_back(key);
				types.push_back(value_type);
				continue;
			}
			Variant::Type &column_type = types[*index];
			if (column_type == value_type) {
				continue;
			}
			column_type = Variant::NIL;
		}
	}
	for (uint32_t i = 0; i < names.size(); i++) {
		table->add_column(names[i], types[i]);
	}
	for (const Variant &record : p_records) {
		if (record.get_type() != Variant::DICTIONARY) {
			continue;
		}
		// Keys as String, the column names (YAML and TOML records can have int keys).
		Dictionary row;
		for (const KeyValue<Variant, Variant> &entry : record.operator Dictionary()) {
			row[String(entry.key)] = entry.value;
		}
		table->add_row(row);
	}
	return table;
}

Array DataTable::to_records() const {
	Array records;
	const int rows = get_row_count();
	for (int row = 0; row < rows; row++) {
		records.push_back(get_row(row));
	}
	return records;
}

Ref<DataTable> DataTable::create_from_csv(const Ref<CSV> &p_csv, const Ref<DataSchema> &p_schema) {
	ERR_FAIL_COND_V(p_csv.is_null(), Ref<DataTable>());
	Ref<DataTable> table;
	table.instantiate();
	const PackedStringArray header = p_csv->get_header();
	const int column_count = MAX((int)header.size(), p_csv->get_column_count());
	for (int column = 0; column < column_count; column++) {
		// The matching field of the schema keeps its type and rules; other columns are String columns.
		const DataField *source = p_schema.is_valid() ? p_schema->get_column_field(header, column) : nullptr;
		Ref<DataField> field;
		if (source) {
			field = source->duplicate();
		} else {
			field.instantiate();
		}
		StringName column_name = column < header.size() ? StringName(header[column]) : field->get_field_name();
		if (column_name == StringName() || table->find_column(column_name) != -1) {
			column_name = vformat("column_%d", column + 1);
		}
		field->set_field_name(column_name);
		table->_insert_field(column, field);
	}
	int failures = 0;
	const int rows = p_csv->get_row_count();
	for (int row = 0; row < rows; row++) {
		table->_insert_rows(row, 1);
		const PackedStringArray cells = p_csv->get_row(row);
		for (int column = 0; column < column_count && column < cells.size(); column++) {
			const Variant::Type column_type = table->columns[column].type;
			const String &text = cells[column];
			// An empty cell of a typed column is absent; text columns keep the empty text.
			if (text.is_empty() && column_type != Variant::STRING && column_type != Variant::NIL) {
				continue;
			}
			Variant value;
			if (!DataField::text_to_type(column_type, text, value)) {
				failures++;
				continue;
			}
			table->_set_cell(row, column, value);
		}
	}
	if (failures > 0) {
		WARN_PRINT(vformat("%d CSV cells do not convert to the type of their column; they are absent in the table.", failures));
	}
	return table;
}

Ref<CSV> DataTable::to_csv() const {
	Ref<CSV> csv;
	csv.instantiate();
	const int column_count = get_column_count();
	Array rows;
	for (int row = 0; row < get_row_count(); row++) {
		PackedStringArray cells;
		cells.resize(column_count);
		for (int column = 0; column < column_count; column++) {
			if (_has_cell(row, column)) {
				cells.write[column] = DataField::value_to_text(_get_cell(row, column));
			}
		}
		rows.push_back(cells);
	}
	csv->set_data(rows);
	PackedStringArray header;
	for (int column = 0; column < column_count; column++) {
		header.push_back(get_column_name(column));
	}
	csv->set_header(header);
	return csv;
}

// Saved as [row count, then per column: name, cells (absent cells hold the default), absent rows].
Array DataTable::_get_data() const {
	Array data;
	const int rows = get_row_count();
	data.push_back(rows);
	for (int column = 0; column < get_column_count(); column++) {
		PackedInt32Array absent;
		for (int row = 0; row < rows; row++) {
			if (!_has_cell(row, column)) {
				absent.push_back(row);
			}
		}
		data.push_back(get_column_name(column));
		data.push_back(get_column(column));
		data.push_back(absent);
	}
	return data;
}

void DataTable::_set_data(const Array &p_data) {
	ERR_FAIL_COND_MSG(p_data.is_empty() || (p_data.size() - 1) % 3 != 0, "Invalid DataTable data.");
	const int rows = p_data[0];
	ERR_FAIL_COND(rows < 0);
	if (get_row_count() > 0) {
		_remove_rows(0, get_row_count());
	}
	_insert_rows(0, rows);
	for (int i = 1; i < p_data.size(); i += 3) {
		const StringName column_name = p_data[i];
		const int column = find_column(column_name);
		ERR_CONTINUE_MSG(column == -1, vformat("The saved column \"%s\" is not in the schema; its cells are dropped.", column_name));
		const Array values = p_data[i + 1];
		const PackedInt32Array absent = p_data[i + 2];
		ERR_CONTINUE(values.size() != rows);
		const Variant::Type column_type = columns[column].type;
		int absent_index = 0;
		for (int row = 0; row < rows; row++) {
			if (absent_index < absent.size() && absent[absent_index] == row) {
				absent_index++;
				continue;
			}
			// A bool column is saved as a PackedByteArray of 0 and 1.
			const Variant &value = values[row];
			Variant typed;
			if (column_type == Variant::BOOL && value.get_type() == Variant::INT) {
				_set_cell(row, column, (int64_t)value != 0);
			} else if (_data_table_convert(column_type, value, typed)) {
				_set_cell(row, column, typed);
			} else {
				ERR_PRINT(vformat("Saved cell %d of column \"%s\" does not convert to %s; it is absent.", row, column_name, Variant::get_type_name(column_type)));
			}
		}
	}
}

// The table's own schema (built-in) is copied with its fields, also by a shallow duplicate(): changing the columns
// of a copy must not change the original. A schema saved in a file stays shared.
Ref<Resource> DataTable::_duplicate(const DuplicateParams &p_params) const {
	Ref<DataTable> copy = Resource::_duplicate(p_params);
	if (copy.is_valid() && schema.is_valid() && copy->schema == schema && schema->is_built_in()) {
		copy->set_schema(schema->duplicate(true));
	}
	return copy;
}

void DataTable::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_schema", "schema"), &DataTable::set_schema);
	ClassDB::bind_method(D_METHOD("get_schema"), &DataTable::get_schema);

	ClassDB::bind_method(D_METHOD("get_column_count"), &DataTable::get_column_count);
	ClassDB::bind_method(D_METHOD("get_column_name", "column"), &DataTable::get_column_name);
	ClassDB::bind_method(D_METHOD("get_column_type", "column"), &DataTable::get_column_type);
	ClassDB::bind_method(D_METHOD("find_column", "name"), &DataTable::find_column);
	ClassDB::bind_method(D_METHOD("add_column", "name", "type", "at"), &DataTable::add_column, DEFVAL(Variant::NIL), DEFVAL(-1));
	ClassDB::bind_method(D_METHOD("remove_column", "column"), &DataTable::remove_column);
	ClassDB::bind_method(D_METHOD("rename_column", "column", "name"), &DataTable::rename_column);
	ClassDB::bind_method(D_METHOD("set_column_type", "column", "type"), &DataTable::set_column_type);

	ClassDB::bind_method(D_METHOD("get_row_count"), &DataTable::get_row_count);
	ClassDB::bind_method(D_METHOD("add_row", "values"), &DataTable::add_row, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("insert_row", "row", "values"), &DataTable::insert_row, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("remove_rows", "row", "count"), &DataTable::remove_rows, DEFVAL(1));
	ClassDB::bind_method(D_METHOD("get_row", "row"), &DataTable::get_row);
	ClassDB::bind_method(D_METHOD("set_row", "row", "values"), &DataTable::set_row);

	ClassDB::bind_method(D_METHOD("get_cell", "row", "column"), &DataTable::get_cell);
	ClassDB::bind_method(D_METHOD("set_cell", "row", "column", "value"), &DataTable::set_cell);
	ClassDB::bind_method(D_METHOD("has_cell", "row", "column"), &DataTable::has_cell);
	ClassDB::bind_method(D_METHOD("erase_cell", "row", "column"), &DataTable::erase_cell);
	ClassDB::bind_method(D_METHOD("get_column", "column"), &DataTable::get_column);

	ClassDB::bind_method(D_METHOD("find_rows", "column", "value"), &DataTable::find_rows);
	ClassDB::bind_method(D_METHOD("sort_rows", "column", "order"), &DataTable::sort_rows, DEFVAL(SORT_ORDER_ASCENDING));
	ClassDB::bind_method(D_METHOD("validate", "options"), &DataTable::validate, DEFVAL(Ref<DataValidationOptions>()));

	ClassDB::bind_static_method("DataTable", D_METHOD("create_from_records", "records"), &DataTable::create_from_records);
	ClassDB::bind_method(D_METHOD("to_records"), &DataTable::to_records);
	ClassDB::bind_static_method("DataTable", D_METHOD("create_from_csv", "csv", "schema"), &DataTable::create_from_csv, DEFVAL(Ref<DataSchema>()));
	ClassDB::bind_method(D_METHOD("to_csv"), &DataTable::to_csv);

	ClassDB::bind_method(D_METHOD("_set_data", "data"), &DataTable::_set_data);
	ClassDB::bind_method(D_METHOD("_get_data"), &DataTable::_get_data);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "schema", PROPERTY_HINT_RESOURCE_TYPE, "DataSchema"), "set_schema", "get_schema");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "_data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "_set_data", "_get_data");

	ADD_SIGNAL(MethodInfo("cells_changed", PropertyInfo(Variant::INT, "row"), PropertyInfo(Variant::INT, "column"), PropertyInfo(Variant::INT, "row_count"), PropertyInfo(Variant::INT, "column_count")));
	ADD_SIGNAL(MethodInfo("rows_inserted", PropertyInfo(Variant::INT, "row"), PropertyInfo(Variant::INT, "count")));
	ADD_SIGNAL(MethodInfo("rows_removed", PropertyInfo(Variant::INT, "row"), PropertyInfo(Variant::INT, "count")));
	ADD_SIGNAL(MethodInfo("columns_changed"));

	GDVIRTUAL_BIND(_get_row_count);
	GDVIRTUAL_BIND(_get_cell, "row", "column");
	GDVIRTUAL_BIND(_has_cell, "row", "column");
	GDVIRTUAL_BIND(_set_cell, "row", "column", "value");
	GDVIRTUAL_BIND(_erase_cell, "row", "column");
	GDVIRTUAL_BIND(_insert_rows, "row", "count");
	GDVIRTUAL_BIND(_remove_rows, "row", "count");
	GDVIRTUAL_BIND(_insert_column, "column");
	GDVIRTUAL_BIND(_remove_column, "column");

	BIND_ENUM_CONSTANT(SORT_ORDER_ASCENDING);
	BIND_ENUM_CONSTANT(SORT_ORDER_DESCENDING);
}

DataTable::DataTable() {
}
