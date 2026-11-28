/**************************************************************************/
/*  subtitle.cpp                                                          */
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

#include "subtitle.h"

#include "core/io/file_access.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/subtitle/subtitle_format_srt.h"
#include "servers/subtitle/subtitle_formats.h"

// SubtitleSegment

void SubtitleSegment::set_start_time(double p_time) {
	start_time = p_time;
	emit_changed();
}

double SubtitleSegment::get_start_time() const {
	return start_time;
}

void SubtitleSegment::set_end_time(double p_time) {
	end_time = p_time;
	emit_changed();
}

double SubtitleSegment::get_end_time() const {
	return end_time;
}

void SubtitleSegment::set_text(const String &p_text) {
	text = p_text;
	emit_changed();
}

String SubtitleSegment::get_text() const {
	return text;
}

void SubtitleSegment::set_kind(Kind p_kind) {
	ERR_FAIL_INDEX(p_kind, KIND_MAX);
	kind = p_kind;
	emit_changed();
}

SubtitleSegment::Kind SubtitleSegment::get_kind() const {
	return kind;
}

void SubtitleSegment::set_speaker(const StringName &p_speaker) {
	speaker = p_speaker;
	emit_changed();
}

StringName SubtitleSegment::get_speaker() const {
	return speaker;
}

void SubtitleSegment::set_off_screen(bool p_off_screen) {
	off_screen = p_off_screen;
	emit_changed();
}

bool SubtitleSegment::is_off_screen() const {
	return off_screen;
}

void SubtitleSegment::set_placement(Placement p_placement) {
	ERR_FAIL_INDEX(p_placement, PLACEMENT_MAX);
	placement = p_placement;
	emit_changed();
}

SubtitleSegment::Placement SubtitleSegment::get_placement() const {
	return placement;
}

void SubtitleSegment::set_translation_context(const StringName &p_context) {
	translation_context = p_context;
	emit_changed();
}

StringName SubtitleSegment::get_translation_context() const {
	return translation_context;
}

double SubtitleSegment::get_duration() const {
	return MAX(0.0, end_time - start_time);
}

bool SubtitleSegment::contains_time(double p_time) const {
	return p_time >= start_time && p_time < end_time;
}

bool SubtitleSegment::is_caption() const {
	return kind == KIND_SOUND || kind == KIND_REACTION || kind == KIND_MUSIC || kind == KIND_LYRICS;
}

void SubtitleSegment::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_start_time", "time"), &SubtitleSegment::set_start_time);
	ClassDB::bind_method(D_METHOD("get_start_time"), &SubtitleSegment::get_start_time);
	ClassDB::bind_method(D_METHOD("set_end_time", "time"), &SubtitleSegment::set_end_time);
	ClassDB::bind_method(D_METHOD("get_end_time"), &SubtitleSegment::get_end_time);
	ClassDB::bind_method(D_METHOD("set_text", "text"), &SubtitleSegment::set_text);
	ClassDB::bind_method(D_METHOD("get_text"), &SubtitleSegment::get_text);
	ClassDB::bind_method(D_METHOD("set_kind", "kind"), &SubtitleSegment::set_kind);
	ClassDB::bind_method(D_METHOD("get_kind"), &SubtitleSegment::get_kind);
	ClassDB::bind_method(D_METHOD("set_speaker", "speaker"), &SubtitleSegment::set_speaker);
	ClassDB::bind_method(D_METHOD("get_speaker"), &SubtitleSegment::get_speaker);
	ClassDB::bind_method(D_METHOD("set_off_screen", "off_screen"), &SubtitleSegment::set_off_screen);
	ClassDB::bind_method(D_METHOD("is_off_screen"), &SubtitleSegment::is_off_screen);
	ClassDB::bind_method(D_METHOD("set_placement", "placement"), &SubtitleSegment::set_placement);
	ClassDB::bind_method(D_METHOD("get_placement"), &SubtitleSegment::get_placement);
	ClassDB::bind_method(D_METHOD("set_translation_context", "context"), &SubtitleSegment::set_translation_context);
	ClassDB::bind_method(D_METHOD("get_translation_context"), &SubtitleSegment::get_translation_context);

	ClassDB::bind_method(D_METHOD("get_duration"), &SubtitleSegment::get_duration);
	ClassDB::bind_method(D_METHOD("contains_time", "time"), &SubtitleSegment::contains_time);
	ClassDB::bind_method(D_METHOD("is_caption"), &SubtitleSegment::is_caption);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "start_time", PROPERTY_HINT_RANGE, "0,86400,0.001,or_greater,suffix:s"), "set_start_time", "get_start_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "end_time", PROPERTY_HINT_RANGE, "0,86400,0.001,or_greater,suffix:s"), "set_end_time", "get_end_time");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "text", PROPERTY_HINT_MULTILINE_TEXT), "set_text", "get_text");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "kind", PROPERTY_HINT_ENUM, "Dialog,Narration,Sound,Reaction,Music,Lyrics,On-Screen Text"), "set_kind", "get_kind");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "speaker"), "set_speaker", "get_speaker");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "off_screen"), "set_off_screen", "is_off_screen");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "placement", PROPERTY_HINT_ENUM, "Auto,Bottom,Top"), "set_placement", "get_placement");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "translation_context"), "set_translation_context", "get_translation_context");

	BIND_ENUM_CONSTANT(KIND_DIALOG);
	BIND_ENUM_CONSTANT(KIND_NARRATION);
	BIND_ENUM_CONSTANT(KIND_SOUND);
	BIND_ENUM_CONSTANT(KIND_REACTION);
	BIND_ENUM_CONSTANT(KIND_MUSIC);
	BIND_ENUM_CONSTANT(KIND_LYRICS);
	BIND_ENUM_CONSTANT(KIND_ON_SCREEN_TEXT);
	BIND_ENUM_CONSTANT(KIND_MAX);

	BIND_ENUM_CONSTANT(PLACEMENT_AUTO);
	BIND_ENUM_CONSTANT(PLACEMENT_BOTTOM);
	BIND_ENUM_CONSTANT(PLACEMENT_TOP);
	BIND_ENUM_CONSTANT(PLACEMENT_MAX);
}

// SubtitleValidation

bool SubtitleValidation::is_valid() const {
	return get_problem_count() == 0;
}

int SubtitleValidation::get_problem_count() const {
	return invalid_time_segments.size() + empty_text_segments.size() + overlapping_segments.size() + fast_reading_segments.size();
}

void SubtitleValidation::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_valid"), &SubtitleValidation::is_valid);
	ClassDB::bind_method(D_METHOD("get_problem_count"), &SubtitleValidation::get_problem_count);
	ClassDB::bind_method(D_METHOD("get_invalid_time_segments"), &SubtitleValidation::get_invalid_time_segments);
	ClassDB::bind_method(D_METHOD("get_empty_text_segments"), &SubtitleValidation::get_empty_text_segments);
	ClassDB::bind_method(D_METHOD("get_overlapping_segments"), &SubtitleValidation::get_overlapping_segments);
	ClassDB::bind_method(D_METHOD("get_fast_reading_segments"), &SubtitleValidation::get_fast_reading_segments);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "valid"), "", "is_valid");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "invalid_time_segments"), "", "get_invalid_time_segments");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "empty_text_segments"), "", "get_empty_text_segments");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "overlapping_segments"), "", "get_overlapping_segments");
	ADD_PROPERTY(PropertyInfo(Variant::PACKED_INT32_ARRAY, "fast_reading_segments"), "", "get_fast_reading_segments");
}

// Subtitle

void Subtitle::_segment_changed() {
	emit_changed();
}

void Subtitle::_connect_segment(const Ref<SubtitleSegment> &p_segment) {
	if (p_segment.is_valid()) {
		p_segment->connect_changed(callable_mp(this, &Subtitle::_segment_changed));
	}
}

void Subtitle::_disconnect_segment(const Ref<SubtitleSegment> &p_segment) {
	if (p_segment.is_valid()) {
		p_segment->disconnect_changed(callable_mp(this, &Subtitle::_segment_changed));
	}
}

void Subtitle::_disconnect_all_segments() {
	for (const Ref<SubtitleSegment> &segment : segments) {
		_disconnect_segment(segment);
	}
}

Subtitle::~Subtitle() {
	_disconnect_all_segments();
}

void Subtitle::set_segments(const TypedArray<SubtitleSegment> &p_segments) {
	_disconnect_all_segments();
	segments.clear();
	for (int i = 0; i < p_segments.size(); i++) {
		// The Inspector adds empty slots while an array is edited; keep them so the edit can be finished.
		Ref<SubtitleSegment> segment = p_segments[i];
		_connect_segment(segment);
		segments.push_back(segment);
	}
	emit_changed();
}

TypedArray<SubtitleSegment> Subtitle::get_segments() const {
	TypedArray<SubtitleSegment> result;
	result.resize(segments.size());
	for (int i = 0; i < segments.size(); i++) {
		result[i] = segments[i];
	}
	return result;
}

void Subtitle::set_time_offset(double p_offset) {
	time_offset = p_offset;
	emit_changed();
}

double Subtitle::get_time_offset() const {
	return time_offset;
}

void Subtitle::set_language(const String &p_language) {
	language = p_language;
	emit_changed();
}

String Subtitle::get_language() const {
	return language;
}

void Subtitle::set_auto_translate(bool p_enabled) {
	auto_translate = p_enabled;
	emit_changed();
}

bool Subtitle::is_auto_translate() const {
	return auto_translate;
}

TypedArray<PackedStringArray> Subtitle::get_translatable_strings() const {
	TypedArray<PackedStringArray> result;
	for (const Ref<SubtitleSegment> &segment : segments) {
		if (segment.is_null() || segment->get_text().is_empty()) {
			continue;
		}
		const StringName speaker = segment->get_speaker();
		result.push_back(PackedStringArray({ segment->get_text(), String(segment->get_translation_context()), String(), speaker == StringName() ? String() : vformat("Speaker: %s", speaker) }));
	}
	return result;
}

void Subtitle::add_segment(const Ref<SubtitleSegment> &p_segment) {
	ERR_FAIL_COND(p_segment.is_null());
	int index = segments.size();
	while (index > 0 && (segments[index - 1].is_null() || segments[index - 1]->get_start_time() > p_segment->get_start_time())) {
		index--;
	}
	segments.insert(index, p_segment);
	_connect_segment(p_segment);
	emit_changed();
}

void Subtitle::remove_segment(int p_index) {
	ERR_FAIL_INDEX(p_index, segments.size());
	const Ref<SubtitleSegment> removed = segments[p_index];
	segments.remove_at(p_index);
	// The same segment can be in the list more than once: keep listening while it is.
	if (!segments.has(removed)) {
		_disconnect_segment(removed);
	}
	emit_changed();
}

Ref<SubtitleSegment> Subtitle::get_segment(int p_index) const {
	ERR_FAIL_INDEX_V(p_index, segments.size(), Ref<SubtitleSegment>());
	return segments[p_index];
}

int Subtitle::get_segment_count() const {
	return segments.size();
}

void Subtitle::clear_segments() {
	_disconnect_all_segments();
	segments.clear();
	emit_changed();
}

void Subtitle::sort_segments() {
	// Insertion sort: stable, and subtitles are almost always sorted already.
	for (int i = 1; i < segments.size(); i++) {
		Ref<SubtitleSegment> current = segments[i];
		int j = i;
		while (j > 0) {
			const Ref<SubtitleSegment> &previous = segments[j - 1];
			bool move;
			if (current.is_null()) {
				move = false; // Empty slots go to the end and keep their order.
			} else if (previous.is_null()) {
				move = true;
			} else if (previous->get_start_time() != current->get_start_time()) {
				move = previous->get_start_time() > current->get_start_time();
			} else {
				move = previous->get_end_time() > current->get_end_time();
			}
			if (!move) {
				break;
			}
			segments.write[j] = segments[j - 1];
			j--;
		}
		segments.write[j] = current;
	}
	emit_changed();
}

TypedArray<SubtitleSegment> Subtitle::get_segments_at(double p_time) const {
	TypedArray<SubtitleSegment> result;
	const double local_time = p_time - time_offset;
	for (const Ref<SubtitleSegment> &segment : segments) {
		if (segment.is_valid() && segment->contains_time(local_time)) {
			result.push_back(segment);
		}
	}
	return result;
}

Ref<SubtitleSegment> Subtitle::find_segment_at(double p_time) const {
	const double local_time = p_time - time_offset;
	for (const Ref<SubtitleSegment> &segment : segments) {
		if (segment.is_valid() && segment->contains_time(local_time)) {
			return segment;
		}
	}
	return Ref<SubtitleSegment>();
}

double Subtitle::get_start_time() const {
	bool found = false;
	double result = 0.0;
	for (const Ref<SubtitleSegment> &segment : segments) {
		if (segment.is_valid() && (!found || segment->get_start_time() < result)) {
			result = segment->get_start_time();
			found = true;
		}
	}
	return found ? result + time_offset : 0.0;
}

double Subtitle::get_end_time() const {
	bool found = false;
	double result = 0.0;
	for (const Ref<SubtitleSegment> &segment : segments) {
		if (segment.is_valid() && (!found || segment->get_end_time() > result)) {
			result = segment->get_end_time();
			found = true;
		}
	}
	return found ? result + time_offset : 0.0;
}

void Subtitle::shift_times(double p_seconds) {
	for (const Ref<SubtitleSegment> &segment : segments) {
		if (segment.is_valid()) {
			segment->set_start_time(segment->get_start_time() + p_seconds);
			segment->set_end_time(segment->get_end_time() + p_seconds);
		}
	}
	emit_changed();
}

void Subtitle::sync_times(double p_first_time, double p_first_target, double p_second_time, double p_second_target) {
	ERR_FAIL_COND_MSG(!Math::is_finite(p_first_time) || !Math::is_finite(p_first_target) || !Math::is_finite(p_second_time) || !Math::is_finite(p_second_target), "The sync points must be finite numbers.");
	ERR_FAIL_COND_MSG(p_second_time == p_first_time, "The two sync points need different times.");
	const double scale = (p_second_target - p_first_target) / (p_second_time - p_first_time);
	ERR_FAIL_COND_MSG(!(scale > 0.0), "The sync points would reverse the order of the segments.");
	for (const Ref<SubtitleSegment> &segment : segments) {
		if (segment.is_valid()) {
			segment->set_start_time(p_first_target + (segment->get_start_time() - p_first_time) * scale);
			segment->set_end_time(p_first_target + (segment->get_end_time() - p_first_time) * scale);
		}
	}
	emit_changed();
}

// Splits `p_text` near `p_ratio` of its length: at the nearest line break, else at the nearest space.
// Without either, the whole text stays in the first part.
static void _subtitle_split_text(const String &p_text, double p_ratio, String &r_first, String &r_second) {
	const int target = int(Math::round(p_text.length() * p_ratio));
	for (const char32_t separator : { U'\n', U' ' }) {
		int best = -1;
		for (int i = p_text.find_char(separator); i >= 0; i = p_text.find_char(separator, i + 1)) {
			if (best < 0 || Math::abs(i - target) < Math::abs(best - target)) {
				best = i;
			}
		}
		if (best >= 0) {
			r_first = p_text.substr(0, best).strip_edges();
			r_second = p_text.substr(best + 1).strip_edges();
			return;
		}
	}
	r_first = p_text;
	r_second = String();
}

int Subtitle::split_segment(int p_index, double p_time) {
	ERR_FAIL_INDEX_V(p_index, segments.size(), -1);
	const Ref<SubtitleSegment> first = segments[p_index];
	ERR_FAIL_COND_V(first.is_null(), -1);
	const double start = first->get_start_time();
	const double end = first->get_end_time();
	ERR_FAIL_COND_V_MSG(!(p_time > start && p_time < end), -1, "The split time must be inside the segment."); // Also rejects NaN.
	// The second part keeps speaker, kind, placement and translation context.
	Ref<SubtitleSegment> second = first->duplicate();
	String first_text;
	String second_text;
	_subtitle_split_text(first->get_text(), (p_time - start) / (end - start), first_text, second_text);
	second->set_start_time(p_time);
	second->set_text(second_text);
	first->set_end_time(p_time);
	first->set_text(first_text);
	segments.insert(p_index + 1, second);
	_connect_segment(second);
	emit_changed();
	return p_index + 1;
}

Error Subtitle::merge_segments(int p_index, int p_count) {
	ERR_FAIL_COND_V_MSG(p_count < 2, ERR_INVALID_PARAMETER, "Merging needs at least two segments.");
	ERR_FAIL_COND_V(p_index < 0 || p_index + p_count > segments.size(), ERR_INVALID_PARAMETER);
	const Ref<SubtitleSegment> merged = segments[p_index];
	ERR_FAIL_COND_V(merged.is_null(), ERR_INVALID_PARAMETER);
	double start = merged->get_start_time();
	double end = merged->get_end_time();
	String text = merged->get_text();
	for (int i = 1; i < p_count; i++) {
		const Ref<SubtitleSegment> removed = segments[p_index + 1];
		segments.remove_at(p_index + 1);
		if (removed.is_null()) {
			continue;
		}
		if (!segments.has(removed)) {
			_disconnect_segment(removed);
		}
		start = MIN(start, removed->get_start_time());
		end = MAX(end, removed->get_end_time());
		if (!removed->get_text().is_empty()) {
			text += text.is_empty() ? removed->get_text() : "\n" + removed->get_text();
		}
	}
	merged->set_start_time(start);
	merged->set_end_time(end);
	merged->set_text(text);
	emit_changed();
	return OK;
}

Ref<SubtitleValidation> Subtitle::validate(double p_max_characters_per_second) const {
	Ref<SubtitleValidation> result;
	result.instantiate();
	for (int i = 0; i < segments.size(); i++) {
		const Ref<SubtitleSegment> &segment = segments[i];
		if (segment.is_null()) {
			result->invalid_time_segments.push_back(i);
			continue;
		}
		const double duration = segment->get_end_time() - segment->get_start_time();
		if (segment->get_start_time() < 0.0 || duration <= 0.0) {
			result->invalid_time_segments.push_back(i);
		}
		const String text = segment->get_text().strip_edges();
		if (text.is_empty()) {
			result->empty_text_segments.push_back(i);
		} else if (duration > 0.0 && p_max_characters_per_second > 0.0) {
			const double characters = text.replace("\n", " ").length();
			if (characters / duration > p_max_characters_per_second) {
				result->fast_reading_segments.push_back(i);
			}
		}
		// Overlap with the next segment in time (the list is expected to be sorted).
		if (i + 1 < segments.size() && segments[i + 1].is_valid() && segments[i + 1]->get_start_time() < segment->get_end_time() && segments[i + 1]->get_start_time() >= segment->get_start_time()) {
			result->overlapping_segments.push_back(i);
		}
	}
	return result;
}

Subtitle::Format Subtitle::get_format_for_extension(const String &p_extension) {
	const String extension = p_extension.to_lower();
	if (extension == "srt") {
		return FORMAT_SRT;
	} else if (extension == "vtt") {
		return FORMAT_VTT;
	} else if (extension == "ttml" || extension == "dfxp") {
		return FORMAT_TTML;
	} else if (extension == "ass" || extension == "ssa") {
		return FORMAT_ASS;
	} else if (extension == "sbv") {
		return FORMAT_SBV;
	}
	return FORMAT_MAX;
}

PackedStringArray Subtitle::get_recognized_extensions() {
	return PackedStringArray({ "srt", "vtt", "ttml", "dfxp", "ass", "ssa", "sbv" });
}

Error Subtitle::parse_text(const String &p_text, Format p_format, Vector<Ref<SubtitleSegment>> &r_segments, int *r_skipped, String *r_language) {
	switch (p_format) {
		case FORMAT_SRT:
			return SubtitleFormatSRT::parse(p_text, r_segments, r_skipped);
		case FORMAT_VTT:
			return SubtitleFormatVTT::parse(p_text, r_segments, r_skipped);
		case FORMAT_TTML:
			return SubtitleFormatTTML::parse(p_text, r_segments, r_skipped, r_language);
		case FORMAT_ASS:
			return SubtitleFormatASS::parse(p_text, r_segments, r_skipped);
		case FORMAT_SBV:
			return SubtitleFormatSBV::parse(p_text, r_segments, r_skipped);
		default:
			ERR_FAIL_V(ERR_INVALID_PARAMETER);
	}
}

String Subtitle::to_text(Format p_format) const {
	switch (p_format) {
		case FORMAT_SRT:
			return SubtitleFormatSRT::write(segments, time_offset);
		case FORMAT_VTT:
			return SubtitleFormatVTT::write(segments, time_offset);
		case FORMAT_TTML:
			return SubtitleFormatTTML::write(segments, time_offset, language);
		case FORMAT_ASS:
			return SubtitleFormatASS::write(segments, time_offset);
		case FORMAT_SBV:
			return SubtitleFormatSBV::write(segments, time_offset);
		default:
			ERR_FAIL_V(String());
	}
}

Error Subtitle::load_from_text(const String &p_text, Format p_format) {
	ERR_FAIL_INDEX_V(p_format, FORMAT_MAX, ERR_INVALID_PARAMETER);
	Vector<Ref<SubtitleSegment>> parsed;
	String parsed_language;
	const Error err = parse_text(p_text, p_format, parsed, nullptr, &parsed_language);
	if (err != OK) {
		return err;
	}
	_disconnect_all_segments();
	segments = parsed;
	for (const Ref<SubtitleSegment> &segment : segments) {
		_connect_segment(segment);
	}
	time_offset = 0.0;
	if (!parsed_language.is_empty()) {
		language = parsed_language;
	}
	emit_changed();
	return OK;
}

Error Subtitle::load_from_file(const String &p_path) {
	const Format format = get_format_for_extension(p_path.get_extension());
	ERR_FAIL_COND_V_MSG(format == FORMAT_MAX, ERR_FILE_UNRECOGNIZED, vformat("Not a subtitle file extension: '%s'.", p_path));
	Error err = OK;
	const Vector<uint8_t> bytes = FileAccess::get_file_as_bytes(p_path, &err);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot read subtitle file '%s'.", p_path));
	err = load_from_text(SubtitleFormatSRT::decode(bytes), format);
	ERR_FAIL_COND_V_MSG(err != OK, err, vformat("Cannot parse subtitle file '%s'.", p_path));
	return OK;
}

Error Subtitle::save_to_file(const String &p_path) const {
	const Format format = get_format_for_extension(p_path.get_extension());
	ERR_FAIL_COND_V_MSG(format == FORMAT_MAX, ERR_FILE_UNRECOGNIZED, vformat("Not a subtitle file extension: '%s'.", p_path));
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	ERR_FAIL_COND_V_MSG(file.is_null(), err, vformat("Cannot write subtitle file '%s'.", p_path));
	file->store_string(to_text(format));
	return file->get_error() == OK ? OK : ERR_FILE_CANT_WRITE;
}

void Subtitle::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_segments", "segments"), &Subtitle::set_segments);
	ClassDB::bind_method(D_METHOD("get_segments"), &Subtitle::get_segments);
	ClassDB::bind_method(D_METHOD("set_time_offset", "offset"), &Subtitle::set_time_offset);
	ClassDB::bind_method(D_METHOD("get_time_offset"), &Subtitle::get_time_offset);
	ClassDB::bind_method(D_METHOD("set_language", "language"), &Subtitle::set_language);
	ClassDB::bind_method(D_METHOD("get_language"), &Subtitle::get_language);
	ClassDB::bind_method(D_METHOD("set_auto_translate", "enabled"), &Subtitle::set_auto_translate);
	ClassDB::bind_method(D_METHOD("is_auto_translate"), &Subtitle::is_auto_translate);

	ClassDB::bind_method(D_METHOD("add_segment", "segment"), &Subtitle::add_segment);
	ClassDB::bind_method(D_METHOD("remove_segment", "index"), &Subtitle::remove_segment);
	ClassDB::bind_method(D_METHOD("get_segment", "index"), &Subtitle::get_segment);
	ClassDB::bind_method(D_METHOD("get_segment_count"), &Subtitle::get_segment_count);
	ClassDB::bind_method(D_METHOD("clear_segments"), &Subtitle::clear_segments);
	ClassDB::bind_method(D_METHOD("sort_segments"), &Subtitle::sort_segments);
	ClassDB::bind_method(D_METHOD("get_segments_at", "time"), &Subtitle::get_segments_at);
	ClassDB::bind_method(D_METHOD("find_segment_at", "time"), &Subtitle::find_segment_at);
	ClassDB::bind_method(D_METHOD("get_start_time"), &Subtitle::get_start_time);
	ClassDB::bind_method(D_METHOD("get_end_time"), &Subtitle::get_end_time);
	ClassDB::bind_method(D_METHOD("shift_times", "seconds"), &Subtitle::shift_times);
	ClassDB::bind_method(D_METHOD("sync_times", "first_time", "first_target", "second_time", "second_target"), &Subtitle::sync_times);
	ClassDB::bind_method(D_METHOD("split_segment", "index", "time"), &Subtitle::split_segment);
	ClassDB::bind_method(D_METHOD("merge_segments", "index", "count"), &Subtitle::merge_segments, DEFVAL(2));
	ClassDB::bind_method(D_METHOD("validate", "max_characters_per_second"), &Subtitle::validate, DEFVAL(20.0));
	ClassDB::bind_method(D_METHOD("get_translatable_strings"), &Subtitle::get_translatable_strings);
	ClassDB::bind_method(D_METHOD("to_text", "format"), &Subtitle::to_text);
	ClassDB::bind_method(D_METHOD("load_from_text", "text", "format"), &Subtitle::load_from_text);
	ClassDB::bind_method(D_METHOD("load_from_file", "path"), &Subtitle::load_from_file);
	ClassDB::bind_method(D_METHOD("save_to_file", "path"), &Subtitle::save_to_file);

	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "segments", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("SubtitleSegment")), "set_segments", "get_segments");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_offset", PROPERTY_HINT_RANGE, "-86400,86400,0.001,or_less,or_greater,suffix:s"), "set_time_offset", "get_time_offset");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "language", PROPERTY_HINT_LOCALE_ID), "set_language", "get_language");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "auto_translate"), "set_auto_translate", "is_auto_translate");

	BIND_ENUM_CONSTANT(FORMAT_SRT);
	BIND_ENUM_CONSTANT(FORMAT_VTT);
	BIND_ENUM_CONSTANT(FORMAT_TTML);
	BIND_ENUM_CONSTANT(FORMAT_ASS);
	BIND_ENUM_CONSTANT(FORMAT_SBV);
	BIND_ENUM_CONSTANT(FORMAT_MAX);
}
