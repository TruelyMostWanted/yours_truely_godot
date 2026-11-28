/**************************************************************************/
/*  subtitle_editor_plugin.cpp                                            */
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

#include "subtitle_editor_plugin.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/templates/local_vector.h"
#include "editor/audio/subtitles_dock.h"
#include "editor/docks/editor_dock_manager.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/inspector/dialogue_speaker_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/button.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/label.h"
#include "scene/gui/tree.h"
#include "scene/main/subtitle_player.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/scene_string_names.h"
#include "servers/subtitle/subtitle_format_srt.h"
#include "servers/subtitle/subtitle_server.h"

enum {
	COLUMN_INDEX,
	COLUMN_START,
	COLUMN_TEXT,
	COLUMN_MAX,
};

// SubtitleEditorHelper

String SubtitleEditorHelper::format_short_time(double p_seconds) {
	int64_t total_ms = int64_t(Math::round(MAX(0.0, p_seconds) * 1000.0));
	const int64_t ms = total_ms % 1000;
	total_ms /= 1000;
	const int64_t seconds = total_ms % 60;
	const int64_t minutes = (total_ms / 60) % 60;
	const int64_t hours = total_ms / 3600;
	const String rest = String::num_int64(seconds).pad_zeros(2) + "." + String::num_int64(ms).pad_zeros(3);
	if (hours > 0) {
		return itos(hours) + ":" + String::num_int64(minutes).pad_zeros(2) + ":" + rest;
	}
	return itos(minutes) + ":" + rest;
}

String SubtitleEditorHelper::get_row_text(const Ref<Subtitle> &p_subtitle, const Ref<SubtitleSegment> &p_segment) {
	ERR_FAIL_COND_V(p_segment.is_null(), String());
	String text = SubtitleServer::get_singleton() ? SubtitleServer::get_singleton()->get_display_text(p_subtitle, p_segment) : p_segment->get_text();
	const String speaker = String(p_segment->get_speaker());
	if (!speaker.is_empty() && !p_segment->is_caption()) {
		text = speaker + ": " + text;
	}
	return text.replace("\n", " / ");
}

double SubtitleEditorHelper::get_suggested_time_offset(const Ref<Subtitle> &p_subtitle) {
	if (p_subtitle.is_null() || p_subtitle->get_segment_count() == 0) {
		return 0.0;
	}
	const double first_start = p_subtitle->get_start_time() - p_subtitle->get_time_offset();
	if (first_start < 3600.0) {
		return 0.0;
	}
	return -Math::floor(first_start / 3600.0) * 3600.0;
}

TypedArray<SubtitleSegment> SubtitleEditorHelper::get_sorted_segments(const Ref<Subtitle> &p_subtitle) {
	ERR_FAIL_COND_V(p_subtitle.is_null(), TypedArray<SubtitleSegment>());
	Ref<Subtitle> copy;
	copy.instantiate();
	copy->set_segments(p_subtitle->get_segments());
	copy->sort_segments();
	return copy->get_segments();
}

TypedArray<SubtitleSegment> SubtitleEditorHelper::get_segments_with_new_segment(const Ref<Subtitle> &p_subtitle) {
	ERR_FAIL_COND_V(p_subtitle.is_null(), TypedArray<SubtitleSegment>());
	TypedArray<SubtitleSegment> segments = p_subtitle->get_segments();
	double start = 0.0;
	for (int i = 0; i < segments.size(); i++) {
		const Ref<SubtitleSegment> segment = segments[i];
		if (segment.is_valid()) {
			start = MAX(start, segment->get_end_time());
		}
	}
	Ref<SubtitleSegment> segment;
	segment.instantiate();
	segment->set_start_time(start);
	segment->set_end_time(start + 2.0);
	segments.push_back(segment);
	return segments;
}

String SubtitleEditorHelper::get_kind_name(SubtitleSegment::Kind p_kind) {
	switch (p_kind) {
		case SubtitleSegment::KIND_DIALOG:
			return TTR("Dialog");
		case SubtitleSegment::KIND_NARRATION:
			return TTR("Narration");
		case SubtitleSegment::KIND_SOUND:
			return TTR("Sound");
		case SubtitleSegment::KIND_REACTION:
			return TTR("Reaction");
		case SubtitleSegment::KIND_MUSIC:
			return TTR("Music");
		case SubtitleSegment::KIND_LYRICS:
			return TTR("Lyrics");
		case SubtitleSegment::KIND_ON_SCREEN_TEXT:
			return TTR("On-Screen Text");
		default:
			return String();
	}
}

bool SubtitleEditorHelper::parse_short_time(const String &p_text, double &r_seconds) {
	const PackedStringArray parts = p_text.strip_edges().replace(",", ".").split(":");
	if (parts.size() > 3) {
		return false;
	}
	double seconds = 0.0;
	for (int i = 0; i < parts.size(); i++) {
		const bool last = i == parts.size() - 1;
		if (last ? !parts[i].is_valid_float() : !parts[i].is_valid_int()) {
			return false;
		}
		seconds = seconds * 60.0 + (last ? parts[i].to_float() : double(parts[i].to_int()));
	}
	if (seconds < 0.0) {
		return false;
	}
	r_seconds = seconds;
	return true;
}

int SubtitleEditorHelper::get_insert_index(const Ref<Subtitle> &p_subtitle, double p_start_time) {
	ERR_FAIL_COND_V(p_subtitle.is_null(), 0);
	const int count = p_subtitle->get_segment_count();
	for (int i = 0; i < count; i++) {
		const Ref<SubtitleSegment> segment = p_subtitle->get_segment(i);
		if (segment.is_valid() && segment->get_start_time() > p_start_time) {
			return i;
		}
	}
	return count;
}

TypedArray<SubtitleSegment> SubtitleEditorHelper::get_segments_with_segment_at(const Ref<Subtitle> &p_subtitle, double p_start_time, double p_end_time) {
	ERR_FAIL_COND_V(p_subtitle.is_null(), TypedArray<SubtitleSegment>());
	TypedArray<SubtitleSegment> segments = p_subtitle->get_segments();
	Ref<SubtitleSegment> segment;
	segment.instantiate();
	segment->set_start_time(p_start_time);
	segment->set_end_time(p_end_time);
	segments.insert(get_insert_index(p_subtitle, p_start_time), segment);
	return segments;
}

HashMap<int, PackedStringArray> SubtitleEditorHelper::get_problems(const Ref<Subtitle> &p_subtitle) {
	ERR_FAIL_COND_V(p_subtitle.is_null(), (HashMap<int, PackedStringArray>()));
	return get_problems(p_subtitle->validate());
}

HashMap<int, PackedStringArray> SubtitleEditorHelper::get_problems(const Ref<SubtitleValidation> &p_validation) {
	HashMap<int, PackedStringArray> problems;
	ERR_FAIL_COND_V(p_validation.is_null(), problems);
	const Ref<SubtitleValidation> &validation = p_validation;
	const String invalid_time = TTR("The end is not after the start, or the start is negative.");
	const String empty_text = TTR("The text is empty.");
	const String overlapping = TTR("Overlaps the next segment.");
	const String fast_reading = TTR("More than 20 characters per second: hard to read in time.");
	for (int index : validation->get_invalid_time_segments()) {
		problems[index].push_back(invalid_time);
	}
	for (int index : validation->get_empty_text_segments()) {
		problems[index].push_back(empty_text);
	}
	for (int index : validation->get_overlapping_segments()) {
		problems[index].push_back(overlapping);
	}
	for (int index : validation->get_fast_reading_segments()) {
		problems[index].push_back(fast_reading);
	}
	return problems;
}

// SubtitleEditor

void SubtitleEditor::_queue_update() {
	if (update_queued) {
		return;
	}
	update_queued = true;
	callable_mp(this, &SubtitleEditor::_update).call_deferred();
}

void SubtitleEditor::_watch_segments() {
	const Vector<Ref<SubtitleSegment>> segments = subtitle->get_segment_list();
	// Rewire only when the segment objects themselves changed (add, remove, reorder), not on every edit.
	if (segments == watched_segments) {
		return;
	}
	const Callable callback = callable_mp(this, &SubtitleEditor::_queue_update);
	for (const Ref<SubtitleSegment> &segment : watched_segments) {
		if (segment.is_valid() && segment->is_connected(CoreStringName(changed), callback)) {
			segment->disconnect_changed(callback);
		}
	}
	watched_segments = segments;
	for (const Ref<SubtitleSegment> &segment : watched_segments) {
		if (segment.is_valid() && !segment->is_connected(CoreStringName(changed), callback)) {
			segment->connect_changed(callback);
		}
	}
}

void SubtitleEditor::_update() {
	update_queued = false;
	if (subtitle.is_null()) {
		return;
	}
	_watch_segments();

	const Ref<SubtitleValidation> validation = subtitle->validate();
	const int count = subtitle->get_segment_count();
	String summary = vformat(TTRN("%d segment", "%d segments", count), count);
	if (count > 0) {
		summary += ", " + SubtitleEditorHelper::format_short_time(subtitle->get_start_time()) + " - " + SubtitleEditorHelper::format_short_time(subtitle->get_end_time());
	}
	if (!subtitle->get_language().is_empty()) {
		summary += ", " + subtitle->get_language();
	}
	if (validation->get_problem_count() > 0) {
		summary += ". " + vformat(TTR("Problems: %d (highlighted rows, see their tooltips)."), validation->get_problem_count());
	}
	summary_label->set_text(summary);

	const double suggested = SubtitleEditorHelper::get_suggested_time_offset(subtitle);
	offset_box->set_visible(suggested != 0.0 && subtitle->get_time_offset() == 0.0);
	offset_label->set_text(TTR("The first segment starts after one hour, as in files exported from video editors."));
	offset_button->set_text(vformat(TTR("Set Time Offset to -%s"), SubtitleFormatSRT::format_time(-suggested).get_slice(",", 0)));

	// Subtitle files have no offset field: saving them moves the offset into the written times.
	const String extension = subtitle->get_path().get_extension().to_lower();
	save_note_label->set_visible(subtitle->get_time_offset() != 0.0 && Subtitle::get_recognized_extensions().has(extension));
	save_note_label->set_text(vformat(TTR("Saving as .%s adds the time offset to the written times, because the format has no offset field."), extension));

	const HashMap<int, PackedStringArray> problems = SubtitleEditorHelper::get_problems(validation);

	// Update the rows in place: only cells whose text changed are touched, and the selection and scroll position stay.
	TreeItem *root = tree->get_root();
	if (root == nullptr) {
		root = tree->create_item();
	}
	const Color warning_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	for (int i = 0; i < count; i++) {
		TreeItem *item = i < root->get_child_count() ? root->get_child(i) : nullptr;
		if (item == nullptr) {
			item = tree->create_item(root);
			item->set_metadata(0, i);
		}
		const Ref<SubtitleSegment> segment = subtitle->get_segment(i);
		String start_text;
		String row_text;
		String details;
		if (segment.is_valid()) {
			const double start = segment->get_start_time() + subtitle->get_time_offset();
			const double end = segment->get_end_time() + subtitle->get_time_offset();
			start_text = SubtitleEditorHelper::format_short_time(start);
			row_text = SubtitleEditorHelper::get_row_text(subtitle, segment);
			details = vformat(TTR("%s - %s (%s s), %s"), SubtitleFormatSRT::format_time(start), SubtitleFormatSRT::format_time(end), String::num(segment->get_duration(), 3), SubtitleEditorHelper::get_kind_name(segment->get_kind()));
		} else {
			row_text = TTR("<empty slot>");
		}
		item->set_text(COLUMN_INDEX, itos(i + 1));
		item->set_text(COLUMN_START, start_text);
		item->set_text(COLUMN_TEXT, row_text);
		String tooltip_start = details;
		String tooltip_text = segment.is_valid() ? details + "\n" + segment->get_text() : String();
		const bool has_problem = problems.has(i);
		if (has_problem) {
			const String problem_text = String("\n").join(problems.get(i));
			tooltip_start += "\n" + problem_text;
			tooltip_text += "\n" + problem_text;
			item->set_tooltip_text(COLUMN_INDEX, problem_text);
		} else {
			item->set_tooltip_text(COLUMN_INDEX, String());
		}
		item->set_tooltip_text(COLUMN_START, tooltip_start);
		item->set_tooltip_text(COLUMN_TEXT, tooltip_text);
		if (has_problem) {
			for (int column = 0; column < COLUMN_MAX; column++) {
				item->set_custom_color(column, warning_color);
			}
		} else if (problem_rows.has(i)) {
			for (int column = 0; column < COLUMN_MAX; column++) {
				item->clear_custom_color(column);
			}
		}
	}
	// Remove the rows of segments that are gone.
	// Collect the surplus rows first: each delete clears the parent's child cache.
	LocalVector<TreeItem *> surplus;
	for (TreeItem *item = count < root->get_child_count() ? root->get_child(count) : nullptr; item != nullptr; item = item->get_next()) {
		surplus.push_back(item);
	}
	for (TreeItem *item : surplus) {
		memdelete(item);
	}
	problem_rows.clear();
	for (const KeyValue<int, PackedStringArray> &problem : problems) {
		problem_rows.insert(problem.key);
	}
}

void SubtitleEditor::_commit_segments(const String &p_action, const TypedArray<SubtitleSegment> &p_segments) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action, UndoRedo::MERGE_DISABLE, subtitle.ptr());
	undo_redo->add_do_property(subtitle.ptr(), "segments", p_segments);
	undo_redo->add_undo_property(subtitle.ptr(), "segments", subtitle->get_segments());
	undo_redo->commit_action();
}

void SubtitleEditor::_apply_suggested_offset() {
	const double suggested = SubtitleEditorHelper::get_suggested_time_offset(subtitle);
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Set Subtitle Time Offset"), UndoRedo::MERGE_DISABLE, subtitle.ptr());
	undo_redo->add_do_property(subtitle.ptr(), "time_offset", suggested);
	undo_redo->add_undo_property(subtitle.ptr(), "time_offset", subtitle->get_time_offset());
	undo_redo->commit_action();
}

void SubtitleEditor::_add_segment() {
	_commit_segments(TTR("Add Subtitle Segment"), SubtitleEditorHelper::get_segments_with_new_segment(subtitle));
}

void SubtitleEditor::_sort_segments() {
	_commit_segments(TTR("Sort Subtitle Segments"), SubtitleEditorHelper::get_sorted_segments(subtitle));
}

void SubtitleEditor::_item_activated() {
	TreeItem *item = tree->get_selected();
	if (item == nullptr) {
		return;
	}
	const Ref<SubtitleSegment> segment = subtitle->get_segment(item->get_metadata(0));
	if (segment.is_valid()) {
		EditorInterface::get_singleton()->edit_resource(segment);
	}
}

void SubtitleEditor::_open_in_dock() {
	SubtitlesDock *dock = SubtitlesDock::get_singleton();
	ERR_FAIL_NULL(dock);
	dock->edit(subtitle);
	dock->make_visible();
}

void SubtitleEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			sort_button->set_button_icon(get_editor_theme_icon(SNAME("Sort")));
			_queue_update();
		} break;
	}
}

SubtitleEditor::SubtitleEditor(const Ref<Subtitle> &p_subtitle) {
	subtitle = p_subtitle;

	summary_label = memnew(Label);
	summary_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	summary_label->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	add_child(summary_label);

	offset_box = memnew(VBoxContainer);
	offset_box->hide();
	add_child(offset_box);
	offset_label = memnew(Label);
	offset_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	offset_box->add_child(offset_label);
	offset_button = memnew(Button);
	offset_button->set_h_size_flags(SIZE_SHRINK_BEGIN);
	offset_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitleEditor::_apply_suggested_offset));
	offset_box->add_child(offset_button);

	save_note_label = memnew(Label);
	save_note_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	save_note_label->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	save_note_label->hide();
	add_child(save_note_label);

	HFlowContainer *toolbar = memnew(HFlowContainer);
	add_child(toolbar);
	add_button = memnew(Button(TTR("Add Segment")));
	add_button->set_tooltip_text(TTR("Add a two-second segment after the last one."));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitleEditor::_add_segment));
	toolbar->add_child(add_button);
	sort_button = memnew(Button(TTR("Sort by Time")));
	sort_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitleEditor::_sort_segments));
	toolbar->add_child(sort_button);
	Button *dock_button = memnew(Button(TTR("Open in Subtitles Dock")));
	dock_button->set_tooltip_text(TTR("Edit the cues on a timeline with the waveform of the audio, and spot them while it plays."));
	dock_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitleEditor::_open_in_dock));
	toolbar->add_child(dock_button);

	tree = memnew(Tree);
	tree->set_columns(COLUMN_MAX);
	tree->set_hide_root(true);
	tree->set_column_titles_visible(true);
	tree->set_column_title(COLUMN_INDEX, "#");
	tree->set_column_title(COLUMN_START, TTR("Start"));
	tree->set_column_title(COLUMN_TEXT, TTR("Text"));
	for (int column = 0; column < COLUMN_TEXT; column++) {
		tree->set_column_expand(column, false);
		tree->set_column_clip_content(column, false);
	}
	tree->set_column_custom_minimum_width(COLUMN_INDEX, 32 * EDSCALE);
	tree->set_column_custom_minimum_width(COLUMN_START, 116 * EDSCALE);
	tree->set_column_expand(COLUMN_TEXT, true);
	tree->set_column_clip_content(COLUMN_TEXT, true);
	tree->set_custom_minimum_size(Size2(0, 240) * EDSCALE);
	tree->set_tooltip_text(TTR("Double-click a row to edit the segment."));
	// Deferred: editing the segment rebuilds the Inspector, which frees this tree while it emits.
	tree->connect("item_activated", callable_mp(this, &SubtitleEditor::_item_activated), CONNECT_DEFERRED);
	add_child(tree);

	subtitle->connect_changed(callable_mp(this, &SubtitleEditor::_queue_update));
}

SubtitleEditor::~SubtitleEditor() {
	const Callable callback = callable_mp(this, &SubtitleEditor::_queue_update);
	for (const Ref<SubtitleSegment> &segment : watched_segments) {
		if (segment.is_valid() && segment->is_connected(CoreStringName(changed), callback)) {
			segment->disconnect_changed(callback);
		}
	}
	if (subtitle.is_valid() && subtitle->is_connected(CoreStringName(changed), callback)) {
		subtitle->disconnect_changed(callback);
	}
}

// EditorInspectorPluginSubtitle

bool EditorInspectorPluginSubtitle::can_handle(Object *p_object) {
	return Object::cast_to<Subtitle>(p_object) != nullptr || Object::cast_to<AudioStream>(p_object) != nullptr;
}

void EditorInspectorPluginSubtitle::_open_stream_in_dock(ObjectID p_stream) {
	SubtitlesDock *dock = SubtitlesDock::get_singleton();
	AudioStream *stream = ObjectDB::get_instance<AudioStream>(p_stream);
	ERR_FAIL_COND(dock == nullptr || stream == nullptr);
	dock->edit(Ref<AudioStream>(stream));
	dock->make_visible();
}

void EditorInspectorPluginSubtitle::parse_category(Object *p_object, const String &p_category) {
	AudioStream *stream = Object::cast_to<AudioStream>(p_object);
	if (stream != nullptr) {
		if (p_category == "AudioStream" && !SubtitlesDock::get_stream_subtitle_path(stream).is_empty()) {
			Button *button = memnew(Button(TTR("Open in Subtitles Dock")));
			button->set_tooltip_text(TTR("Edit the subtitle file of this stream on a timeline with its waveform."));
			button->connect(SceneStringName(pressed), callable_mp(this, &EditorInspectorPluginSubtitle::_open_stream_in_dock).bind(stream->get_instance_id()));
			add_custom_control(button);
		}
		return;
	}
	// Below the class header of Subtitle, like the class's own properties.
	if (p_category != "Subtitle") {
		return;
	}
	Ref<Subtitle> subtitle(Object::cast_to<Subtitle>(p_object));
	ERR_FAIL_COND(subtitle.is_null());
	add_custom_control(memnew(SubtitleEditor(subtitle)));
}

// EditorInspectorPluginSubtitleSync

static const char *const _subtitle_sync_classes[] = { "AudioStreamPlayer", "AudioStreamPlayer2D", "AudioStreamPlayer3D", "VideoStreamPlayer" };

bool EditorInspectorPluginSubtitleSync::can_handle(Object *p_object) {
	// Variant, Array and Dictionary editors ask every plugin with a null object.
	if (p_object == nullptr) {
		return false;
	}
	for (const char *class_name : _subtitle_sync_classes) {
		if (p_object->is_class(class_name)) {
			return true;
		}
	}
	return false;
}

// The child SubtitlePlayer that already follows `p_player`, if any.
static SubtitlePlayer *_find_subtitle_player(Node *p_player) {
	for (int i = 0; i < p_player->get_child_count(); i++) {
		SubtitlePlayer *subtitle_player = Object::cast_to<SubtitlePlayer>(p_player->get_child(i));
		if (subtitle_player != nullptr && subtitle_player->get_node_or_null(subtitle_player->get_sync_node()) == p_player) {
			return subtitle_player;
		}
	}
	return nullptr;
}

void EditorInspectorPluginSubtitleSync::parse_category(Object *p_object, const String &p_category) {
	for (const char *class_name : _subtitle_sync_classes) {
		if (p_category == class_name) {
			Node *player = Object::cast_to<Node>(p_object);
			const bool has_player = player != nullptr && _find_subtitle_player(player) != nullptr;
			Button *button = memnew(Button);
			button->set_text(has_player ? TTR("Select SubtitlePlayer") : TTR("Add SubtitlePlayer"));
			button->set_tooltip_text(has_player ? TTR("Selects the child SubtitlePlayer that follows this player.") : TTR("Adds a child SubtitlePlayer that follows this player and plays the subtitle_path of this player or of its stream."));
			// Deferred: selecting a node rebuilds the Inspector, which frees this button while it emits.
			button->connect(SceneStringName(pressed), callable_mp(this, &EditorInspectorPluginSubtitleSync::_add_subtitle_player).bind(p_object->get_instance_id()), CONNECT_DEFERRED);
			add_custom_control(button);
			return;
		}
	}
}

void EditorInspectorPluginSubtitleSync::_add_subtitle_player(ObjectID p_player) {
	Node *player = ObjectDB::get_instance<Node>(p_player);
	Node *scene_root = EditorInterface::get_singleton()->get_edited_scene_root();
	ERR_FAIL_COND(player == nullptr || scene_root == nullptr);
	// One SubtitlePlayer per player: a second press selects the one there is.
	SubtitlePlayer *existing = _find_subtitle_player(player);
	if (existing != nullptr) {
		EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
		selection->clear();
		selection->add_node(existing);
		EditorInterface::get_singleton()->edit_node(existing);
		return;
	}
	SubtitlePlayer *subtitle_player = memnew(SubtitlePlayer);
	subtitle_player->set_name("SubtitlePlayer");
	subtitle_player->set_sync_node(NodePath(".."));

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Add SubtitlePlayer"), UndoRedo::MERGE_DISABLE, player);
	undo_redo->add_do_method(player, "add_child", subtitle_player, true);
	undo_redo->add_do_method(subtitle_player, "set_owner", scene_root);
	undo_redo->add_do_reference(subtitle_player);
	undo_redo->add_undo_method(player, "remove_child", subtitle_player);
	undo_redo->commit_action();
}

// SubtitleEditorPlugin

void SubtitleEditorPlugin::edit(Object *p_object) {
	dock->edit(Ref<Resource>(Object::cast_to<Resource>(p_object)));
}

bool SubtitleEditorPlugin::handles(Object *p_object) const {
	return SubtitlesDock::can_edit(p_object);
}

void SubtitleEditorPlugin::make_visible(bool p_visible) {
	// Never closed here (like the Animation dock): it closes with its own close button.
	if (p_visible) {
		dock->make_visible();
	}
}

SubtitleEditorPlugin::SubtitleEditorPlugin() {
	dock = memnew(SubtitlesDock);
	EditorDockManager::get_singleton()->add_dock(dock);
	dock->close();

	Ref<EditorInspectorPluginSubtitle> plugin;
	plugin.instantiate();
	add_inspector_plugin(plugin);
	Ref<EditorInspectorPluginSubtitleSync> sync_plugin;
	sync_plugin.instantiate();
	add_inspector_plugin(sync_plugin);
	Ref<EditorInspectorPluginDialogueSpeaker> speaker_plugin; // Portrait and validation previews (#483).
	speaker_plugin.instantiate();
	add_inspector_plugin(speaker_plugin);
}
