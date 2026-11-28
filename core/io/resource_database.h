/**************************************************************************/
/*  resource_database.h                                                   */
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
#include "core/io/preloads.h"
#include "core/io/resource.h"
#include "core/object/object.h"
#include "core/os/mutex.h"
#include "core/os/rw_lock.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"
#include "core/templates/pair.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/typed_array.h"

// Keeps resources in memory with strong references and finds them again by path, key, type
// or pattern. Loaded resources are stored under their resource path, so they also stay in
// `ResourceCache` and `ResourceLoader.load()` of that path returns the stored instance.
// Thread-safe: every method can be called from any thread. Signals are emitted on the main thread.
class ResourceDatabase : public Object {
	GDCLASS(ResourceDatabase, Object);

	static ResourceDatabase *singleton;

	struct Entry {
		Ref<Resource> resource;
		// Other keys that lead to this entry (keys of the preloads, requested paths that differ from the resource path).
		LocalVector<String> aliases;
	};

	// One preload of a Preloads resource, synchronous or on a worker thread.
	struct Job {
		uint64_t id = 0;
		Ref<Preloads> preloads;
		// Requested path and type hint, in load order.
		LocalVector<Pair<String, String>> paths;
		// Key -> path, applied after all paths are loaded.
		LocalVector<Pair<String, String>> keys;
		Error error = OK;
		// Paths loaded so far (written by the worker thread, read on the main thread).
		SafeNumeric<uint32_t> loaded_count;
		// A plain thread, not a WorkerThreadPool task: `ResourceLoader::load()` inside a pool task
		// spawns its own sub-task and keeps its load task registered (resources leak at exit).
		Thread *thread = nullptr;
	};

	mutable RWLock lock;
	HashMap<String, Entry> entries;
	HashMap<String, String> aliases; // Alias -> primary key.

	mutable Mutex jobs_mutex;
	HashMap<uint64_t, Job *> jobs;
	uint64_t next_job_id = 1;
	bool finished = false;

	static String _normalize_key(const String &p_key);
	static bool _matches_pattern(const String &p_key, const Vector<String> &p_parts);
	static bool _matches_type(const Ref<Resource> &p_resource, const StringName &p_type);
	static Vector<String> _split_pattern(const String &p_pattern);
	static uint64_t _estimate_variant_size(const Variant &p_value, HashSet<ObjectID> &r_visited, int p_depth);
	static void _job_task(void *p_userdata);

	// Callers hold the lock.
	// Primary key for a key or alias, empty if unknown.
	String _resolve(const String &p_key) const;
	bool _entry_matches(const String &p_key, const Entry &p_entry, const Vector<String> &p_parts, const StringName &p_type) const;
	void _add_alias(const String &p_alias, const String &p_primary);
	// Moves the resource of the entry into `r_released`, so that it is freed after the lock is dropped.
	void _erase_entry(const String &p_primary, LocalVector<Ref<Resource>> &r_released);
	void _store(const String &p_key, const Ref<Resource> &p_resource, LocalVector<Ref<Resource>> &r_released);

	// Loads one path and stores it under its resource path; `p_requested` becomes an alias if it differs.
	Error _load_and_store(const String &p_requested, const String &p_type_hint);
	void _apply_keys(const LocalVector<Pair<String, String>> &p_keys);
	Error _collect(const Ref<Preloads> &p_preloads, Job *r_job);
	Error _store_direct_resources(const Ref<Preloads> &p_preloads);
	void _run_job(Job *p_job);
	void _job_done(uint64_t p_job_id);
	void _emit_preload_finished(const Ref<Preloads> &p_preloads, int p_error);
	void _emit_preload_progress(const Ref<Preloads> &p_preloads, int p_loaded, int p_total);

protected:
	static void _bind_methods();

public:
	static ResourceDatabase *get_singleton();

	// A preload split into steps of one file, for loading screens without threads (the default Web export).
	// `begin_preload_steps()` collects the files once (type hints included), `step_preload()` loads the next one.
	struct PreloadSteps {
		Ref<Preloads> preloads;
		LocalVector<Pair<String, String>> paths; // Requested path and type hint.
		LocalVector<Pair<String, String>> keys;
		uint32_t index = 0; // Files loaded so far.
		Error error = OK; // The first error.
	};
	// Stores the direct resources of the preloads and collects its files.
	void begin_preload_steps(const Ref<Preloads> &p_preloads, PreloadSteps &r_steps);
	// Loads the next file. After the last file the keys are applied and `preload_finished` is emitted
	// (deferred, like `preload_from()`). Returns true while files remain.
	bool step_preload(PreloadSteps &r_steps);

	// Loads everything the preloads name. With `p_use_threads` (and threads available), the paths load
	// on a worker thread and the call returns at once; otherwise it returns when all paths are loaded.
	// `preload_progress` follows after each loaded path and `preload_finished` at the end, on the main thread in both cases.
	Error preload_from(const Ref<Preloads> &p_preloads, bool p_use_threads = true);
	// Loads one path now and stores it; `p_key` becomes an extra key for it.
	Error preload_resource(const String &p_path, const String &p_key = String());

	Error add_resource(const String &p_key, const Ref<Resource> &p_resource);
	bool add_resource_if_missing(const String &p_key, const Ref<Resource> &p_resource);
	int remove_matching(const String &p_pattern);
	void clear();

	Ref<Resource> get_resource(const String &p_key_or_path) const;
	bool has_resource(const String &p_key_or_path) const;
	int get_entry_count() const;
	TypedArray<Resource> find_resources(const String &p_pattern = String(), const StringName &p_type = StringName()) const;
	PackedStringArray find_keys(const String &p_pattern = String(), const StringName &p_type = StringName()) const;

	// Estimated CPU-side bytes of the stored resources (stored properties, each resource counted once).
	int64_t get_memory_usage() const;
	bool is_preloading() const;
	// Files loaded by the running preloads (every file counts the same); complete when nothing is preloading.
	Ref<LoadProgress> get_preload_progress() const;

	// Preloads the resources named in `filesystem/resources/preloads` synchronously. Called once at startup
	// of a game (not in the editor).
	void preload_project_preloads();
	// Waits for running preloads and drops every entry. Called before scenes, scripts and servers
	// shut down, so that stored resources are freed while their servers still exist.
	void finish();

	ResourceDatabase();
	~ResourceDatabase();
};
