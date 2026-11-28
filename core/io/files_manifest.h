/**************************************************************************/
/*  files_manifest.h                                                      */
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

#include "core/io/load_progress.h"
#include "core/io/resource.h"
#include "core/io/resource_loader_constants.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"

// Describes a set of files: folders (optionally recursive) filtered by extensions and type,
// plus single files. `ResourceLoader.load_manifest()` loads all of them in one call.
// All properties are stored (`PROPERTY_USAGE_STORAGE`), so the manifest can be saved in any
// resource format. Servers that build on manifests use the C++ API below directly.
class FilesManifest : public Resource {
	GDCLASS(FilesManifest, Resource);

	StringName type_hint;
	PackedStringArray extensions;
	PackedStringArray folder_paths;
	PackedStringArray file_paths;
	bool recursive = true;
	bool create_missing_folders = false;

	// Paths resolved for the threaded load (one folder scan per request, not per status call).
	mutable PackedStringArray load_paths;
	mutable bool load_paths_cached = false;
	mutable bool load_active = false; // Between a request and the result.
#ifndef THREADS_ENABLED
	// Without threads the status calls load one file each; their results wait here for `load_threaded_get_resources()`.
	mutable HashMap<String, Ref<Resource>> loaded_without_threads;
	mutable int load_step = 0;
	mutable bool load_use_sub_threads = false;
	mutable ResourceLoaderConstants::CacheMode load_cache_mode = ResourceLoaderConstants::CACHE_MODE_REUSE;
#endif

	static void _split_folder_path(const String &p_folder_path, String &r_base, Vector<String> &r_pattern);
	void _collect_folder(const String &p_folder, int p_depth, PackedStringArray &r_paths) const;
	bool _type_matches(const String &p_path) const;
	String _get_load_type_hint() const;
	const PackedStringArray &_get_load_paths(Error *r_error = nullptr) const;

protected:
	static void _bind_methods();

public:
	// Maximum folder depth below a manifest folder, against link loops.
	static constexpr int MAX_FOLDER_DEPTH = 64;

	// `png`, `*.PNG` and `.png` all become `.png`. Returns an empty string for an empty extension.
	static String normalize_extension(const String &p_extension);
	// Canonical form of a manifest path: prefixed paths and relative paths (to `res://`) become
	// canonical prefixed paths, OS paths below a prefix get their prefixed form, other OS paths
	// stay OS paths. Returns an empty string and sets `r_error` if the path cannot be resolved.
	static String normalize_path(const String &p_path, bool *r_read_only = nullptr, Error *r_error = nullptr);
	// The part of a folder entry before its first segment with a wildcard (`*` or `?`), or the whole entry.
	static String get_folder_base(const String &p_folder_path);
	// Starts one threaded load per path. Returns `OK`, or the first error (all paths are tried).
	static Error load_threaded_request_paths(const PackedStringArray &p_paths, const String &p_type_hint, bool p_use_sub_threads, ResourceLoaderConstants::CacheMode p_cache_mode);

	void set_type_hint(const StringName &p_type_hint);
	StringName get_type_hint() const;

	void set_extensions(const PackedStringArray &p_extensions);
	PackedStringArray get_extensions() const;

	void set_folder_paths(const PackedStringArray &p_folder_paths);
	PackedStringArray get_folder_paths() const;

	void set_file_paths(const PackedStringArray &p_file_paths);
	PackedStringArray get_file_paths() const;

	void set_recursive(bool p_recursive);
	bool is_recursive() const;

	void set_create_missing_folders(bool p_create);
	bool is_create_missing_folders() const;

	// True if `p_path` ends with one of the extensions (case-insensitive), or if there are no extensions.
	bool matches_extension(const String &p_path) const;

	// All paths of the manifest in load order: the files of each folder entry (in the order of
	// `folder_paths`; wildcard matches sorted by name; the files of one folder sorted by path), then
	// the single files (in the order of `file_paths`). A path that appears again keeps its first
	// position. Missing folders are created if `create_missing_folders` is set and the folder is
	// writable. `r_error` receives the first error; everything else is still collected.
	// With `r_entry_counts` (editor preview), nothing is created or reported as missing, and it
	// receives per entry (folders, then files) the number of files the entry resolves to.
	PackedStringArray get_paths(Error *r_error = nullptr, LocalVector<int> *r_entry_counts = nullptr) const;
	// Loads all paths. Returns path -> Resource; paths that fail to load are left out.
	Dictionary load_resources(ResourceLoaderConstants::CacheMode p_cache_mode = ResourceLoaderConstants::CACHE_MODE_REUSE, Error *r_error = nullptr) const;
	// Starts a threaded load for every path, see `ResourceLoader::load_threaded_request()`.
	Error load_threaded_request_resources(bool p_use_sub_threads = false, ResourceLoaderConstants::CacheMode p_cache_mode = ResourceLoaderConstants::CACHE_MODE_REUSE) const;
	// Progress of the threaded load: files loaded, total, and the ratio including the files still loading. A pure
	// query with threads. Without threads (Web) a requested file loads in `load_threaded_get_resources()`, so each
	// call loads one more requested file: calling it every frame advances the load step by step.
	Ref<LoadProgress> load_threaded_get_status_resources() const;
	// Returns path -> Resource of the requested files (waits for those still loading); paths that fail are left out.
	Dictionary load_threaded_get_resources() const;
};
