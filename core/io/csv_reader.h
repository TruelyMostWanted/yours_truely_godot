/**************************************************************************/
/*  csv_reader.h                                                          */
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

#include "core/io/file_access.h"
#include "core/io/serializer_options.h"
#include "core/object/ref_counted.h"
#include "core/templates/local_vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

// Reads a CSV file (UTF-8, RFC 4180, same rules as CSV) row by row from a file buffer, so memory stays
// bounded by one row and the read buffer, not by the file. A sparse index (one entry per INDEX_STEP rows,
// filled while reading) lets seek_row() jump close to a row, which paging needs. Not a Resource.
class CSVReader : public RefCounted {
	GDCLASS(CSVReader, RefCounted);

	static constexpr int BUFFER_SIZE = 65536;
	static constexpr int INDEX_STEP = 1024;

	Ref<FileAccess> file;
	String path;
	PackedStringArray header;
	char32_t delimiter = ',';

	// Read buffer: buffer_start is the file offset of buffer[0].
	LocalVector<uint8_t> buffer;
	int64_t buffer_start = 0;
	int buffer_length = 0;
	int buffer_position = 0;

	int line = 1; // 1-based line of the next byte.
	int row_index = 0; // Data rows read or skipped so far.
	int row_total = -1; // Known once the end of the file was reached.
	bool eof = false;

	Error error = OK;
	String error_message;
	int error_line = 0;

	// Offset and line of rows 0, INDEX_STEP, 2 * INDEX_STEP, ... that were reached.
	LocalVector<int64_t> index_positions;
	LocalVector<int> index_lines;

	int _peek();
	int _next_byte();
	void _jump(int64_t p_position, int p_line);
	bool _read_record(PackedStringArray *r_cells);
	void _fail(Error p_error, const String &p_message, int p_line);
	Error _skip(int p_count);

protected:
	static void _bind_methods();

public:
	static Ref<CSVReader> open(const String &p_path, const Ref<SerializerOptions> &p_options = Ref<SerializerOptions>());
	void close();
	bool is_open() const { return file.is_valid(); }
	String get_path() const { return path; }
	PackedStringArray get_header() const { return header; }

	bool is_eof() const { return eof; }
	int get_row_index() const { return row_index; }
	int get_line() const { return line; }
	Error get_error() const { return error; }
	String get_error_message() const { return error_message; }
	int get_error_line() const { return error_line; }

	PackedStringArray read_row();
	Array read_rows(int p_count);
	Dictionary read_record();
	Error skip_rows(int p_count);
	Error seek_row(int p_row);
	int count_rows();
};
