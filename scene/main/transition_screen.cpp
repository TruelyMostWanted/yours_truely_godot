/**************************************************************************/
/*  transition_screen.cpp                                                 */
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

#include "transition_screen.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/preloads.h"
#include "core/io/resource_database.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/range.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "servers/rendering/rendering_server.h"

// TransitionScreen

void TransitionScreen::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return; // Shown fully visible and idle, so it can be designed like any scene.
			}
			_set_fade(0.0f);
			set_process_internal(true);
			set_process_input(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_process_transition(get_process_delta_time());
		} break;
		case NOTIFICATION_PREDELETE: {
			if (load_requested) {
				ResourceLoader::load_threaded_get(loading_path); // Releases the load token (waits for a running load).
			}
			if (running_screen == get_instance_id()) {
				running_screen = ObjectID();
			}
		} break;
	}
}

void TransitionScreen::input(const Ref<InputEvent> &p_event) {
	if (state == STATE_IDLE || state >= STATE_FADING_OUT || p_event.is_null()) {
		return;
	}
	// The covered scene gets no input while the screen hides it.
	Node::get_viewport()->set_input_as_handled();
	if (!skippable || finish_requested || !p_event->is_pressed() || p_event->is_echo()) {
		return;
	}
	const Ref<InputEventMouseButton> mouse_button = p_event;
	if (mouse_button.is_valid() && mouse_button->get_button_index() >= MouseButton::WHEEL_UP && mouse_button->get_button_index() <= MouseButton::WHEEL_RIGHT) {
		return; // Scrolling is not a skip.
	}
	emit_signal(SNAME("skipped"));
	finish();
}

TransitionScreen *TransitionScreen::_get_running_screen() {
	TransitionScreen *screen = Object::cast_to<TransitionScreen>(ObjectDB::get_instance(running_screen));
	return screen && screen->state < STATE_COMPLETED ? screen : nullptr;
}

String TransitionScreen::_get_next_scene_path() const {
	return next_scene.is_empty() ? String() : ResourceUID::ensure_path(next_scene);
}

void TransitionScreen::_start() {
	const TransitionScreen *running = _get_running_screen();
	if (running && running != this) {
		ERR_PRINT("TransitionScreen: Another scene transition is running; this one is ignored.");
		set_process_internal(false);
		queue_free();
		return;
	}
	running_screen = get_instance_id();
	Node *current = get_tree()->get_current_scene();
	if (current && current != this && current->is_ancestor_of(this)) {
		// Inside the current scene it would be removed with it: run from the root (starts on the next frame).
		call_deferred(SNAME("reparent"), Variant(get_tree()->get_root().ptr()), false);
		return;
	}
	state = STATE_FADING_IN;
	phase_time = 0.0;
	loading_path = _get_next_scene_path();
	if (!loading_path.is_empty() && loading_path == get_scene_file_path()) {
		loading_path = String(); // Never load the screen's own scene again (a splash set as main scene).
	}
	if (loading_path.is_empty() && current == this && !Object::cast_to<SplashScreen>(this)) {
		WARN_PRINT("TransitionScreen: The current scene is a transition screen without a next scene; no scene follows it.");
	}
	animation_length = _play_animation(intro_animation);
	emit_signal(SNAME("started"));
#ifdef THREADS_ENABLED
	if (!free_current_scene_before_loading) {
		_request_loading(); // Loads on worker threads while the screen fades in.
		return;
	}
#endif
	// Without threads, `load_threaded_request()` loads on the calling thread: it waits for the opaque screen.
	request_pending = true;
}

void TransitionScreen::_process_transition(double p_delta) {
	if (state == STATE_IDLE) {
		_start();
		p_delta = 0.0;
	}

	if (state == STATE_FADING_IN) {
		phase_time += p_delta;
		const double duration = animation_length >= 0.0 ? animation_length : fade_in_duration;
		_set_fade(duration > 0.0 ? float(MIN(phase_time / duration, 1.0)) : 1.0f);
		if (fade < 1.0f) {
			return;
		}
		intro_animated = animation_length >= 0.0;
		animation_length = -1.0;
		state = STATE_LOADING;
		phase_time = 0.0;
		p_delta = 0.0;
		if (free_current_scene_before_loading) {
			Node *current = get_tree()->get_current_scene();
			if (current && current != this) {
				// Removing clears `SceneTree.current_scene`; the node is freed at the end of the frame.
				current->get_parent()->remove_child(current);
				current->queue_free();
			}
		}
		if (request_pending) {
			return; // This fully visible frame is drawn before loading starts.
		}
	}

	if (state == STATE_LOADING || state == STATE_LOADED) {
		phase_time += p_delta;
		if (state == STATE_LOADING) {
			if (request_pending) {
				request_pending = false;
				_request_loading();
			}
			_poll_loading();
		}
		if (state == STATE_LOADING) {
			return;
		}
		if (state == STATE_LOADED) {
			if (!finish_requested && !(auto_finish && phase_time >= min_duration)) {
				return;
			}
			_change_scene();
			state = STATE_FADING_OUT;
		}
		phase_time = 0.0; // Fading out: after the change, or after a failed load.
		p_delta = 0.0;
	}

	if (state == STATE_FADING_OUT) {
		if (!outro_started) {
			outro_started = true;
			animation_length = _play_animation(outro_animation);
			if (animation_length < 0.0 && intro_animated) {
				_set_fade(1.0f); // The intro left the canvas untouched: the built-in fade starts opaque.
			}
		}
		phase_time += p_delta;
		const double duration = animation_length >= 0.0 ? animation_length : fade_out_duration;
		_set_fade(duration > 0.0 ? float(1.0 - MIN(phase_time / duration, 1.0)) : 0.0f);
		if (fade > 0.0f) {
			return;
		}
		state = STATE_COMPLETED;
		set_process_internal(false);
		set_process_input(false);
		emit_signal(SNAME("finished"));
		queue_free();
	}
}

void TransitionScreen::_set_fade(float p_fade) {
	fade = p_fade;
	if (animation_length >= 0.0) {
		return; // The animation drives the visuals; the canvas stays opaque.
	}
	if (!GDVIRTUAL_CALL(_update_fade, fade)) {
		// The layer's own canvas: every child fades together, their own `modulate` stays untouched.
		RenderingServer::get_singleton()->canvas_set_modulate(get_canvas(), Color(1, 1, 1, fade));
	}
}

void TransitionScreen::_set_progress(float p_progress) {
	if (p_progress == progress) {
		return;
	}
	progress = p_progress;
	_progress_changed(progress);
	emit_signal(SNAME("progress_changed"), progress);
}

void TransitionScreen::_update_progress() {
	float sum = 0.0f;
	int parts = 0;
	if (!loading_path.is_empty()) {
		sum += scene_progress;
		parts++;
	}
	if (preload_tracked) {
		sum += assets_progress;
		parts++;
	}
	_set_progress(parts > 0 ? MIN(sum / float(parts), 0.99f) : 0.0f); // 1.0 only when everything is loaded.
}

double TransitionScreen::_play_animation(const StringName &p_animation) {
	if (p_animation == StringName()) {
		return -1.0;
	}
	AnimationPlayer *player = Object::cast_to<AnimationPlayer>(get_node_or_null(animation_player));
	if (!player || !player->has_animation(p_animation)) {
		WARN_PRINT(vformat("TransitionScreen: The animation '%s' is not found in the animation player; the built-in fade runs instead.", String(p_animation)));
		return -1.0;
	}
	// The animation covers the screen itself: no canvas fade.
	RenderingServer::get_singleton()->canvas_set_modulate(get_canvas(), Color(1, 1, 1, 1));
	player->play(p_animation);
	player->advance(0.0); // The first frame is applied before the screen is drawn.
	const double speed = Math::abs(double(player->get_speed_scale()));
	const double length = player->get_animation(p_animation)->get_length();
	return speed > CMP_EPSILON ? length / speed : length;
}

void TransitionScreen::_request_loading() {
	if (preload_assets.is_valid() && ResourceDatabase::get_singleton()) {
		preload_pending = true;
		preload_tracked = true;
		assets_steps = preload_assets->get_paths();
		const TypedArray<FilesManifest> manifests = preload_assets->get_manifests();
		for (int idx = 0; idx < manifests.size(); idx++) {
			const Ref<FilesManifest> manifest = manifests[idx];
			if (manifest.is_valid()) {
				assets_steps.append_array(manifest->get_paths());
			}
		}
		assets_total = assets_steps.size();
#ifdef THREADS_ENABLED
		assets_steps.clear();
		ResourceDatabase *database = ResourceDatabase::get_singleton();
		const Callable on_finished = callable_mp(this, &TransitionScreen::_preload_finished);
		database->connect(SNAME("preload_finished"), on_finished);
		database->connect(SNAME("preload_progress"), callable_mp(this, &TransitionScreen::_preload_progress));
		const Error preload_error = database->preload_from(preload_assets);
		if (preload_error == ERR_INVALID_PARAMETER || preload_error == ERR_UNAVAILABLE) {
			// Rejected before starting: `preload_finished` will not be emitted.
			preload_pending = false;
			preload_tracked = false;
			database->disconnect(SNAME("preload_finished"), on_finished);
			database->disconnect(SNAME("preload_progress"), callable_mp(this, &TransitionScreen::_preload_progress));
		}
#endif
	}
	if (loading_path.is_empty()) {
		return;
	}
	load_error = ResourceLoader::load_threaded_request(loading_path, "PackedScene");
	load_requested = load_error == OK;
}

void TransitionScreen::_poll_loading() {
	if (load_error != OK) {
		_fail_loading(load_error);
		return;
	}
	if (!loading_path.is_empty() && loaded_scene.is_null()) {
#ifdef THREADS_ENABLED
		float load_progress = 0.0f;
		if (ResourceLoader::load_threaded_get_status(loading_path, &load_progress) == ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			scene_progress = load_progress;
			_update_progress();
			return;
		}
#endif
		Error err = OK;
		loaded_scene = ResourceLoader::load_threaded_get(loading_path, &err);
		load_requested = false;
		scene_progress = 1.0f;
		if (loaded_scene.is_null()) {
			_fail_loading(err != OK ? err : ERR_FILE_UNRECOGNIZED);
			return;
		}
	}
#ifndef THREADS_ENABLED
	if (preload_pending) {
		_step_assets();
	}
#endif
	if (preload_pending) {
		_update_progress();
		return;
	}
	_set_progress(1.0f);
	state = STATE_LOADED;
	emit_signal(SNAME("loaded"));
}

void TransitionScreen::_fail_loading(Error p_error) {
	ERR_PRINT(vformat("TransitionScreen: Can't load the next scene '%s' (error %d). The scene does not change.", loading_path, int(p_error)));
	load_error = p_error;
	loading_path = String();
	state = STATE_FADING_OUT;
	emit_signal(SNAME("load_failed"), int(p_error));
}

void TransitionScreen::_preload_progress(const Ref<Preloads> &p_preloads, const Ref<LoadProgress> &p_progress) {
	if (p_preloads == preload_assets && p_progress.is_valid()) {
		assets_progress = p_progress->get_ratio();
		assets_loaded = p_progress->get_loaded_count();
		assets_total = p_progress->get_total_count();
	}
}

// Without threads the assets load one file per frame, so the progress advances while the screen is drawn.
void TransitionScreen::_step_assets() {
	ResourceDatabase *database = ResourceDatabase::get_singleton();
	if (assets_step_index < assets_steps.size()) {
		const String &path = assets_steps[assets_step_index];
		if (!path.is_empty()) {
			database->preload_resource(path);
		}
		assets_step_index++;
		assets_loaded = assets_step_index;
		assets_progress = float(assets_step_index) / float(assets_steps.size());
	}
	if (assets_step_index >= assets_steps.size()) {
		// The direct resources and keys of the preloads; the files are stored already.
		Ref<Preloads> rest;
		rest.instantiate();
		rest->set_resources(preload_assets->get_resources());
		rest->set_keys(preload_assets->get_keys());
		database->preload_from(rest, false);
		preload_pending = false;
		assets_progress = 1.0f;
	}
}

void TransitionScreen::_preload_finished(const Ref<Preloads> &p_preloads, int p_error) {
	if (p_preloads != preload_assets) {
		return;
	}
	preload_pending = false;
	assets_progress = 1.0f;
	assets_loaded = assets_total;
	ResourceDatabase *database = ResourceDatabase::get_singleton();
	const Callable on_finished = callable_mp(this, &TransitionScreen::_preload_finished);
	if (database && database->is_connected(SNAME("preload_finished"), on_finished)) {
		database->disconnect(SNAME("preload_finished"), on_finished);
	}
	const Callable on_progress = callable_mp(this, &TransitionScreen::_preload_progress);
	if (database && database->is_connected(SNAME("preload_progress"), on_progress)) {
		database->disconnect(SNAME("preload_progress"), on_progress);
	}
}

void TransitionScreen::_change_scene() {
	if (loaded_scene.is_null()) {
		return;
	}
	SceneTree *tree = get_tree();
	if (tree->get_current_scene() == this) {
		// Opened as the current scene (boot splash, `change_scene_to_file()`): stay in the tree for the fade-out.
		tree->set_current_scene(nullptr);
	}
	const Error err = tree->change_scene_to_packed(loaded_scene);
	loaded_scene.unref();
	if (err != OK) {
		load_error = err;
		emit_signal(SNAME("load_failed"), int(err));
	}
}

Error TransitionScreen::change_scene_with_transition(SceneTree *p_tree, const String &p_path, const Ref<PackedScene> &p_transition_scene) {
	ERR_FAIL_NULL_V(p_tree, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V_MSG(p_path.is_empty(), ERR_INVALID_PARAMETER, "The path of the next scene is empty.");
	ERR_FAIL_COND_V_MSG(_get_running_screen(), ERR_BUSY, "A scene transition is already running.");
	ERR_FAIL_COND_V_MSG(!ResourceLoader::exists(ResourceUID::ensure_path(p_path)), ERR_CANT_OPEN, vformat("The next scene '%s' does not exist.", p_path));

	TransitionScreen *screen = nullptr;
	if (p_transition_scene.is_valid()) {
		Node *node = p_transition_scene->instantiate();
		ERR_FAIL_NULL_V(node, ERR_CANT_CREATE);
		screen = Object::cast_to<TransitionScreen>(node);
		if (!screen) {
			memdelete(node);
			ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "The root node of the transition scene must be a TransitionScreen, LoadingScreen or SplashScreen.");
		}
	} else {
		// Built-in black fade.
		screen = memnew(TransitionScreen);
		ColorRect *background = memnew(ColorRect);
		background->set_color(Color(0, 0, 0));
		background->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		screen->add_child(background);
	}
	screen->set_next_scene(p_path);
	running_screen = screen->get_instance_id(); // Busy from now on, also before the screen enters the tree.
	// Deferred: also works when called from `_ready()` while the root is busy adding children.
	p_tree->get_root()->call_deferred(SNAME("add_child"), screen);
	return OK;
}

void TransitionScreen::set_next_scene(const String &p_path) {
	next_scene = p_path;
}

String TransitionScreen::get_next_scene() const {
	return next_scene;
}

void TransitionScreen::set_min_duration(double p_seconds) {
	min_duration = MAX(p_seconds, 0.0);
}

double TransitionScreen::get_min_duration() const {
	return min_duration;
}

void TransitionScreen::set_fade_in_duration(double p_seconds) {
	fade_in_duration = MAX(p_seconds, 0.0);
}

double TransitionScreen::get_fade_in_duration() const {
	return fade_in_duration;
}

void TransitionScreen::set_fade_out_duration(double p_seconds) {
	fade_out_duration = MAX(p_seconds, 0.0);
}

double TransitionScreen::get_fade_out_duration() const {
	return fade_out_duration;
}

void TransitionScreen::set_skippable(bool p_skippable) {
	skippable = p_skippable;
}

bool TransitionScreen::is_skippable() const {
	return skippable;
}

void TransitionScreen::set_auto_finish(bool p_auto_finish) {
	auto_finish = p_auto_finish;
}

bool TransitionScreen::is_auto_finish() const {
	return auto_finish;
}

void TransitionScreen::set_free_current_scene_before_loading(bool p_free) {
	free_current_scene_before_loading = p_free;
}

bool TransitionScreen::is_free_current_scene_before_loading() const {
	return free_current_scene_before_loading;
}

void TransitionScreen::set_preload_assets(const Ref<Preloads> &p_assets) {
	preload_assets = p_assets;
}

Ref<Preloads> TransitionScreen::get_preload_assets() const {
	return preload_assets;
}

void TransitionScreen::set_animation_player(const NodePath &p_path) {
	animation_player = p_path;
	notify_property_list_changed(); // The animation names offered in the inspector depend on it.
}

NodePath TransitionScreen::get_animation_player() const {
	return animation_player;
}

void TransitionScreen::set_intro_animation(const StringName &p_animation) {
	intro_animation = p_animation;
}

StringName TransitionScreen::get_intro_animation() const {
	return intro_animation;
}

void TransitionScreen::set_outro_animation(const StringName &p_animation) {
	outro_animation = p_animation;
}

StringName TransitionScreen::get_outro_animation() const {
	return outro_animation;
}

void TransitionScreen::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name != "intro_animation" && p_property.name != "outro_animation") {
		return;
	}
	// Offer the animations of the player as suggestions.
	const AnimationPlayer *player = Object::cast_to<AnimationPlayer>(get_node_or_null(animation_player));
	if (!player) {
		return;
	}
	LocalVector<StringName> names;
	player->get_animation_list(&names);
	String hint;
	for (const StringName &animation_name : names) {
		hint += (hint.is_empty() ? "" : ",") + String(animation_name);
	}
	p_property.hint = PROPERTY_HINT_ENUM_SUGGESTION;
	p_property.hint_string = hint;
}

void TransitionScreen::finish() {
	finish_requested = true;
}

TransitionScreen::State TransitionScreen::get_state() const {
	return state;
}

float TransitionScreen::get_progress() const {
	return progress;
}

Ref<LoadProgress> TransitionScreen::get_load_progress() const {
	// The scene counts as one file next to the files of the assets.
	const bool has_scene = !loading_path.is_empty() || loaded_scene.is_valid() || scene_progress >= 1.0f;
	const int total = (has_scene ? 1 : 0) + (preload_tracked ? assets_total : 0);
	const int loaded = (has_scene && scene_progress >= 1.0f ? 1 : 0) + (preload_tracked ? assets_loaded : 0);
	return LoadProgress::create(loaded, total, progress);
}

float TransitionScreen::get_fade() const {
	return fade;
}

Error TransitionScreen::get_load_error() const {
	return load_error;
}

void TransitionScreen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_next_scene", "path"), &TransitionScreen::set_next_scene);
	ClassDB::bind_method(D_METHOD("get_next_scene"), &TransitionScreen::get_next_scene);
	ClassDB::bind_method(D_METHOD("set_min_duration", "seconds"), &TransitionScreen::set_min_duration);
	ClassDB::bind_method(D_METHOD("get_min_duration"), &TransitionScreen::get_min_duration);
	ClassDB::bind_method(D_METHOD("set_fade_in_duration", "seconds"), &TransitionScreen::set_fade_in_duration);
	ClassDB::bind_method(D_METHOD("get_fade_in_duration"), &TransitionScreen::get_fade_in_duration);
	ClassDB::bind_method(D_METHOD("set_fade_out_duration", "seconds"), &TransitionScreen::set_fade_out_duration);
	ClassDB::bind_method(D_METHOD("get_fade_out_duration"), &TransitionScreen::get_fade_out_duration);
	ClassDB::bind_method(D_METHOD("set_skippable", "skippable"), &TransitionScreen::set_skippable);
	ClassDB::bind_method(D_METHOD("is_skippable"), &TransitionScreen::is_skippable);
	ClassDB::bind_method(D_METHOD("set_auto_finish", "auto_finish"), &TransitionScreen::set_auto_finish);
	ClassDB::bind_method(D_METHOD("is_auto_finish"), &TransitionScreen::is_auto_finish);
	ClassDB::bind_method(D_METHOD("set_free_current_scene_before_loading", "enabled"), &TransitionScreen::set_free_current_scene_before_loading);
	ClassDB::bind_method(D_METHOD("is_free_current_scene_before_loading"), &TransitionScreen::is_free_current_scene_before_loading);
	ClassDB::bind_method(D_METHOD("set_preload_assets", "assets"), &TransitionScreen::set_preload_assets);
	ClassDB::bind_method(D_METHOD("get_preload_assets"), &TransitionScreen::get_preload_assets);
	ClassDB::bind_method(D_METHOD("set_animation_player", "path"), &TransitionScreen::set_animation_player);
	ClassDB::bind_method(D_METHOD("get_animation_player"), &TransitionScreen::get_animation_player);
	ClassDB::bind_method(D_METHOD("set_intro_animation", "animation"), &TransitionScreen::set_intro_animation);
	ClassDB::bind_method(D_METHOD("get_intro_animation"), &TransitionScreen::get_intro_animation);
	ClassDB::bind_method(D_METHOD("set_outro_animation", "animation"), &TransitionScreen::set_outro_animation);
	ClassDB::bind_method(D_METHOD("get_outro_animation"), &TransitionScreen::get_outro_animation);

	ClassDB::bind_method(D_METHOD("finish"), &TransitionScreen::finish);
	ClassDB::bind_method(D_METHOD("get_state"), &TransitionScreen::get_state);
	ClassDB::bind_method(D_METHOD("get_progress"), &TransitionScreen::get_progress);
	ClassDB::bind_method(D_METHOD("get_load_progress"), &TransitionScreen::get_load_progress);
	ClassDB::bind_method(D_METHOD("get_fade"), &TransitionScreen::get_fade);
	ClassDB::bind_method(D_METHOD("get_load_error"), &TransitionScreen::get_load_error);
	ClassDB::bind_static_method("TransitionScreen", D_METHOD("create_template", "template"), &TransitionScreen::create_template);

	GDVIRTUAL_BIND(_update_fade, "fade");

	ADD_GROUP("Transition", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "next_scene", PROPERTY_HINT_FILE, "*.tscn,*.scn,*.res"), "set_next_scene", "get_next_scene");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_duration", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:s"), "set_min_duration", "get_min_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_in_duration", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_fade_in_duration", "get_fade_in_duration");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fade_out_duration", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_fade_out_duration", "get_fade_out_duration");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "skippable"), "set_skippable", "is_skippable");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_finish"), "set_auto_finish", "is_auto_finish");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "free_current_scene_before_loading"), "set_free_current_scene_before_loading", "is_free_current_scene_before_loading");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "preload_assets", PROPERTY_HINT_RESOURCE_TYPE, "Preloads"), "set_preload_assets", "get_preload_assets");

	ADD_GROUP("Animation", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "animation_player", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "AnimationPlayer"), "set_animation_player", "get_animation_player");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "intro_animation"), "set_intro_animation", "get_intro_animation");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "outro_animation"), "set_outro_animation", "get_outro_animation");

	ADD_SIGNAL(MethodInfo("started"));
	ADD_SIGNAL(MethodInfo("progress_changed", PropertyInfo(Variant::FLOAT, "progress")));
	ADD_SIGNAL(MethodInfo("loaded"));
	ADD_SIGNAL(MethodInfo("load_failed", PropertyInfo(Variant::INT, "error", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM, "Error")));
	ADD_SIGNAL(MethodInfo("skipped"));
	ADD_SIGNAL(MethodInfo("finished"));

	BIND_ENUM_CONSTANT(STATE_IDLE);
	BIND_ENUM_CONSTANT(STATE_FADING_IN);
	BIND_ENUM_CONSTANT(STATE_LOADING);
	BIND_ENUM_CONSTANT(STATE_LOADED);
	BIND_ENUM_CONSTANT(STATE_FADING_OUT);
	BIND_ENUM_CONSTANT(STATE_COMPLETED);

	BIND_ENUM_CONSTANT(TEMPLATE_SPLASH);
	BIND_ENUM_CONSTANT(TEMPLATE_LOADING);
}

TransitionScreen::TransitionScreen() {
	set_layer(100); // Above the HUD layers of the scenes it covers.
	set_process_mode(PROCESS_MODE_ALWAYS); // Also while the tree is paused.
}

// LoadingScreen

void LoadingScreen::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (Engine::get_singleton()->is_editor_hint()) {
				return;
			}
			_progress_changed(0.0f);
			_show_tip(0);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update_count(); // The total can become known without a change of the ratio.
			if (tips.size() < 2 || tip_interval <= 0.0) {
				return;
			}
			tip_time += get_process_delta_time();
			if (tip_time >= tip_interval) {
				_show_tip((tip_index + 1) % tips.size());
			}
		} break;
	}
}

void LoadingScreen::_progress_changed(float p_progress) {
	Range *bar = Object::cast_to<Range>(get_node_or_null(progress_bar));
	if (bar) {
		bar->set_as_ratio(p_progress);
	}
	_update_count();
}

void LoadingScreen::_update_count() {
	Node *counter = get_node_or_null(count_label);
	if (!counter) {
		return;
	}
	// Hidden (empty) until the total is known, so "0 / 0" never shows.
	const Ref<LoadProgress> load_progress = get_load_progress();
	String text;
	if (!count_format.is_empty() && load_progress->get_total_count() > 0) {
		text = count_format.sprintf(Array({ load_progress->get_loaded_count(), load_progress->get_total_count() }), nullptr);
	}
	if (counter->get(SNAME("text")) != Variant(text)) {
		counter->set(SNAME("text"), text);
	}
}

void LoadingScreen::_show_tip(int p_index) {
	tip_time = 0.0;
	if (tips.is_empty()) {
		return;
	}
	tip_index = p_index;
	Node *label = get_node_or_null(tip_label);
	if (label) {
		label->set(SNAME("text"), tips[tip_index]);
	}
}

void LoadingScreen::set_progress_bar(const NodePath &p_path) {
	progress_bar = p_path;
}

NodePath LoadingScreen::get_progress_bar() const {
	return progress_bar;
}

void LoadingScreen::set_tip_label(const NodePath &p_path) {
	tip_label = p_path;
}

NodePath LoadingScreen::get_tip_label() const {
	return tip_label;
}

void LoadingScreen::set_count_label(const NodePath &p_path) {
	count_label = p_path;
}

NodePath LoadingScreen::get_count_label() const {
	return count_label;
}

void LoadingScreen::set_count_format(const String &p_format) {
	count_format = p_format;
}

String LoadingScreen::get_count_format() const {
	return count_format;
}

void LoadingScreen::set_tips(const PackedStringArray &p_tips) {
	tips = p_tips;
	tip_index = -1;
	if (is_ready() && !Engine::get_singleton()->is_editor_hint()) {
		_show_tip(0);
	}
}

PackedStringArray LoadingScreen::get_tips() const {
	return tips;
}

void LoadingScreen::set_tip_interval(double p_seconds) {
	tip_interval = MAX(p_seconds, 0.0);
}

double LoadingScreen::get_tip_interval() const {
	return tip_interval;
}

String LoadingScreen::get_current_tip() const {
	return tip_index >= 0 && tip_index < tips.size() ? tips[tip_index] : String();
}

void LoadingScreen::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_progress_bar", "path"), &LoadingScreen::set_progress_bar);
	ClassDB::bind_method(D_METHOD("get_progress_bar"), &LoadingScreen::get_progress_bar);
	ClassDB::bind_method(D_METHOD("set_tip_label", "path"), &LoadingScreen::set_tip_label);
	ClassDB::bind_method(D_METHOD("get_tip_label"), &LoadingScreen::get_tip_label);
	ClassDB::bind_method(D_METHOD("set_count_label", "path"), &LoadingScreen::set_count_label);
	ClassDB::bind_method(D_METHOD("get_count_label"), &LoadingScreen::get_count_label);
	ClassDB::bind_method(D_METHOD("set_count_format", "format"), &LoadingScreen::set_count_format);
	ClassDB::bind_method(D_METHOD("get_count_format"), &LoadingScreen::get_count_format);
	ClassDB::bind_method(D_METHOD("set_tips", "tips"), &LoadingScreen::set_tips);
	ClassDB::bind_method(D_METHOD("get_tips"), &LoadingScreen::get_tips);
	ClassDB::bind_method(D_METHOD("set_tip_interval", "seconds"), &LoadingScreen::set_tip_interval);
	ClassDB::bind_method(D_METHOD("get_tip_interval"), &LoadingScreen::get_tip_interval);
	ClassDB::bind_method(D_METHOD("get_current_tip"), &LoadingScreen::get_current_tip);

	ADD_GROUP("Loading", "");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "progress_bar", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Range"), "set_progress_bar", "get_progress_bar");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "tip_label"), "set_tip_label", "get_tip_label");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "count_label"), "set_count_label", "get_count_label");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "count_format"), "set_count_format", "get_count_format");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_STRING_ARRAY, "tips"), "set_tips", "get_tips");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tip_interval", PROPERTY_HINT_RANGE, "0,30,0.1,or_greater,suffix:s"), "set_tip_interval", "get_tip_interval");
}

// SplashScreen

String SplashScreen::_get_next_scene_path() const {
	const String path = TransitionScreen::_get_next_scene_path();
	if (!path.is_empty()) {
		return path;
	}
	const String main_scene = GLOBAL_GET("application/run/main_scene");
	return main_scene.is_empty() ? String() : ResourceUID::ensure_path(main_scene);
}

SplashScreen::SplashScreen() {
	set_min_duration(2.0);
	set_fade_in_duration(0.0); // Follows the boot image without a black frame.
	set_skippable(true);
}
