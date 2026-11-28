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

#include "core/config/project_settings.h"
#include "core/io/resource_uid.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/inspector/editor_speaker_picker.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/label.h"
#include "scene/gui/texture_rect.h"
#include "scene/resources/texture.h"
#include "scene/scene_string_names.h"
#include "servers/subtitle/dialogue_speaker.h"
#include "servers/subtitle/subtitle_server.h"

void DialogueSpeakerPreview::_update() {
	if (!is_inside_tree()) {
		return;
	}
	warning->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
	const Ref<DialogueSpeaker> speaker = resource;
	if (speaker.is_valid()) {
		const Ref<Texture2D> texture = speaker->load_portrait();
		portrait->set_texture(texture);
		portrait->set_visible(texture.is_valid());
		title->set_text(speaker->get_translated_name());
		const Color color = speaker->get_color();
		swatch->set_color(color);
		swatch->set_visible(color.a > 0.0f);
		const Ref<TTSPersona> persona = speaker->get_tts_persona();
		const String persona_text = persona.is_null() ? TTR("none") : (persona->get_persona_name() == StringName() ? persona->get_path().get_file() : String(persona->get_persona_name()));
		details->set_text(vformat(TTR("id: %s, persona: %s"), speaker->get_id(), persona_text));
		warning->set_text(TTR("No id: lines and subtitles cannot refer to this speaker."));
		warning->set_visible(speaker->get_id() == StringName());
		return;
	}
	const Ref<DialogueSpeakers> speakers = resource;
	if (speakers.is_valid()) {
		portrait->hide();
		swatch->hide();
		const String setting_path = GLOBAL_GET(SubtitleServer::SETTING_SPEAKERS);
		const bool project_list = !setting_path.is_empty() && !speakers->get_path().is_empty() && ResourceUID::ensure_path(setting_path) == speakers->get_path();
		title->set_text(project_list ? TTR("Project speakers (audio/subtitles/speakers)") : TTR("Speakers (not the project's list)"));
		details->set_text(vformat(TTR("%d speakers: %s"), speakers->get_ids().size(), String(", ").join(speakers->get_ids())));
		const PackedStringArray problems = speakers->validate();
		warning->set_text(String("\n").join(problems));
		warning->set_visible(!problems.is_empty());
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

	VBoxContainer *text_box = memnew(VBoxContainer);
	text_box->set_h_size_flags(SIZE_EXPAND_FILL);
	text_box->set_v_size_flags(SIZE_SHRINK_CENTER);
	add_child(text_box);
	HBoxContainer *title_row = memnew(HBoxContainer);
	text_box->add_child(title_row);
	swatch = memnew(ColorRect);
	swatch->set_custom_minimum_size(Size2(16, 16) * EDSCALE);
	swatch->set_v_size_flags(SIZE_SHRINK_CENTER);
	swatch->set_visible(false);
	title_row->add_child(swatch);
	title = memnew(Label);
	title_row->add_child(title);
	details = memnew(Label);
	details->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	text_box->add_child(details);
	warning = memnew(Label);
	warning->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	warning->set_visible(false);
	text_box->add_child(warning);
}

// EditorInspectorPluginDialogueSpeaker

void EditorInspectorPluginDialogueSpeaker::_refresh_id_hints() {
	refresh_queued = false;
	EditorInspector *inspector = EditorInterface::get_singleton()->get_inspector();
	Object *edited = inspector != nullptr ? inspector->get_edited_object() : nullptr;
	// Only objects with a speaker id property: rebuilding the Inspector of a speaker being edited would close its pickers.
	if (edited != nullptr && (Object::cast_to<SubtitleSegment>(edited) != nullptr || edited->is_class("SubtitleLabel") || edited->is_class("DialogueLineNode"))) {
		edited->notify_property_list_changed();
	}
}

void EditorInspectorPluginDialogueSpeaker::_speakers_changed(const Ref<Resource> &p_speakers) {
	if (!refresh_queued) {
		refresh_queued = true;
		callable_mp(this, &EditorInspectorPluginDialogueSpeaker::_refresh_id_hints).call_deferred();
	}
}

static bool _has_speaker_id_property(const Object *p_object) {
	return Object::cast_to<SubtitleSegment>(p_object) != nullptr || p_object->is_class("DialogueLineNode");
}

bool EditorInspectorPluginDialogueSpeaker::can_handle(Object *p_object) {
	return Object::cast_to<DialogueSpeaker>(p_object) != nullptr || Object::cast_to<DialogueSpeakers>(p_object) != nullptr || _has_speaker_id_property(p_object);
}

bool EditorInspectorPluginDialogueSpeaker::parse_property(Object *p_object, const Variant::Type p_type, const String &p_path, const PropertyHint p_hint, const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage, const bool p_wide) {
	if (p_type != Variant::STRING_NAME || p_path != "speaker" || !_has_speaker_id_property(p_object)) {
		return false;
	}
	add_property_editor(p_path, memnew(EditorPropertySpeakerId));
	return true;
}

void EditorInspectorPluginDialogueSpeaker::parse_begin(Object *p_object) {
	Resource *resource = Object::cast_to<Resource>(p_object);
	if (resource != nullptr && !_has_speaker_id_property(p_object)) {
		add_custom_control(memnew(DialogueSpeakerPreview(Ref<Resource>(resource))));
	}
}

EditorInspectorPluginDialogueSpeaker::EditorInspectorPluginDialogueSpeaker() {
	SubtitleServer *server = SubtitleServer::get_singleton();
	if (server != nullptr) {
		server->connect(SNAME("speakers_changed"), callable_mp(this, &EditorInspectorPluginDialogueSpeaker::_speakers_changed));
	}
}
