/**************************************************************************/
/*  test_dialogue_speakers.cpp                                            */
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

TEST_FORCE_LINK(test_dialogue_speakers)

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_saver.h"
#include "core/string/translation.h"
#include "core/string/translation_domain.h"
#include "core/string/translation_server.h"
#include "servers/subtitle/dialogue_speaker.h"
#include "servers/subtitle/subtitle_server.h"
#include "servers/tts/tts_server.h"
#include "tests/signal_watcher.h"
#include "tests/test_utils.h"

namespace TestDialogueSpeakers {

static Ref<DialogueSpeaker> make_speaker(const StringName &p_id, const String &p_display_name = String(), const Ref<TTSPersona> &p_persona = Ref<TTSPersona>()) {
	Ref<DialogueSpeaker> speaker;
	speaker.instantiate();
	speaker->set_id(p_id);
	speaker->set_display_name(p_display_name);
	speaker->set_tts_persona(p_persona);
	return speaker;
}

TEST_CASE("[DialogueSpeaker] Properties, translated name in the subtitles channel, lazy portrait") {
	Ref<DialogueSpeaker> speaker = make_speaker("anna");
	CHECK(speaker->get_color() == Color(0, 0, 0, 0)); // Alpha 0: no color of its own.
	CHECK(speaker->get_translated_name() == "anna"); // No display name: the id.
	CHECK(speaker->load_portrait().is_null()); // No portrait path.

	speaker->set_display_name("SPEAKER_ANNA");
	speaker->set_portrait_path("res://portraits/anna.png");
	speaker->set_color(Color(1, 0, 0));
	CHECK(speaker->get_display_name() == "SPEAKER_ANNA");
	CHECK(speaker->get_portrait_path() == "res://portraits/anna.png");
	CHECK(speaker->get_color() == Color(1, 0, 0));

	TranslationServer *translation_server = TranslationServer::get_singleton();
	Ref<Translation> german;
	german.instantiate();
	german->set_locale("de");
	german->add_message("SPEAKER_ANNA", "Anna", "speaker");
	const Ref<TranslationDomain> domain = translation_server->get_or_add_domain("subtitles");
	domain->add_translation(german);
	const String locale = translation_server->get_locale();
	translation_server->set_locale("en");
	CHECK(speaker->get_translated_name() == "SPEAKER_ANNA"); // No English entry.
	translation_server->set_locale_for("subtitles", "de");
	CHECK(speaker->get_translated_name() == "Anna");
	translation_server->clear_locale_for("subtitles");
	domain->remove_translation(german);
	translation_server->set_locale(locale);
}

TEST_CASE("[DialogueSpeaker] A change of the persona is a change of the speaker") {
	Ref<TTSPersona> persona;
	persona.instantiate();
	Ref<DialogueSpeaker> speaker = make_speaker("anna", String(), persona);
	Ref<DialogueSpeakers> speakers;
	speakers.instantiate();
	speakers->add_speaker(speaker);

	SIGNAL_WATCH(speakers.ptr(), "changed");
	persona->set_rate(1.5f);
	SIGNAL_CHECK("changed", Array({ {} }));
	speaker->set_tts_persona(Ref<TTSPersona>());
	SIGNAL_CHECK("changed", Array({ {} }));
	persona->set_rate(0.5f); // No longer connected.
	SIGNAL_CHECK_FALSE("changed");
	SIGNAL_UNWATCH(speakers.ptr(), "changed");
}

TEST_CASE("[DialogueSpeakers] Find by id, ids, validation and POT entries") {
	Ref<DialogueSpeakers> speakers;
	speakers.instantiate();
	speakers->add_speaker(make_speaker("anna", "SPEAKER_ANNA"));
	speakers->add_speaker(make_speaker("ben"));
	CHECK(speakers->has_speaker("anna"));
	CHECK(speakers->has_speaker("ANNA")); // SubRip writes speakers in capitals.
	CHECK(speakers->get_speaker("Anna")->get_id() == StringName("anna")); // WebVTT <v Anna>; the id stays as written.
	CHECK(DialogueSpeaker::ids_match("Ben", "bEN"));
	CHECK_FALSE(DialogueSpeaker::ids_match("ben", StringName()));
	CHECK_FALSE(speakers->has_speaker("carl"));
	CHECK_FALSE(speakers->has_speaker(StringName()));
	CHECK(speakers->get_speaker("ben")->get_id() == StringName("ben"));
	CHECK(speakers->get_speaker("carl").is_null());
	CHECK(speakers->get_ids() == PackedStringArray({ "anna", "ben" }));
	CHECK(speakers->validate().is_empty());

	const TypedArray<PackedStringArray> strings = speakers->get_translatable_strings();
	REQUIRE(strings.size() == 2);
	const PackedStringArray first = strings[0];
	const PackedStringArray second = strings[1];
	CHECK(first == PackedStringArray({ "SPEAKER_ANNA", "speaker", "", "Speaker name (anna)" }));
	CHECK(second == PackedStringArray({ "ben", "speaker", "", "Speaker name (ben)" }));

	TypedArray<DialogueSpeaker> list = speakers->get_speakers();
	list.push_back(Ref<DialogueSpeaker>()); // An empty Inspector slot.
	list.push_back(make_speaker(StringName()));
	list.push_back(make_speaker("ANNA")); // The same id, ignoring case.
	list.push_back(make_speaker("x:y")); // Would break the Inspector's suggestions.
	speakers->set_speakers(list);
	CHECK(speakers->get_speakers().size() == 6);
	CHECK(speakers->get_ids() == PackedStringArray({ "anna", "ben", "x:y" }));
	const PackedStringArray problems = speakers->validate();
	CHECK(problems == PackedStringArray({ "Speaker 2 is empty.", "Speaker 3 has no id.", "Speakers 0 and 4 use the same id \"ANNA\" (ids ignore case).", "Speaker 5: the id \"x:y\" contains \",\" or \":\"." }));
	CHECK(speakers->get_speaker("anna")->get_display_name() == "SPEAKER_ANNA"); // The first one wins.

	speakers->remove_speaker("anna");
	CHECK(speakers->get_speaker("anna")->get_id() == StringName("ANNA")); // The second one.
	speakers->remove_speaker("anna");
	CHECK_FALSE(speakers->has_speaker("anna"));

	ERR_PRINT_OFF;
	speakers->add_speaker(Ref<DialogueSpeaker>());
	ERR_PRINT_ON;
	CHECK(speakers->get_speakers().size() == 4);
}

TEST_CASE("[SubtitleServer] Speakers: never null, signals, display names") {
	SubtitleServer *server = SubtitleServer::get_singleton();
	REQUIRE(server->get_speakers().is_valid());

	Ref<DialogueSpeakers> speakers;
	speakers.instantiate();
	speakers->add_speaker(make_speaker("anna", "Anna Smith"));

	SIGNAL_WATCH(server, "speakers_changed");
	server->set_speakers(speakers);
	CHECK(server->get_speakers() == speakers);
	SIGNAL_CHECK("speakers_changed", { { speakers } });
	speakers->get_speaker("anna")->set_color(Color(0, 0, 1)); // Changes of the list are signaled too.
	SIGNAL_CHECK("speakers_changed", { { speakers } });

	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	Ref<SubtitleSegment> segment;
	segment.instantiate();
	segment->set_text("Hello");
	segment->set_speaker("anna");
	CHECK(server->get_display_speaker(subtitle, segment) == "Anna Smith");
	segment->set_speaker("ANNA"); // A SubRip speaker.
	CHECK(server->get_display_speaker(subtitle, segment) == "Anna Smith");
	CHECK(server->get_display_text(subtitle, segment) == "Hello"); // The text stays without the name.
	segment->set_speaker("ben");
	CHECK(server->get_display_speaker(subtitle, segment) == "ben"); // Unknown: the id, as before.

	server->set_speakers(Ref<DialogueSpeakers>());
	CHECK(server->get_speakers().is_valid());
	CHECK(server->get_speakers() != speakers);
	CHECK(server->get_speakers()->get_speakers().is_empty());
	const Ref<DialogueSpeakers> empty = server->get_speakers();
	SIGNAL_CHECK("speakers_changed", { { empty } }); // The new, empty list.
	SIGNAL_UNWATCH(server, "speakers_changed");
}

TEST_CASE("[SubtitleServer][TTSServer] The TTS persona of a speaker is registered under the speaker id") {
	SubtitleServer *server = SubtitleServer::get_singleton();
	TTSServer *tts_server = TTSServer::get_singleton();
	REQUIRE(tts_server != nullptr);

	Ref<TTSPersona> shared;
	shared.instantiate();
	shared->set_persona_name("robot");
	shared->set_rate(1.5f);
	Ref<DialogueSpeakers> speakers;
	speakers.instantiate();
	speakers->add_speaker(make_speaker("unit_a", String(), shared));
	speakers->add_speaker(make_speaker("unit_b", String(), shared));
	speakers->add_speaker(make_speaker("anna")); // Text only.
	server->set_speakers(speakers);

	REQUIRE(tts_server->has_persona("unit_a"));
	REQUIRE(tts_server->has_persona("unit_b"));
	CHECK_FALSE(tts_server->has_persona("anna"));
	CHECK_FALSE(tts_server->has_persona("robot")); // The persona's own name is not used.
	CHECK(tts_server->get_persona("unit_a")->get_rate() == doctest::Approx(1.5f));
	CHECK(tts_server->get_persona("unit_a") != shared); // A copy per speaker.

	// An edit replaces our copy in place: a default persona of that name stays the default.
	tts_server->set_default_persona("unit_a");
	shared->set_rate(0.75f); // Edited in the Inspector: registered again.
	CHECK(tts_server->get_persona("unit_b")->get_rate() == doctest::Approx(0.75f));
	CHECK(tts_server->get_default_persona_name() == StringName("unit_a"));

	// A persona the game added under a speaker id is never overwritten or removed.
	Ref<TTSPersona> own;
	own.instantiate();
	own->set_persona_name("unit_b");
	own->set_rate(3.0f);
	tts_server->add_persona(own);
	ERR_PRINT_OFF; // The warning about the persona of the game.
	shared->set_rate(2.0f);
	ERR_PRINT_ON;
	CHECK(tts_server->get_persona("unit_b") == own);
	CHECK(tts_server->get_persona("unit_a")->get_rate() == doctest::Approx(2.0f));
	speakers->remove_speaker("unit_b");
	CHECK(tts_server->get_persona("unit_b") == own);

	server->set_speakers(Ref<DialogueSpeakers>());
	CHECK_FALSE(tts_server->has_persona("unit_a")); // Our copy is gone with its speaker.
	CHECK(tts_server->get_persona("unit_b") == own);
	tts_server->remove_persona("unit_b");
	tts_server->set_default_persona(StringName());
}

TEST_CASE("[SubtitleServer] Subtitle.validate() reports segments with unknown speakers") {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	Ref<SubtitleSegment> anna;
	anna.instantiate();
	anna->set_end_time(1.0);
	anna->set_text("Hello");
	anna->set_speaker("ANNA");
	subtitle->add_segment(anna);
	Ref<SubtitleSegment> carl;
	carl.instantiate();
	carl->set_start_time(2.0);
	carl->set_end_time(3.0);
	carl->set_text("Hi");
	carl->set_speaker("carl");
	subtitle->add_segment(carl);
	CHECK(subtitle->validate()->get_unknown_speaker_segments().is_empty()); // No project speakers: not checked.

	Ref<DialogueSpeakers> speakers;
	speakers.instantiate();
	speakers->add_speaker(make_speaker("anna"));
	SubtitleServer::get_singleton()->set_speakers(speakers);
	const Ref<SubtitleValidation> validation = subtitle->validate();
	CHECK(validation->get_unknown_speaker_segments() == PackedInt32Array({ 1 }));
	CHECK(validation->get_problem_count() == 1);
	SubtitleServer::get_singleton()->set_speakers(Ref<DialogueSpeakers>());
}

TEST_CASE("[SubtitleServer] A failed load of the project speakers is tried again on the next settings change") {
	SubtitleServer *server = SubtitleServer::get_singleton();
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const String previous = GLOBAL_GET(SubtitleServer::SETTING_SPEAKERS);
	const String path = TestUtils::get_temp_path("dialogue_speakers").path_join("speakers.tres");
	DirAccess::make_dir_recursive_absolute(path.get_base_dir());
	if (FileAccess::exists(path)) {
		DirAccess::remove_absolute(path);
	}

	project_settings->set_setting(SubtitleServer::SETTING_SPEAKERS, path);
	ERR_PRINT_OFF;
	server->load_project_speakers(); // The file does not exist yet.
	ERR_PRINT_ON;
	CHECK(server->get_speakers().is_valid());
	CHECK(server->get_speakers()->get_speakers().is_empty());

	Ref<DialogueSpeakers> speakers;
	speakers.instantiate();
	speakers->add_speaker(make_speaker("anna"));
	REQUIRE(ResourceSaver::save(speakers, path) == OK);
	project_settings->emit_signal("settings_changed"); // Any change of the settings; the path is the same.
	CHECK(server->get_speakers()->has_speaker("anna"));

	project_settings->set_setting(SubtitleServer::SETTING_SPEAKERS, previous);
	server->load_project_speakers();
	DirAccess::remove_absolute(path);
}

} // namespace TestDialogueSpeakers
