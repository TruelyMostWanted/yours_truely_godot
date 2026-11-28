/**************************************************************************/
/*  dialogue_speaker.h                                                    */
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
#include "core/variant/typed_array.h"
#include "servers/tts/tts_persona.h"

class Texture2D;

// Who speaks a line. One id for dialogue lines, subtitle segments, `SubtitleLabel` colors and the TTS persona.
class DialogueSpeaker : public Resource {
	GDCLASS(DialogueSpeaker, Resource);

	StringName id;
	String display_name;
	String portrait_path;
	Color color = Color(0, 0, 0, 0); // Alpha 0: no color of its own (the label's text color).
	Ref<TTSPersona> tts_persona;

	void _persona_changed();

protected:
	static void _bind_methods();

public:
	// Speaker ids compare without case: SubRip writes `ANNA:`, WebVTT `<v Anna>`, a game `anna`.
	static bool ids_match(const StringName &p_a, const StringName &p_b);

	void set_id(const StringName &p_id);
	StringName get_id() const;
	void set_display_name(const String &p_display_name);
	String get_display_name() const;
	void set_portrait_path(const String &p_path);
	String get_portrait_path() const;
	void set_color(const Color &p_color);
	Color get_color() const;
	void set_tts_persona(const Ref<TTSPersona> &p_persona);
	Ref<TTSPersona> get_tts_persona() const;

	// `display_name` (the id if it is empty), translated in the `subtitles` channel with the context `speaker`.
	String get_translated_name() const;
	// Loads `portrait_path` only when called, so a list of speakers keeps no textures in memory.
	Ref<Texture2D> load_portrait() const;

	~DialogueSpeaker();
};

// The speakers of a game, found by id. `SubtitleServer.speakers` holds the project's speakers.
class DialogueSpeakers : public Resource {
	GDCLASS(DialogueSpeakers, Resource);

	Vector<Ref<DialogueSpeaker>> speakers;

	void _speaker_changed();
	void _connect_speakers(bool p_connect);

protected:
	static void _bind_methods();

public:
	void set_speakers(const TypedArray<DialogueSpeaker> &p_speakers);
	TypedArray<DialogueSpeaker> get_speakers() const;
	const Vector<Ref<DialogueSpeaker>> &get_speaker_list() const { return speakers; }

	void add_speaker(const Ref<DialogueSpeaker> &p_speaker);
	void remove_speaker(const StringName &p_id);
	// The first speaker with this id (case-insensitive), or null.
	Ref<DialogueSpeaker> get_speaker(const StringName &p_id) const;
	bool has_speaker(const StringName &p_id) const;
	PackedStringArray get_ids() const;
	// One message per problem (empty slot, empty id, `,` or `:` in an id, id used twice ignoring case); empty if valid.
	PackedStringArray validate() const;
	// POT entries [text, context, "", comment] for ResourceTranslationParserPlugin: the display names.
	TypedArray<PackedStringArray> get_translatable_strings() const;

	~DialogueSpeakers();
};
