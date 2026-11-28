/**************************************************************************/
/*  editor_speaker_picker.h                                               */
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

#include "editor/inspector/editor_inspector.h"
#include "scene/gui/dialogs.h"
#include "servers/subtitle/dialogue_speaker.h"

class Button;
class EditorFileSystemDirectory;
class LineEdit;
class Tree;

// Quick find for speakers, like Quick Open: the project's speakers (SubtitleServer.speakers) and every
// DialogueSpeaker and DialogueSpeakers resource of the project. Rows show portrait, name, id and color; a fuzzy
// search filters by name and id. Emits `speaker_picked` with the id, the stable key a field stores.
class EditorSpeakerPicker : public ConfirmationDialog {
	GDCLASS(EditorSpeakerPicker, ConfirmationDialog);

	LineEdit *search = nullptr;
	Tree *list = nullptr;
	Vector<Ref<DialogueSpeaker>> speakers;
	Vector<String> sources;
	StringName current_id;

	void _collect();
	void _collect_dir(EditorFileSystemDirectory *p_dir);
	void _update_list();
	void _search_input(const Ref<InputEvent> &p_event);
	void _pick();

protected:
	void _notification(int p_what);
	static void _bind_methods();
	// The OK button and Enter in the search field. Not the `confirmed` signal: the class is not in ClassDB until
	// its constructor returns, so connecting to inherited signals there fails.
	virtual void ok_pressed() override;

public:
	// Adds the speakers of `p_add` whose id is not in `r_speakers` yet (ignoring case), with their source path.
	static void merge_speakers(const Vector<Ref<DialogueSpeaker>> &p_add, const String &p_source, Vector<Ref<DialogueSpeaker>> &r_speakers, Vector<String> &r_sources);
	// Indices of the speakers that match `p_query` (fuzzy, over name and id), best first; all of them for an empty query.
	static Vector<int> rank_speakers(const Vector<Ref<DialogueSpeaker>> &p_speakers, const String &p_query);

	void popup_picker(const StringName &p_current_id = StringName());

	EditorSpeakerPicker();
};

// The Inspector editor of a speaker id (SubtitleSegment.speaker, DialogueLineNode.speaker): a text field that
// takes any id, and a button that opens EditorSpeakerPicker.
class EditorPropertySpeakerId : public EditorProperty {
	GDCLASS(EditorPropertySpeakerId, EditorProperty);

	LineEdit *line_edit = nullptr;
	Button *pick_button = nullptr;
	EditorSpeakerPicker *picker = nullptr;

	void _text_submitted(const String &p_text);
	void _commit();
	void _open_picker();
	void _picked(const StringName &p_id);

protected:
	void _notification(int p_what);

public:
	virtual void update_property() override;

	EditorPropertySpeakerId();
};
