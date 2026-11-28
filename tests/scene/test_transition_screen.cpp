/**************************************************************************/
/*  test_transition_screen.cpp                                            */
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

#include "core/io/resource_loader.h"
#include "tests/test_macros.h"

TEST_FORCE_LINK(test_transition_screen)

#include "core/io/dir_access.h"
#include "core/io/preloads.h"
#include "core/io/resource_database.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/label.h"
#include "scene/gui/progress_bar.h"
#include "scene/main/scene_tree.h"
#include "scene/main/transition_screen.h"
#include "scene/main/window.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/packed_scene.h"
#include "tests/display_server_mock.h"
#include "tests/signal_watcher.h"

namespace TestTransitionScreen {

static String _scene_dir() {
	const String dir = OS::get_singleton()->get_user_data_dir().path_join("test_transition_screen");
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

// Saves a scene whose root is a plain node with the given name; returns its path.
static String _save_scene(const String &p_file, const String &p_root_name) {
	Node *root = memnew(Node);
	root->set_name(p_root_name);
	Ref<PackedScene> packed;
	packed.instantiate();
	packed->pack(root);
	memdelete(root);
	const String path = _scene_dir().path_join(p_file);
	CHECK(ResourceSaver::save(packed, path) == OK);
	return path;
}

// One emission without arguments, for SIGNAL_CHECK (a nested initializer list would be flattened).
static Array _one_emission() {
	Array emissions;
	emissions.push_back(Array());
	return emissions;
}

static bool _is_alive(ObjectID p_id) {
	return ObjectDB::get_instance(p_id) != nullptr;
}

// Runs frames of `p_delta` seconds until the object is freed (or a frame limit is hit); loading may run on threads.
static void _run_until_freed(ObjectID p_id, double p_delta = 0.05) {
	for (int frame = 0; frame < 2000 && _is_alive(p_id); frame++) {
		SceneTree::get_singleton()->process(p_delta);
		OS::get_singleton()->delay_usec(500);
	}
}

static void _run_until_state(TransitionScreen *p_screen, TransitionScreen::State p_state, double p_delta = 0.0) {
	for (int frame = 0; frame < 2000 && p_screen->get_state() < p_state; frame++) {
		SceneTree::get_singleton()->process(p_delta);
		OS::get_singleton()->delay_usec(500);
	}
}

TEST_CASE("[SceneTree][TransitionScreen] Defaults, overrides and clamped durations") {
	TransitionScreen *screen = memnew(TransitionScreen);
	CHECK(screen->get_layer() == 100);
	CHECK(screen->get_process_mode() == Node::PROCESS_MODE_ALWAYS);
	CHECK(screen->get_next_scene().is_empty());
	CHECK(screen->get_min_duration() == doctest::Approx(0.0));
	CHECK(screen->get_fade_in_duration() == doctest::Approx(0.3));
	CHECK(screen->get_fade_out_duration() == doctest::Approx(0.3));
	CHECK_FALSE(screen->is_skippable());
	CHECK(screen->is_auto_finish());
	CHECK_FALSE(screen->is_free_current_scene_before_loading());
	CHECK(screen->get_preload_assets().is_null());
	CHECK(screen->get_state() == TransitionScreen::STATE_IDLE);
	CHECK(screen->get_progress() == doctest::Approx(0.0));

	screen->set_min_duration(-1.0);
	screen->set_fade_in_duration(-2.0);
	screen->set_fade_out_duration(-3.0);
	CHECK(screen->get_min_duration() == doctest::Approx(0.0));
	CHECK(screen->get_fade_in_duration() == doctest::Approx(0.0));
	CHECK(screen->get_fade_out_duration() == doctest::Approx(0.0));
	memdelete(screen);

	SplashScreen *splash = memnew(SplashScreen);
	CHECK(splash->get_min_duration() == doctest::Approx(2.0));
	CHECK(splash->get_fade_in_duration() == doctest::Approx(0.0));
	CHECK(splash->is_skippable());
	memdelete(splash);

	LoadingScreen *loading = memnew(LoadingScreen);
	CHECK(loading->get_progress_bar().is_empty());
	CHECK(loading->get_tip_label().is_empty());
	CHECK(loading->get_tips().is_empty());
	CHECK(loading->get_tip_interval() == doctest::Approx(5.0));
	CHECK(loading->get_current_tip().is_empty());
	loading->set_tip_interval(-1.0);
	CHECK(loading->get_tip_interval() == doctest::Approx(0.0));
	memdelete(loading);
}

TEST_CASE("[SceneTree][TransitionScreen] Without a next scene: fade in, wait, fade out, free") {
	TransitionScreen *screen = memnew(TransitionScreen);
	screen->set_fade_in_duration(0.4);
	screen->set_fade_out_duration(0.4);
	screen->set_min_duration(0.2);
	const ObjectID id = screen->get_instance_id();
	SIGNAL_WATCH(screen, "started");
	SIGNAL_WATCH(screen, "loaded");
	SIGNAL_WATCH(screen, "finished");

	SceneTree::get_singleton()->get_root()->add_child(screen);
	CHECK(screen->get_fade() == doctest::Approx(0.0));
	CHECK(screen->get_state() == TransitionScreen::STATE_IDLE);

	SceneTree::get_singleton()->process(0.1); // First frame starts; its delta is not counted.
	SIGNAL_CHECK("started", _one_emission());
	CHECK(screen->get_state() == TransitionScreen::STATE_FADING_IN);

	SceneTree::get_singleton()->process(0.2);
	CHECK(screen->get_fade() == doctest::Approx(0.5));

	SceneTree::get_singleton()->process(0.2);
	CHECK(screen->get_fade() == doctest::Approx(1.0));
	CHECK(screen->get_state() == TransitionScreen::STATE_LOADED);
	CHECK(screen->get_progress() == doctest::Approx(1.0));
	SIGNAL_CHECK("loaded", _one_emission());

	SceneTree::get_singleton()->process(0.1);
	CHECK(screen->get_state() == TransitionScreen::STATE_LOADED); // 0.1 of 0.2 s fully visible.

	SceneTree::get_singleton()->process(0.1);
	CHECK(screen->get_state() == TransitionScreen::STATE_FADING_OUT);
	CHECK(screen->get_fade() == doctest::Approx(1.0));

	SceneTree::get_singleton()->process(0.2);
	CHECK(screen->get_fade() == doctest::Approx(0.5));
	SIGNAL_CHECK_FALSE("finished");

	SceneTree::get_singleton()->process(0.2);
	SIGNAL_CHECK("finished", _one_emission());
	SceneTree::get_singleton()->process(0.0);
	CHECK_FALSE(_is_alive(id)); // Freed: the watcher's connections are gone with it.
}

TEST_CASE("[SceneTree][TransitionScreen] Changes the current scene while fully visible") {
	SceneTree *tree = SceneTree::get_singleton();
	const String path = _save_scene("next.tscn", "NextScene");
	Node *old_scene = memnew(Node);
	old_scene->set_name("OldScene");
	tree->add_current_scene(old_scene);
	const ObjectID old_id = old_scene->get_instance_id();

	TransitionScreen *screen = memnew(TransitionScreen);
	screen->set_next_scene(path);
	screen->set_fade_in_duration(0.0);
	screen->set_fade_out_duration(0.1);
	const ObjectID id = screen->get_instance_id();
	tree->get_root()->add_child(screen);

	_run_until_state(screen, TransitionScreen::STATE_FADING_OUT);
	REQUIRE(screen->get_state() == TransitionScreen::STATE_FADING_OUT);
	CHECK(screen->get_progress() == doctest::Approx(1.0));
	tree->process(0.0); // The new scene is added at the end of the frame of the change.
	REQUIRE(tree->get_current_scene() != nullptr);
	CHECK(tree->get_current_scene()->get_name() == StringName("NextScene"));
	CHECK_FALSE(_is_alive(old_id));
	CHECK(screen->is_inside_tree()); // Still there for the fade-out.

	_run_until_freed(id);
	CHECK_FALSE(_is_alive(id));
	tree->unload_current_scene();
}

TEST_CASE("[SceneTree][TransitionScreen] As the current scene it survives the change; auto_finish waits for finish()") {
	SceneTree *tree = SceneTree::get_singleton();
	const String path = _save_scene("next_manual.tscn", "ManualNext");
	TransitionScreen *screen = memnew(TransitionScreen);
	screen->set_next_scene(path);
	screen->set_fade_in_duration(0.0);
	screen->set_fade_out_duration(0.0);
	screen->set_auto_finish(false);
	const ObjectID id = screen->get_instance_id();
	tree->add_current_scene(screen);

	_run_until_state(screen, TransitionScreen::STATE_LOADED);
	REQUIRE(screen->get_state() == TransitionScreen::STATE_LOADED);
	tree->process(1.0);
	tree->process(1.0);
	CHECK(screen->get_state() == TransitionScreen::STATE_LOADED); // Waits for finish().
	CHECK(tree->get_current_scene() == screen);

	screen->finish();
	tree->process(0.0);
	tree->process(0.0);
	REQUIRE(tree->get_current_scene() != nullptr);
	CHECK(tree->get_current_scene()->get_name() == StringName("ManualNext"));
	CHECK_FALSE(_is_alive(id)); // Fade-out of 0 s: freed right after the change.
	tree->unload_current_scene();
}

TEST_CASE("[SceneTree][TransitionScreen] Skippable: a key press ends min_duration") {
	SceneTree *tree = SceneTree::get_singleton();
	TransitionScreen *screen = memnew(TransitionScreen);
	screen->set_fade_in_duration(0.0);
	screen->set_fade_out_duration(1.0);
	screen->set_min_duration(100.0);
	screen->set_skippable(true);
	tree->get_root()->add_child(screen);
	SIGNAL_WATCH(screen, "skipped");

	tree->process(0.0);
	CHECK(screen->get_state() == TransitionScreen::STATE_LOADED);
	SEND_GUI_KEY_EVENT(Key::A);
	SIGNAL_CHECK("skipped", _one_emission());
	tree->process(0.0);
	CHECK(screen->get_state() == TransitionScreen::STATE_FADING_OUT);

	SIGNAL_UNWATCH(screen, "skipped");
	memdelete(screen);
}

TEST_CASE("[SceneTree][TransitionScreen] Free the current scene before loading; never load its own scene") {
	SceneTree *tree = SceneTree::get_singleton();
	const String path = _save_scene("next_low_memory.tscn", "LowMemoryNext");
	Node *old_scene = memnew(Node);
	tree->add_current_scene(old_scene);
	const ObjectID old_id = old_scene->get_instance_id();

	TransitionScreen *screen = memnew(TransitionScreen);
	screen->set_next_scene(path);
	screen->set_fade_in_duration(0.0);
	screen->set_fade_out_duration(0.0);
	screen->set_free_current_scene_before_loading(true);
	screen->set_auto_finish(false);
	tree->get_root()->add_child(screen);
	tree->process(0.0);
	tree->process(0.0);
	CHECK(tree->get_current_scene() == nullptr);
	CHECK_FALSE(_is_alive(old_id));
	_run_until_state(screen, TransitionScreen::STATE_LOADED);
	const ObjectID id = screen->get_instance_id();
	screen->finish();
	_run_until_freed(id);
	REQUIRE(tree->get_current_scene() != nullptr);
	CHECK(tree->get_current_scene()->get_name() == StringName("LowMemoryNext"));
	tree->unload_current_scene();

	// A screen whose next scene is its own file does not change the scene.
	TransitionScreen *self_loop = memnew(TransitionScreen);
	self_loop->set_scene_file_path(path);
	self_loop->set_next_scene(path);
	self_loop->set_fade_in_duration(0.0);
	self_loop->set_fade_out_duration(0.0);
	const ObjectID loop_id = self_loop->get_instance_id();
	tree->get_root()->add_child(self_loop);
	_run_until_freed(loop_id);
	CHECK(tree->get_current_scene() == nullptr);
}

TEST_CASE("[SceneTree][TransitionScreen] SceneTree.change_scene_with_transition()") {
	SceneTree *tree = SceneTree::get_singleton();
	const String path = _save_scene("next_tree.tscn", "TreeNext");
	Node *old_scene = memnew(Node);
	tree->add_current_scene(old_scene);
	const int child_count = tree->get_root()->get_child_count();

	ERR_PRINT_OFF;
	CHECK(tree->change_scene_with_transition(String(), Ref<PackedScene>()) == ERR_INVALID_PARAMETER);
	Node *not_a_screen = memnew(Node);
	Ref<PackedScene> wrong;
	wrong.instantiate();
	wrong->pack(not_a_screen);
	memdelete(not_a_screen);
	CHECK(tree->change_scene_with_transition(path, wrong) == ERR_INVALID_PARAMETER);
	CHECK(tree->change_scene_with_transition(_scene_dir().path_join("missing.tscn"), Ref<PackedScene>()) == ERR_CANT_OPEN);
	ERR_PRINT_ON;

	// Null transition scene: built-in black fade. A second transition is rejected while it runs.
	REQUIRE(tree->change_scene_with_transition(path, Ref<PackedScene>()) == OK);
	ERR_PRINT_OFF;
	CHECK(tree->change_scene_with_transition(path, Ref<PackedScene>()) == ERR_BUSY);
	ERR_PRINT_ON;
	tree->process(0.0); // Added deferred.
	REQUIRE(tree->get_root()->get_child_count() == child_count + 1);
	TransitionScreen *screen = Object::cast_to<TransitionScreen>(tree->get_root()->get_child(child_count));
	REQUIRE(screen != nullptr);
	CHECK(screen->get_next_scene() == path);
	CHECK(screen->get_child_count() == 1);
	_run_until_freed(screen->get_instance_id());
	REQUIRE(tree->get_current_scene() != nullptr);
	CHECK(tree->get_current_scene()->get_name() == StringName("TreeNext"));
	tree->unload_current_scene();
}

TEST_CASE("[SceneTree][LoadingScreen] Progress bar, tips and the preload list") {
	SceneTree *tree = SceneTree::get_singleton();
	const String path = _save_scene("next_loading.tscn", "LoadingNext");
	const String asset_path = _save_scene("asset.tscn", "Asset");
	ResourceDatabase *database = ResourceDatabase::get_singleton();
	REQUIRE(database != nullptr);
	database->clear();

	LoadingScreen *screen = memnew(LoadingScreen);
	ProgressBar *bar = memnew(ProgressBar);
	bar->set_name("Bar");
	bar->set_max(200.0);
	screen->add_child(bar);
	Label *label = memnew(Label);
	label->set_name("Tip");
	screen->add_child(label);
	screen->set_progress_bar(NodePath("Bar"));
	screen->set_tip_label(NodePath("Tip"));
	screen->set_tips(PackedStringArray({ "first", "second" }));
	screen->set_tip_interval(1.0);
	screen->set_next_scene(path);
	screen->set_fade_in_duration(0.0);
	screen->set_fade_out_duration(10.0);
	screen->set_auto_finish(false);
	Ref<Preloads> preloads;
	preloads.instantiate();
	preloads->set_paths(PackedStringArray({ asset_path }));
	screen->set_preload_assets(preloads);
	tree->get_root()->add_child(screen);

	CHECK(label->get_text() == "first");
	CHECK(screen->get_current_tip() == "first");
	CHECK(bar->get_value() == doctest::Approx(0.0));
	tree->process(0.6);
	tree->process(0.6);
	CHECK(label->get_text() == "second");
	tree->process(1.0);
	CHECK(label->get_text() == "first");

	_run_until_state(screen, TransitionScreen::STATE_LOADED);
	REQUIRE(screen->get_state() == TransitionScreen::STATE_LOADED);
	CHECK(bar->get_value() == doctest::Approx(200.0));
	CHECK(database->has_resource(asset_path));

	const ObjectID id = screen->get_instance_id();
	screen->finish();
	tree->process(0.0);
	tree->process(0.0);
	REQUIRE(tree->get_current_scene() != nullptr);
	CHECK(tree->get_current_scene()->get_name() == StringName("LoadingNext"));
	memdelete(screen);
	CHECK_FALSE(_is_alive(id));
	tree->unload_current_scene();
	database->clear();
}

TEST_CASE("[SceneTree][TransitionScreen] A failed load emits load_failed, not loaded, and keeps the scene") {
	SceneTree *tree = SceneTree::get_singleton();
	Node *old_scene = memnew(Node);
	old_scene->set_name("KeptScene");
	tree->add_current_scene(old_scene);

	TransitionScreen *screen = memnew(TransitionScreen);
	screen->set_next_scene(_scene_dir().path_join("missing.tscn"));
	screen->set_fade_in_duration(0.0);
	screen->set_fade_out_duration(0.5);
	const ObjectID id = screen->get_instance_id();
	SIGNAL_WATCH(screen, "loaded");
	SIGNAL_WATCH(screen, "load_failed");
	SIGNAL_WATCH(screen, "finished");
	tree->get_root()->add_child(screen);

	ERR_PRINT_OFF;
	_run_until_state(screen, TransitionScreen::STATE_FADING_OUT);
	ERR_PRINT_ON;
	CHECK(screen->get_load_error() != OK);
	CHECK(screen->get_progress() == doctest::Approx(0.0));
	SIGNAL_CHECK_FALSE("loaded");
	Array failed;
	failed.push_back(Array({ int(screen->get_load_error()) }));
	SIGNAL_CHECK("load_failed", failed);
	tree->process(0.5); // Fades out over the unchanged scene.
	SIGNAL_CHECK("finished", _one_emission());
	CHECK_FALSE(_is_alive(id));
	CHECK(tree->get_current_scene() == old_scene); // Unchanged.
	tree->unload_current_scene();
}

TEST_CASE("[SceneTree][TransitionScreen] One transition at a time; input does not reach the covered scene") {
	SceneTree *tree = SceneTree::get_singleton();
	TransitionScreen *first = memnew(TransitionScreen);
	first->set_fade_in_duration(0.0);
	first->set_fade_out_duration(1.0);
	first->set_auto_finish(false);
	tree->get_root()->add_child(first);
	TransitionScreen *second = memnew(TransitionScreen);
	const ObjectID second_id = second->get_instance_id();
	tree->get_root()->add_child(second);

	ERR_PRINT_OFF;
	tree->process(0.0);
	tree->process(0.0);
	ERR_PRINT_ON;
	CHECK(first->get_state() == TransitionScreen::STATE_LOADED);
	CHECK_FALSE(_is_alive(second_id)); // Ignored and freed.

	SEND_GUI_KEY_EVENT(Key::A);
	CHECK(tree->get_root()->is_input_handled()); // Not skippable, still blocked.
	CHECK(first->get_state() == TransitionScreen::STATE_LOADED);

	first->finish();
	tree->process(0.0);
	CHECK(first->get_state() == TransitionScreen::STATE_FADING_OUT);
	SEND_GUI_KEY_EVENT(Key::A);
	CHECK_FALSE(tree->get_root()->is_input_handled()); // The new scene gets input while the screen fades out.
	memdelete(first);
}

TEST_CASE("[SceneTree][TransitionScreen] A screen inside the current scene runs from the root; freeing releases the load") {
	SceneTree *tree = SceneTree::get_singleton();
	const String path = _save_scene("next_from_level.tscn", "FromLevelNext");
	Node *level = memnew(Node);
	tree->add_current_scene(level);
	TransitionScreen *screen = memnew(TransitionScreen);
	screen->set_next_scene(path);
	screen->set_fade_in_duration(0.0);
	screen->set_fade_out_duration(0.0);
	const ObjectID id = screen->get_instance_id();
	level->add_child(screen);

	tree->process(0.0); // Moves to the root (deferred).
	CHECK(screen->get_parent() == tree->get_root());
	CHECK(screen->get_state() == TransitionScreen::STATE_IDLE);
	_run_until_freed(id);
	CHECK_FALSE(_is_alive(id));
	REQUIRE(tree->get_current_scene() != nullptr);
	CHECK(tree->get_current_scene()->get_name() == StringName("FromLevelNext"));
	tree->unload_current_scene();

	// Freed while loading: the threaded load token is released.
	TransitionScreen *freed = memnew(TransitionScreen);
	freed->set_next_scene(path);
	freed->set_fade_in_duration(10.0);
	tree->get_root()->add_child(freed);
	tree->process(0.0);
	CHECK(freed->get_state() == TransitionScreen::STATE_FADING_IN);
	memdelete(freed);
	CHECK(ResourceLoader::load_threaded_get_status(path) == ResourceLoader::THREAD_LOAD_INVALID_RESOURCE);
}

static AnimationPlayer *_add_player(TransitionScreen *p_screen, double p_intro_length, double p_outro_length) {
	AnimationPlayer *player = memnew(AnimationPlayer);
	player->set_name("Player");
	Ref<AnimationLibrary> library;
	library.instantiate();
	Ref<Animation> intro;
	intro.instantiate();
	intro->set_length(p_intro_length);
	library->add_animation("intro", intro);
	Ref<Animation> outro;
	outro.instantiate();
	outro->set_length(p_outro_length);
	library->add_animation("outro", outro);
	player->add_animation_library("", library);
	p_screen->add_child(player);
	p_screen->set_animation_player(NodePath("Player"));
	return player;
}

TEST_CASE("[SceneTree][TransitionScreen] Animations replace the built-in fade; their length sets the duration") {
	TransitionScreen *screen = memnew(TransitionScreen);
	CHECK(screen->get_animation_player().is_empty());
	CHECK(screen->get_intro_animation() == StringName());
	CHECK(screen->get_outro_animation() == StringName());
	AnimationPlayer *player = _add_player(screen, 1.0, 0.5);
	screen->set_intro_animation("intro");
	screen->set_outro_animation("outro");
	screen->set_min_duration(0.2);
	screen->set_fade_in_duration(10.0); // Ignored with an animation.
	screen->set_fade_out_duration(10.0);
	const ObjectID id = screen->get_instance_id();
	SceneTree::get_singleton()->get_root()->add_child(screen);

	SceneTree::get_singleton()->process(0.1); // Starts.
	CHECK(screen->get_state() == TransitionScreen::STATE_FADING_IN);
	CHECK(player->get_current_animation() == "intro");
	SceneTree::get_singleton()->process(0.5);
	CHECK(screen->get_fade() == doctest::Approx(0.5));
	SceneTree::get_singleton()->process(0.5);
	CHECK(screen->get_fade() < 1.0f); // The phase ends when the player reports the end of the animation.
	SceneTree::get_singleton()->process(0.0);
	CHECK(screen->get_fade() == doctest::Approx(1.0));
	CHECK(screen->get_state() == TransitionScreen::STATE_LOADED);

	SceneTree::get_singleton()->process(0.2); // Waited min_duration, no next scene: the outro starts.
	CHECK(screen->get_state() == TransitionScreen::STATE_FADING_OUT);
	CHECK(player->get_current_animation() == "outro");
	SceneTree::get_singleton()->process(0.25);
	CHECK(screen->get_fade() == doctest::Approx(0.5));
	SceneTree::get_singleton()->process(0.25);
	SceneTree::get_singleton()->process(0.0);
	CHECK_FALSE(_is_alive(id));
}

TEST_CASE("[SceneTree][TransitionScreen] An animation without length or a looping one runs as the built-in fade") {
	TransitionScreen *screen = memnew(TransitionScreen);
	AnimationPlayer *player = _add_player(screen, 0.0, 0.5);
	player->get_animation("outro")->set_loop_mode(Animation::LOOP_LINEAR);
	screen->set_intro_animation("intro");
	screen->set_outro_animation("outro");
	screen->set_fade_in_duration(0.4);
	screen->set_fade_out_duration(0.4);
	const ObjectID id = screen->get_instance_id();
	SceneTree::get_singleton()->get_root()->add_child(screen);

	ERR_PRINT_OFF;
	SceneTree::get_singleton()->process(0.1); // Starts; the zero-length intro falls back.
	SceneTree::get_singleton()->process(0.2);
	CHECK(screen->get_fade() == doctest::Approx(0.5)); // The built-in fade-in (0.4 s), not an animation wait.
	SceneTree::get_singleton()->process(0.2);
	CHECK(screen->get_fade() == doctest::Approx(1.0));
	SceneTree::get_singleton()->process(0.0);
	SceneTree::get_singleton()->process(0.2); // The looping outro falls back too.
	ERR_PRINT_ON;
	CHECK(screen->get_fade() == doctest::Approx(0.5));
	SceneTree::get_singleton()->process(0.2);
	SceneTree::get_singleton()->process(0.0);
	CHECK_FALSE(_is_alive(id));
}

TEST_CASE("[SceneTree][TransitionScreen] A missing animation falls back to the built-in fade; mixed phases work") {
	TransitionScreen *screen = memnew(TransitionScreen);
	_add_player(screen, 1.0, 0.5);
	screen->set_intro_animation("missing");
	screen->set_outro_animation("outro");
	screen->set_fade_in_duration(0.4);
	screen->set_fade_out_duration(10.0);
	const ObjectID id = screen->get_instance_id();
	SceneTree::get_singleton()->get_root()->add_child(screen);

	ERR_PRINT_OFF;
	SceneTree::get_singleton()->process(0.1);
	ERR_PRINT_ON;
	SceneTree::get_singleton()->process(0.2);
	CHECK(screen->get_fade() == doctest::Approx(0.5)); // Built-in fade-in over 0.4 s.
	SceneTree::get_singleton()->process(0.2);
	CHECK(screen->get_fade() == doctest::Approx(1.0));
	SceneTree::get_singleton()->process(0.0);
	SceneTree::get_singleton()->process(0.25);
	CHECK(screen->get_fade() == doctest::Approx(0.5)); // Outro animation (0.5 s), not the 10 s fade.
	SceneTree::get_singleton()->process(0.25);
	SceneTree::get_singleton()->process(0.0);
	CHECK_FALSE(_is_alive(id));

	// Built-in outro after an animated intro starts opaque.
	TransitionScreen *second = memnew(TransitionScreen);
	_add_player(second, 0.2, 0.2);
	second->set_intro_animation("intro");
	second->set_fade_out_duration(0.4);
	const ObjectID second_id = second->get_instance_id();
	SceneTree::get_singleton()->get_root()->add_child(second);
	SceneTree::get_singleton()->process(0.1);
	SceneTree::get_singleton()->process(0.2);
	SceneTree::get_singleton()->process(0.0);
	CHECK(second->get_state() == TransitionScreen::STATE_FADING_OUT);
	CHECK(second->get_fade() == doctest::Approx(1.0));
	SceneTree::get_singleton()->process(0.2);
	CHECK(second->get_fade() == doctest::Approx(0.5));
	_run_until_freed(second_id);
	CHECK_FALSE(_is_alive(second_id));
}

TEST_CASE("[SceneTree][TransitionScreen] Progress covers the scene and the preload assets") {
	SceneTree *tree = SceneTree::get_singleton();
	const String path = _save_scene("progress_next.tscn", "ProgressNext");
	ResourceDatabase *database = ResourceDatabase::get_singleton();
	REQUIRE(database != nullptr);
	database->clear();

	LoadingScreen *screen = memnew(LoadingScreen);
	Label *counter = memnew(Label);
	counter->set_name("Count");
	screen->add_child(counter);
	screen->set_count_label(NodePath("Count"));
	screen->set_next_scene(path);
	screen->set_fade_in_duration(0.0);
	screen->set_auto_finish(false);
	Ref<Preloads> preloads;
	preloads.instantiate();
	const String asset_path = _save_scene("progress_asset.tscn", "ProgressAsset");
	preloads->set_paths(PackedStringArray({ asset_path }));
	screen->set_preload_assets(preloads);
	tree->get_root()->add_child(screen);
	CHECK(counter->get_text() == ""); // Hidden until the total is known (never "0 / 0").

	float last_progress = 0.0f;
	for (int frame = 0; frame < 2000 && screen->get_state() < TransitionScreen::STATE_LOADED; frame++) {
		tree->process(0.0);
		CHECK(screen->get_progress() >= last_progress); // Never goes back.
		CHECK(screen->get_progress() <= 1.0f);
		last_progress = screen->get_progress();
		OS::get_singleton()->delay_usec(500);
	}
	REQUIRE(screen->get_state() == TransitionScreen::STATE_LOADED);
	CHECK(screen->get_progress() == doctest::Approx(1.0));
	CHECK(database->has_resource(asset_path));
	const Ref<LoadProgress> load_progress = screen->get_load_progress();
	CHECK(load_progress->get_total_count() == 2); // The scene and one asset file.
	CHECK(load_progress->get_loaded_count() == 2);
	CHECK(counter->get_text() == "2 / 2");

	memdelete(screen);
	database->clear();
}

TEST_CASE("[SceneTree][TransitionScreen] Ready-made templates are complete scenes") {
	const Ref<PackedScene> splash_scene = TransitionScreen::create_template(TransitionScreen::TEMPLATE_SPLASH);
	REQUIRE(splash_scene.is_valid());
	SplashScreen *splash = Object::cast_to<SplashScreen>(splash_scene->instantiate());
	REQUIRE(splash != nullptr);
	CHECK(splash->get_node_or_null(NodePath("Background")) != nullptr);
	CHECK(splash->get_node_or_null(NodePath("Content/Column")) != nullptr);
	AnimationPlayer *splash_player = Object::cast_to<AnimationPlayer>(splash->get_node_or_null(splash->get_animation_player()));
	REQUIRE(splash_player != nullptr);
	CHECK(splash_player->has_animation(splash->get_intro_animation()));
	CHECK(splash_player->has_animation(splash->get_outro_animation()));
	memdelete(splash);

	const Ref<PackedScene> loading_scene = TransitionScreen::create_template(TransitionScreen::TEMPLATE_LOADING);
	REQUIRE(loading_scene.is_valid());
	LoadingScreen *loading = Object::cast_to<LoadingScreen>(loading_scene->instantiate());
	REQUIRE(loading != nullptr);
	CHECK(Object::cast_to<ProgressBar>(loading->get_node_or_null(loading->get_progress_bar())) != nullptr);
	CHECK(Object::cast_to<Label>(loading->get_node_or_null(loading->get_tip_label())) != nullptr);
	CHECK(Object::cast_to<Label>(loading->get_node_or_null(loading->get_count_label())) != nullptr);
	CHECK(loading->get_tips().size() > 0);
	CHECK(loading->get_animation_player() == NodePath("AnimationPlayer"));
	memdelete(loading);

	ERR_PRINT_OFF;
	CHECK(TransitionScreen::create_template(TransitionScreen::TEMPLATE_MAX).is_null());
	ERR_PRINT_ON;
}

} // namespace TestTransitionScreen
