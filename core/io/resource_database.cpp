/**************************************************************************/
/*  resource_database.cpp                                                 */
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

#include "resource_database.h"

#include "core/config/project_settings.h"
#include "core/io/files_manifest.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/os/os.h"

ResourceDatabase *ResourceDatabase::singleton = nullptr;

ResourceDatabase *ResourceDatabase::get_singleton() {
	return singleton;
}

String ResourceDatabase::_normalize_key(const String &p_key) {
	String key = p_key;
	if (key.begins_with("uid://") && ResourceUID::get_singleton()) {
		const ResourceUID::ID id = ResourceUID::get_singleton()->text_to_id(key);
		if (ResourceUID::get_singleton()->has_id(id)) {
			key = ResourceUID::get_singleton()->get_id_path(id);
		}
	}
	// Paths get the form `ResourceCache` uses; other keys stay as they are.
	if (key.contains("://") || key.is_absolute_path()) {
		if (ProjectSettings::get_singleton()) {
			key = ProjectSettings::get_singleton()->localize_path(key);
		}
		if (!key.contains("::")) {
			key = key.simplify_path();
		}
	}
	return key;
}

Vector<String> ResourceDatabase::_split_pattern(const String &p_pattern) {
	if (p_pattern.is_empty()) {
		return Vector<String>();
	}
	return p_pattern.to_lower().split("%");
}

bool ResourceDatabase::_matches_pattern(const String &p_key, const Vector<String> &p_parts) {
	if (p_parts.is_empty()) {
		return true;
	}
	const String key = p_key.to_lower();
	if (p_parts.size() == 1) {
		return key == p_parts[0];
	}
	// `%` matches any run of characters: the first part is a prefix, the last a suffix and the
	// parts between must follow each other.
	const String &first = p_parts[0];
	const String &last = p_parts[p_parts.size() - 1];
	if (!key.begins_with(first)) {
		return false;
	}
	int pos = first.length();
	for (int i = 1; i < p_parts.size() - 1; i++) {
		const String &part = p_parts[i];
		if (part.is_empty()) {
			continue;
		}
		const int found = key.find(part, pos);
		if (found < 0) {
			return false;
		}
		pos = found + part.length();
	}
	return key.length() - last.length() >= pos && key.ends_with(last);
}

bool ResourceDatabase::_matches_type(const Ref<Resource> &p_resource, const StringName &p_type) {
	if (p_type == StringName()) {
		return true;
	}
	if (p_resource->is_class(p_type)) {
		return true;
	}
	// Script classes (`class_name`), base scripts included.
	Ref<Script> script = p_resource->get_script();
	while (script.is_valid()) {
		if (script->get_global_name() == p_type) {
			return true;
		}
		script = script->get_base_script();
	}
	return false;
}

uint64_t ResourceDatabase::_estimate_variant_size(const Variant &p_value, HashSet<ObjectID> &r_visited, int p_depth) {
	if (p_depth > 32) {
		return 0;
	}
	switch (p_value.get_type()) {
		case Variant::STRING:
			return sizeof(Variant) + String(p_value).length() * sizeof(char32_t);
		case Variant::PACKED_BYTE_ARRAY:
			return sizeof(Variant) + PackedByteArray(p_value).size();
		case Variant::PACKED_INT32_ARRAY:
			return sizeof(Variant) + PackedInt32Array(p_value).size() * sizeof(int32_t);
		case Variant::PACKED_INT64_ARRAY:
			return sizeof(Variant) + PackedInt64Array(p_value).size() * sizeof(int64_t);
		case Variant::PACKED_FLOAT32_ARRAY:
			return sizeof(Variant) + PackedFloat32Array(p_value).size() * sizeof(float);
		case Variant::PACKED_FLOAT64_ARRAY:
			return sizeof(Variant) + PackedFloat64Array(p_value).size() * sizeof(double);
		case Variant::PACKED_VECTOR2_ARRAY:
			return sizeof(Variant) + PackedVector2Array(p_value).size() * sizeof(Vector2);
		case Variant::PACKED_VECTOR3_ARRAY:
			return sizeof(Variant) + PackedVector3Array(p_value).size() * sizeof(Vector3);
		case Variant::PACKED_VECTOR4_ARRAY:
			return sizeof(Variant) + PackedVector4Array(p_value).size() * sizeof(Vector4);
		case Variant::PACKED_COLOR_ARRAY:
			return sizeof(Variant) + PackedColorArray(p_value).size() * sizeof(Color);
		case Variant::PACKED_STRING_ARRAY: {
			uint64_t size = sizeof(Variant);
			const PackedStringArray strings = p_value;
			for (const String &string : strings) {
				size += sizeof(String) + string.length() * sizeof(char32_t);
			}
			return size;
		}
		case Variant::ARRAY: {
			uint64_t size = sizeof(Variant);
			const Array array = p_value;
			for (int i = 0; i < array.size(); i++) {
				size += _estimate_variant_size(array[i], r_visited, p_depth + 1);
			}
			return size;
		}
		case Variant::DICTIONARY: {
			uint64_t size = sizeof(Variant);
			const Dictionary dictionary = p_value;
			for (const KeyValue<Variant, Variant> &kv : dictionary) {
				size += _estimate_variant_size(kv.key, r_visited, p_depth + 1);
				size += _estimate_variant_size(kv.value, r_visited, p_depth + 1);
			}
			return size;
		}
		case Variant::OBJECT: {
			const Resource *resource = Object::cast_to<Resource>(p_value.get_validated_object());
			if (!resource) {
				return sizeof(Variant);
			}
			if (r_visited.has(resource->get_instance_id())) {
				return sizeof(Variant);
			}
			r_visited.insert(resource->get_instance_id());
			uint64_t size = sizeof(Variant) + sizeof(Resource);
			List<PropertyInfo> properties;
			resource->get_property_list(&properties);
			for (const PropertyInfo &property : properties) {
				if (property.usage & PROPERTY_USAGE_STORAGE) {
					size += _estimate_variant_size(resource->get(property.name), r_visited, p_depth + 1);
				}
			}
			return size;
		}
		default:
			return sizeof(Variant);
	}
}

String ResourceDatabase::_resolve(const String &p_key) const {
	if (entries.has(p_key)) {
		return p_key;
	}
	const String *primary = aliases.getptr(p_key);
	return primary ? *primary : String();
}

bool ResourceDatabase::_entry_matches(const String &p_key, const Entry &p_entry, const Vector<String> &p_parts, const StringName &p_type) const {
	if (!_matches_type(p_entry.resource, p_type)) {
		return false;
	}
	if (_matches_pattern(p_key, p_parts)) {
		return true;
	}
	for (const String &alias : p_entry.aliases) {
		if (_matches_pattern(alias, p_parts)) {
			return true;
		}
	}
	return false;
}

void ResourceDatabase::_add_alias(const String &p_alias, const String &p_primary) {
	if (p_alias.is_empty() || p_alias == p_primary || entries.has(p_alias)) {
		// Primary keys win over aliases.
		return;
	}
	Entry *entry = entries.getptr(p_primary);
	if (!entry) {
		return;
	}
	if (const String *old_primary = aliases.getptr(p_alias)) {
		if (*old_primary == p_primary) {
			return;
		}
		if (Entry *old_entry = entries.getptr(*old_primary)) {
			old_entry->aliases.erase(p_alias);
		}
	}
	aliases[p_alias] = p_primary;
	entry->aliases.push_back(p_alias);
}

void ResourceDatabase::_erase_entry(const String &p_primary, LocalVector<Ref<Resource>> &r_released) {
	Entry *entry = entries.getptr(p_primary);
	if (!entry) {
		return;
	}
	for (const String &alias : entry->aliases) {
		aliases.erase(alias);
	}
	r_released.push_back(entry->resource);
	entries.erase(p_primary);
}

void ResourceDatabase::_store(const String &p_key, const Ref<Resource> &p_resource, LocalVector<Ref<Resource>> &r_released) {
	if (Entry *entry = entries.getptr(p_key)) {
		r_released.push_back(entry->resource);
		entry->resource = p_resource;
		return;
	}
	// A new primary key replaces an alias of the same name.
	if (const String *old_primary = aliases.getptr(p_key)) {
		if (Entry *old_entry = entries.getptr(*old_primary)) {
			old_entry->aliases.erase(p_key);
		}
		aliases.erase(p_key);
	}
	Entry entry;
	entry.resource = p_resource;
	entries.insert(p_key, entry);
}

Error ResourceDatabase::_load_and_store(const String &p_requested, const String &p_type_hint) {
	const String path = _normalize_key(p_requested);
	Error err = OK;
	const Ref<Resource> resource = ResourceLoader::load(path, p_type_hint, ResourceFormatLoader::CACHE_MODE_REUSE, &err);
	if (resource.is_null()) {
		// `ResourceLoader` already reported the failure.
		return err != OK ? err : ERR_CANT_OPEN;
	}
	const String primary = resource->get_path().is_empty() ? path : _normalize_key(resource->get_path());

	LocalVector<Ref<Resource>> released;
	{
		RWLockWrite write_lock(lock);
		_store(primary, resource, released);
		_add_alias(path, primary);
	}
	return OK;
}

void ResourceDatabase::_apply_keys(const LocalVector<Pair<String, String>> &p_keys) {
	LocalVector<Pair<String, String>> missing;
	{
		RWLockWrite write_lock(lock);
		for (const Pair<String, String> &key : p_keys) {
			const String primary = _resolve(_normalize_key(key.second));
			if (primary.is_empty()) {
				missing.push_back(key);
				continue;
			}
			_add_alias(_normalize_key(key.first), primary);
		}
	}
	for (const Pair<String, String> &key : missing) {
		WARN_PRINT(vformat("ResourceDatabase: the key \"%s\" names \"%s\", which is not stored.", key.first, key.second));
	}
}

Error ResourceDatabase::_collect(const Ref<Preloads> &p_preloads, Job *r_job) {
	Error first_error = OK;
	const PackedStringArray paths = p_preloads->get_paths();
	for (const String &path : paths) {
		if (!path.is_empty()) {
			r_job->paths.push_back(Pair<String, String>(path, String()));
		}
	}

	const TypedArray<FilesManifest> manifests = p_preloads->get_manifests();
	for (int i = 0; i < manifests.size(); i++) {
		const Ref<FilesManifest> manifest = manifests[i];
		if (manifest.is_null()) {
			continue;
		}
		// Script class names are not known to the loaders: load those without a hint.
		const StringName type_hint = manifest->get_type_hint();
		const String hint = (type_hint != StringName() && ClassDB::class_exists(type_hint)) ? String(type_hint) : String();
		Error manifest_error = OK;
		const PackedStringArray manifest_paths = manifest->get_paths(&manifest_error);
		for (const String &path : manifest_paths) {
			r_job->paths.push_back(Pair<String, String>(path, hint));
		}
		if (manifest_error != OK && first_error == OK) {
			first_error = manifest_error;
		}
	}

	const Dictionary keys = p_preloads->get_keys();
	for (const KeyValue<Variant, Variant> &kv : keys) {
		r_job->keys.push_back(Pair<String, String>(kv.key, kv.value));
	}
	return first_error;
}

void ResourceDatabase::_run_job(Job *p_job) {
	const uint32_t total = p_job->paths.size();
	for (const Pair<String, String> &path : p_job->paths) {
		const Error err = _load_and_store(path.first, path.second);
		if (err != OK && p_job->error == OK) {
			p_job->error = err;
		}
		const uint32_t loaded = p_job->loaded_count.increment();
		callable_mp(this, &ResourceDatabase::_emit_preload_progress).call_deferred(p_job->preloads, int(loaded), int(total));
	}
	_apply_keys(p_job->keys);
}

void ResourceDatabase::_job_task(void *p_userdata) {
	Job *job = static_cast<Job *>(p_userdata);
	singleton->_run_job(job);
	callable_mp(singleton, &ResourceDatabase::_job_done).call_deferred(job->id);
}

void ResourceDatabase::_job_done(uint64_t p_job_id) {
	Job *job = nullptr;
	{
		MutexLock jobs_lock(jobs_mutex);
		Job **found = jobs.getptr(p_job_id);
		if (!found) {
			// Already collected by `finish()`.
			return;
		}
		job = *found;
		jobs.erase(p_job_id);
	}
	if (job->thread) {
		job->thread->wait_to_finish();
		memdelete(job->thread);
	}
	_emit_preload_finished(job->preloads, job->error);
	memdelete(job);
}

void ResourceDatabase::_emit_preload_finished(const Ref<Preloads> &p_preloads, int p_error) {
	emit_signal(SNAME("preload_finished"), p_preloads, p_error);
}

void ResourceDatabase::_emit_preload_progress(const Ref<Preloads> &p_preloads, int p_loaded, int p_total) {
	emit_signal(SNAME("preload_progress"), p_preloads, LoadProgress::create_counted(p_loaded, p_total));
}

Ref<LoadProgress> ResourceDatabase::get_preload_progress() const {
	MutexLock jobs_lock(jobs_mutex);
	uint64_t loaded = 0;
	uint64_t total = 0;
	for (const KeyValue<uint64_t, Job *> &job : jobs) {
		loaded += job.value->loaded_count.get();
		total += job.value->paths.size();
	}
	return LoadProgress::create_counted(int(loaded), int(total));
}

// Direct references: stored now, under their path or their name.
Error ResourceDatabase::_store_direct_resources(const Ref<Preloads> &p_preloads) {
	Error first_error = OK;
	const TypedArray<Resource> resources = p_preloads->get_resources();
	for (int i = 0; i < resources.size(); i++) {
		const Ref<Resource> resource = resources[i];
		if (resource.is_null()) {
			continue;
		}
		const String key = resource->get_path().is_empty() ? resource->get_name() : resource->get_path();
		if (key.is_empty()) {
			ERR_PRINT(vformat("ResourceDatabase: resource %d of the preloads has neither a path nor a name and is skipped.", i));
			if (first_error == OK) {
				first_error = ERR_INVALID_DATA;
			}
			continue;
		}
		add_resource(key, resource);
	}
	return first_error;
}

void ResourceDatabase::begin_preload_steps(const Ref<Preloads> &p_preloads, PreloadSteps &r_steps) {
	ERR_FAIL_COND_MSG(p_preloads.is_null(), "The preloads are null.");
	r_steps.preloads = p_preloads;
	r_steps.error = _store_direct_resources(p_preloads);
	Job job;
	const Error collect_error = _collect(p_preloads, &job);
	if (r_steps.error == OK) {
		r_steps.error = collect_error;
	}
	r_steps.paths = job.paths;
	r_steps.keys = job.keys;
}

bool ResourceDatabase::step_preload(PreloadSteps &r_steps) {
	if (r_steps.index < r_steps.paths.size()) {
		const Pair<String, String> &path = r_steps.paths[r_steps.index];
		const Error err = _load_and_store(path.first, path.second);
		if (err != OK && r_steps.error == OK) {
			r_steps.error = err;
		}
		r_steps.index++;
	}
	if (r_steps.index < r_steps.paths.size()) {
		return true;
	}
	_apply_keys(r_steps.keys);
	callable_mp(this, &ResourceDatabase::_emit_preload_finished).call_deferred(r_steps.preloads, int(r_steps.error));
	return false;
}

Error ResourceDatabase::preload_from(const Ref<Preloads> &p_preloads, bool p_use_threads) {
	ERR_FAIL_COND_V_MSG(p_preloads.is_null(), ERR_INVALID_PARAMETER, "The preloads are null.");
	bool is_finished = false;
	{
		MutexLock jobs_lock(jobs_mutex);
		is_finished = finished;
	}
	ERR_FAIL_COND_V_MSG(is_finished, ERR_UNAVAILABLE, "ResourceDatabase is shutting down.");

	Error first_error = _store_direct_resources(p_preloads);

	Job *job = memnew(Job);
	job->preloads = p_preloads;
	const Error collect_error = _collect(p_preloads, job);
	if (first_error == OK) {
		first_error = collect_error;
	}

	bool threaded = false;
#ifdef THREADS_ENABLED
	// Without threads (default Web export) or with a single core, load synchronously.
	threaded = p_use_threads && !job->paths.is_empty() && OS::get_singleton()->get_processor_count() > 1;
#else
	(void)p_use_threads;
#endif

	if (threaded) {
		MutexLock jobs_lock(jobs_mutex);
		job->error = first_error;
		job->id = next_job_id++;
		jobs.insert(job->id, job);
		job->thread = memnew(Thread);
		job->thread->start(&ResourceDatabase::_job_task, job);
		return first_error;
	}

	_run_job(job);
	if (first_error == OK) {
		first_error = job->error;
	}
	memdelete(job);
	callable_mp(this, &ResourceDatabase::_emit_preload_finished).call_deferred(p_preloads, int(first_error));
	return first_error;
}

Error ResourceDatabase::preload_resource(const String &p_path, const String &p_key) {
	ERR_FAIL_COND_V_MSG(p_path.is_empty(), ERR_INVALID_PARAMETER, "The path to preload is empty.");
	const Error err = _load_and_store(p_path, String());
	if (err == OK && !p_key.is_empty()) {
		LocalVector<Pair<String, String>> keys;
		keys.push_back(Pair<String, String>(p_key, p_path));
		_apply_keys(keys);
	}
	return err;
}

Error ResourceDatabase::add_resource(const String &p_key, const Ref<Resource> &p_resource) {
	ERR_FAIL_COND_V_MSG(p_key.is_empty(), ERR_INVALID_PARAMETER, "The key can't be empty.");
	ERR_FAIL_COND_V_MSG(p_resource.is_null(), ERR_INVALID_PARAMETER, "The resource is null.");
	const String key = _normalize_key(p_key);
	const String resource_path = p_resource->get_path().is_empty() ? String() : _normalize_key(p_resource->get_path());

	LocalVector<Ref<Resource>> released;
	{
		RWLockWrite write_lock(lock);
		const Entry *by_path = resource_path.is_empty() ? nullptr : entries.getptr(resource_path);
		if (!entries.has(key) && by_path && by_path->resource == p_resource) {
			// Already stored under its path: the key becomes another name for it.
			_add_alias(key, resource_path);
		} else {
			_store(key, p_resource, released);
			_add_alias(resource_path, key);
		}
	}
	return OK;
}

bool ResourceDatabase::add_resource_if_missing(const String &p_key, const Ref<Resource> &p_resource) {
	ERR_FAIL_COND_V_MSG(p_key.is_empty(), false, "The key can't be empty.");
	ERR_FAIL_COND_V_MSG(p_resource.is_null(), false, "The resource is null.");
	const String key = _normalize_key(p_key);
	const String resource_path = p_resource->get_path().is_empty() ? String() : _normalize_key(p_resource->get_path());

	LocalVector<Ref<Resource>> released;
	{
		RWLockWrite write_lock(lock);
		if (!_resolve(key).is_empty()) {
			return false;
		}
		const Entry *by_path = resource_path.is_empty() ? nullptr : entries.getptr(resource_path);
		if (by_path && by_path->resource == p_resource) {
			_add_alias(key, resource_path);
		} else {
			_store(key, p_resource, released);
			_add_alias(resource_path, key);
		}
	}
	return true;
}

int ResourceDatabase::remove_matching(const String &p_pattern) {
	const Vector<String> parts = _split_pattern(p_pattern);
	LocalVector<Ref<Resource>> released;
	{
		RWLockWrite write_lock(lock);
		LocalVector<String> matching;
		for (const KeyValue<String, Entry> &kv : entries) {
			if (_entry_matches(kv.key, kv.value, parts, StringName())) {
				matching.push_back(kv.key);
			}
		}
		for (const String &key : matching) {
			_erase_entry(key, released);
		}
	}
	return int(released.size());
}

void ResourceDatabase::clear() {
	LocalVector<Ref<Resource>> released;
	{
		RWLockWrite write_lock(lock);
		released.reserve(entries.size());
		for (const KeyValue<String, Entry> &kv : entries) {
			released.push_back(kv.value.resource);
		}
		entries.clear();
		aliases.clear();
	}
}

Ref<Resource> ResourceDatabase::get_resource(const String &p_key_or_path) const {
	const String key = _normalize_key(p_key_or_path);
	RWLockRead read_lock(lock);
	const String primary = _resolve(key);
	if (primary.is_empty()) {
		return Ref<Resource>();
	}
	return entries[primary].resource;
}

bool ResourceDatabase::has_resource(const String &p_key_or_path) const {
	const String key = _normalize_key(p_key_or_path);
	RWLockRead read_lock(lock);
	return !_resolve(key).is_empty();
}

int ResourceDatabase::get_entry_count() const {
	RWLockRead read_lock(lock);
	return int(entries.size());
}

TypedArray<Resource> ResourceDatabase::find_resources(const String &p_pattern, const StringName &p_type) const {
	const Vector<String> parts = _split_pattern(p_pattern);
	TypedArray<Resource> result;
	RWLockRead read_lock(lock);
	for (const KeyValue<String, Entry> &kv : entries) {
		if (_entry_matches(kv.key, kv.value, parts, p_type)) {
			result.push_back(kv.value.resource);
		}
	}
	return result;
}

PackedStringArray ResourceDatabase::find_keys(const String &p_pattern, const StringName &p_type) const {
	const Vector<String> parts = _split_pattern(p_pattern);
	PackedStringArray result;
	RWLockRead read_lock(lock);
	for (const KeyValue<String, Entry> &kv : entries) {
		if (_entry_matches(kv.key, kv.value, parts, p_type)) {
			result.push_back(kv.key);
		}
	}
	return result;
}

int64_t ResourceDatabase::get_memory_usage() const {
	LocalVector<Ref<Resource>> resources;
	{
		RWLockRead read_lock(lock);
		resources.reserve(entries.size());
		for (const KeyValue<String, Entry> &kv : entries) {
			resources.push_back(kv.value.resource);
		}
	}
	// Property access may run script code: outside of the lock.
	HashSet<ObjectID> visited;
	uint64_t total = 0;
	for (const Ref<Resource> &resource : resources) {
		total += _estimate_variant_size(Variant(resource.ptr()), visited, 0);
	}
	return int64_t(total);
}

bool ResourceDatabase::is_preloading() const {
	MutexLock jobs_lock(jobs_mutex);
	return !jobs.is_empty();
}

void ResourceDatabase::preload_project_preloads() {
	ProjectSettings *settings_singleton = ProjectSettings::get_singleton();
	if (!settings_singleton) {
		return;
	}
	PackedStringArray paths;
	if (settings_singleton->has_setting("filesystem/resources/preloads")) {
		paths = GLOBAL_GET("filesystem/resources/preloads");
	}
#ifndef DISABLE_DEPRECATED
	// Deprecated name of the setting, still read.
	if (settings_singleton->has_setting("filesystem/resources/preload_lists")) {
		const PackedStringArray legacy_paths = GLOBAL_GET("filesystem/resources/preload_lists");
		paths.append_array(legacy_paths);
	}
#endif
	for (const String &path : paths) {
		if (path.is_empty()) {
			continue;
		}
		const Ref<Preloads> preloads = ResourceLoader::load(path, "Preloads");
		if (preloads.is_null()) {
			ERR_PRINT(vformat("ResourceDatabase: can't load the preloads \"%s\" named in \"filesystem/resources/preloads\".", path));
			continue;
		}
		preload_from(preloads, false);
	}
}

void ResourceDatabase::finish() {
	LocalVector<Job *> pending;
	{
		MutexLock jobs_lock(jobs_mutex);
		finished = true;
		for (const KeyValue<uint64_t, Job *> &kv : jobs) {
			pending.push_back(kv.value);
		}
		jobs.clear();
	}
	for (Job *job : pending) {
		if (job->thread) {
			job->thread->wait_to_finish();
			memdelete(job->thread);
		}
		memdelete(job);
	}
	clear();
}

void ResourceDatabase::_bind_methods() {
	ClassDB::bind_method(D_METHOD("preload_from", "preloads", "use_threads"), &ResourceDatabase::preload_from, DEFVAL(true));
#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("preload_from_list", "preloads", "use_threads"), &ResourceDatabase::preload_from, DEFVAL(true)); // Deprecated alias.
#endif
	ClassDB::bind_method(D_METHOD("preload_resource", "path", "key"), &ResourceDatabase::preload_resource, DEFVAL(String()));

	ClassDB::bind_method(D_METHOD("add_resource", "key", "resource"), &ResourceDatabase::add_resource);
	ClassDB::bind_method(D_METHOD("add_resource_if_missing", "key", "resource"), &ResourceDatabase::add_resource_if_missing);
	ClassDB::bind_method(D_METHOD("remove_matching", "pattern"), &ResourceDatabase::remove_matching);
	ClassDB::bind_method(D_METHOD("clear"), &ResourceDatabase::clear);

	ClassDB::bind_method(D_METHOD("get_resource", "key_or_path"), &ResourceDatabase::get_resource);
	ClassDB::bind_method(D_METHOD("has_resource", "key_or_path"), &ResourceDatabase::has_resource);
	ClassDB::bind_method(D_METHOD("get_entry_count"), &ResourceDatabase::get_entry_count);
	ClassDB::bind_method(D_METHOD("find_resources", "pattern", "type"), &ResourceDatabase::find_resources, DEFVAL(String()), DEFVAL(StringName()));
	ClassDB::bind_method(D_METHOD("find_keys", "pattern", "type"), &ResourceDatabase::find_keys, DEFVAL(String()), DEFVAL(StringName()));

	ClassDB::bind_method(D_METHOD("get_memory_usage"), &ResourceDatabase::get_memory_usage);
	ClassDB::bind_method(D_METHOD("is_preloading"), &ResourceDatabase::is_preloading);
	ClassDB::bind_method(D_METHOD("get_preload_progress"), &ResourceDatabase::get_preload_progress);

	ADD_SIGNAL(MethodInfo("preload_progress", PropertyInfo(Variant::OBJECT, "preloads", PROPERTY_HINT_RESOURCE_TYPE, "Preloads"), PropertyInfo(Variant::OBJECT, "progress", PROPERTY_HINT_RESOURCE_TYPE, "LoadProgress")));
	ADD_SIGNAL(MethodInfo("preload_finished", PropertyInfo(Variant::OBJECT, "preloads", PROPERTY_HINT_RESOURCE_TYPE, "Preloads"), PropertyInfo(Variant::INT, "error", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM, "Error")));
}

ResourceDatabase::ResourceDatabase() {
	singleton = this;
}

ResourceDatabase::~ResourceDatabase() {
	finish();
	singleton = nullptr;
}
