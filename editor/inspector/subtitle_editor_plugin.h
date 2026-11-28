/**************************************************************************/
/*  subtitle_editor_plugin.h                                              */
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

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"
#include "servers/subtitle/subtitle.h"

class Button;
class Label;
class Tree;

// Pure helpers of the Subtitle Inspector, unit-tested without editor state.
class SubtitleEditorHelper {
public:
	// The offset that moves a subtitle exported from a video editor (first segment at one hour or later) to zero:
	// minus the whole hours of the first start time. Zero if the first segment starts before one hour.
	static double get_suggested_time_offset(const Ref<Subtitle> &p_subtitle);
	// The segments sorted by start time, then end time, without changing `p_subtitle`.
	static TypedArray<SubtitleSegment> get_sorted_segments(const Ref<Subtitle> &p_subtitle);
	// The segments with a new two-second segment after the last one.
	static TypedArray<SubtitleSegment> get_segments_with_new_segment(const Ref<Subtitle> &p_subtitle);
	// "0:05.500", or "1:00:05.500" from one hour on.
	static String format_short_time(double p_seconds);
	// The text as the player sees it (decorated by kind), with the speaker in front; one line.
	static String get_row_text(const Ref<Subtitle> &p_subtitle, const Ref<SubtitleSegment> &p_segment);
	// "Dialog", "Sound", …
	static String get_kind_name(SubtitleSegment::Kind p_kind);
	// Reads "5.5", "0:05.500", "1:00:05.500" or an SRT time ("00:00:05,500") as seconds.
	static bool parse_short_time(const String &p_text, double &r_seconds);
	// The index at which a segment starting at `p_start_time` keeps the segments sorted by start time.
	static int get_insert_index(const Ref<Subtitle> &p_subtitle, double p_start_time);
	// The segments with a new segment from `p_start_time` to `p_end_time`, inserted at `get_insert_index()`.
	static TypedArray<SubtitleSegment> get_segments_with_segment_at(const Ref<Subtitle> &p_subtitle, double p_start_time, double p_end_time);
	// The validation problems of each segment index, as readable messages.
	static HashMap<int, PackedStringArray> get_problems(const Ref<Subtitle> &p_subtitle);
	// The same from an existing validation result, so a caller that needs both validates once.
	static HashMap<int, PackedStringArray> get_problems(const Ref<SubtitleValidation> &p_validation);
};

// The segments of a `Subtitle` as a table under its class header in the Inspector.
class SubtitleEditor : public VBoxContainer {
	GDCLASS(SubtitleEditor, VBoxContainer);

	Ref<Subtitle> subtitle;
	Vector<Ref<SubtitleSegment>> watched_segments;
	HashSet<int> problem_rows;
	bool update_queued = false;

	Label *summary_label = nullptr;
	VBoxContainer *offset_box = nullptr;
	Label *offset_label = nullptr;
	Button *offset_button = nullptr;
	Label *save_note_label = nullptr;
	Button *add_button = nullptr;
	Button *sort_button = nullptr;
	Tree *tree = nullptr;

	void _queue_update();
	void _update();
	void _watch_segments();
	void _commit_segments(const String &p_action, const TypedArray<SubtitleSegment> &p_segments);
	void _apply_suggested_offset();
	void _add_segment();
	void _sort_segments();
	void _item_activated();
	void _open_in_dock();

protected:
	void _notification(int p_what);

public:
	SubtitleEditor(const Ref<Subtitle> &p_subtitle);
	~SubtitleEditor();
};

class EditorInspectorPluginSubtitle : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginSubtitle, EditorInspectorPlugin);

	void _open_stream_in_dock(ObjectID p_stream);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_category(Object *p_object, const String &p_category) override;
};

// A button "Add SubtitlePlayer" under the class header of the audio and video players.
class EditorInspectorPluginSubtitleSync : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginSubtitleSync, EditorInspectorPlugin);

	void _add_subtitle_player(ObjectID p_player);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_category(Object *p_object, const String &p_category) override;
};

class SubtitlesDock;

class SubtitleEditorPlugin : public EditorPlugin {
	GDCLASS(SubtitleEditorPlugin, EditorPlugin);

	SubtitlesDock *dock = nullptr;

public:
	virtual String get_plugin_name() const override { return "Subtitle"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	// The dock stays open while a segment or another object is inspected; it closes with its close button.
	virtual bool can_auto_hide() const override { return false; }
	virtual void make_visible(bool p_visible) override;

	SubtitleEditorPlugin();
};
