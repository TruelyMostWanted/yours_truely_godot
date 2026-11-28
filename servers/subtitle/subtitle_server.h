/**************************************************************************/
/*  subtitle_server.h                                                     */
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

#include "core/object/object.h"
#include "core/templates/hash_map.h"
#include "core/variant/callable.h"
#include "servers/subtitle/dialogue_speaker.h"
#include "servers/subtitle/subtitle.h"

class AudioServer;

// Plays subtitles on tracks. A track has a `Subtitle`, a clock (its own, or a time source such as
// `AudioStreamPlayer.get_playback_position`) and the segments that are visible now. Signals are emitted
// on the main thread, from `play()`/`seek()`/`stop()` and once per frame.
class SubtitleServer : public Object {
	GDCLASS(SubtitleServer, Object);

	static SubtitleServer *singleton;

	struct Track {
		int id = 0;
		Ref<Subtitle> subtitle;
		Callable time_source;
		double own_time = 0.0;
		double last_time = 0.0;
		bool paused = false;
		Vector<Ref<SubtitleSegment>> active;
		String text;
	};

	HashMap<int, Track> tracks;
	int next_track_id = 1;
	bool enabled = true;
	bool captions_enabled = true;

	// The project's speakers (`audio/subtitles/speakers`); never null. Their TTS personas are registered at
	// TTSServer under the speaker ids, so player overrides (TTSPersonaSettingsServer) work per speaker.
	Ref<DialogueSpeakers> speakers;
	String project_speakers_path;
	bool project_settings_connected = false;
	HashMap<StringName, Ref<TTSPersona>> registered_personas;

	AudioServer *callback_server = nullptr;
	uint64_t last_tick_usec = 0;

	static void _update_callback(void *p_self);
	void _ensure_callback();
	void _remove_callback();

	bool _is_shown(const Ref<SubtitleSegment> &p_segment) const;
	double _read_time(Track &r_track, bool &r_source_valid) const;
	// Updates the active segments of a track to `p_time` and emits segment_ended, segment_started and text_changed.
	void _update_track(int p_track_id, double p_time);
	String _combined_text(const Track &p_track) const;
	void _end_track(int p_track_id, bool p_finished);

	void _speakers_changed();
	void _register_personas();
	void _unregister_personas();
	void _load_speakers_from(const String &p_path);
	void _project_settings_changed();

protected:
	static void _bind_methods();

public:
	static constexpr const char *SETTING_SPEAKERS = "audio/subtitles/speakers";

	static SubtitleServer *get_singleton();

	int play(const Ref<Subtitle> &p_subtitle, const Callable &p_time_source = Callable(), double p_from_time = 0.0);
	void stop(int p_track_id);
	void stop_all();
	void pause(int p_track_id);
	void resume(int p_track_id);
	void seek(int p_track_id, double p_time);

	bool is_playing(int p_track_id) const;
	bool is_paused(int p_track_id) const;
	PackedInt32Array get_track_ids() const;
	Ref<Subtitle> get_track_subtitle(int p_track_id) const;
	double get_track_time(int p_track_id) const;
	TypedArray<SubtitleSegment> get_active_segments(int p_track_id) const;
	String get_track_text(int p_track_id) const;

	String get_display_text(const Ref<Subtitle> &p_subtitle, const Ref<SubtitleSegment> &p_segment, bool p_bbcode = false) const;
	String get_display_speaker(const Ref<Subtitle> &p_subtitle, const Ref<SubtitleSegment> &p_segment) const;

	void set_speakers(const Ref<DialogueSpeakers> &p_speakers);
	Ref<DialogueSpeakers> get_speakers() const;
	// Loads the speakers of `audio/subtitles/speakers`. Called at startup (the first `catalogs_changed` of
	// ResourceLocalizationServer, from `load_project_catalogs()`) and when the setting names another file.
	void load_project_speakers();
	// Inspector: the speaker ids as suggestions on a StringName property (in the editor only).
	static void add_speaker_id_hint(PropertyInfo &r_property);

	void set_enabled(bool p_enabled);
	bool is_enabled() const;
	void set_captions_enabled(bool p_enabled);
	bool is_captions_enabled() const;

	// Called every frame from AudioServer::update(); unit tests call it directly.
	void process(double p_delta);

	SubtitleServer();
	~SubtitleServer();
};
