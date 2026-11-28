/**************************************************************************/
/*  dialogue_speaker.cpp                                                  */
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

#include "dialogue_speaker.h"

#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/string/translation_domain.h"
#include "core/string/translation_server.h"
#include "core/templates/hash_map.h"
#include "scene/resources/texture.h" // Like DisplayServer: the portrait is a Texture2D.
#include "servers/subtitle/subtitle.h"

// DialogueSpeaker

bool DialogueSpeaker::ids_match(const StringName &p_a, const StringName &p_b) {
	return p_a == p_b || (p_a != StringName() && p_b != StringName() && String(p_a).nocasecmp_to(String(p_b)) == 0);
}

void DialogueSpeaker::_persona_changed() {
	emit_changed();
}

void DialogueSpeaker::set_id(const StringName &p_id) {
	if (id == p_id) {
		return;
	}
	id = p_id;
	emit_changed();
}

StringName DialogueSpeaker::get_id() const {
	return id;
}

void DialogueSpeaker::set_display_name(const String &p_display_name) {
	if (display_name == p_display_name) {
		return;
	}
	display_name = p_display_name;
	emit_changed();
}

String DialogueSpeaker::get_display_name() const {
	return display_name;
}

void DialogueSpeaker::set_portrait_path(const String &p_path) {
	if (portrait_path == p_path) {
		return;
	}
	portrait_path = p_path;
	emit_changed();
}

String DialogueSpeaker::get_portrait_path() const {
	return portrait_path;
}

void DialogueSpeaker::set_color(const Color &p_color) {
	if (color == p_color) {
		return;
	}
	color = p_color;
	emit_changed();
}

Color DialogueSpeaker::get_color() const {
	return color;
}

void DialogueSpeaker::set_tts_persona(const Ref<TTSPersona> &p_persona) {
	if (tts_persona == p_persona) {
		return;
	}
	const Callable callback = callable_mp(this, &DialogueSpeaker::_persona_changed);
	if (tts_persona.is_valid()) {
		tts_persona->disconnect_changed(callback);
	}
	tts_persona = p_persona;
	if (tts_persona.is_valid()) {
		tts_persona->connect_changed(callback);
	}
	emit_changed();
}

Ref<TTSPersona> DialogueSpeaker::get_tts_persona() const {
	return tts_persona;
}

String DialogueSpeaker::get_translated_name() const {
	const String source = display_name.is_empty() ? String(id) : display_name;
	if (source.is_empty() || TranslationServer::get_singleton() == nullptr) {
		return source;
	}
	// The subtitles channel (#357), like the subtitle text and the speaker names of subtitle files.
	return TranslationServer::get_singleton()->get_or_add_domain(SNAME("subtitles"))->translate(source, Subtitle::SPEAKER_TRANSLATION_CONTEXT);
}

Ref<Texture2D> DialogueSpeaker::load_portrait() const {
	if (portrait_path.is_empty()) {
		return Ref<Texture2D>();
	}
	return ResourceLoader::load(portrait_path, "Texture2D");
}

DialogueSpeaker::~DialogueSpeaker() {
	if (tts_persona.is_valid()) {
		tts_persona->disconnect_changed(callable_mp(this, &DialogueSpeaker::_persona_changed));
	}
}

void DialogueSpeaker::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_id", "id"), &DialogueSpeaker::set_id);
	ClassDB::bind_method(D_METHOD("get_id"), &DialogueSpeaker::get_id);
	ClassDB::bind_method(D_METHOD("set_display_name", "display_name"), &DialogueSpeaker::set_display_name);
	ClassDB::bind_method(D_METHOD("get_display_name"), &DialogueSpeaker::get_display_name);
	ClassDB::bind_method(D_METHOD("set_portrait_path", "path"), &DialogueSpeaker::set_portrait_path);
	ClassDB::bind_method(D_METHOD("get_portrait_path"), &DialogueSpeaker::get_portrait_path);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &DialogueSpeaker::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &DialogueSpeaker::get_color);
	ClassDB::bind_method(D_METHOD("set_tts_persona", "persona"), &DialogueSpeaker::set_tts_persona);
	ClassDB::bind_method(D_METHOD("get_tts_persona"), &DialogueSpeaker::get_tts_persona);
	ClassDB::bind_method(D_METHOD("get_translated_name"), &DialogueSpeaker::get_translated_name);
	ClassDB::bind_method(D_METHOD("load_portrait"), &DialogueSpeaker::load_portrait);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "id"), "set_id", "get_id");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "display_name"), "set_display_name", "get_display_name");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "portrait_path", PROPERTY_HINT_FILE, "*.png,*.jpg,*.jpeg,*.webp,*.svg,*.ktx,*.tres,*.res"), "set_portrait_path", "get_portrait_path");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color"), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "tts_persona", PROPERTY_HINT_RESOURCE_TYPE, "TTSPersona"), "set_tts_persona", "get_tts_persona");
}

// DialogueSpeakers

void DialogueSpeakers::_speaker_changed() {
	emit_changed();
}

void DialogueSpeakers::_connect_speakers(bool p_connect) {
	const Callable callback = callable_mp(this, &DialogueSpeakers::_speaker_changed);
	for (const Ref<DialogueSpeaker> &speaker : speakers) {
		if (speaker.is_null()) {
			continue;
		}
		if (p_connect) {
			speaker->connect_changed(callback);
		} else {
			speaker->disconnect_changed(callback);
		}
	}
}

void DialogueSpeakers::set_speakers(const TypedArray<DialogueSpeaker> &p_speakers) {
	_connect_speakers(false);
	speakers.clear();
	for (int idx = 0; idx < p_speakers.size(); idx++) {
		// The Inspector adds empty slots while an array is edited; keep them so the edit can be finished.
		const Ref<DialogueSpeaker> speaker = p_speakers[idx];
		speakers.push_back(speaker);
	}
	_connect_speakers(true);
	emit_changed();
}

TypedArray<DialogueSpeaker> DialogueSpeakers::get_speakers() const {
	TypedArray<DialogueSpeaker> result;
	for (const Ref<DialogueSpeaker> &speaker : speakers) {
		result.push_back(speaker);
	}
	return result;
}

void DialogueSpeakers::add_speaker(const Ref<DialogueSpeaker> &p_speaker) {
	ERR_FAIL_COND_MSG(p_speaker.is_null(), "Cannot add a null speaker.");
	ERR_FAIL_COND_MSG(speakers.has(p_speaker), "The speaker is already in the list.");
	speakers.push_back(p_speaker);
	p_speaker->connect_changed(callable_mp(this, &DialogueSpeakers::_speaker_changed));
	emit_changed();
}

void DialogueSpeakers::remove_speaker(const StringName &p_id) {
	for (int idx = 0; idx < speakers.size(); idx++) {
		if (speakers[idx].is_valid() && DialogueSpeaker::ids_match(speakers[idx]->get_id(), p_id)) {
			speakers[idx]->disconnect_changed(callable_mp(this, &DialogueSpeakers::_speaker_changed));
			speakers.remove_at(idx);
			emit_changed();
			return;
		}
	}
}

Ref<DialogueSpeaker> DialogueSpeakers::get_speaker(const StringName &p_id) const {
	if (p_id == StringName()) {
		return Ref<DialogueSpeaker>();
	}
	for (const Ref<DialogueSpeaker> &speaker : speakers) {
		if (speaker.is_valid() && DialogueSpeaker::ids_match(speaker->get_id(), p_id)) {
			return speaker;
		}
	}
	return Ref<DialogueSpeaker>();
}

bool DialogueSpeakers::has_speaker(const StringName &p_id) const {
	return get_speaker(p_id).is_valid();
}

PackedStringArray DialogueSpeakers::get_ids() const {
	PackedStringArray ids;
	PackedStringArray lower_ids;
	for (const Ref<DialogueSpeaker> &speaker : speakers) {
		if (speaker.is_null() || speaker->get_id() == StringName()) {
			continue;
		}
		const String lower = String(speaker->get_id()).to_lower();
		if (!lower_ids.has(lower)) {
			lower_ids.push_back(lower);
			ids.push_back(speaker->get_id());
		}
	}
	return ids;
}

PackedStringArray DialogueSpeakers::validate() const {
	PackedStringArray problems;
	HashMap<String, int> first_index; // By lower-case id.
	for (int idx = 0; idx < speakers.size(); idx++) {
		const Ref<DialogueSpeaker> &speaker = speakers[idx];
		if (speaker.is_null()) {
			problems.push_back(vformat("Speaker %d is empty.", idx));
			continue;
		}
		const StringName speaker_id = speaker->get_id();
		if (speaker_id == StringName()) {
			problems.push_back(vformat("Speaker %d has no id.", idx));
			continue;
		}
		if (String(speaker_id).contains_char(',') || String(speaker_id).contains_char(':')) {
			// `,` and `:` separate the suggestions of the Inspector id picker.
			problems.push_back(vformat("Speaker %d: the id \"%s\" contains \",\" or \":\".", idx, speaker_id));
		}
		const String lower = String(speaker_id).to_lower();
		const int *first = first_index.getptr(lower);
		if (first != nullptr) {
			problems.push_back(vformat("Speakers %d and %d use the same id \"%s\" (ids ignore case).", *first, idx, speaker_id));
		} else {
			first_index.insert(lower, idx);
		}
	}
	return problems;
}

TypedArray<PackedStringArray> DialogueSpeakers::get_translatable_strings() const {
	TypedArray<PackedStringArray> result;
	for (const Ref<DialogueSpeaker> &speaker : speakers) {
		if (speaker.is_null()) {
			continue;
		}
		const String source = speaker->get_display_name().is_empty() ? String(speaker->get_id()) : speaker->get_display_name();
		if (!source.is_empty()) {
			result.push_back(PackedStringArray({ source, Subtitle::SPEAKER_TRANSLATION_CONTEXT, String(), vformat("Speaker name (%s)", speaker->get_id()) }));
		}
	}
	return result;
}

DialogueSpeakers::~DialogueSpeakers() {
	_connect_speakers(false);
}

void DialogueSpeakers::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_speakers", "speakers"), &DialogueSpeakers::set_speakers);
	ClassDB::bind_method(D_METHOD("get_speakers"), &DialogueSpeakers::get_speakers);
	ClassDB::bind_method(D_METHOD("add_speaker", "speaker"), &DialogueSpeakers::add_speaker);
	ClassDB::bind_method(D_METHOD("remove_speaker", "id"), &DialogueSpeakers::remove_speaker);
	ClassDB::bind_method(D_METHOD("get_speaker", "id"), &DialogueSpeakers::get_speaker);
	ClassDB::bind_method(D_METHOD("has_speaker", "id"), &DialogueSpeakers::has_speaker);
	ClassDB::bind_method(D_METHOD("get_ids"), &DialogueSpeakers::get_ids);
	ClassDB::bind_method(D_METHOD("validate"), &DialogueSpeakers::validate);
	ClassDB::bind_method(D_METHOD("get_translatable_strings"), &DialogueSpeakers::get_translatable_strings);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "speakers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("DialogueSpeaker")), "set_speakers", "get_speakers");
}
