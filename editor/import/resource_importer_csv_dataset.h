/**************************************************************************/
/*  resource_importer_csv_dataset.h                                       */
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

#include "core/io/resource_importer.h"

// Default importer of data CSV files: writes nothing and keeps the file, which the CSV loader reads as a CSV
// resource (also in exported projects, like Keep File). Unlike Keep File, the file has the type CSV in the
// FileSystem dock, so it can be selected, inspected and edited. Files whose header is a translation header
// (keys, then only locale, note and ?context/?plural columns) default to ResourceImporterCSVTranslation.
class ResourceImporterCSVDataset : public ResourceImporter {
	GDCLASS(ResourceImporterCSVDataset, ResourceImporter);

public:
	static constexpr const char *IMPORTER_NAME = "csv_dataset";

	// True if the first line of the file is a translation header (CSV::TranslationHeader::is_translation_table()).
	static bool is_translation_file(const String &p_path);

	virtual String get_importer_name() const override { return IMPORTER_NAME; }
	virtual String get_visible_name() const override { return "CSV Dataset"; }
	virtual void get_recognized_extensions(List<String> *p_extensions) const override;
	virtual String get_save_extension() const override { return String(); }
	virtual String get_resource_type() const override { return "CSV"; }
	virtual float get_priority_for_file(const String &p_path) const override;
	virtual void get_import_options(const String &p_path, List<ImportOption> *r_options, int p_preset = 0) const override {}
	virtual bool get_option_visibility(const String &p_path, const String &p_option, const HashMap<StringName, Variant> &p_options) const override { return true; }
	virtual Error import(ResourceUID::ID p_source_id, const String &p_source_file, const String &p_save_path, const HashMap<StringName, Variant> &p_options, List<String> *r_platform_variants, List<String> *r_gen_files = nullptr, Variant *r_metadata = nullptr) override { return OK; }
	virtual bool can_import_threaded() const override { return true; }
};
