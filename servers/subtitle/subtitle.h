/**************************************************************************/
/*  subtitle.h                                                            */
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

#include "core/io/resource.h"
#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"

// One timed text of a `Subtitle`: a time range in seconds, the text without decoration, and what kind of text it is.
class SubtitleSegment : public Resource {
	GDCLASS(SubtitleSegment, Resource);

public:
	enum Kind {
		KIND_DIALOG,
		KIND_NARRATION,
		KIND_SOUND,
		KIND_REACTION,
		KIND_MUSIC,
		KIND_LYRICS,
		KIND_ON_SCREEN_TEXT,
		KIND_MAX,
	};

	enum Placement {
		PLACEMENT_AUTO,
		PLACEMENT_BOTTOM,
		PLACEMENT_TOP,
		PLACEMENT_MAX,
	};

private:
	double start_time = 0.0;
	double end_time = 0.0;
	String text;
	Kind kind = KIND_DIALOG;
	StringName speaker;
	bool off_screen = false;
	Placement placement = PLACEMENT_AUTO;
	StringName translation_context;

protected:
	static void _bind_methods();

public:
	void set_start_time(double p_time);
	double get_start_time() const;
	void set_end_time(double p_time);
	double get_end_time() const;
	void set_text(const String &p_text);
	String get_text() const;
	void set_kind(Kind p_kind);
	Kind get_kind() const;
	void set_speaker(const StringName &p_speaker);
	StringName get_speaker() const;
	void set_off_screen(bool p_off_screen);
	bool is_off_screen() const;
	void set_placement(Placement p_placement);
	Placement get_placement() const;
	void set_translation_context(const StringName &p_context);
	StringName get_translation_context() const;

	double get_duration() const;
	// True for `p_time` in [start_time, end_time). Times are segment times, without the subtitle's `time_offset`.
	bool contains_time(double p_time) const;
	// Sound, reaction, music and lyrics: closed captions rather than speech.
	bool is_caption() const;
};

// The result of `Subtitle.validate()`: indices of segments with problems.
class SubtitleValidation : public RefCounted {
	GDCLASS(SubtitleValidation, RefCounted);

	friend class Subtitle;

	PackedInt32Array invalid_time_segments;
	PackedInt32Array empty_text_segments;
	PackedInt32Array overlapping_segments;
	PackedInt32Array fast_reading_segments;

protected:
	static void _bind_methods();

public:
	bool is_valid() const;
	int get_problem_count() const;
	PackedInt32Array get_invalid_time_segments() const { return invalid_time_segments; }
	PackedInt32Array get_empty_text_segments() const { return empty_text_segments; }
	PackedInt32Array get_overlapping_segments() const { return overlapping_segments; }
	PackedInt32Array get_fast_reading_segments() const { return fast_reading_segments; }
};

// A timed list of texts. It knows nothing about audio or video: whatever has a time can drive it through `SubtitleServer`.
class Subtitle : public Resource {
	GDCLASS(Subtitle, Resource);

public:
	enum Format {
		FORMAT_SRT,
		FORMAT_VTT,
		FORMAT_TTML,
		FORMAT_ASS,
		FORMAT_SBV,
		FORMAT_MAX,
	};

private:
	Vector<Ref<SubtitleSegment>> segments;
	double time_offset = 0.0;
	String language;
	bool auto_translate = false;

	void _segment_changed();
	void _connect_segment(const Ref<SubtitleSegment> &p_segment);
	void _disconnect_segment(const Ref<SubtitleSegment> &p_segment);
	void _disconnect_all_segments();

protected:
	static void _bind_methods();

public:
	~Subtitle();

	void set_segments(const TypedArray<SubtitleSegment> &p_segments);
	TypedArray<SubtitleSegment> get_segments() const;
	void set_time_offset(double p_offset);
	double get_time_offset() const;
	void set_language(const String &p_language);
	String get_language() const;
	void set_auto_translate(bool p_enabled);
	bool is_auto_translate() const;

	// Inserts after the last segment with the same or an earlier start time, so the list stays sorted.
	void add_segment(const Ref<SubtitleSegment> &p_segment);
	void remove_segment(int p_index);
	Ref<SubtitleSegment> get_segment(int p_index) const;
	int get_segment_count() const;
	void clear_segments();
	// Stable sort by start time, then end time.
	void sort_segments();

	// `p_time` is a playback time: segment times plus `time_offset`.
	TypedArray<SubtitleSegment> get_segments_at(double p_time) const;
	Ref<SubtitleSegment> find_segment_at(double p_time) const;
	double get_start_time() const;
	double get_end_time() const;
	void shift_times(double p_seconds);
	void sync_times(double p_first_time, double p_first_target, double p_second_time, double p_second_target);
	int split_segment(int p_index, double p_time);
	Error merge_segments(int p_index, int p_count = 2);

	Ref<SubtitleValidation> validate(double p_max_characters_per_second = 20.0) const;
	// The translation context of speaker names: POT entries, `SubtitleServer::get_display_speaker()`.
	static constexpr const char *SPEAKER_TRANSLATION_CONTEXT = "speaker";
	// POT entries [text, translation_context, "", comment] for ResourceTranslationParserPlugin: the segment texts, then the speaker names.
	TypedArray<PackedStringArray> get_translatable_strings() const;

	String to_text(Format p_format) const;
	Error load_from_text(const String &p_text, Format p_format);
	Error load_from_file(const String &p_path);
	Error save_to_file(const String &p_path) const;

	// The format of a file extension ("srt", "vtt", "ttml", "dfxp", "ass", "ssa", "sbv"), or FORMAT_MAX.
	static Format get_format_for_extension(const String &p_extension);
	static PackedStringArray get_recognized_extensions();
	// Reads `p_text` in `p_format`. `r_language` is set by formats that store a language (TTML).
	static Error parse_text(const String &p_text, Format p_format, Vector<Ref<SubtitleSegment>> &r_segments, int *r_skipped = nullptr, String *r_language = nullptr);

	// Segments as they are stored, for the formats and the server (no copy of the array).
	const Vector<Ref<SubtitleSegment>> &get_segment_list() const { return segments; }
};

// Hint of the `subtitle_path` properties of streams and players (`PROPERTY_HINT_FILE`): subtitle files and saved `Subtitle` resources.
#define SUBTITLE_PATH_HINT "*.srt,*.vtt,*.ttml,*.dfxp,*.ass,*.ssa,*.sbv,*.tres,*.res"

VARIANT_ENUM_CAST(SubtitleSegment::Kind);
VARIANT_ENUM_CAST(SubtitleSegment::Placement);
VARIANT_ENUM_CAST(Subtitle::Format);
