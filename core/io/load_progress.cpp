/**************************************************************************/
/*  load_progress.cpp                                                     */
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

#include "load_progress.h"

#include "core/object/class_db.h"

int LoadProgress::get_loaded_count() const {
	return loaded_count;
}

int LoadProgress::get_total_count() const {
	return total_count;
}

float LoadProgress::get_ratio() const {
	return ratio;
}

bool LoadProgress::is_finished() const {
	return loaded_count >= total_count;
}

Ref<LoadProgress> LoadProgress::create(int p_loaded_count, int p_total_count, float p_ratio) {
	Ref<LoadProgress> progress;
	progress.instantiate();
	progress->loaded_count = p_loaded_count;
	progress->total_count = p_total_count;
	progress->ratio = CLAMP(p_ratio, 0.0f, 1.0f);
	return progress;
}

Ref<LoadProgress> LoadProgress::create_counted(int p_loaded_count, int p_total_count) {
	return create(p_loaded_count, p_total_count, p_total_count > 0 ? float(p_loaded_count) / float(p_total_count) : 1.0f);
}

void LoadProgress::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_loaded_count"), &LoadProgress::get_loaded_count);
	ClassDB::bind_method(D_METHOD("get_total_count"), &LoadProgress::get_total_count);
	ClassDB::bind_method(D_METHOD("get_ratio"), &LoadProgress::get_ratio);
	ClassDB::bind_method(D_METHOD("is_finished"), &LoadProgress::is_finished);
}
