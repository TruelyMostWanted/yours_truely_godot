/**************************************************************************/
/*  test_editor_speaker_picker.cpp                                        */
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

TEST_FORCE_LINK(test_editor_speaker_picker)

#ifdef TOOLS_ENABLED

#include "editor/inspector/editor_speaker_picker.h"

namespace TestEditorSpeakerPicker {

static Ref<DialogueSpeaker> make_speaker(const StringName &p_id, const String &p_display_name = String()) {
	Ref<DialogueSpeaker> speaker;
	speaker.instantiate();
	speaker->set_id(p_id);
	speaker->set_display_name(p_display_name);
	return speaker;
}

TEST_CASE("[EditorSpeakerPicker] Speakers of several sources, each id once") {
	Vector<Ref<DialogueSpeaker>> speakers;
	Vector<String> sources;
	EditorSpeakerPicker::merge_speakers({ make_speaker("anna", "Anna Smith"), make_speaker("ben") }, "res://speakers.tres", speakers, sources);
	// A second file: "ANNA" is the same speaker (ids ignore case), empty ids and null slots are skipped.
	EditorSpeakerPicker::merge_speakers({ make_speaker("ANNA"), make_speaker(StringName()), Ref<DialogueSpeaker>(), make_speaker("carl") }, "res://extra/carl.tres", speakers, sources);
	REQUIRE(speakers.size() == 3);
	CHECK(speakers[0]->get_display_name() == "Anna Smith"); // The first source wins.
	CHECK(speakers[2]->get_id() == StringName("carl"));
	CHECK(sources == Vector<String>({ "res://speakers.tres", "res://speakers.tres", "res://extra/carl.tres" }));
}

TEST_CASE("[EditorSpeakerPicker] Fuzzy search over names and ids") {
	const Vector<Ref<DialogueSpeaker>> speakers = { make_speaker("anna", "Anna Smith"), make_speaker("ben", "Benjamin"), make_speaker("guard_1", "Gate Guard") };
	CHECK(EditorSpeakerPicker::rank_speakers(speakers, "") == Vector<int>({ 0, 1, 2 })); // No query: all, in list order.
	CHECK(EditorSpeakerPicker::rank_speakers(speakers, "  ") == Vector<int>({ 0, 1, 2 }));
	const Vector<int> guard = EditorSpeakerPicker::rank_speakers(speakers, "guard");
	REQUIRE_FALSE(guard.is_empty());
	CHECK(guard[0] == 2);
	const Vector<int> by_id = EditorSpeakerPicker::rank_speakers(speakers, "ben");
	REQUIRE_FALSE(by_id.is_empty());
	CHECK(by_id[0] == 1);
	const Vector<int> partial = EditorSpeakerPicker::rank_speakers(speakers, "smi"); // Part of the display name.
	REQUIRE_FALSE(partial.is_empty());
	CHECK(partial[0] == 0);
	CHECK(EditorSpeakerPicker::rank_speakers(speakers, "zzzz").is_empty());
}

} // namespace TestEditorSpeakerPicker

#endif // TOOLS_ENABLED
