/**************************************************************************/
/*  test_csv_reader.cpp                                                   */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_csv_reader)

#include "core/io/csv_format.h"
#include "core/io/csv_reader.h"
#include "core/io/file_access.h"
#include "core/io/serializer_options.h"
#include "tests/test_utils.h"

namespace TestCSVReader {

static String write_csv(const String &p_name, const String &p_text) {
	const String path = TestUtils::get_temp_path(p_name);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(p_text);
	return path;
}

TEST_CASE("[CSVReader] Rows, quotes, header and byte order mark") {
	const String text = String::utf8("\xEF\xBB\xBFname,note\r\nAnn,\"a, b\"\r\n\r\n\"Bob\",\"line1\nline2 \"\"q\"\"\"\n\xC3\x84rger,\nlast,x");
	const String path = write_csv("csv_reader_basic.csv", text);
	Ref<CSVReader> reader = CSVReader::open(path);
	REQUIRE(reader.is_valid());
	CHECK(reader->is_open());
	CHECK(reader->get_path() == path);
	CHECK(reader->get_header() == PackedStringArray({ "name", "note" }));
	CHECK(reader->get_row_index() == 0);
	CHECK(reader->read_row() == PackedStringArray({ "Ann", "a, b" }));
	CHECK(reader->read_row() == PackedStringArray({ "Bob", "line1\nline2 \"q\"" }));
	CHECK(reader->get_row_index() == 2);
	const Dictionary record = reader->read_record();
	CHECK(String(record["name"]) == String::utf8("\xC3\x84rger"));
	CHECK(String(record["note"]) == "");
	CHECK(reader->read_row() == PackedStringArray({ "last", "x" }));
	CHECK_FALSE(reader->is_eof());
	CHECK(reader->read_row().is_empty());
	CHECK(reader->is_eof());
	CHECK(reader->get_error() == OK);
	CHECK(reader->count_rows() == 4);
	CHECK(reader->seek_row(1) == OK);
	CHECK_FALSE(reader->is_eof());
	CHECK(reader->read_row() == PackedStringArray({ "Bob", "line1\nline2 \"q\"" }));
	reader->close();
	CHECK_FALSE(reader->is_open());
	ERR_PRINT_OFF;
	CHECK(reader->read_row().is_empty());
	CHECK(reader->seek_row(0) == ERR_UNCONFIGURED);
	ERR_PRINT_ON;
}

TEST_CASE("[CSVReader] Options: no header, other delimiter") {
	const String path = write_csv("csv_reader_options.csv", "a;b\nc;d\n");
	Ref<SerializerOptions> options;
	options.instantiate();
	options->set_csv_delimiter(";");
	options->set_csv_headers(false);
	Ref<CSVReader> reader = CSVReader::open(path, options);
	REQUIRE(reader.is_valid());
	CHECK(reader->get_header().is_empty());
	CHECK(reader->read_rows(10).size() == 2);
	CHECK(reader->read_row().is_empty());
	CHECK(reader->seek_row(1) == OK);
	CHECK(reader->read_row() == PackedStringArray({ "c", "d" }));
	CHECK(reader->seek_row(5) == ERR_FILE_EOF);
	CHECK(reader->is_eof());
	CHECK(reader->get_row_index() == 2);
}

TEST_CASE("[CSVReader] Large file matches CSV, seek and count across buffers and index blocks") {
	const int row_count = 3500;
	String text = "id,payload\n";
	for (int row = 0; row < row_count; row++) {
		text += itos(row) + ",\"" + String("x").repeat(40) + (row % 7 == 0 ? "\n" : "") + itos(row) + "\"\n";
	}
	const String path = write_csv("csv_reader_large.csv", text);
	Ref<CSV> csv;
	csv.instantiate();
	REQUIRE(csv->parse(text) == OK);
	REQUIRE(csv->get_row_count() == row_count);

	Ref<CSVReader> reader = CSVReader::open(path);
	REQUIRE(reader.is_valid());
	for (int row = 0; row < row_count; row++) {
		CHECK(reader->get_row_index() == row);
		CHECK(reader->read_row() == csv->get_row(row));
	}
	CHECK(reader->read_row().is_empty());
	CHECK(reader->count_rows() == row_count);

	const int targets[] = { 0, 1, 1023, 1024, 1025, 2048, 3499, 700, 3000, 5 };
	for (const int target : targets) {
		CHECK(reader->seek_row(target) == OK);
		CHECK(reader->read_row() == csv->get_row(target));
	}
	// The row after the last one is a valid position (at the end); only rows beyond it are an error.
	CHECK(reader->seek_row(row_count) == OK);
	CHECK(reader->read_row().is_empty());
	CHECK(reader->seek_row(row_count + 1) == ERR_FILE_EOF);

	// A fresh reader jumps far ahead and counts without reading in order first.
	Ref<CSVReader> fresh = CSVReader::open(path);
	REQUIRE(fresh.is_valid());
	CHECK(fresh->seek_row(3200) == OK);
	CHECK(fresh->read_row() == csv->get_row(3200));
	Ref<CSVReader> counter = CSVReader::open(path);
	REQUIRE(counter.is_valid());
	CHECK(counter->read_row() == csv->get_row(0));
	CHECK(counter->count_rows() == row_count);
	CHECK(counter->get_row_index() == 1);
	CHECK(counter->read_row() == csv->get_row(1));
	CHECK(counter->skip_rows(10) == OK);
	CHECK(counter->read_row() == csv->get_row(12));
}

TEST_CASE("[CSVReader] Malformed quotes parse like CSV") {
	const String text = "h1,h2,h3\na\"b,\"x\"y,z\n\"\"\"q\"\"\",,\n";
	const String path = write_csv("csv_reader_quotes.csv", text);
	Ref<CSV> csv;
	csv.instantiate();
	REQUIRE(csv->parse(text) == OK);
	Ref<CSVReader> reader = CSVReader::open(path);
	REQUIRE(reader.is_valid());
	for (int row = 0; row < csv->get_row_count(); row++) {
		CHECK(reader->read_row() == csv->get_row(row));
	}
	CHECK(reader->read_row().is_empty());
}

TEST_CASE("[CSVReader] Errors") {
	ERR_PRINT_OFF;
	CHECK(CSVReader::open(TestUtils::get_temp_path("csv_reader_missing.csv")).is_null());
	ERR_PRINT_ON;

	const String path = write_csv("csv_reader_error.csv", "a,b\n1,2\n3,4,5\n6,7\n");
	Ref<CSVReader> reader = CSVReader::open(path);
	REQUIRE(reader.is_valid());
	CHECK(reader->read_row() == PackedStringArray({ "1", "2" }));
	CHECK(reader->read_row().is_empty());
	CHECK(reader->get_error() == ERR_PARSE_ERROR);
	CHECK(reader->get_error_line() == 3);
	CHECK(reader->get_error_message().contains("3 cells"));
	CHECK(reader->count_rows() == -1);
	// count_rows() leaves the error and the position as they were.
	CHECK(reader->get_error() == ERR_PARSE_ERROR);
	CHECK(reader->get_row_index() == 1);
	CHECK(reader->seek_row(0) == OK);
	CHECK(reader->get_error() == OK);
	CHECK(reader->seek_row(2) == ERR_PARSE_ERROR);

	const String open_quote = write_csv("csv_reader_quote.csv", "a,b\n1,\"2\n3,4\n");
	Ref<CSVReader> quoted = CSVReader::open(open_quote);
	REQUIRE(quoted.is_valid());
	CHECK(quoted->read_row().is_empty());
	CHECK(quoted->get_error() == ERR_PARSE_ERROR);
	CHECK(quoted->get_error_line() == 2);
	CHECK(quoted->get_error_message().contains("not closed"));

	// An invalid header row: open() fails with the cause.
	const String bad_header = write_csv("csv_reader_bad_header.csv", "a,\"b\n1,2\n");
	ERR_PRINT_OFF;
	CHECK(CSVReader::open(bad_header).is_null());
	ERR_PRINT_ON;
}

} // namespace TestCSVReader
