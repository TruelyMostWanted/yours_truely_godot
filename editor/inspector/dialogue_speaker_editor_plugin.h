/**************************************************************************/
/*  dialogue_speaker_editor_plugin.h                                      */
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
#include "scene/gui/box_container.h"

class ColorRect;
class Label;
class TextureRect;

// Read-only preview at the top of the Inspector, without buttons. A DialogueSpeaker: portrait, name (theme font
// color) with a color swatch, id and TTS persona, a warning without an id. A DialogueSpeakers: whether it is the
// project's list and its validation. Follows `changed` of the resource.
class DialogueSpeakerPreview : public HBoxContainer {
	GDCLASS(DialogueSpeakerPreview, HBoxContainer);

	Ref<Resource> resource;
	TextureRect *portrait = nullptr;
	ColorRect *swatch = nullptr;
	Label *title = nullptr;
	Label *details = nullptr;
	Label *warning = nullptr;

	void _update();

protected:
	void _notification(int p_what);

public:
	DialogueSpeakerPreview(const Ref<Resource> &p_resource);
};

// Also refreshes the speaker id suggestions of the inspected object when the project's speakers change.
class EditorInspectorPluginDialogueSpeaker : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginDialogueSpeaker, EditorInspectorPlugin);

	bool refresh_queued = false;
	void _speakers_changed(const Ref<Resource> &p_speakers);
	void _refresh_id_hints();

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;

	EditorInspectorPluginDialogueSpeaker();
};
