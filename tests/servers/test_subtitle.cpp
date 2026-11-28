/**************************************************************************/
/*  test_subtitle.cpp                                                     */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_subtitle)

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "servers/subtitle/subtitle.h"
#include "servers/subtitle/subtitle_format_srt.h"
#include "tests/signal_watcher.h"
#include "tests/test_utils.h"

namespace TestSubtitle {

static Ref<SubtitleSegment> make_segment(double p_start, double p_end, const String &p_text, SubtitleSegment::Kind p_kind = SubtitleSegment::KIND_DIALOG) {
	Ref<SubtitleSegment> segment;
	segment.instantiate();
	segment->set_start_time(p_start);
	segment->set_end_time(p_end);
	segment->set_text(p_text);
	segment->set_kind(p_kind);
	return segment;
}

// A file as exported from a video editor: BOM, CRLF, timeline starting at one hour, two-line cues.
static const char *VIDEO_EDITOR_SRT =
		"\xEF\xBB\xBF"
		"1\r\n01:00:00,016 --> 01:00:05,533\r\nWith the release of Godot Engine\r\n4.8 we look at the news.\r\n\r\n"
		"2\r\n01:00:05,966 --> 01:00:11,300\r\n[Applause]\r\n\r\n"
		"3\r\n01:00:11,699 --> 01:00:16,283\r\n\xE2\x99\xAA calm music \xE2\x99\xAA\r\n\r\n"
		"4\r\n01:00:16,833 --> 01:00:18,816\r\nJOHN: Did you hear that?\r\n\r\n"
		"5\r\n01:00:18,849 --> 01:00:21,483\r\n<i>Somewhere far away.</i>\r\n\r\n"
		"6\r\n01:00:21.516 --> 01:00:26.099 X1:10 X2:20\r\n<b>Bold</b> and {\\an8}top\r\n\r\n"
		"7\r\nbroken --> line\r\nskipped\r\n\r\n"
		"8\r\n01:00:27,000 --> 01:00:28,000\r\nNote: lower-case is not a speaker\r\n";

TEST_CASE("[Subtitle] SubtitleSegment defaults and helpers") {
	Ref<SubtitleSegment> segment;
	segment.instantiate();
	CHECK(segment->get_start_time() == 0.0);
	CHECK(segment->get_end_time() == 0.0);
	CHECK(segment->get_kind() == SubtitleSegment::KIND_DIALOG);
	CHECK(segment->get_placement() == SubtitleSegment::PLACEMENT_AUTO);
	CHECK_FALSE(segment->is_off_screen());
	CHECK_FALSE(segment->is_caption());

	segment = make_segment(1.5, 2.3, "Hi");
	CHECK(segment->get_duration() == doctest::Approx(0.8));
	CHECK(segment->contains_time(1.5));
	CHECK(segment->contains_time(2.0));
	CHECK_FALSE(segment->contains_time(2.3));
	CHECK_FALSE(segment->contains_time(1.4));

	for (SubtitleSegment::Kind kind : { SubtitleSegment::KIND_SOUND, SubtitleSegment::KIND_REACTION, SubtitleSegment::KIND_MUSIC, SubtitleSegment::KIND_LYRICS }) {
		segment->set_kind(kind);
		CHECK(segment->is_caption());
	}
	segment->set_kind(SubtitleSegment::KIND_ON_SCREEN_TEXT);
	CHECK_FALSE(segment->is_caption());
}

TEST_CASE("[Subtitle] Segments stay sorted, overlap, offset, shift") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(5.0, 6.0, "c"));
	subtitle->add_segment(make_segment(1.0, 4.0, "a"));
	subtitle->add_segment(make_segment(2.0, 3.0, "b"));
	REQUIRE(subtitle->get_segment_count() == 3);
	CHECK(subtitle->get_segment(0)->get_text() == "a");
	CHECK(subtitle->get_segment(1)->get_text() == "b");
	CHECK(subtitle->get_segment(2)->get_text() == "c");

	CHECK(subtitle->get_segments_at(2.5).size() == 2);
	CHECK(subtitle->find_segment_at(2.5)->get_text() == "a");
	CHECK(subtitle->get_segments_at(4.5).is_empty());
	CHECK(subtitle->find_segment_at(4.5).is_null());
	CHECK(subtitle->get_start_time() == 1.0);
	CHECK(subtitle->get_end_time() == 6.0);

	subtitle->set_time_offset(10.0);
	CHECK(subtitle->get_segments_at(2.5).is_empty());
	CHECK(subtitle->get_segments_at(12.5).size() == 2);
	CHECK(subtitle->get_start_time() == 11.0);
	CHECK(subtitle->get_end_time() == 16.0);

	subtitle->shift_times(-1.0);
	CHECK(subtitle->get_segment(0)->get_start_time() == 0.0);
	CHECK(subtitle->get_segment(2)->get_end_time() == 5.0);

	// sort_segments() is stable and sorts by start, then end.
	TypedArray<SubtitleSegment> unsorted;
	unsorted.push_back(make_segment(3.0, 4.0, "late"));
	unsorted.push_back(make_segment(1.0, 3.0, "long"));
	unsorted.push_back(make_segment(1.0, 2.0, "short"));
	subtitle->set_segments(unsorted);
	subtitle->sort_segments();
	CHECK(subtitle->get_segment(0)->get_text() == "short");
	CHECK(subtitle->get_segment(1)->get_text() == "long");
	CHECK(subtitle->get_segment(2)->get_text() == "late");

	subtitle->remove_segment(0);
	CHECK(subtitle->get_segment_count() == 2);
	subtitle->clear_segments();
	CHECK(subtitle->get_segment_count() == 0);
	CHECK(subtitle->get_end_time() == 0.0);
}

TEST_CASE("[Subtitle] validate() finds invalid times, empty texts, overlaps and fast reading") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(0.0, 2.0, "Fine."));
	subtitle->add_segment(make_segment(3.0, 3.0, "Zero length."));
	subtitle->add_segment(make_segment(4.0, 6.0, ""));
	subtitle->add_segment(make_segment(7.0, 9.0, "Overlaps the next one."));
	subtitle->add_segment(make_segment(8.0, 8.5, "Far too much text for half a second."));

	Ref<SubtitleValidation> validation = subtitle->validate();
	CHECK_FALSE(validation->is_valid());
	CHECK(validation->get_invalid_time_segments() == PackedInt32Array({ 1 }));
	CHECK(validation->get_empty_text_segments() == PackedInt32Array({ 2 }));
	CHECK(validation->get_overlapping_segments() == PackedInt32Array({ 3 }));
	CHECK(validation->get_fast_reading_segments() == PackedInt32Array({ 4 }));
	CHECK(validation->get_problem_count() == 4);

	Ref<Subtitle> clean;
	clean.instantiate();
	clean->add_segment(make_segment(0.0, 2.0, "Fine."));
	CHECK(clean->validate()->is_valid());
}

TEST_CASE("[Subtitle][SRT] Times") {
	double seconds = 0.0;
	CHECK(SubtitleFormatSRT::parse_time("01:02:03,450", seconds));
	CHECK(seconds == doctest::Approx(3723.45));
	CHECK(SubtitleFormatSRT::parse_time("02:03.45", seconds));
	CHECK(seconds == doctest::Approx(123.45));
	CHECK(SubtitleFormatSRT::parse_time(" 00:00:01,5 ", seconds));
	CHECK(seconds == doctest::Approx(1.5));
	CHECK_FALSE(SubtitleFormatSRT::parse_time("broken", seconds));
	CHECK_FALSE(SubtitleFormatSRT::parse_time("1:2:3:4,000", seconds));
	CHECK(SubtitleFormatSRT::format_time(3723.45) == "01:02:03,450");
	CHECK(SubtitleFormatSRT::format_time(-1.0) == "00:00:00,000");
	CHECK(SubtitleFormatSRT::format_time(1.5) == "00:00:01,500");
}

TEST_CASE("[Subtitle][SRT] Tags are removed, a lone < stays") {
	CHECK(SubtitleFormatSRT::strip_tags("<i>a</i> <b>b</b> <u>c</u> <font color=\"#ff0000\">d</font>") == "a b c d");
	CHECK(SubtitleFormatSRT::strip_tags("{\\an8}top") == "top");
	CHECK(SubtitleFormatSRT::strip_tags("1 < 2 and 3 > 2") == "1 < 2 and 3 > 2");
}

TEST_CASE("[Subtitle][SRT] A file from a video editor") {
	Vector<uint8_t> bytes;
	const CharString source = CharString(VIDEO_EDITOR_SRT);
	for (int i = 0; i < source.length(); i++) {
		bytes.push_back(uint8_t(source[i]));
	}
	Vector<Ref<SubtitleSegment>> segments;
	int skipped = 0;
	REQUIRE(SubtitleFormatSRT::parse(SubtitleFormatSRT::decode(bytes), segments, &skipped) == OK);
	CHECK(skipped == 1);
	REQUIRE(segments.size() == 7);

	CHECK(segments[0]->get_start_time() == doctest::Approx(3600.016));
	CHECK(segments[0]->get_end_time() == doctest::Approx(3605.533));
	CHECK(segments[0]->get_text() == "With the release of Godot Engine\n4.8 we look at the news.");
	CHECK(segments[0]->get_kind() == SubtitleSegment::KIND_DIALOG);

	CHECK(segments[1]->get_kind() == SubtitleSegment::KIND_SOUND);
	CHECK(segments[1]->get_text() == "Applause");

	CHECK(segments[2]->get_kind() == SubtitleSegment::KIND_MUSIC);
	CHECK(segments[2]->get_text() == "calm music");

	CHECK(segments[3]->get_speaker() == StringName("JOHN"));
	CHECK(segments[3]->get_text() == "Did you hear that?");

	CHECK(segments[4]->is_off_screen());
	CHECK(segments[4]->get_text() == "Somewhere far away.");

	CHECK(segments[5]->get_start_time() == doctest::Approx(3621.516));
	CHECK(segments[5]->get_end_time() == doctest::Approx(3626.099));
	CHECK(segments[5]->get_text() == "Bold and top");

	CHECK(segments[6]->get_speaker() == StringName());
	CHECK(segments[6]->get_text() == "Note: lower-case is not a speaker");

	// Nothing readable at all is an error; an empty file is an empty subtitle.
	CHECK(SubtitleFormatSRT::parse("only text\nno cues", segments) == ERR_PARSE_ERROR);
	CHECK(SubtitleFormatSRT::parse("", segments) == OK);
	CHECK(segments.is_empty());
}

TEST_CASE("[Subtitle][SRT] UTF-16 with BOM") {
	const String text = "1\n00:00:01,000 --> 00:00:02,000\nÄpfel ♪\n";
	const Char16String utf16 = text.utf16();
	Vector<uint8_t> bytes = { 0xFF, 0xFE };
	for (int i = 0; i < utf16.length(); i++) {
		bytes.push_back(uint8_t(utf16[i] & 0xFF));
		bytes.push_back(uint8_t(utf16[i] >> 8));
	}
	CHECK(SubtitleFormatSRT::decode(bytes) == text);
}

TEST_CASE("[Subtitle][SRT] Writing puts decorations back and applies the offset") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(3601.0, 3602.5, "Hello"));
	subtitle->add_segment(make_segment(3603.0, 3604.0, "Applause", SubtitleSegment::KIND_SOUND));
	subtitle->add_segment(make_segment(3605.0, 3606.0, "calm music", SubtitleSegment::KIND_MUSIC));
	Ref<SubtitleSegment> spoken = make_segment(3607.0, 3608.0, "Who is there?");
	spoken->set_speaker("GUARD");
	spoken->set_off_screen(true);
	subtitle->add_segment(spoken);
	subtitle->set_time_offset(-3600.0);

	const String text = subtitle->to_text(Subtitle::FORMAT_SRT);
	CHECK(text.begins_with("1\n00:00:01,000 --> 00:00:02,500\nHello\n"));
	CHECK(text.contains("[Applause]"));
	CHECK(text.contains(String::chr(0x266A) + " calm music " + String::chr(0x266A)));
	CHECK(text.contains("<i>GUARD: Who is there?</i>"));

	Ref<Subtitle> loaded;
	loaded.instantiate();
	REQUIRE(loaded->load_from_text(text, Subtitle::FORMAT_SRT) == OK);
	REQUIRE(loaded->get_segment_count() == 4);
	CHECK(loaded->get_time_offset() == 0.0);
	CHECK(loaded->get_segment(0)->get_start_time() == doctest::Approx(1.0));
	CHECK(loaded->get_segment(0)->get_end_time() == doctest::Approx(2.5));
	CHECK(loaded->get_segment(1)->get_kind() == SubtitleSegment::KIND_SOUND);
	CHECK(loaded->get_segment(1)->get_text() == "Applause");
	CHECK(loaded->get_segment(2)->get_kind() == SubtitleSegment::KIND_MUSIC);
	CHECK(loaded->get_segment(2)->get_text() == "calm music");
	CHECK(loaded->get_segment(3)->get_speaker() == StringName("GUARD"));
	CHECK(loaded->get_segment(3)->is_off_screen());
	CHECK(loaded->get_segment(3)->get_text() == "Who is there?");
}

TEST_CASE("[Subtitle][SRT] Writing leaves out segments without text") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(1.0, 2.0, "One"));
	subtitle->add_segment(make_segment(3.0, 4.0, ""));
	subtitle->add_segment(make_segment(5.0, 6.0, "Two"));

	const String text = subtitle->to_text(Subtitle::FORMAT_SRT);
	CHECK(text == "1\n00:00:01,000 --> 00:00:02,000\nOne\n\n2\n00:00:05,000 --> 00:00:06,000\nTwo\n");
}

TEST_CASE("[Subtitle][SRT] ResourceLoader and ResourceSaver") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(0.5, 1.5, "One"));
	subtitle->add_segment(make_segment(2.0, 3.0, "Two"));

	const String path = TestUtils::get_temp_path("subtitle_test.srt");
	REQUIRE(ResourceSaver::save(subtitle, path) == OK);
	CHECK(ResourceLoader::get_resource_type(path) == "Subtitle");
	Ref<Subtitle> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());
	REQUIRE(loaded->get_segment_count() == 2);
	CHECK(loaded->get_segment(1)->get_text() == "Two");
	CHECK(loaded->get_segment(1)->get_start_time() == doctest::Approx(2.0));

	Ref<Subtitle> from_file;
	from_file.instantiate();
	CHECK(from_file->load_from_file(path) == OK);
	CHECK(from_file->get_segment_count() == 2);

	// As .tres, every property is kept.
	subtitle->get_segment(0)->set_speaker("narrator");
	subtitle->get_segment(0)->set_placement(SubtitleSegment::PLACEMENT_TOP);
	subtitle->set_language("de");
	subtitle->set_time_offset(-1.0);
	const String tres_path = TestUtils::get_temp_path("subtitle_test.tres");
	REQUIRE(ResourceSaver::save(subtitle, tres_path) == OK);
	Ref<Subtitle> tres = ResourceLoader::load(tres_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(tres.is_valid());
	CHECK(tres->get_language() == "de");
	CHECK(tres->get_time_offset() == -1.0);
	CHECK(tres->get_segment(0)->get_speaker() == StringName("narrator"));
	CHECK(tres->get_segment(0)->get_placement() == SubtitleSegment::PLACEMENT_TOP);
}

TEST_CASE("[Subtitle] Changes of a segment reach the subtitle") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	Ref<SubtitleSegment> segment = make_segment(0.0, 1.0, "A");
	subtitle->add_segment(segment);
	// Adding the same segment again must not connect twice.
	subtitle->add_segment(segment);
	subtitle->remove_segment(1);

	SIGNAL_WATCH(subtitle.ptr(), "changed");
	segment->set_end_time(2.0);
	SIGNAL_CHECK("changed", { {} });

	subtitle->remove_segment(0);
	SIGNAL_DISCARD("changed");
	segment->set_end_time(3.0);
	SIGNAL_CHECK_FALSE("changed");

	TypedArray<SubtitleSegment> array;
	array.push_back(segment);
	subtitle->set_segments(array);
	SIGNAL_DISCARD("changed");
	segment->set_end_time(4.0);
	SIGNAL_CHECK("changed", { {} });

	subtitle->set_segments(TypedArray<SubtitleSegment>());
	SIGNAL_DISCARD("changed");
	segment->set_end_time(5.0);
	SIGNAL_CHECK_FALSE("changed");
	SIGNAL_UNWATCH(subtitle.ptr(), "changed");
}

TEST_CASE("[Subtitle] Translatable strings for POT generation") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(0, 1, "Hello"));
	Ref<SubtitleSegment> spoken = make_segment(1, 2, "Run!");
	spoken->set_speaker("Anna");
	spoken->set_translation_context("chase");
	subtitle->add_segment(spoken);
	subtitle->add_segment(make_segment(2, 3, ""));

	// Extraction does not depend on auto_translate; empty texts are skipped.
	// Then one entry per speaker with the context "speaker" (a caption's speaker is not shown, so not listed).
	Ref<SubtitleSegment> caption = make_segment(3, 4, "Door", SubtitleSegment::KIND_SOUND);
	caption->set_speaker("Door");
	subtitle->add_segment(caption);
	Ref<SubtitleSegment> again = make_segment(4, 5, "Again");
	again->set_speaker("Anna");
	subtitle->add_segment(again);
	const TypedArray<PackedStringArray> strings = subtitle->get_translatable_strings();
	REQUIRE(strings.size() == 5);
	CHECK(PackedStringArray(strings[0]) == PackedStringArray({ "Hello", "", "", "" }));
	CHECK(PackedStringArray(strings[1]) == PackedStringArray({ "Run!", "chase", "", "Speaker: Anna" }));
	CHECK(PackedStringArray(strings[4]) == PackedStringArray({ "Anna", "speaker", "", "Speaker name" }));

	// A loaded .srt file gives its cue texts.
	const String path = TestUtils::get_temp_path("subtitle_pot.srt");
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string("1\n00:00:01,000 --> 00:00:02,000\nWhere are we?\n\n");
	file->close();
	const Ref<Subtitle> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());
	const TypedArray<PackedStringArray> loaded_strings = loaded->get_translatable_strings();
	REQUIRE(loaded_strings.size() == 1);
	CHECK(PackedStringArray(loaded_strings[0])[0] == "Where are we?");
}

TEST_CASE("[Subtitle] sync_times() maps two points linearly") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(10.0, 12.0, "a"));
	subtitle->add_segment(make_segment(20.0, 22.0, "b"));
	subtitle->add_segment(make_segment(30.0, 31.0, "c"));
	// 10 -> 11 and 30 -> 51: scale 2, everything else in between.
	subtitle->sync_times(10.0, 11.0, 30.0, 51.0);
	CHECK(subtitle->get_segment(0)->get_start_time() == doctest::Approx(11.0));
	CHECK(subtitle->get_segment(0)->get_end_time() == doctest::Approx(15.0));
	CHECK(subtitle->get_segment(1)->get_start_time() == doctest::Approx(31.0));
	CHECK(subtitle->get_segment(2)->get_end_time() == doctest::Approx(53.0));

	// Equal times or reversed targets change nothing.
	ERR_PRINT_OFF;
	subtitle->sync_times(5.0, 1.0, 5.0, 2.0);
	subtitle->sync_times(11.0, 20.0, 51.0, 10.0);
	subtitle->sync_times(11.0, Math::NaN, 51.0, 52.0);
	subtitle->sync_times(11.0, 12.0, 51.0, Math::INF);
	ERR_PRINT_ON;
	CHECK(subtitle->get_segment(0)->get_start_time() == doctest::Approx(11.0));
}

TEST_CASE("[Subtitle] split_segment() and merge_segments()") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	Ref<SubtitleSegment> segment = make_segment(0.0, 4.0, "First line\nSecond line");
	segment->set_speaker("Ann");
	segment->set_kind(SubtitleSegment::KIND_NARRATION);
	segment->set_placement(SubtitleSegment::PLACEMENT_TOP);
	segment->set_translation_context("intro");
	segment->set_off_screen(true);
	subtitle->add_segment(segment);
	subtitle->add_segment(make_segment(5.0, 6.0, "next"));

	CHECK(subtitle->split_segment(0, 1.5) == 1);
	REQUIRE(subtitle->get_segment_count() == 3);
	CHECK(subtitle->get_segment(0)->get_end_time() == 1.5);
	CHECK(subtitle->get_segment(0)->get_text() == "First line");
	CHECK(subtitle->get_segment(1)->get_start_time() == 1.5);
	CHECK(subtitle->get_segment(1)->get_end_time() == 4.0);
	CHECK(subtitle->get_segment(1)->get_text() == "Second line");
	CHECK(subtitle->get_segment(1)->get_speaker() == StringName("Ann"));
	CHECK(subtitle->get_segment(1)->get_kind() == SubtitleSegment::KIND_NARRATION);
	CHECK(subtitle->get_segment(1)->get_placement() == SubtitleSegment::PLACEMENT_TOP);
	CHECK(subtitle->get_segment(1)->get_translation_context() == StringName("intro"));
	CHECK(subtitle->get_segment(1)->is_off_screen());
	CHECK(subtitle->get_segment(1) != segment);

	// One line: at the space nearest to the split; one word: all in the first part.
	subtitle->add_segment(make_segment(10.0, 14.0, "one two three four"));
	CHECK(subtitle->split_segment(3, 13.0) == 4);
	CHECK(subtitle->get_segment(3)->get_text() == "one two three");
	CHECK(subtitle->get_segment(4)->get_text() == "four");
	subtitle->add_segment(make_segment(20.0, 22.0, "word"));
	CHECK(subtitle->split_segment(5, 21.0) == 6);
	CHECK(subtitle->get_segment(5)->get_text() == "word");
	CHECK(subtitle->get_segment(6)->get_text().is_empty());

	ERR_PRINT_OFF;
	CHECK(subtitle->split_segment(0, 1.5) == -1); // At the end: not inside.
	CHECK(subtitle->split_segment(99, 1.0) == -1);
	CHECK(subtitle->split_segment(1, Math::NaN) == -1);
	CHECK(subtitle->merge_segments(0, 1) == ERR_INVALID_PARAMETER);
	CHECK(subtitle->merge_segments(6) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;

	// Merging undoes the split, the second segment's texts go on new lines.
	CHECK(subtitle->merge_segments(0) == OK);
	CHECK(subtitle->get_segment_count() == 6);
	CHECK(subtitle->get_segment(0) == segment);
	CHECK(subtitle->get_segment(0)->get_start_time() == 0.0);
	CHECK(subtitle->get_segment(0)->get_end_time() == 4.0);
	CHECK(subtitle->get_segment(0)->get_text() == "First line\nSecond line");
	CHECK(subtitle->merge_segments(1, 3) == OK);
	CHECK(subtitle->get_segment(1)->get_end_time() == 14.0);
	CHECK(subtitle->get_segment(1)->get_text() == "next\none two three\nfour");
	CHECK(subtitle->get_segment_count() == 4);

	// Empty slots inside the range are dropped; out-of-order segments still give the earliest start and latest end.
	TypedArray<SubtitleSegment> unordered;
	unordered.push_back(make_segment(5.0, 6.0, "late"));
	unordered.push_back(Ref<SubtitleSegment>());
	unordered.push_back(make_segment(1.0, 7.0, "early"));
	subtitle->set_segments(unordered);
	CHECK(subtitle->merge_segments(0, 3) == OK);
	REQUIRE(subtitle->get_segment_count() == 1);
	CHECK(subtitle->get_segment(0)->get_start_time() == 1.0);
	CHECK(subtitle->get_segment(0)->get_end_time() == 7.0);
	CHECK(subtitle->get_segment(0)->get_text() == "late\nearly");
}

} // namespace TestSubtitle
