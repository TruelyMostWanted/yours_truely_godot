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

#include "core/string/translation.h"
#include "core/string/translation_domain.h"
#include "core/string/translation_server.h"
#include "servers/subtitle/dialogue_speaker.h"
#include "servers/subtitle/subtitle_server.h"
#include "servers/tts/tts_server.h"
#include "tests/signal_watcher.h"

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
	CHECK(speaker->get_color() == Color(1, 1, 1));
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
	list.push_back(make_speaker("anna"));
	speakers->set_speakers(list);
	CHECK(speakers->get_speakers().size() == 5);
	CHECK(speakers->get_ids() == PackedStringArray({ "anna", "ben" }));
	CHECK(speakers->validate() == PackedStringArray({ "Speaker 2 is empty.", "Speaker 3 has no id.", "Speakers 0 and 4 use the same id \"anna\"." }));
	CHECK(speakers->get_speaker("anna")->get_display_name() == "SPEAKER_ANNA"); // The first one wins.

	speakers->remove_speaker("anna");
	CHECK(speakers->get_speaker("anna")->get_display_name().is_empty()); // The second one.
	speakers->remove_speaker("anna");
	CHECK_FALSE(speakers->has_speaker("anna"));

	ERR_PRINT_OFF;
	speakers->add_speaker(Ref<DialogueSpeaker>());
	ERR_PRINT_ON;
	CHECK(speakers->get_speakers().size() == 3);
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
	CHECK(server->get_display_text(subtitle, segment) == "Hello"); // The text stays without the name.
	segment->set_speaker("ben");
	CHECK(server->get_display_speaker(subtitle, segment) == "ben"); // Unknown: the id, as before.

	server->set_speakers(Ref<DialogueSpeakers>());
	CHECK(server->get_speakers().is_valid());
	CHECK(server->get_speakers() != speakers);
	CHECK(server->get_speakers()->get_speakers().is_empty());
	SIGNAL_CHECK("speakers_changed", Array({ {} }));
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

	shared->set_rate(0.75f); // Edited in the Inspector: registered again.
	CHECK(tts_server->get_persona("unit_b")->get_rate() == doctest::Approx(0.75f));

	// A persona the game added under a speaker id is left alone.
	Ref<TTSPersona> own;
	own.instantiate();
	own->set_persona_name("unit_b");
	tts_server->add_persona(own);
	server->set_speakers(Ref<DialogueSpeakers>());
	CHECK_FALSE(tts_server->has_persona("unit_a"));
	CHECK(tts_server->get_persona("unit_b") == own);
	tts_server->remove_persona("unit_b");
}

} // namespace TestDialogueSpeakers
