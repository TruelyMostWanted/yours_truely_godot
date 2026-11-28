/**************************************************************************/
/*  text_data_format.h                                                    */
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

#include "core/io/data_validation.h"
#include "core/io/resource.h"
#include "core/io/serializer_options.h"
#include "core/templates/list.h"
#include "core/variant/variant.h"

class DataSchema;

// Base of the data file classes INI, CSV, XML, TOML and YAML (and of formats added by modules or extensions).
// One instance holds the parsed `data` of one text file. Like JSON, the classes are Resources: load() of a
// plain file returns them, and the static parse_string()/stringify() of every format stay available.
// The text given to parse() can be kept (the editor does), so saving an unchanged file keeps comments and
// formatting; any change of `data` drops the kept text.
// Responsibility (#421, #434): the text document only — parse with kept text, errors with line/column, options,
// load/save, validate, extensions. No data model shared by the formats: each class types `data` and has its own
// editing API, inspector and dock. New virtuals here only for this lifecycle (truely/ARCHITECTURE.md 2.7).
class TextDataFormat : public Resource {
	GDCLASS(TextDataFormat, Resource);

	Variant format_data;
	Ref<SerializerOptions> serializer_options;
	String parsed_text;
	String parse_error_message;
	int parse_error_line = 0;
	int parse_error_column = 0;

protected:
	static void _bind_methods();

	// Format hooks. `p_options` may be null (project settings). On error, set r_error and the 1-based
	// r_line/r_column (0 = unknown).
	virtual Error _parse_data(const String &p_text, const Ref<SerializerOptions> &p_options, Variant &r_data, String &r_error, int &r_line, int &r_column);
	virtual Error _encode_data(const Variant &p_data, const Ref<SerializerOptions> &p_options, String &r_text, String &r_error) const;
	// Value of `data` after clear() or a failed parse().
	virtual Variant _get_empty_data() const { return Variant(); }
	// Type `data` must have (Variant::NIL = any).
	virtual Variant::Type _get_data_type() const { return Variant::NIL; }
	virtual void _get_extensions(List<String> *r_extensions) const {}
	// Options for files with the extension of `p_path` when `options` is not set (CSV: a tab for `.tsv`), or null.
	virtual Ref<SerializerOptions> _get_path_options(const String &p_path) const { return Ref<SerializerOptions>(); }
	// Called after `data` was replaced (parse(), set_data(), clear()), for caches of subclasses.
	virtual void _data_replaced() {}
	// Called by clear(), for state of subclasses beside `data`.
	virtual void _cleared() {}

	// Validates `data` against a schema (default: DataSchema.validate(data)).
	virtual Ref<DataValidation> _validate_data(const Ref<DataSchema> &p_schema, const Ref<DataValidationOptions> &p_options) const;
	// Sets the lines of the issues from the kept text (default: DataValidation.resolve_lines()).
	virtual void _resolve_issue_lines(const Ref<DataValidation> &p_validation, const String &p_text) const;

	// For subclasses that change `data` in place: drops the kept text and emits `changed`.
	void _mark_data_changed();
	// Direct access for subclasses; call _mark_data_changed() after a change.
	Variant &_get_data_ref() { return format_data; }

public:
	void set_data(const Variant &p_data);
	Variant get_data() const { return format_data; }

	void set_options(const Ref<SerializerOptions> &p_options);
	Ref<SerializerOptions> get_options() const { return serializer_options; }
	// Without `options`, sets the options that the extension of `p_path` needs (a tab for `.tsv`). Called by
	// load(), save() and the resource loader and saver before reading or writing a file.
	void apply_path_options(const String &p_path);

	// Parses `p_text` into `data`. With `p_keep_text`, the text is kept for get_parsed_text() (also on error).
	Error parse(const String &p_text, bool p_keep_text = false);
	// `data` as text of this format, using `options`. Prints an error and returns an empty String if `data` does not fit the format.
	String encode_to_text() const;
	// Text for saving: the kept text if `data` was not changed since parse(), otherwise the encoded `data`.
	Error get_save_text(String &r_text, String &r_error) const;
	void clear();

	// Validates `data` against `p_schema`. After a failed parse(), the result is the parse error. With
	// DataValidationOptions.keep_positions and the text kept by parse(), issues get their line.
	Ref<DataValidation> validate(const Ref<DataSchema> &p_schema, const Ref<DataValidationOptions> &p_options = Ref<DataValidationOptions>()) const;

	// Reads and parses a file (like ConfigFile.load(); no resource cache, resource_path unchanged).
	Error load(const String &p_path);
	// Writes the text of get_save_text() to a file (like ConfigFile.save()).
	Error save(const String &p_path);

	String get_parsed_text() const { return parsed_text; }
	String get_error_message() const { return parse_error_message; }
	int get_error_line() const { return parse_error_line; }
	int get_error_column() const { return parse_error_column; }

	void get_recognized_extensions_list(List<String> *r_extensions) const { _get_extensions(r_extensions); }
	PackedStringArray get_recognized_extensions() const;
	// True if `p_path` has one of the extensions of this format.
	bool is_recognized_path(const String &p_path) const;
};
