/**************************************************************************/
/*  load_progress.h                                                       */
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

#include "core/object/ref_counted.h"

// Progress of loading a set of files: `ResourceDatabase.preload_from()` (`preload_progress`), a
// `FilesManifest` load (`ResourceLoader.load_threaded_get_manifest_status()`) and transition screens
// report it the same way. A snapshot: it does not change after it was created.
class LoadProgress : public RefCounted {
	GDCLASS(LoadProgress, RefCounted);

	int loaded_count = 0;
	int total_count = 0;
	float ratio = 1.0f;

protected:
	static void _bind_methods();

public:
	int get_loaded_count() const;
	int get_total_count() const;
	float get_ratio() const;
	bool is_finished() const;

	// `p_ratio` is 0 to 1 and may include the progress of files that are still loading.
	static Ref<LoadProgress> create(int p_loaded_count, int p_total_count, float p_ratio);
	// Every file counts the same: `loaded / total`, 1 when there is nothing to load.
	static Ref<LoadProgress> create_counted(int p_loaded_count, int p_total_count);
};
