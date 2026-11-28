/**************************************************************************/
/*  resource_importer_csv_dataset.cpp                                     */
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

#include "resource_importer_csv_dataset.h"

#include "core/io/csv_format.h"
#include "core/io/file_access.h"

bool ResourceImporterCSVDataset::is_translation_file(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null() || file->get_length() == 0) {
		return false;
	}
	CSV::TranslationHeader header;
	Ref<DataValidation> issues;
	issues.instantiate();
	CSV::check_translation_header(file->get_csv_line(), header, issues.ptr());
	return header.is_translation_table();
}

void ResourceImporterCSVDataset::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("csv");
}

float ResourceImporterCSVDataset::get_priority_for_file(const String &p_path) const {
	// Above CSV Translation (1.0) for data files, below it for translation files.
	return is_translation_file(p_path) ? 0.5 : 2.0;
}
