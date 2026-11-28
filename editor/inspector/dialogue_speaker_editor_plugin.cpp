/**************************************************************************/
/*  dialogue_speaker_editor_plugin.cpp                                    */
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

#include "dialogue_speaker_editor_plugin.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_string_names.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/label.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/texture.h"
#include "scene/scene_string_names.h"
#include "servers/subtitle/dialogue_speaker.h"

void DialogueSpeakerPreview::_update() {
	if (!is_inside_tree()) {
		return;
	}
	const Ref<DialogueSpeaker> speaker = resource;
	if (speaker.is_valid()) {
		const Ref<Texture2D> texture = speaker->load_portrait();
		portrait->set_texture(texture);
		portrait->set_visible(texture.is_valid());
		const String speaker_name = speaker->get_translated_name();
		info->set_text(speaker_name.is_empty() ? TTR("No id") : speaker_name);
		info->add_theme_color_override(SceneStringName(font_color), speaker->get_color());
		return;
	}
	const Ref<DialogueSpeakers> speakers = resource;
	if (speakers.is_valid()) {
		const PackedStringArray problems = speakers->validate();
		if (problems.is_empty()) {
			info->set_text(vformat(TTR("%d speakers, ids valid."), speakers->get_ids().size()));
			info->remove_theme_color_override(SceneStringName(font_color));
		} else {
			info->set_text(String("\n").join(problems));
			info->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
		}
	}
}

void DialogueSpeakerPreview::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			resource->connect_changed(callable_mp(this, &DialogueSpeakerPreview::_update));
			_update();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			resource->disconnect_changed(callable_mp(this, &DialogueSpeakerPreview::_update));
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			_update();
		} break;
	}
}

DialogueSpeakerPreview::DialogueSpeakerPreview(const Ref<Resource> &p_resource) {
	resource = p_resource;
	portrait = memnew(TextureRect);
	portrait->set_expand_mode(TextureRect::EXPAND_IGNORE_SIZE);
	portrait->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_CENTERED);
	portrait->set_custom_minimum_size(Size2(64, 64) * EDSCALE);
	portrait->set_visible(false);
	add_child(portrait);
	info = memnew(Label);
	info->set_h_size_flags(SIZE_EXPAND_FILL);
	info->set_v_size_flags(SIZE_SHRINK_CENTER);
	info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(info);
}

bool EditorInspectorPluginDialogueSpeaker::can_handle(Object *p_object) {
	return Object::cast_to<DialogueSpeaker>(p_object) != nullptr || Object::cast_to<DialogueSpeakers>(p_object) != nullptr;
}

void EditorInspectorPluginDialogueSpeaker::parse_begin(Object *p_object) {
	Resource *resource = Object::cast_to<Resource>(p_object);
	if (resource != nullptr) {
		add_custom_control(memnew(DialogueSpeakerPreview(Ref<Resource>(resource))));
	}
}
