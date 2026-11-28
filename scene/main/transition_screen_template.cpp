/**************************************************************************/
/*  transition_screen_template.cpp                                        */
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

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "scene/animation/animation_player.h"
#include "scene/gui/box_container.h"
#include "scene/gui/center_container.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/label.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/transition_screen.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/texture.h"

// Ready-made splash and loading screen scenes (#440). The editor saves them as `.tscn` files to start from; the
// builder lives here (not in the editor) so scripts can create them too.

static void _add_fade_track(const Ref<Animation> &p_animation, const NodePath &p_property, double p_from_time, double p_to_time, float p_from_alpha, float p_to_alpha) {
	const int track = p_animation->add_track(Animation::TYPE_VALUE);
	p_animation->track_set_path(track, p_property);
	p_animation->track_insert_key(track, p_from_time, Color(1, 1, 1, p_from_alpha));
	p_animation->track_insert_key(track, p_to_time, Color(1, 1, 1, p_to_alpha));
}

// The animation covers (intro) or uncovers (outro) the screen itself: the background fades in first and out last.
static Ref<Animation> _make_fade_animation(bool p_intro, double p_length) {
	Ref<Animation> animation;
	animation.instantiate();
	animation->set_length(p_length);
	const double half = p_length * 0.5;
	if (p_intro) {
		_add_fade_track(animation, NodePath("Background:modulate"), 0.0, half, 0.0f, 1.0f);
		_add_fade_track(animation, NodePath("Content:modulate"), half, p_length, 0.0f, 1.0f);
	} else {
		_add_fade_track(animation, NodePath("Content:modulate"), 0.0, half, 1.0f, 0.0f);
		_add_fade_track(animation, NodePath("Background:modulate"), half, p_length, 1.0f, 0.0f);
	}
	return animation;
}

// Readable on the background: dark text on a light color, light text on a dark one.
static void _style_label(Label *p_label, const Color &p_background, int p_font_size = 0) {
	p_label->add_theme_color_override(SNAME("font_color"), p_background.get_luminance() < 0.5f ? Color(0.9f, 0.9f, 0.9f) : Color(0.1f, 0.1f, 0.1f));
	if (p_font_size > 0) {
		p_label->add_theme_font_size_override(SNAME("font_size"), p_font_size);
	}
}

static Ref<Texture2D> _get_logo_texture() {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	const char *const image_settings[2] = { "application/boot_splash/image", "application/config/icon" };
	for (const char *image_setting : image_settings) {
		const String path = settings->get_setting(image_setting, String());
		if (!path.is_empty() && ResourceLoader::exists(path)) {
			const Ref<Texture2D> texture = ResourceLoader::load(path);
			if (texture.is_valid()) {
				return texture;
			}
		}
	}
	return Ref<Texture2D>();
}

Ref<PackedScene> TransitionScreen::create_template(Template p_template) {
	ERR_FAIL_INDEX_V(int(p_template), int(TEMPLATE_MAX), Ref<PackedScene>());
	const bool loading = p_template == TEMPLATE_LOADING;
	ProjectSettings *settings = ProjectSettings::get_singleton();
	TransitionScreen *root = loading ? static_cast<TransitionScreen *>(memnew(LoadingScreen)) : static_cast<TransitionScreen *>(memnew(SplashScreen));
	root->set_name(loading ? "LoadingScreen" : "SplashScreen");
	root->set_animation_player(NodePath("AnimationPlayer"));
	root->set_intro_animation("intro");
	root->set_outro_animation("outro");

	ColorRect *background = memnew(ColorRect);
	background->set_name("Background");
	const Color background_color = settings->get_setting("application/boot_splash/bg_color", Color(0.141f, 0.141f, 0.141f));
	background->set_color(background_color);
	root->add_child(background);
	background->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	background->set_owner(root);

	CenterContainer *content = memnew(CenterContainer);
	content->set_name("Content");
	content->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	root->add_child(content);
	content->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	content->set_owner(root);

	VBoxContainer *column = memnew(VBoxContainer);
	column->set_name("Column");
	column->set_alignment(BoxContainer::ALIGNMENT_CENTER);
	content->add_child(column);
	column->set_owner(root);

	// The logo of the project, or its name when it has no image.
	const Ref<Texture2D> logo_texture = _get_logo_texture();
	if (logo_texture.is_valid()) {
		TextureRect *logo = memnew(TextureRect);
		logo->set_name("Logo");
		logo->set_texture(logo_texture);
		logo->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
		logo->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
		logo->set_custom_minimum_size(loading ? Size2(128, 128) : Size2(256, 256));
		column->add_child(logo);
		logo->set_owner(root);
	} else {
		Label *title = memnew(Label);
		title->set_name("Title");
		title->set_text(String(settings->get_setting("application/config/name", String())));
		title->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		_style_label(title, background_color, loading ? 24 : 48);
		column->add_child(title);
		title->set_owner(root);
	}

	if (loading) {
		LoadingScreen *screen = static_cast<LoadingScreen *>(root);
		Label *tip = memnew(Label);
		tip->set_name("Tip");
		tip->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		tip->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
		tip->set_custom_minimum_size(Size2(400, 0));
		_style_label(tip, background_color);
		column->add_child(tip);
		tip->set_owner(root);

		ProgressBar *bar = memnew(ProgressBar);
		bar->set_name("ProgressBar");
		bar->set_custom_minimum_size(Size2(400, 24));
		bar->set_show_percentage(false);
		column->add_child(bar);
		bar->set_owner(root);

		Label *count = memnew(Label);
		count->set_name("Count");
		count->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
		_style_label(count, background_color);
		column->add_child(count);
		count->set_owner(root);

		screen->set_progress_bar(NodePath("Content/Column/ProgressBar"));
		screen->set_tip_label(NodePath("Content/Column/Tip"));
		screen->set_count_label(NodePath("Content/Column/Count"));
		screen->set_tips(PackedStringArray({ "Edit this scene to change the look of the loading screen.", "Set your own tips in the Tips property of the root node." }));
		screen->set_min_duration(0.5);
	}

	AnimationPlayer *player = memnew(AnimationPlayer);
	player->set_name("AnimationPlayer");
	root->add_child(player);
	player->set_owner(root);
	Ref<AnimationLibrary> library;
	library.instantiate();
	library->add_animation("intro", _make_fade_animation(true, 0.6));
	library->add_animation("outro", _make_fade_animation(false, 0.6));
	player->add_animation_library(StringName(), library);

	Ref<PackedScene> scene;
	scene.instantiate();
	const Error err = scene->pack(root);
	memdelete(root);
	ERR_FAIL_COND_V(err != OK, Ref<PackedScene>());
	return scene;
}
