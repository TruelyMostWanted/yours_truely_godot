/**************************************************************************/
/*  csv_format.h                                                          */
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

#include "core/io/serializer_options.h"
#include "core/io/text_data_format.h"
#include "core/templates/hash_map.h"
#include "core/variant/array.h"

// A CSV file (RFC 4180) as a Resource: `data` is an Array of rows (PackedStringArray), `header` the column
// names when SerializerOptions.csv_headers is on. Columns are addressed by index; find_column() resolves a
// header name. The static functions also read and write any list Resource as CSV (format FORMAT_CSV of
// ResourceTextSerializer); parse_string() returns rows as Dictionary (with headers) or PackedStringArray.
class CSV : public TextDataFormat {
	GDCLASS(CSV, TextDataFormat);

	PackedStringArray header;

	Array _get_rows() const;

protected:
	static void _bind_methods();

	virtual Error _parse_data(const String &p_text, const Ref<SerializerOptions> &p_options, Variant &r_data, String &r_error, int &r_line, int &r_column) override;
	virtual Error _encode_data(const Variant &p_data, const Ref<SerializerOptions> &p_options, String &r_text, String &r_error) const override;
	virtual Variant _get_empty_data() const override { return Array(); }
	virtual Variant::Type _get_data_type() const override { return Variant::ARRAY; }
	virtual void _get_extensions(List<String> *r_extensions) const override;
	virtual Ref<SerializerOptions> _get_path_options(const String &p_path) const override;
	virtual void _cleared() override { header = PackedStringArray(); }
	virtual Ref<DataValidation> _validate_data(const Ref<DataSchema> &p_schema, const Ref<DataValidationOptions> &p_options) const override;
	virtual void _resolve_issue_lines(const Ref<DataValidation> &p_validation, const String &p_text) const override;

public:
	// Column roles of a translation CSV header, see check_translation_header().
	struct TranslationHeader {
		HashMap<int, String> locale_columns; // Column -> standardized locale, in column order.
		int context_column = -1;
		int plural_column = -1;
		Vector<int> invalid_columns; // Not a locale (e.g. an empty cell): ignored.
		Vector<int> unknown_columns; // A locale whose language code is unknown: still a locale column.
		bool has_known_language = false;

		// A translation table, not data that happens to have a locale-like column ("unit,id,hp": "id" is
		// Indonesian): every column after the key column is a locale of a known language, a `_` note or
		// `?context` / `?plural`. The name of the key column does not matter (usually "keys").
		bool is_translation_table() const { return has_known_language && invalid_columns.is_empty() && unknown_columns.is_empty(); }
	};

	// Checks a header row the way ResourceImporterCSVTranslation reads it: column 0 holds the keys,
	// "?context" and "?plural" columns are special, "_" columns are ignored, all others are locales.
	static void check_translation_header(const Vector<String> &p_header, TranslationHeader &r_header, DataValidation *r_validation);
	// Validates `header` and the rows as a translation table (locales, duplicate keys, missing translations).
	Ref<DataValidation> validate_translation(const Ref<DataValidationOptions> &p_options = Ref<DataValidationOptions>()) const;

	// On error, r_line is the 1-based line of the problem.
	static Error parse_text(const String &p_text, const Ref<SerializerOptions> &p_options, Array &r_data, String &r_error, int &r_line);
	// Rows are arrays (PackedStringArray or Array) or dictionaries (columns in order of first appearance).
	static Error write_text(const Array &p_data, const Ref<SerializerOptions> &p_options, String &r_text, String &r_error);

	void set_header(const PackedStringArray &p_header);
	PackedStringArray get_header() const { return header; }

	int get_row_count() const;
	int get_column_count() const;
	PackedStringArray get_row(int p_row) const;
	Dictionary get_row_as_dictionary(int p_row) const;
	void append_row(const PackedStringArray &p_values);
	void insert_row(int p_row, const PackedStringArray &p_values);
	void remove_row(int p_row);
	void set_row(int p_row, const PackedStringArray &p_values);
	String get_cell(int p_row, int p_column) const;
	void set_cell(int p_row, int p_column, const String &p_value);
	// A block of cells as an Array of PackedStringArray (rows past the end are left out, missing cells are empty).
	Array get_cells(int p_row, int p_column, int p_row_count, int p_column_count) const;
	// Writes a block of cells (an Array of PackedStringArray) at p_row/p_column, appending rows as needed. Emits `changed` once.
	void set_cells(int p_row, int p_column, const Array &p_cells);
	// Inserts a column at p_column (up to get_column_count()): `p_name` into the header (if there is one or
	// p_name is set), `p_value` into every row. Emits `changed` once.
	void insert_column(int p_column, const String &p_name = String(), const String &p_value = String());
	// Removes the column at p_column from the header and every row. Emits `changed` once.
	void remove_column(int p_column);
	int find_column(const String &p_name) const;
	PackedStringArray get_column(int p_column) const;
	PackedInt32Array find_rows(int p_column, const String &p_value) const;

	// Typed access through a schema: cells converted to the type of their field (by header name, or by
	// position without a header); empty or unconvertible cells give the field's default_value.
	Dictionary get_typed_row(int p_row, const Ref<DataSchema> &p_schema) const;
	Error append_typed_row(const Dictionary &p_values, const Ref<DataSchema> &p_schema);
	Variant get_typed_column(int p_column, const Ref<DataSchema> &p_schema) const;

	static Array parse_string(const String &p_text, const Ref<SerializerOptions> &p_options = Ref<SerializerOptions>());
	static String stringify(const Array &p_data, const Ref<SerializerOptions> &p_options = Ref<SerializerOptions>());
	static String serialize(const Ref<Resource> &p_resource, const Ref<SerializerOptions> &p_options = Ref<SerializerOptions>());
	static Ref<Resource> deserialize(const String &p_text, const StringName &p_type_hint = StringName(), const Ref<SerializerOptions> &p_options = Ref<SerializerOptions>());

	CSV();
};
