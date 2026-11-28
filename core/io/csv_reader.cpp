/**************************************************************************/
/*  csv_reader.cpp                                                        */
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

#include "csv_reader.h"

#include "core/io/resource_text_serializer.h"
#include "core/object/class_db.h"

int CSVReader::_peek() {
	if (buffer_position >= buffer_length) {
		buffer_start += buffer_length;
		buffer_position = 0;
		buffer_length = 0;
		if (file.is_null()) {
			return -1;
		}
		buffer.resize(BUFFER_SIZE);
		buffer_length = (int)file->get_buffer(buffer.ptr(), BUFFER_SIZE);
		if (buffer_length <= 0) {
			buffer_length = 0;
			return -1;
		}
	}
	return buffer[buffer_position];
}

int CSVReader::_next_byte() {
	const int c = _peek();
	if (c >= 0) {
		buffer_position++;
	}
	return c;
}

void CSVReader::_jump(int64_t p_position, int p_line) {
	file->seek(p_position);
	buffer_start = p_position;
	buffer_length = 0;
	buffer_position = 0;
	line = p_line;
	eof = false;
}

void CSVReader::_fail(Error p_error, const String &p_message, int p_line) {
	error = p_error;
	error_message = p_message;
	error_line = p_line;
}

bool CSVReader::_read_record(PackedStringArray *r_cells) {
	if (eof || error != OK || file.is_null()) {
		return false;
	}
	if (row_index > 0 && row_index % INDEX_STEP == 0 && row_index / INDEX_STEP == (int)index_positions.size()) {
		index_positions.push_back(buffer_start + buffer_position);
		index_lines.push_back(line);
	}
	int record_line = line; // Moved past blank lines below.
	LocalVector<uint8_t> cell;
	int cell_count = 0;
	bool in_quotes = false;
	bool has_content = false;
	int quote_line = 0;
	while (true) {
		const int c = _next_byte();
		if (c < 0) {
			break;
		}
		if (in_quotes) {
			if (c == '"') {
				if (_peek() == '"') {
					_next_byte();
					cell.push_back('"');
				} else {
					in_quotes = false;
				}
			} else {
				if (c == '\n') {
					line++;
				}
				cell.push_back((uint8_t)c);
			}
			continue;
		}
		if (c == '"' && cell.is_empty()) {
			in_quotes = true;
			quote_line = line;
			has_content = true;
		} else if ((char32_t)c == delimiter) {
			if (r_cells) {
				r_cells->push_back(cell.is_empty() ? String() : String::utf8((const char *)cell.ptr(), (int)cell.size()));
			}
			cell_count++;
			cell.clear();
			has_content = true;
		} else if (c == '\n' || c == '\r') {
			if (c == '\n') {
				line++;
			} else if (_peek() == '\n') {
				_next_byte();
				line++;
			}
			if (has_content || !cell.is_empty()) {
				break;
			}
			record_line = line;
		} else {
			cell.push_back((uint8_t)c);
		}
	}
	if (in_quotes) {
		_fail(ERR_PARSE_ERROR, vformat("Line %d: the quoted cell is not closed.", quote_line), quote_line);
		return false;
	}
	if (!has_content && cell.is_empty()) {
		eof = true;
		row_total = row_index;
		return false;
	}
	if (r_cells) {
		r_cells->push_back(cell.is_empty() ? String() : String::utf8((const char *)cell.ptr(), (int)cell.size()));
	}
	cell_count++;
	if (!header.is_empty() && cell_count > header.size()) {
		// The row number counts the header row as row 1 (the check only runs with a header) and is the line
		// number unless a quoted cell contains a line break.
		_fail(ERR_PARSE_ERROR, vformat("Row %d: %d cells, but the header has %d columns.", row_index + 2, cell_count, header.size()), record_line);
		return false;
	}
	return true;
}

Error CSVReader::_skip(int p_count) {
	for (int i = 0; i < p_count; i++) {
		if (!_read_record(nullptr)) {
			return error != OK ? error : ERR_FILE_EOF;
		}
		row_index++;
	}
	return OK;
}

Ref<CSVReader> CSVReader::open(const String &p_path, const Ref<SerializerOptions> &p_options) {
	Error open_error = OK;
	Ref<FileAccess> opened = FileAccess::open(p_path, FileAccess::READ, &open_error);
	ERR_FAIL_COND_V_MSG(opened.is_null(), Ref<CSVReader>(), vformat("Cannot open the CSV file '%s': %s.", p_path, String(error_names[open_error])));
	const ResourceTextSerializer::Options options = ResourceTextSerializer::get_options(p_options);
	const char32_t csv_delimiter = ResourceTextSerializer::get_csv_delimiter(options);
	ERR_FAIL_COND_V_MSG(csv_delimiter > 0x7F, Ref<CSVReader>(), "CSVReader needs an ASCII delimiter.");

	Ref<CSVReader> reader;
	reader.instantiate();
	reader->file = opened;
	reader->path = p_path;
	reader->delimiter = csv_delimiter;
	// Skip the UTF-8 byte order mark.
	uint8_t bom[3] = { 0, 0, 0 };
	const bool has_bom = opened->get_buffer(bom, 3) == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF;
	reader->_jump(has_bom ? 3 : 0, 1);
	if (options.csv_headers) {
		PackedStringArray columns;
		if (reader->_read_record(&columns)) {
			reader->header = columns;
		}
		ERR_FAIL_COND_V_MSG(reader->error != OK, Ref<CSVReader>(), vformat("Cannot read the header of '%s': %s", p_path, reader->error_message));
	}
	reader->index_positions.push_back(reader->buffer_start + reader->buffer_position);
	reader->index_lines.push_back(reader->line);
	return reader;
}

void CSVReader::close() {
	file.unref();
	buffer.clear();
	buffer_length = 0;
	buffer_position = 0;
	eof = true;
}

PackedStringArray CSVReader::read_row() {
	ERR_FAIL_COND_V_MSG(file.is_null(), PackedStringArray(), "The CSVReader is closed.");
	PackedStringArray cells;
	if (!_read_record(&cells)) {
		return PackedStringArray();
	}
	row_index++;
	return cells;
}

Array CSVReader::read_rows(int p_count) {
	ERR_FAIL_COND_V(p_count < 0, Array());
	ERR_FAIL_COND_V_MSG(file.is_null(), Array(), "The CSVReader is closed.");
	Array rows;
	for (int i = 0; i < p_count; i++) {
		PackedStringArray cells;
		if (!_read_record(&cells)) {
			break;
		}
		row_index++;
		rows.push_back(cells);
	}
	return rows;
}

Dictionary CSVReader::read_record() {
	ERR_FAIL_COND_V_MSG(header.is_empty(), Dictionary(), "The CSV has no header: use read_row() instead.");
	Dictionary record;
	const PackedStringArray cells = read_row();
	if (cells.is_empty()) {
		return record;
	}
	for (int column = 0; column < header.size(); column++) {
		record[header[column]] = column < cells.size() ? cells[column] : String();
	}
	return record;
}

Error CSVReader::skip_rows(int p_count) {
	ERR_FAIL_COND_V(p_count < 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_UNCONFIGURED, "The CSVReader is closed.");
	return _skip(p_count);
}

Error CSVReader::seek_row(int p_row) {
	ERR_FAIL_COND_V(p_row < 0, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V_MSG(file.is_null(), ERR_UNCONFIGURED, "The CSVReader is closed.");
	const bool failed = error != OK;
	error = OK;
	error_message = String();
	error_line = 0;
	const int block = MIN(p_row / INDEX_STEP, (int)index_positions.size() - 1);
	const int block_row = block * INDEX_STEP;
	// Reading on is cheaper than a jump when the target is ahead of the current row in the same block.
	if (failed || row_index > p_row || row_index < block_row) {
		_jump(index_positions[block], index_lines[block]);
		row_index = block_row;
	}
	return _skip(p_row - row_index);
}

int CSVReader::count_rows() {
	if (row_total >= 0) {
		return row_total;
	}
	ERR_FAIL_COND_V_MSG(file.is_null(), -1, "The CSVReader is closed.");
	// The position and the error of the reader are the same after the call, whatever it finds.
	const int saved = row_index;
	const Error saved_error = error;
	const String saved_message = error_message;
	const int saved_line = error_line;
	const bool failed = seek_row(INT32_MAX) == ERR_PARSE_ERROR;
	const int total = failed ? -1 : row_total;
	seek_row(saved);
	error = saved_error;
	error_message = saved_message;
	error_line = saved_line;
	return total;
}

void CSVReader::_bind_methods() {
	ClassDB::bind_static_method("CSVReader", D_METHOD("open", "path", "options"), &CSVReader::open, DEFVAL(Ref<SerializerOptions>()));
	ClassDB::bind_method(D_METHOD("close"), &CSVReader::close);
	ClassDB::bind_method(D_METHOD("is_open"), &CSVReader::is_open);
	ClassDB::bind_method(D_METHOD("get_path"), &CSVReader::get_path);
	ClassDB::bind_method(D_METHOD("get_header"), &CSVReader::get_header);
	ClassDB::bind_method(D_METHOD("is_eof"), &CSVReader::is_eof);
	ClassDB::bind_method(D_METHOD("get_row_index"), &CSVReader::get_row_index);
	ClassDB::bind_method(D_METHOD("get_line"), &CSVReader::get_line);
	ClassDB::bind_method(D_METHOD("get_error"), &CSVReader::get_error);
	ClassDB::bind_method(D_METHOD("get_error_message"), &CSVReader::get_error_message);
	ClassDB::bind_method(D_METHOD("get_error_line"), &CSVReader::get_error_line);
	ClassDB::bind_method(D_METHOD("read_row"), &CSVReader::read_row);
	ClassDB::bind_method(D_METHOD("read_rows", "count"), &CSVReader::read_rows);
	ClassDB::bind_method(D_METHOD("read_record"), &CSVReader::read_record);
	ClassDB::bind_method(D_METHOD("skip_rows", "count"), &CSVReader::skip_rows);
	ClassDB::bind_method(D_METHOD("seek_row", "row"), &CSVReader::seek_row);
	ClassDB::bind_method(D_METHOD("count_rows"), &CSVReader::count_rows);
}
