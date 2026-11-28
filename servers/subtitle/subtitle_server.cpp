/**************************************************************************/
/*  subtitle_server.cpp                                                   */
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

#include "subtitle_server.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/string/translation_domain.h"
#include "core/string/translation_server.h"
#include "servers/audio/audio_server.h"

SubtitleServer *SubtitleServer::singleton = nullptr;

static const int INVALID_TRACK = 0;

SubtitleServer *SubtitleServer::get_singleton() {
	return singleton;
}

// Frame callback.

void SubtitleServer::_update_callback(void *p_self) {
	SubtitleServer *self = static_cast<SubtitleServer *>(p_self);
	const uint64_t now = OS::get_singleton()->get_ticks_usec();
	const double delta = self->last_tick_usec == 0 ? 0.0 : double(now - self->last_tick_usec) / 1000000.0;
	self->last_tick_usec = now;
	self->process(delta);
}

void SubtitleServer::_ensure_callback() {
	AudioServer *audio_server = AudioServer::get_singleton();
	if (audio_server == nullptr || audio_server == callback_server) {
		return;
	}
	audio_server->add_update_callback(&SubtitleServer::_update_callback, this);
	callback_server = audio_server;
	last_tick_usec = OS::get_singleton()->get_ticks_usec();
}

void SubtitleServer::_remove_callback() {
	if (callback_server != nullptr && callback_server == AudioServer::get_singleton()) {
		callback_server->remove_update_callback(&SubtitleServer::_update_callback, this);
	}
	callback_server = nullptr;
	last_tick_usec = 0;
}

// Tracks.

bool SubtitleServer::_is_shown(const Ref<SubtitleSegment> &p_segment) const {
	if (!enabled || p_segment.is_null()) {
		return false;
	}
	return captions_enabled || !p_segment->is_caption();
}

double SubtitleServer::_read_time(Track &r_track, bool &r_source_valid) const {
	r_source_valid = true;
	if (r_track.time_source.is_null()) {
		return r_track.own_time;
	}
	if (!r_track.time_source.is_valid()) {
		r_source_valid = false; // The object of the time source was freed.
		return r_track.last_time;
	}
	Variant result;
	Callable::CallError call_error;
	r_track.time_source.callp(nullptr, 0, result, call_error);
	if (call_error.error != Callable::CallError::CALL_OK || (result.get_type() != Variant::FLOAT && result.get_type() != Variant::INT)) {
		r_source_valid = false;
		return r_track.last_time;
	}
	return double(result);
}

String SubtitleServer::_combined_text(const Track &p_track) const {
	Vector<String> parts;
	for (const Ref<SubtitleSegment> &segment : p_track.active) {
		const String text = get_display_text(p_track.subtitle, segment);
		if (!text.is_empty()) {
			parts.push_back(text);
		}
	}
	return String("\n").join(parts);
}

void SubtitleServer::_update_track(int p_track_id, double p_time) {
	Track *track = tracks.getptr(p_track_id);
	if (track == nullptr) {
		return;
	}

	Vector<Ref<SubtitleSegment>> now_active;
	if (track->subtitle.is_valid()) {
		const double local_time = p_time - track->subtitle->get_time_offset();
		for (const Ref<SubtitleSegment> &segment : track->subtitle->get_segment_list()) {
			if (_is_shown(segment) && segment->contains_time(local_time)) {
				now_active.push_back(segment);
			}
		}
	}

	Vector<Ref<SubtitleSegment>> ended;
	for (const Ref<SubtitleSegment> &segment : track->active) {
		if (!now_active.has(segment)) {
			ended.push_back(segment);
		}
	}
	Vector<Ref<SubtitleSegment>> started;
	for (const Ref<SubtitleSegment> &segment : now_active) {
		if (!track->active.has(segment)) {
			started.push_back(segment);
		}
	}

	track->active = now_active;
	const String old_text = track->text;
	track->text = _combined_text(*track);
	const String new_text = track->text;
	const Ref<Subtitle> subtitle = track->subtitle;
	track = nullptr; // Handlers may stop the track: don't use the pointer after emitting.

	for (const Ref<SubtitleSegment> &segment : ended) {
		emit_signal(SNAME("segment_ended"), p_track_id, segment);
	}
	for (const Ref<SubtitleSegment> &segment : started) {
		emit_signal(SNAME("segment_started"), p_track_id, segment, get_display_text(subtitle, segment));
	}
	if (new_text != old_text && tracks.has(p_track_id)) {
		emit_signal(SNAME("text_changed"), p_track_id, new_text);
	}
}

void SubtitleServer::_end_track(int p_track_id, bool p_finished) {
	Track *track = tracks.getptr(p_track_id);
	if (track == nullptr) {
		return;
	}
	const Vector<Ref<SubtitleSegment>> active = track->active;
	const bool had_text = !track->text.is_empty();
	tracks.erase(p_track_id);
	if (tracks.is_empty()) {
		_remove_callback();
	}

	for (const Ref<SubtitleSegment> &segment : active) {
		emit_signal(SNAME("segment_ended"), p_track_id, segment);
	}
	if (had_text) {
		emit_signal(SNAME("text_changed"), p_track_id, String());
	}
	emit_signal(p_finished ? SNAME("track_finished") : SNAME("track_canceled"), p_track_id);
}

int SubtitleServer::play(const Ref<Subtitle> &p_subtitle, const Callable &p_time_source, double p_from_time) {
	ERR_FAIL_COND_V_MSG(p_subtitle.is_null(), INVALID_TRACK, "Cannot play a null Subtitle.");

	const int id = next_track_id++;
	Track track;
	track.id = id;
	track.subtitle = p_subtitle;
	track.time_source = p_time_source;
	track.own_time = p_from_time;
	bool source_valid = true;
	const double time = _read_time(track, source_valid);
	ERR_FAIL_COND_V_MSG(!source_valid, INVALID_TRACK, "The time source of a subtitle track must return a number of seconds.");
	track.last_time = time;
	tracks.insert(id, track);
	_ensure_callback();

	emit_signal(SNAME("track_started"), id, p_subtitle);
	_update_track(id, time);
	return id;
}

void SubtitleServer::stop(int p_track_id) {
	ERR_FAIL_COND_MSG(!tracks.has(p_track_id), vformat("No subtitle track with id %d.", p_track_id));
	_end_track(p_track_id, false);
}

void SubtitleServer::stop_all() {
	Vector<int> ids;
	for (const KeyValue<int, Track> &E : tracks) {
		ids.push_back(E.key);
	}
	for (int id : ids) {
		_end_track(id, false);
	}
}

void SubtitleServer::pause(int p_track_id) {
	Track *track = tracks.getptr(p_track_id);
	ERR_FAIL_NULL_MSG(track, vformat("No subtitle track with id %d.", p_track_id));
	if (track->paused) {
		return;
	}
	track->paused = true;
	emit_signal(SNAME("track_paused"), p_track_id);
}

void SubtitleServer::resume(int p_track_id) {
	Track *track = tracks.getptr(p_track_id);
	ERR_FAIL_NULL_MSG(track, vformat("No subtitle track with id %d.", p_track_id));
	if (!track->paused) {
		return;
	}
	track->paused = false;
	emit_signal(SNAME("track_resumed"), p_track_id);
}

void SubtitleServer::seek(int p_track_id, double p_time) {
	Track *track = tracks.getptr(p_track_id);
	ERR_FAIL_NULL_MSG(track, vformat("No subtitle track with id %d.", p_track_id));
	ERR_FAIL_COND_MSG(!track->time_source.is_null(), "A subtitle track with a time source follows its source: seek the source instead.");
	track->own_time = p_time;
	track->last_time = p_time;
	emit_signal(SNAME("track_seeked"), p_track_id, p_time);
	_update_track(p_track_id, p_time);
}

bool SubtitleServer::is_playing(int p_track_id) const {
	return tracks.has(p_track_id);
}

bool SubtitleServer::is_paused(int p_track_id) const {
	const Track *track = tracks.getptr(p_track_id);
	return track != nullptr && track->paused;
}

PackedInt32Array SubtitleServer::get_track_ids() const {
	PackedInt32Array ids;
	for (const KeyValue<int, Track> &E : tracks) {
		ids.push_back(E.key);
	}
	ids.sort();
	return ids;
}

Ref<Subtitle> SubtitleServer::get_track_subtitle(int p_track_id) const {
	const Track *track = tracks.getptr(p_track_id);
	ERR_FAIL_NULL_V_MSG(track, Ref<Subtitle>(), vformat("No subtitle track with id %d.", p_track_id));
	return track->subtitle;
}

double SubtitleServer::get_track_time(int p_track_id) const {
	const Track *track = tracks.getptr(p_track_id);
	ERR_FAIL_NULL_V_MSG(track, 0.0, vformat("No subtitle track with id %d.", p_track_id));
	return track->last_time;
}

TypedArray<SubtitleSegment> SubtitleServer::get_active_segments(int p_track_id) const {
	TypedArray<SubtitleSegment> result;
	const Track *track = tracks.getptr(p_track_id);
	ERR_FAIL_NULL_V_MSG(track, result, vformat("No subtitle track with id %d.", p_track_id));
	for (const Ref<SubtitleSegment> &segment : track->active) {
		result.push_back(segment);
	}
	return result;
}

String SubtitleServer::get_track_text(int p_track_id) const {
	const Track *track = tracks.getptr(p_track_id);
	ERR_FAIL_NULL_V_MSG(track, String(), vformat("No subtitle track with id %d.", p_track_id));
	return track->text;
}

String SubtitleServer::get_display_text(const Ref<Subtitle> &p_subtitle, const Ref<SubtitleSegment> &p_segment, bool p_bbcode) const {
	ERR_FAIL_COND_V(p_segment.is_null(), String());
	String text = p_segment->get_text();
	if (p_subtitle.is_valid() && p_subtitle->is_auto_translate() && TranslationServer::get_singleton()) {
		// The subtitles channel (#357): the domain "subtitles" follows TranslationServer.set_locale_for(&"subtitles", ...).
		text = TranslationServer::get_singleton()->get_or_add_domain(SNAME("subtitles"))->translate(text, p_segment->get_translation_context());
	}
	if (p_bbcode) {
		text = text.replace("[", "[lb]");
	}
	const String open_bracket = p_bbcode ? "[lb]" : "[";
	const String close_bracket = p_bbcode ? "[rb]" : "]";
	switch (p_segment->get_kind()) {
		case SubtitleSegment::KIND_SOUND:
		case SubtitleSegment::KIND_REACTION:
			text = open_bracket + text + close_bracket;
			break;
		case SubtitleSegment::KIND_MUSIC:
		case SubtitleSegment::KIND_LYRICS: {
			const String note = String::chr(0x266A);
			Vector<String> lines = text.split("\n");
			for (int i = 0; i < lines.size(); i++) {
				lines.write[i] = note + " " + lines[i] + " " + note;
			}
			text = String("\n").join(lines);
		} break;
		default:
			break;
	}
	if (p_bbcode && (p_segment->is_off_screen() || p_segment->get_kind() == SubtitleSegment::KIND_NARRATION)) {
		text = "[i]" + text + "[/i]";
	}
	return text;
}

String SubtitleServer::get_display_speaker(const Ref<Subtitle> &p_subtitle, const Ref<SubtitleSegment> &p_segment) const {
	ERR_FAIL_COND_V(p_segment.is_null(), String());
	String speaker = p_segment->get_speaker();
	if (!speaker.is_empty() && p_subtitle.is_valid() && p_subtitle->is_auto_translate() && TranslationServer::get_singleton()) {
		speaker = TranslationServer::get_singleton()->get_or_add_domain(SNAME("subtitles"))->translate(speaker, Subtitle::SPEAKER_TRANSLATION_CONTEXT);
	}
	return speaker;
}

void SubtitleServer::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	for (const int id : get_track_ids()) {
		const Track *track = tracks.getptr(id);
		if (track) {
			_update_track(id, track->last_time);
		}
	}
	emit_signal(SNAME("enabled_changed"), p_enabled);
}

bool SubtitleServer::is_enabled() const {
	return enabled;
}

void SubtitleServer::set_captions_enabled(bool p_enabled) {
	if (captions_enabled == p_enabled) {
		return;
	}
	captions_enabled = p_enabled;
	for (const int id : get_track_ids()) {
		const Track *track = tracks.getptr(id);
		if (track) {
			_update_track(id, track->last_time);
		}
	}
	emit_signal(SNAME("captions_enabled_changed"), p_enabled);
}

bool SubtitleServer::is_captions_enabled() const {
	return captions_enabled;
}

void SubtitleServer::process(double p_delta) {
	for (const int id : get_track_ids()) {
		Track *track = tracks.getptr(id);
		if (track == nullptr || track->paused) {
			continue;
		}
		if (track->time_source.is_null()) {
			track->own_time += p_delta;
		}
		bool source_valid = true;
		const double time = _read_time(*track, source_valid);
		if (!source_valid) {
			_end_track(id, false);
			continue;
		}
		const bool jumped_back = !track->time_source.is_null() && time < track->last_time - 0.001;
		track->last_time = time;
		const double end_time = track->subtitle.is_valid() ? track->subtitle->get_end_time() : 0.0;
		track = nullptr;

		if (jumped_back) {
			emit_signal(SNAME("track_seeked"), id, time);
		}
		_update_track(id, time);

		track = tracks.getptr(id);
		if (track != nullptr && track->active.is_empty() && time >= end_time) {
			_end_track(id, true);
		}
	}
}

void SubtitleServer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("play", "subtitle", "time_source", "from_time"), &SubtitleServer::play, DEFVAL(Callable()), DEFVAL(0.0));
	ClassDB::bind_method(D_METHOD("stop", "track_id"), &SubtitleServer::stop);
	ClassDB::bind_method(D_METHOD("stop_all"), &SubtitleServer::stop_all);
	ClassDB::bind_method(D_METHOD("pause", "track_id"), &SubtitleServer::pause);
	ClassDB::bind_method(D_METHOD("resume", "track_id"), &SubtitleServer::resume);
	ClassDB::bind_method(D_METHOD("seek", "track_id", "time"), &SubtitleServer::seek);
	ClassDB::bind_method(D_METHOD("is_playing", "track_id"), &SubtitleServer::is_playing);
	ClassDB::bind_method(D_METHOD("is_paused", "track_id"), &SubtitleServer::is_paused);
	ClassDB::bind_method(D_METHOD("get_track_ids"), &SubtitleServer::get_track_ids);
	ClassDB::bind_method(D_METHOD("get_track_subtitle", "track_id"), &SubtitleServer::get_track_subtitle);
	ClassDB::bind_method(D_METHOD("get_track_time", "track_id"), &SubtitleServer::get_track_time);
	ClassDB::bind_method(D_METHOD("get_active_segments", "track_id"), &SubtitleServer::get_active_segments);
	ClassDB::bind_method(D_METHOD("get_track_text", "track_id"), &SubtitleServer::get_track_text);
	ClassDB::bind_method(D_METHOD("get_display_speaker", "subtitle", "segment"), &SubtitleServer::get_display_speaker);
	ClassDB::bind_method(D_METHOD("get_display_text", "subtitle", "segment", "bbcode"), &SubtitleServer::get_display_text, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &SubtitleServer::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &SubtitleServer::is_enabled);
	ClassDB::bind_method(D_METHOD("set_captions_enabled", "enabled"), &SubtitleServer::set_captions_enabled);
	ClassDB::bind_method(D_METHOD("is_captions_enabled"), &SubtitleServer::is_captions_enabled);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "captions_enabled"), "set_captions_enabled", "is_captions_enabled");

	ClassDB::bind_integer_constant(get_class_static(), StringName(), "INVALID_TRACK", INVALID_TRACK);

	ADD_SIGNAL(MethodInfo("track_started", PropertyInfo(Variant::INT, "track_id"), PropertyInfo(Variant::OBJECT, "subtitle", PROPERTY_HINT_RESOURCE_TYPE, "Subtitle")));
	ADD_SIGNAL(MethodInfo("segment_started", PropertyInfo(Variant::INT, "track_id"), PropertyInfo(Variant::OBJECT, "segment", PROPERTY_HINT_RESOURCE_TYPE, "SubtitleSegment"), PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("segment_ended", PropertyInfo(Variant::INT, "track_id"), PropertyInfo(Variant::OBJECT, "segment", PROPERTY_HINT_RESOURCE_TYPE, "SubtitleSegment")));
	ADD_SIGNAL(MethodInfo("text_changed", PropertyInfo(Variant::INT, "track_id"), PropertyInfo(Variant::STRING, "text")));
	ADD_SIGNAL(MethodInfo("track_paused", PropertyInfo(Variant::INT, "track_id")));
	ADD_SIGNAL(MethodInfo("track_resumed", PropertyInfo(Variant::INT, "track_id")));
	ADD_SIGNAL(MethodInfo("track_seeked", PropertyInfo(Variant::INT, "track_id"), PropertyInfo(Variant::FLOAT, "time")));
	ADD_SIGNAL(MethodInfo("track_finished", PropertyInfo(Variant::INT, "track_id")));
	ADD_SIGNAL(MethodInfo("track_canceled", PropertyInfo(Variant::INT, "track_id")));
	ADD_SIGNAL(MethodInfo("enabled_changed", PropertyInfo(Variant::BOOL, "enabled")));
	ADD_SIGNAL(MethodInfo("captions_enabled_changed", PropertyInfo(Variant::BOOL, "enabled")));
}

SubtitleServer::SubtitleServer() {
	singleton = this;
}

SubtitleServer::~SubtitleServer() {
	_remove_callback();
	tracks.clear();
	singleton = nullptr;
}
