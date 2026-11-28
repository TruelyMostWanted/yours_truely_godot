/**************************************************************************/
/*  test_resource_database.cpp                                            */
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

TEST_FORCE_LINK(test_resource_database)

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/load_progress.h"
#include "core/io/resource_database.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_text_serializer.h"
#include "core/object/callable_mp.h"
#include "core/object/message_queue.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "tests/signal_watcher.h"
#include "tests/test_utils.h"

namespace TestResourceDatabase {

// Recognizes `.dbcount` and counts how often it is asked to load, without touching the disk.
class CountingLoader : public ResourceFormatLoader {
public:
	SafeNumeric<uint32_t> loads;

	virtual Ref<Resource> load(const String &p_path, const String &p_original_path = "", Error *r_error = nullptr, bool p_use_sub_threads = false, float *r_progress = nullptr, CacheMode p_cache_mode = CACHE_MODE_REUSE) override {
		loads.increment();
		Ref<Resource> res;
		res.instantiate();
		res->set_name(p_path.get_file().get_basename());
		if (r_error) {
			*r_error = OK;
		}
		return res;
	}
	virtual void get_recognized_extensions(List<String> *p_extensions) const override {
		p_extensions->push_back("dbcount");
	}
	virtual bool handles_type(const String &p_type) const override {
		return p_type == "Resource";
	}
	virtual String get_resource_type(const String &p_path) const override {
		return p_path.has_extension("dbcount") ? "Resource" : "";
	}
};

static Ref<Resource> _named(const String &p_name) {
	Ref<Resource> res;
	res.instantiate();
	res->set_name(p_name);
	return res;
}

// Expected arguments of one `preload_finished` emission without error.
static Array _finished_args(const Ref<Preloads> &p_list) {
	Array args;
	args.push_back(p_list);
	args.push_back(int(OK));
	Array emissions;
	emissions.push_back(args);
	return emissions;
}

// Runs deferred calls until the database has no running preload (or a timeout).
static void _wait_for_preloads(ResourceDatabase *p_db) {
	for (int i = 0; i < 5000; i++) {
		MessageQueue::get_singleton()->flush();
		if (!p_db->is_preloading()) {
			return;
		}
		OS::get_singleton()->delay_usec(1000);
	}
}

TEST_CASE("[ResourceDatabase] Patterns, keys, types and removal") {
	ResourceDatabase *db = ResourceDatabase::get_singleton();
	REQUIRE(db);
	db->clear();

	Ref<FilesManifest> sword_manifest;
	sword_manifest.instantiate();
	CHECK(db->add_resource("items/Sword_Iron", _named("iron")) == OK);
	CHECK(db->add_resource("items/sword_gold", sword_manifest) == OK);
	CHECK(db->add_resource("items/shield", _named("shield")) == OK);
	CHECK(db->get_entry_count() == 3);

	CHECK(db->has_resource("items/shield"));
	CHECK_FALSE(db->has_resource("items/SHIELD")); // Keys are case-sensitive, patterns are not.
	CHECK(db->get_resource("items/shield")->get_name() == "shield");

	CHECK(db->find_resources().size() == 3);
	CHECK(db->find_resources("items/shield").size() == 1);
	CHECK(db->find_resources("items/%").size() == 3);
	CHECK(db->find_resources("%_iron").size() == 1);
	CHECK(db->find_resources("%SWORD%").size() == 2);
	CHECK(db->find_resources("items/%gold").size() == 1);
	CHECK(db->find_resources("%sword%", "FilesManifest").size() == 1);
	CHECK(db->find_resources("%sword%", "Resource").size() == 2);
	CHECK(db->find_resources("%sword%", "Preloads").is_empty());
	CHECK(db->find_keys("%sword%", "FilesManifest") == PackedStringArray({ "items/sword_gold" }));

	CHECK_FALSE(db->add_resource_if_missing("items/shield", _named("other")));
	CHECK(db->get_resource("items/shield")->get_name() == "shield");
	CHECK(db->add_resource_if_missing("items/helmet", _named("helmet")));
	CHECK(db->add_resource("items/shield", _named("shield2")) == OK);
	CHECK(db->get_resource("items/shield")->get_name() == "shield2");
	CHECK(db->get_entry_count() == 4);

	CHECK(db->get_memory_usage() > 0);

	CHECK(db->remove_matching("%sword%") == 2);
	CHECK(db->get_entry_count() == 2);
	CHECK(db->remove_matching("nothing") == 0);
	db->clear();
	CHECK(db->get_entry_count() == 0);
	CHECK(db->get_resource("items/shield").is_null());

	ERR_PRINT_OFF;
	CHECK(db->add_resource("", _named("x")) == ERR_INVALID_PARAMETER);
	CHECK(db->add_resource("x", Ref<Resource>()) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[ResourceDatabase] Preload list: resources, paths, manifest, keys; load() hits the database") {
	ResourceDatabase *db = ResourceDatabase::get_singleton();
	REQUIRE(db);
	db->clear();

	Ref<CountingLoader> loader;
	loader.instantiate();
	ResourceLoader::add_resource_format_loader(loader);

	// Manifest folder with one file; the counting loader ignores the content.
	const String root = OS::get_singleton()->get_cache_path().path_join("test_resource_database");
	DirAccess::make_dir_recursive_absolute(root);
	{
		Ref<FileAccess> f = FileAccess::open(root.path_join("from_manifest.dbcount"), FileAccess::WRITE);
		REQUIRE(f.is_valid());
		f->store_string("x");
	}
	Ref<FilesManifest> manifest;
	manifest.instantiate();
	manifest->set_folder_paths(PackedStringArray({ root }));
	manifest->set_extensions(PackedStringArray({ ".dbcount" }));

	Ref<Preloads> list;
	list.instantiate();
	TypedArray<Resource> resources;
	resources.push_back(_named("direct_a"));
	resources.push_back(_named("direct_b"));
	resources.push_back(_named("direct_c"));
	list->set_resources(resources);
	list->set_paths(PackedStringArray({ "res://db_a.dbcount", "res://db_b.dbcount" }));
	TypedArray<FilesManifest> manifests;
	manifests.push_back(manifest);
	list->set_manifests(manifests);
	Dictionary keys;
	keys["sword"] = "res://db_a.dbcount";
	list->set_keys(keys);
	CHECK_FALSE(list->is_empty());

	SIGNAL_WATCH(db, "preload_finished");
	CHECK(db->preload_from(list, false) == OK);
	MessageQueue::get_singleton()->flush();
	SIGNAL_CHECK("preload_finished", _finished_args(list));
	SIGNAL_UNWATCH(db, "preload_finished");

	CHECK(db->get_entry_count() == 6);
	CHECK(loader->loads.get() == 3);
	// By name (direct resources), by path, by key, by type.
	CHECK(db->has_resource("direct_a"));
	CHECK(db->has_resource("res://db_b.dbcount"));
	CHECK(db->get_resource("sword") == db->get_resource("res://db_a.dbcount"));
	CHECK(db->find_resources("%from_manifest%").size() == 1);
	CHECK(db->find_resources("", "Resource").size() == 6);
	CHECK(db->find_resources("sword").size() == 1); // Keys match like paths.

	// `ResourceLoader.load()` returns the stored instance without loading again.
	const Ref<Resource> stored = db->get_resource("res://db_a.dbcount");
	CHECK(ResourceLoader::load("res://db_a.dbcount") == stored);
	CHECK(loader->loads.get() == 3);

	// The database keeps the resources alive (and in `ResourceCache`) without other references.
	CHECK(ResourceCache::has("res://db_b.dbcount"));

	// `preload_resource()` with an extra key.
	CHECK(db->preload_resource("res://db_c.dbcount", "shield") == OK);
	CHECK(db->get_resource("shield") == db->get_resource("res://db_c.dbcount"));
	CHECK(loader->loads.get() == 4);

	// Threaded preload: the signal follows on the main thread.
	Ref<Preloads> list2;
	list2.instantiate();
	list2->set_paths(PackedStringArray({ "res://db_t1.dbcount", "res://db_t2.dbcount" }));
	SIGNAL_WATCH(db, "preload_finished");
	CHECK(db->preload_from(list2, true) == OK);
	_wait_for_preloads(db);
	MessageQueue::get_singleton()->flush();
	SIGNAL_CHECK("preload_finished", _finished_args(list2));
	SIGNAL_UNWATCH(db, "preload_finished");
	CHECK_FALSE(db->is_preloading());
	CHECK(db->has_resource("res://db_t1.dbcount"));
	CHECK(db->has_resource("res://db_t2.dbcount"));

	db->clear();
	CHECK_FALSE(ResourceCache::has("res://db_b.dbcount"));

	ResourceLoader::remove_resource_format_loader(loader);
	DirAccess::remove_absolute(root.path_join("from_manifest.dbcount"));
	DirAccess::remove_absolute(root);
}

static LocalVector<int> recorded_loaded;
static LocalVector<int> recorded_total;
static LocalVector<float> recorded_ratio;

static void _record_progress(const Ref<Preloads> &, const Ref<LoadProgress> &p_progress) {
	recorded_loaded.push_back(p_progress->get_loaded_count());
	recorded_total.push_back(p_progress->get_total_count());
	recorded_ratio.push_back(p_progress->get_ratio());
}

TEST_CASE("[ResourceDatabase] preload_progress per loaded file, then preload_finished") {
	ResourceDatabase *db = ResourceDatabase::get_singleton();
	REQUIRE(db);
	db->clear();
	Ref<CountingLoader> loader;
	loader.instantiate();
	ResourceLoader::add_resource_format_loader(loader);

	const Ref<LoadProgress> idle = db->get_preload_progress();
	CHECK(idle->is_finished()); // Nothing preloads.
	CHECK(idle->get_ratio() == doctest::Approx(1.0));

	recorded_loaded.clear();
	recorded_total.clear();
	recorded_ratio.clear();
	db->connect("preload_progress", callable_mp_static(&_record_progress));

	Ref<Preloads> list;
	list.instantiate();
	list->set_paths(PackedStringArray({ "res://db_p1.dbcount", "res://db_p2.dbcount" }));
	CHECK(db->preload_from(list, false) == OK);
	MessageQueue::get_singleton()->flush();
	REQUIRE(recorded_loaded.size() == 2);
	CHECK(recorded_loaded[0] == 1);
	CHECK(recorded_loaded[1] == 2);
	CHECK(recorded_total[0] == 2);
	CHECK(recorded_ratio[0] == doctest::Approx(0.5));
	CHECK(recorded_ratio[1] == doctest::Approx(1.0));

	// Threaded: the last emission is complete and the poll value is complete again once finished.
	recorded_loaded.clear();
	recorded_total.clear();
	recorded_ratio.clear();
	Ref<Preloads> list2;
	list2.instantiate();
	list2->set_paths(PackedStringArray({ "res://db_p3.dbcount" }));
	CHECK(db->preload_from(list2, true) == OK);
	_wait_for_preloads(db);
	MessageQueue::get_singleton()->flush();
	REQUIRE(recorded_ratio.size() == 1);
	CHECK(recorded_loaded[0] == 1);
	CHECK(recorded_ratio[0] == doctest::Approx(1.0));
	CHECK(db->get_preload_progress()->is_finished());

	db->disconnect("preload_progress", callable_mp_static(&_record_progress));
	db->clear();
	ResourceLoader::remove_resource_format_loader(loader);
}

TEST_CASE("[ResourceLoader] Manifest load status: counts, ratio, stepwise and result") {
	Ref<CountingLoader> loader;
	loader.instantiate();
	ResourceLoader::add_resource_format_loader(loader);

	const String root = OS::get_singleton()->get_cache_path().path_join("test_manifest_progress");
	DirAccess::make_dir_recursive_absolute(root);
	for (int idx = 0; idx < 3; idx++) {
		Ref<FileAccess> f = FileAccess::open(root.path_join(vformat("file%d.dbcount", idx)), FileAccess::WRITE);
		REQUIRE(f.is_valid());
		f->store_string("x");
	}
	Ref<FilesManifest> manifest;
	manifest.instantiate();
	manifest->set_folder_paths(PackedStringArray({ root }));
	manifest->set_extensions(PackedStringArray({ ".dbcount" }));

	Ref<LoadProgress> status;
	for (int frame = 0; frame < 2000; frame++) {
		status = manifest->load_threaded_get_status_resources();
		CHECK(status->get_total_count() == 3);
		CHECK(status->get_ratio() <= 1.0f);
		if (status->is_finished()) {
			break;
		}
		OS::get_singleton()->delay_usec(500);
	}
	REQUIRE(status.is_valid());
	CHECK(status->is_finished());
	CHECK(status->get_loaded_count() == 3);
	CHECK(status->get_ratio() == doctest::Approx(1.0));

	const Dictionary result = manifest->load_threaded_get_resources();
	CHECK(result.size() == 3);
	CHECK(loader->loads.get() == 3);

	const Ref<LoadProgress> empty = Ref<FilesManifest>(memnew(FilesManifest))->load_threaded_get_status_resources();
	CHECK(empty->get_total_count() == 0);
	CHECK(empty->is_finished());

	ResourceLoader::remove_resource_format_loader(loader);
	for (int idx = 0; idx < 3; idx++) {
		DirAccess::remove_absolute(root.path_join(vformat("file%d.dbcount", idx)));
	}
	DirAccess::remove_absolute(root);
}

struct ThreadData {
	ResourceDatabase *db = nullptr;
	int index = 0;
	SafeNumeric<uint32_t> *failures = nullptr;
};

static void _add_and_find(void *p_userdata) {
	ThreadData *data = static_cast<ThreadData *>(p_userdata);
	for (int i = 0; i < 100; i++) {
		const String key = vformat("thread%d/item%d", data->index, i);
		if (data->db->add_resource(key, _named(key)) != OK) {
			data->failures->increment();
		}
		if (data->db->find_resources(vformat("thread%d/%%", data->index)).size() != i + 1) {
			data->failures->increment();
		}
	}
}

TEST_CASE("[ResourceDatabase] Parallel add and find") {
	ResourceDatabase *db = ResourceDatabase::get_singleton();
	REQUIRE(db);
	db->clear();

	SafeNumeric<uint32_t> failures;
	ThreadData data[4];
	Thread threads[4];
	for (int i = 0; i < 4; i++) {
		data[i].db = db;
		data[i].index = i;
		data[i].failures = &failures;
		threads[i].start(_add_and_find, &data[i]);
	}
	for (int i = 0; i < 4; i++) {
		threads[i].wait_to_finish();
	}
	CHECK(failures.get() == 0);
	CHECK(db->get_entry_count() == 400);
	CHECK(db->find_resources("thread2/%").size() == 100);
	db->clear();
}

#ifndef DISABLE_DEPRECATED
TEST_CASE("[ResourceDatabase] A .tres with the old type PreloadList loads as Preloads") {
	const String path = TestUtils::get_temp_path("old_preload_list.tres");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string("[gd_resource type=\"PreloadList\" format=3]\n\n[resource]\npaths = PackedStringArray(\"res://a.tres\")\n");
	file.unref();

	const Ref<Preloads> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());
	CHECK(loaded->get_class() == "Preloads");
	CHECK(loaded->get_paths() == PackedStringArray({ "res://a.tres" }));
	DirAccess::remove_absolute(path);
}

TEST_CASE("[ResourceDatabase] A JSON text with the old type PreloadList loads as Preloads") {
	const String text = "{\"$type\": \"PreloadList\", \"paths\": [\"res://a.tres\"]}";
	const Ref<Preloads> loaded = ResourceTextSerializer::get_singleton()->deserialize(text, ResourceTextSerializer::FORMAT_JSON);
	REQUIRE(loaded.is_valid());
	CHECK(loaded->get_class() == "Preloads");
	CHECK(loaded->get_paths() == PackedStringArray({ "res://a.tres" }));
}
#endif // DISABLE_DEPRECATED

} // namespace TestResourceDatabase
