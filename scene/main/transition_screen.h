/**************************************************************************/
/*  transition_screen.h                                                   */
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
#include "core/io/resource_database.h"
#include "core/variant/binder_common.h"
#include "scene/main/canvas_layer.h"
#include "scene/resources/packed_scene.h"

class Preloads;
class SceneTree;

// Scene transitions (#399). A transition screen is the root of a scene that covers the screen while the next
// scene loads: it fades in over the current scene, loads `next_scene` with `ResourceLoader::load_threaded_request()`,
// waits for loading and `min_duration`, changes the scene while it is fully visible, fades out over the new scene
// and frees itself. One transition runs at a time; it lives under the tree root. Main thread only.
class TransitionScreen : public CanvasLayer {
	GDCLASS(TransitionScreen, CanvasLayer);

public:
	enum State {
		STATE_IDLE,
		STATE_FADING_IN,
		STATE_LOADING,
		STATE_LOADED,
		STATE_FADING_OUT,
		STATE_COMPLETED,
	};

	enum Template {
		TEMPLATE_SPLASH,
		TEMPLATE_LOADING,
	};

private:
	String next_scene;
	double min_duration = 0.0;
	double fade_in_duration = 0.3;
	double fade_out_duration = 0.3;
	bool skippable = false;
	bool auto_finish = true;
	bool free_current_scene_before_loading = false;
	Ref<Preloads> preload_assets;
	NodePath animation_player;
	StringName intro_animation;
	StringName outro_animation;

	State state = STATE_IDLE;
	String loading_path; // Resolved `next_scene`; empty when no scene change happens.
	bool preload_pending = false;
	bool preload_tracked = false; // The asset progress counts into `progress`.
	float scene_progress = 0.0f;
	float assets_progress = 0.0f;
	int assets_loaded = 0;
	int assets_total = 0;
#ifndef THREADS_ENABLED
	ResourceDatabase::PreloadSteps assets_steps; // Without threads: the files to load, one per frame.
#endif
	bool animation_done = false; // The animation player reported the end of the animation of the current phase.
	double animation_length = -1.0; // Seconds of the animation that drives the current fade; negative: built-in fade.
	bool intro_animated = false;
	bool outro_started = false;
	bool request_pending = false; // Loading starts on the frame after the screen became fully visible.
	bool load_requested = false; // A threaded load token is held until `load_threaded_get()`.
	Error load_error = OK;
	bool finish_requested = false;
	Ref<PackedScene> loaded_scene;
	double phase_time = 0.0; // Time in the current fade, or time fully visible.
	float progress = 0.0f;
	float fade = 0.0f;

	void _start();
	void _process_transition(double p_delta);
	void _set_fade(float p_fade);
	void _set_progress(float p_progress);
	void _update_progress();
	double _play_animation(const StringName &p_animation);
	void _animation_finished(const StringName &p_animation);
	double _get_phase_ratio(double p_duration) const;
	void _request_loading();
	void _poll_loading();
	void _fail_loading(Error p_error);
	void _change_scene();
	void _preload_progress(const Ref<Preloads> &p_preloads, const Ref<LoadProgress> &p_progress);
#ifndef THREADS_ENABLED
	void _step_assets();
#endif
	void _preload_finished(const Ref<Preloads> &p_preloads, int p_error);

	static inline ObjectID running_screen; // The transition in progress (one at a time).
	static TransitionScreen *_get_running_screen();

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();
	virtual void input(const Ref<InputEvent> &p_event) override;

	// The scene to change to; `SplashScreen` falls back to the project main scene.
	virtual String _get_next_scene_path() const;
	virtual void _progress_changed(float p_progress) {}

	GDVIRTUAL1(_update_fade, float)

public:
	void set_next_scene(const String &p_path);
	String get_next_scene() const;
	void set_min_duration(double p_seconds);
	double get_min_duration() const;
	void set_fade_in_duration(double p_seconds);
	double get_fade_in_duration() const;
	void set_fade_out_duration(double p_seconds);
	double get_fade_out_duration() const;
	void set_skippable(bool p_skippable);
	bool is_skippable() const;
	void set_auto_finish(bool p_auto_finish);
	bool is_auto_finish() const;
	void set_free_current_scene_before_loading(bool p_free);
	bool is_free_current_scene_before_loading() const;
	void set_preload_assets(const Ref<Preloads> &p_assets);
	Ref<Preloads> get_preload_assets() const;
	void set_animation_player(const NodePath &p_path);
	NodePath get_animation_player() const;
	void set_intro_animation(const StringName &p_animation);
	StringName get_intro_animation() const;
	void set_outro_animation(const StringName &p_animation);
	StringName get_outro_animation() const;

	void finish();
	State get_state() const;
	float get_progress() const;
	Ref<LoadProgress> get_load_progress() const;
	float get_fade() const;
	Error get_load_error() const;

	// A ready-made splash or loading screen scene (#440), built from the project settings (logo, background color).
	static Ref<PackedScene> create_template(Template p_template);

	// Backs `SceneTree::change_scene_with_transition()`.
	static Error change_scene_with_transition(SceneTree *p_tree, const String &p_path, const Ref<PackedScene> &p_transition_scene);

	TransitionScreen();
};

// A transition screen that shows the load progress in a `Range` (for example a `ProgressBar`) and rotates tips in
// a node with a `text` property (`Label`, `RichTextLabel`, `Button`).
class LoadingScreen : public TransitionScreen {
	GDCLASS(LoadingScreen, TransitionScreen);

	NodePath progress_bar;
	NodePath tip_label;
	NodePath count_label;
	String count_format = "%d / %d";
	PackedStringArray tips;
	double tip_interval = 5.0;

	int tip_index = -1;
	double tip_time = 0.0;

	void _show_tip(int p_index);

protected:
	void _notification(int p_what);
	static void _bind_methods();
	virtual void _progress_changed(float p_progress) override;
	void _update_count();

public:
	void set_progress_bar(const NodePath &p_path);
	NodePath get_progress_bar() const;
	void set_tip_label(const NodePath &p_path);
	NodePath get_tip_label() const;
	void set_count_label(const NodePath &p_path);
	NodePath get_count_label() const;
	void set_count_format(const String &p_format);
	String get_count_format() const;
	void set_tips(const PackedStringArray &p_tips);
	PackedStringArray get_tips() const;
	void set_tip_interval(double p_seconds);
	double get_tip_interval() const;

	String get_current_tip() const;
};

// The root of the boot splash scene (`application/boot_splash/scene`). An empty `next_scene` is the project main
// scene. Skippable and fully visible for at least two seconds by default.
class SplashScreen : public TransitionScreen {
	GDCLASS(SplashScreen, TransitionScreen);

protected:
	static void _bind_methods() {}
	virtual String _get_next_scene_path() const override;

public:
	SplashScreen();
};

VARIANT_ENUM_CAST(TransitionScreen::State);
VARIANT_ENUM_CAST(TransitionScreen::Template);
