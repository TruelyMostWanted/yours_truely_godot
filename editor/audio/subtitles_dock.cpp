/**************************************************************************/
/*  subtitles_dock.cpp                                                    */
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

#include "subtitles_dock.h"

#include "core/input/input_event.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "editor/audio/audio_stream_preview.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_toaster.h"
#include "editor/inspector/editor_resource_picker.h"
#include "editor/inspector/editor_speaker_picker.h"
#include "editor/inspector/subtitle_editor_plugin.h"
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/gui/aspect_ratio_container.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/gui/video_stream_player.h"
#include "scene/resources/audio/audio_stream.h"
#include "scene/resources/video_stream.h"
#include "scene/scene_string_names.h"

// Static helpers

String SubtitlesDock::get_stream_subtitle_path(const Resource *p_stream) {
	String path;
	if (const AudioStream *audio = Object::cast_to<AudioStream>(p_stream)) {
		path = audio->get_subtitle_path();
	} else if (const VideoStream *video_stream = Object::cast_to<VideoStream>(p_stream)) {
		path = video_stream->get_subtitle_path();
	} else {
		return String();
	}
	if (!path.is_empty()) {
		return path;
	}
	// The same sidecar rule as SubtitlePlayer.
	const String file = p_stream->get_path();
	if (!file.is_resource_file()) {
		return String();
	}
	static const char *const sidecar_extensions[] = { ".srt", ".vtt" };
	for (const char *extension : sidecar_extensions) {
		const String sidecar = file.get_basename() + extension;
		if (ResourceLoader::exists(sidecar, "Subtitle")) {
			return sidecar;
		}
	}
	return String();
}

String SubtitlesDock::find_sidecar_stream(const String &p_subtitle_path) {
	if (!p_subtitle_path.is_resource_file()) {
		return String();
	}
	// Audio first: its waveform is what spotting works with.
	for (const char *type : { "AudioStream", "VideoStream" }) {
		List<String> extensions;
		ResourceLoader::get_recognized_extensions_for_type(type, &extensions);
		for (const String &extension : extensions) {
			const String candidate = p_subtitle_path.get_basename() + "." + extension;
			if (candidate != p_subtitle_path && ResourceLoader::exists(candidate, type)) {
				return candidate;
			}
		}
	}
	return String();
}

bool SubtitlesDock::find_silence_edge(const Vector<float> &p_levels, double p_window, double p_time, double p_radius, bool p_onset, float p_threshold, double &r_time) {
	ERR_FAIL_COND_V(p_window <= 0.0, false);
	// Edge `i` lies between window `i - 1` and window `i`, at `i * p_window`; search outwards from the nearest.
	const int center = int(Math::round(p_time / p_window));
	const int reach = int(Math::ceil(p_radius / p_window));
	for (int distance = 0; distance <= reach; distance++) {
		for (int side = -1; side <= 1; side += 2) {
			const int edge = center + side * distance;
			if ((distance == 0 && side > 0) || edge <= 0 || edge >= p_levels.size() || Math::abs(edge * p_window - p_time) > p_radius) {
				continue;
			}
			const bool sound_before = p_levels[edge - 1] > p_threshold;
			const bool sound_after = p_levels[edge] > p_threshold;
			if (p_onset ? (!sound_before && sound_after) : (sound_before && !sound_after)) {
				r_time = edge * p_window;
				return true;
			}
		}
	}
	return false;
}

bool SubtitlesDock::parse_signed_time(const String &p_text, double &r_seconds) {
	String text = p_text.strip_edges();
	const bool negative = text.begins_with("-");
	if (negative || text.begins_with("+")) {
		text = text.substr(1).strip_edges();
	}
	if (!SubtitleEditorHelper::parse_short_time(text, r_seconds)) {
		return false;
	}
	if (negative) {
		r_seconds = -r_seconds;
	}
	return true;
}

bool SubtitlesDock::can_edit(const Object *p_object) {
	// Not for every audio file with a sidecar subtitle: browsing voice lines must not take over the bottom panel.
	return Object::cast_to<Subtitle>(p_object) != nullptr;
}

// Timeline geometry

real_t SubtitlesDock::_get_ruler_height() {
	return 18 * EDSCALE;
}

real_t SubtitlesDock::_get_lane_height() {
	return 44 * EDSCALE;
}

double SubtitlesDock::_get_offset() const {
	return subtitle.is_valid() ? subtitle->get_time_offset() : 0.0;
}

double SubtitlesDock::_get_length() const {
	if (stream.is_valid() && stream->get_length() > 0.0) {
		return stream->get_length();
	}
	if (video.is_valid() && video_player->get_stream_length() > 0.0) {
		return video_player->get_stream_length();
	}
	// Without a stream: the segments and some room to spot new ones.
	const double end = subtitle.is_valid() && subtitle->get_segment_count() > 0 ? subtitle->get_end_time() : 0.0;
	return MAX(30.0, end + 5.0);
}

double SubtitlesDock::_x_to_time(double p_x) const {
	return view_start + p_x / pixels_per_second;
}

double SubtitlesDock::_time_to_x(double p_time) const {
	return (p_time - view_start) * pixels_per_second;
}

Rect2 SubtitlesDock::_get_segment_rect(int p_index) const {
	const Ref<SubtitleSegment> segment = subtitle->get_segment(p_index);
	if (segment.is_null()) {
		return Rect2();
	}
	const double from = _time_to_x(segment->get_start_time() + _get_offset());
	const double to = _time_to_x(segment->get_end_time() + _get_offset());
	const real_t lane_top = timeline->get_size().height - _get_lane_height();
	return Rect2(from, lane_top + 2 * EDSCALE, MAX(to - from, 2.0), _get_lane_height() - 4 * EDSCALE);
}

int SubtitlesDock::_get_segment_at(const Point2 &p_pos, DragMode &r_mode) const {
	r_mode = DRAG_SEEK;
	if (subtitle.is_null() || p_pos.y < timeline->get_size().height - _get_lane_height()) {
		return -1;
	}
	const real_t edge = 5 * EDSCALE;
	// The selected segment first, so its edges win over a neighbor's.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < subtitle->get_segment_count(); i++) {
			if ((pass == 0) != (i == selected)) {
				continue;
			}
			const Rect2 rect = _get_segment_rect(i);
			if (p_pos.x < rect.position.x - edge || p_pos.x > rect.get_end().x + edge) {
				continue;
			}
			if (Math::abs(p_pos.x - rect.position.x) <= edge) {
				r_mode = DRAG_START;
			} else if (Math::abs(p_pos.x - rect.get_end().x) <= edge) {
				r_mode = DRAG_END;
			} else {
				r_mode = DRAG_MOVE;
			}
			return i;
		}
	}
	return -1;
}

// Updates

void SubtitlesDock::_queue_update() {
	if (update_queued) {
		return;
	}
	update_queued = true;
	callable_mp(this, &SubtitlesDock::_update).call_deferred();
}

void SubtitlesDock::_subtitle_changed() {
	if (drag_mode == DRAG_NONE || drag_mode == DRAG_SEEK) {
		_queue_update();
	} else {
		timeline->queue_redraw(); // While dragging, the table follows on release.
	}
}

void SubtitlesDock::_segment_changed(int p_index) {
	changed_rows.insert(p_index);
}

void SubtitlesDock::_watch_segments(bool p_watch) {
	for (int i = 0; i < shown_segments.size(); i++) {
		const Ref<SubtitleSegment> &segment = shown_segments[i];
		const Callable callback = callable_mp(this, &SubtitlesDock::_segment_changed).bind(i);
		if (segment.is_null()) {
			continue;
		}
		if (p_watch) {
			segment->connect_changed(callback);
		} else if (segment->is_connected(CoreStringName(changed), callback)) {
			segment->disconnect_changed(callback);
		}
	}
}

void SubtitlesDock::_update_file_label() {
	String file = TTR("No subtitle");
	if (subtitle.is_valid()) {
		file = subtitle->get_path().is_resource_file() ? subtitle->get_path().get_file() : TTR("Built-in Subtitle");
		// Set by undo/redo operations on the subtitle, cleared when it is saved (also by Godot's Save).
		if (subtitle->is_edited()) {
			file += " (*)";
		}
	}
	file_label->set_text(file);
	file_label->set_tooltip_text(subtitle.is_valid() ? subtitle->get_path() : String());
}

void SubtitlesDock::_update() {
	update_queued = false;
	const bool has_subtitle = subtitle.is_valid();
	if (has_subtitle && selected >= subtitle->get_segment_count()) {
		selected = subtitle->get_segment_count() - 1;
	}
	_update_tree(); // Also updates the problems.
	_update_file_label();
	for (Button *button : { add_button, spot_start_button, play_button, save_button }) {
		button->set_disabled(!has_subtitle);
	}
	for (Button *button : { remove_button, spot_end_button, split_button, merge_button }) {
		button->set_disabled(!has_subtitle || selected < 0);
	}
	stream_picker->set_editable(has_subtitle);
	timing_menu->set_disabled(!has_subtitle);
	const Ref<Resource> media = stream.is_valid() ? Ref<Resource>(stream) : Ref<Resource>(video);
	stream_label->set_text(media.is_valid() ? (media->get_path().is_resource_file() ? media->get_path().get_file() : TTR("Built-in Stream")) : TTR("No audio"));
	stream_label->set_tooltip_text(media.is_valid() ? media->get_path() : String());
	video_box->set_visible(video.is_valid());

	String status;
	String save_tooltip = TTR("Save the subtitle file in its own format.");
	if (has_subtitle) {
		const int count = subtitle->get_segment_count();
		status = vformat(TTRN("%d segment", "%d segments", count), count);
		if (!problems.is_empty()) {
			status += ", " + vformat(TTRN("%d with a problem (highlighted, see the tooltip)", "%d with problems (highlighted, see the tooltips)", problems.size()), problems.size());
		}
		if (video.is_valid()) {
			status += ". " + TTR("A video has no waveform: snapping to silence needs an audio stream.");
		} else if (stream.is_null()) {
			status += ". " + TTR("Pick an audio or video stream to see and hear it; without one, Play runs a clock.");
		}
		const String extension = subtitle->get_path().get_extension().to_lower();
		if (subtitle->get_time_offset() != 0.0 && Subtitle::get_recognized_extensions().has(extension)) {
			save_tooltip += "\n" + vformat(TTR("Saving as .%s adds the time offset to the written times, because the format has no offset field."), extension);
		}
	} else {
		status = TTR("Open a Subtitle from the FileSystem dock, or an audio stream with subtitles from its Inspector.");
	}
	status_label->set_text(status);
	save_button->set_tooltip_text(save_tooltip);
	_update_scroll();
	_update_time_label();
	_update_video_text();
	timeline->queue_redraw();
}

void SubtitlesDock::_fill_row(TreeItem *p_item, int p_index, const String &p_kinds) {
	const Ref<SubtitleSegment> segment = subtitle->get_segment(p_index);
	p_item->set_metadata(0, p_index);
	p_item->set_text(COLUMN_INDEX, itos(p_index + 1));
	if (segment.is_null()) {
		p_item->set_text(COLUMN_TEXT, TTR("<empty slot>"));
		return;
	}
	const double offset = _get_offset();
	p_item->set_text(COLUMN_START, SubtitleEditorHelper::format_short_time(segment->get_start_time() + offset));
	p_item->set_text(COLUMN_END, SubtitleEditorHelper::format_short_time(segment->get_end_time() + offset));
	p_item->set_text(COLUMN_SPEAKER, segment->get_speaker());
	if (p_item->get_cell_mode(COLUMN_KIND) != TreeItem::CELL_MODE_RANGE) {
		p_item->set_cell_mode(COLUMN_KIND, TreeItem::CELL_MODE_RANGE);
		p_item->set_text(COLUMN_KIND, p_kinds);
		p_item->set_edit_multiline(COLUMN_TEXT, true);
		for (int column = COLUMN_START; column < COLUMN_MAX; column++) {
			p_item->set_editable(column, true);
		}
		p_item->add_button(COLUMN_SPEAKER, get_editor_theme_icon(SNAME("Search")), 0, false, TTR("Pick a speaker"));
	}
	p_item->set_range(COLUMN_KIND, segment->get_kind());
	p_item->set_text(COLUMN_TEXT, segment->get_text());
	const bool has_problem = problems.has(p_index);
	const String tooltip = has_problem ? String("\n").join(problems.get(p_index)) : String();
	const Color warning_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	for (int column = 0; column < COLUMN_MAX; column++) {
		if (has_problem) {
			p_item->set_custom_color(column, warning_color);
		} else if (p_item->get_custom_color(column) != Color()) {
			p_item->clear_custom_color(column);
		}
		p_item->set_tooltip_text(column, tooltip);
	}
}

void SubtitlesDock::_update_tree() {
	updating_tree = true;
	const HashMap<int, PackedStringArray> old_problems(problems);
	problems = subtitle.is_valid() ? SubtitleEditorHelper::get_problems(subtitle) : HashMap<int, PackedStringArray>();
	String kinds;
	for (int kind = 0; kind < SubtitleSegment::KIND_MAX; kind++) {
		kinds += (kind > 0 ? "," : "") + SubtitleEditorHelper::get_kind_name(SubtitleSegment::Kind(kind));
	}
	TreeItem *root = tree->get_root();
	const bool same_segments = subtitle.is_valid() && root != nullptr && shown_segments == subtitle->get_segment_list();
	bool problems_changed = !same_segments || old_problems.size() != problems.size();
	if (same_segments) {
		// Only segments that changed, and rows whose problems changed: spotting stays fast with thousands of rows.
		for (const KeyValue<int, PackedStringArray> &problem : old_problems) {
			if (!problems.has(problem.key) || problems.get(problem.key) != problem.value) {
				changed_rows.insert(problem.key);
				problems_changed = true;
			}
		}
		for (const KeyValue<int, PackedStringArray> &problem : problems) {
			if (!old_problems.has(problem.key)) {
				changed_rows.insert(problem.key);
			}
		}
		for (int index : changed_rows) {
			if (index >= 0 && index < root->get_child_count()) {
				_fill_row(root->get_child(index), index, kinds);
			}
		}
	} else {
		_watch_segments(false);
		shown_segments.clear();
		if (subtitle.is_null()) {
			tree->clear();
		} else {
			// Rows are reused: cells whose text stays the same are not touched (Tree skips equal texts).
			shown_segments = subtitle->get_segment_list();
			_watch_segments(true);
			if (root == nullptr) {
				root = tree->create_item();
			}
			while (root->get_child_count() > shown_segments.size()) {
				TreeItem *last = root->get_child(root->get_child_count() - 1);
				root->remove_child(last);
				memdelete(last);
			}
			while (root->get_child_count() < shown_segments.size()) {
				tree->create_item(root);
			}
			for (int i = 0; i < shown_segments.size(); i++) {
				_fill_row(root->get_child(i), i, kinds);
			}
		}
	}
	changed_rows.clear();
	root = tree->get_root();
	TreeItem *item = root && selected >= 0 && selected < root->get_child_count() ? root->get_child(selected) : nullptr;
	if (item && !item->is_selected(0)) {
		item->select(0);
	}
	updating_tree = false;
	if (problems_changed) {
		_update_problem_view();
	}
}

void SubtitlesDock::_update_problem_view() {
	problem_view->clear();
	LocalVector<int> indices;
	for (const KeyValue<int, PackedStringArray> &problem : problems) {
		indices.push_back(problem.key);
	}
	indices.sort();
	for (int index : indices) {
		for (const String &message : problems.get(index)) {
			const int item = problem_view->add_item(vformat("#%d  %s", index + 1, message));
			problem_view->set_item_metadata(item, index);
			problem_view->set_item_tooltip(item, message);
		}
	}
	problem_header->set_text(problems.is_empty() ? TTR("No problems") : vformat(TTRN("%d segment with problems", "%d segments with problems", problems.size()), problems.size()));
	problems_button->set_text(vformat(TTR("Problems (%d)"), problems.size()));
	problems_button->set_button_icon(problems.is_empty() ? Ref<Texture2D>() : get_editor_theme_icon(SNAME("StatusWarning")));
}

void SubtitlesDock::_problem_clicked(int p_item, const Vector2 &p_position, MouseButton p_button) {
	// Clicked, not selected: a second click on the same problem jumps again.
	if (p_button != MouseButton::LEFT) {
		return;
	}
	const int index = problem_view->get_item_metadata(p_item);
	_select(index);
	_jump_to(index);
}

void SubtitlesDock::_toggle_problems(bool p_visible) {
	problem_box->set_visible(p_visible);
	EditorSettings::get_singleton()->set_project_metadata("subtitles_dock", "show_problems", p_visible);
}

void SubtitlesDock::_jump_to(int p_index) {
	// Show the segment on the timeline and move the playhead to it, ready to play from there.
	const Ref<SubtitleSegment> segment = subtitle.is_valid() && p_index >= 0 ? subtitle->get_segment(p_index) : Ref<SubtitleSegment>();
	if (segment.is_null()) {
		return;
	}
	const double start = segment->get_start_time() + _get_offset();
	const double visible_seconds = timeline->get_size().width / pixels_per_second;
	if (start < view_start || start > view_start + visible_seconds * 0.9) {
		view_start = start - visible_seconds * 0.1;
		_update_scroll();
	}
	_seek(start);
}

void SubtitlesDock::_update_video_text() {
	if (video.is_null()) {
		return;
	}
	// The segments at the playhead over the picture, as a player would show them.
	String text;
	if (subtitle.is_valid()) {
		const TypedArray<SubtitleSegment> current = subtitle->get_segments_at(playhead - _get_offset());
		for (int idx = 0; idx < current.size(); idx++) {
			text += (idx > 0 ? "\n" : "") + SubtitleEditorHelper::get_row_text(subtitle, Ref<SubtitleSegment>(current[idx]));
		}
	}
	video_text_label->set_text(text);
	const Ref<Texture2D> texture = video_player->get_video_texture();
	const Size2i size = texture.is_valid() ? Size2i(texture->get_size()) : Size2i();
	if (size != video_size && size.height > 0) {
		video_size = size;
		video_panel->set_ratio(float(size.width) / float(size.height));
	}
}

void SubtitlesDock::_update_scroll() {
	if (fit_pending && timeline->get_size().width > 0) {
		fit_pending = false;
		pixels_per_second = CLAMP(timeline->get_size().width / _get_length(), 2.0 * EDSCALE, 100.0 * EDSCALE);
	}
	const double visible_seconds = timeline->get_size().width / pixels_per_second;
	const double length = _get_length();
	view_start = CLAMP(view_start, 0.0, MAX(0.0, length - visible_seconds));
	timeline_scroll->set_max(length);
	timeline_scroll->set_page(MIN(visible_seconds, length));
	timeline_scroll->set_value_no_signal(view_start);
}

void SubtitlesDock::_update_time_label() {
	time_label->set_text(SubtitleEditorHelper::format_short_time(playhead) + " / " + SubtitleEditorHelper::format_short_time(_get_length()));
}

void SubtitlesDock::_select(int p_index, bool p_scroll_tree) {
	selected = p_index;
	for (Button *button : { remove_button, spot_end_button, split_button, merge_button }) {
		button->set_disabled(selected < 0);
	}
	if (p_scroll_tree) {
		TreeItem *root = tree->get_root();
		TreeItem *item = root && selected >= 0 && selected < root->get_child_count() ? root->get_child(selected) : nullptr;
		updating_tree = true;
		if (item) {
			item->select(0);
			tree->scroll_to_item(item);
		} else {
			tree->deselect_all();
		}
		updating_tree = false;
	}
	timeline->queue_redraw();
}

// Edits

void SubtitlesDock::_commit_segments(const String &p_action, const TypedArray<SubtitleSegment> &p_segments, int p_select) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action, UndoRedo::MERGE_DISABLE, subtitle.ptr());
	undo_redo->add_do_property(subtitle.ptr(), "segments", p_segments);
	undo_redo->add_undo_property(subtitle.ptr(), "segments", subtitle->get_segments());
	selected = p_select;
	undo_redo->commit_action();
}

void SubtitlesDock::_commit_times(const String &p_action, int p_index, double p_start, double p_end, double p_old_start, double p_old_end) {
	const Ref<SubtitleSegment> segment = subtitle->get_segment(p_index);
	ERR_FAIL_COND(segment.is_null());
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(p_action, UndoRedo::MERGE_DISABLE, subtitle.ptr());
	// Segments without a path count as built into the edited scene: keep the action in the history of the subtitle.
	undo_redo->force_fixed_history();
	undo_redo->add_do_method(segment.ptr(), "set_start_time", p_start);
	undo_redo->add_do_method(segment.ptr(), "set_end_time", p_end);
	undo_redo->add_undo_method(segment.ptr(), "set_start_time", p_old_start);
	undo_redo->add_undo_method(segment.ptr(), "set_end_time", p_old_end);
	// Marks the subtitle as edited, so Godot's Save writes it too.
	undo_redo->add_do_method(subtitle.ptr(), "emit_changed");
	undo_redo->add_undo_method(subtitle.ptr(), "emit_changed");
	undo_redo->commit_action();
}

void SubtitlesDock::_add_segment() {
	ERR_FAIL_COND(subtitle.is_null());
	const double start = MAX(0.0, playhead - _get_offset());
	_commit_segments(TTR("Add Subtitle Segment"), SubtitleEditorHelper::get_segments_with_segment_at(subtitle, start, start + NEW_DURATION), SubtitleEditorHelper::get_insert_index(subtitle, start));
}

void SubtitlesDock::_remove_segment() {
	if (subtitle.is_null() || selected < 0 || selected >= subtitle->get_segment_count()) {
		return;
	}
	TypedArray<SubtitleSegment> segments = subtitle->get_segments();
	segments.remove_at(selected);
	_commit_segments(TTR("Remove Subtitle Segment"), segments, MIN(selected, segments.size() - 1));
}

void SubtitlesDock::_spot(bool p_end) {
	if (subtitle.is_null()) {
		return;
	}
	const double time = MAX(0.0, playhead - _get_offset());
	if (selected < 0 || selected >= subtitle->get_segment_count()) {
		if (!p_end) {
			_add_segment(); // Spotting from scratch: a new segment starts at the playhead.
		}
		return;
	}
	const Ref<SubtitleSegment> segment = subtitle->get_segment(selected);
	ERR_FAIL_COND(segment.is_null());
	const double start = segment->get_start_time();
	const double end = segment->get_end_time();
	if (!p_end) {
		_commit_times(TTR("Set Subtitle Segment Start"), selected, time, end > time ? end : time + NEW_DURATION, start, end);
		return;
	}
	if (time <= start) {
		status_label->set_text(TTR("The playhead is before the start of the selected segment."));
		return;
	}
	_commit_times(TTR("Set Subtitle Segment End"), selected, start, time, start, end);
	// The next segment is the one to spot next.
	if (selected + 1 < subtitle->get_segment_count()) {
		_select(selected + 1);
	}
}

void SubtitlesDock::_save() {
	if (subtitle.is_null()) {
		return;
	}
	if (!subtitle->get_path().is_resource_file()) {
		status_label->set_text(TTR("This subtitle is built into another resource or scene: it is saved with it."));
		return;
	}
	EditorNode::get_singleton()->save_resource(subtitle);
}

// Timing tools: they run the Subtitle methods on a copy, which then replaces the segments as one undo action.

Ref<Subtitle> SubtitlesDock::_make_copy(int p_from, int p_to) const {
	// Segments in [p_from, p_to) are duplicated (the tool changes them), the others are shared.
	TypedArray<SubtitleSegment> segments = subtitle->get_segments();
	for (int i = MAX(0, p_from); i < MIN(p_to, int(segments.size())); i++) {
		const Ref<SubtitleSegment> segment = segments[i];
		if (segment.is_valid()) {
			segments[i] = segment->duplicate();
		}
	}
	Ref<Subtitle> copy;
	copy.instantiate();
	copy->set_segments(segments);
	return copy;
}

void SubtitlesDock::_split_segment() {
	if (subtitle.is_null() || selected < 0 || selected >= subtitle->get_segment_count()) {
		status_label->set_text(TTR("Select the segment to split, then move the playhead to where it splits."));
		return;
	}
	const Ref<SubtitleSegment> segment = subtitle->get_segment(selected);
	const double time = playhead - _get_offset();
	if (segment.is_null() || time <= segment->get_start_time() || time >= segment->get_end_time()) {
		status_label->set_text(TTR("The playhead must be inside the selected segment to split it."));
		return;
	}
	const Ref<Subtitle> copy = _make_copy(selected, selected + 1);
	copy->split_segment(selected, time);
	// The second part is the one to edit next.
	_commit_segments(TTR("Split Subtitle Segment"), copy->get_segments(), selected + 1);
}

void SubtitlesDock::_merge_segments() {
	if (subtitle.is_null() || selected < 0 || selected + 1 >= subtitle->get_segment_count() || subtitle->get_segment(selected).is_null()) {
		status_label->set_text(TTR("Select a segment that has a next segment to merge with."));
		return;
	}
	const Ref<Subtitle> copy = _make_copy(selected, selected + 1);
	copy->merge_segments(selected);
	_commit_segments(TTR("Merge Subtitle Segments"), copy->get_segments(), selected);
}

void SubtitlesDock::_popup_shift() {
	// Prefilled so that the selected segment starts at the playhead.
	double amount = 0.0;
	const Ref<SubtitleSegment> segment = selected >= 0 ? subtitle->get_segment(selected) : Ref<SubtitleSegment>();
	if (segment.is_valid()) {
		amount = playhead - _get_offset() - segment->get_start_time();
	}
	shift_edit->set_text((amount < 0.0 ? "-" : "") + SubtitleEditorHelper::format_short_time(Math::abs(amount)));
	shift_scope->set_item_disabled(SHIFT_SELECTED, segment.is_null());
	shift_scope->set_item_disabled(SHIFT_FROM_SELECTED, segment.is_null());
	if (segment.is_null()) {
		shift_scope->select(SHIFT_ALL);
	}
	shift_dialog->popup_centered();
	shift_edit->grab_focus();
	shift_edit->select_all();
}

void SubtitlesDock::_shift_confirmed() {
	double amount = 0.0;
	if (!parse_signed_time(shift_edit->get_text(), amount)) {
		status_label->set_text(TTR("Enter the shift as seconds (-1.5) or minutes:seconds (-0:01.500)."));
		return;
	}
	const int count = subtitle->get_segment_count();
	const int scope = shift_scope->get_selected_id();
	if (amount == 0.0 || count == 0 || (scope != SHIFT_ALL && (selected < 0 || selected >= count))) {
		return;
	}
	const int from = scope == SHIFT_ALL ? 0 : selected;
	const int to = scope == SHIFT_SELECTED ? selected + 1 : count;
	const Ref<Subtitle> copy = _make_copy(from, to);
	for (int i = from; i < to; i++) {
		const Ref<SubtitleSegment> segment = copy->get_segment(i);
		if (segment.is_valid()) {
			segment->set_start_time(segment->get_start_time() + amount);
			segment->set_end_time(segment->get_end_time() + amount);
		}
	}
	static const char *const actions[] = { TTRC("Shift Subtitle Times"), TTRC("Shift Subtitle Segment"), TTRC("Shift Subtitle Times from Segment") };
	_commit_segments(TTR(actions[scope]), copy->get_segments(), selected);
	_warn_negative_starts(copy);
}

void SubtitlesDock::_popup_sync() {
	const int count = subtitle->get_segment_count();
	if (count < 2) {
		status_label->set_text(TTR("Two-point sync needs at least two segments."));
		return;
	}
	// The selected segment goes to the playhead: as the first point, or as the second when it is the last segment.
	// The other point is the first or last segment where it is now.
	const bool has_selection = selected >= 0 && selected < count;
	const int indices[2] = { has_selection && selected < count - 1 ? selected : 0, has_selection && selected == count - 1 ? selected : count - 1 };
	for (int point = 0; point < 2; point++) {
		sync_segments[point]->set_max(count);
		sync_segments[point]->set_value(indices[point] + 1);
		const Ref<SubtitleSegment> segment = subtitle->get_segment(indices[point]);
		const double target = has_selection && indices[point] == selected ? playhead : (segment.is_valid() ? segment->get_start_time() + _get_offset() : 0.0);
		sync_targets[point]->set_text(SubtitleEditorHelper::format_short_time(target));
	}
	_update_sync_preview();
	sync_dialog->popup_centered();
	sync_targets[0]->grab_focus();
}

void SubtitlesDock::_sync_confirmed() {
	double times[2] = {};
	double targets[2] = {};
	for (int point = 0; point < 2; point++) {
		const int index = int(sync_segments[point]->get_value()) - 1;
		const Ref<SubtitleSegment> segment = index >= 0 && index < subtitle->get_segment_count() ? subtitle->get_segment(index) : Ref<SubtitleSegment>();
		if (segment.is_null() || !SubtitleEditorHelper::parse_short_time(sync_targets[point]->get_text(), targets[point])) {
			status_label->set_text(TTR("Pick two segments and enter the times at which they should start."));
			return;
		}
		times[point] = segment->get_start_time();
		targets[point] -= _get_offset();
	}
	// The checks of Subtitle.sync_times(), as a message instead of an error.
	if (times[0] == times[1] || (targets[1] - targets[0]) / (times[1] - times[0]) <= 0.0) {
		status_label->set_text(TTR("The two segments must start at different times, and their new times must keep their order."));
		return;
	}
	const Ref<Subtitle> copy = _make_copy(0, subtitle->get_segment_count());
	copy->sync_times(times[0], targets[0], times[1], targets[1]);
	_commit_segments(TTR("Sync Subtitle Times"), copy->get_segments(), selected);
	_warn_negative_starts(copy);
}

void SubtitlesDock::_update_sync_preview() {
	if (subtitle.is_null()) {
		return;
	}
	// The current start next to each segment, and what the sync does as a stretch factor and a shift.
	double times[2] = {};
	double targets[2] = {};
	bool valid = true;
	for (int point = 0; point < 2; point++) {
		const int index = int(sync_segments[point]->get_value()) - 1;
		const Ref<SubtitleSegment> segment = index >= 0 && index < subtitle->get_segment_count() ? subtitle->get_segment(index) : Ref<SubtitleSegment>();
		sync_current[point]->set_text(segment.is_valid() ? vformat(TTR("(now %s)"), SubtitleEditorHelper::format_short_time(segment->get_start_time() + _get_offset())) : String());
		valid = valid && segment.is_valid() && SubtitleEditorHelper::parse_short_time(sync_targets[point]->get_text(), targets[point]);
		times[point] = segment.is_valid() ? segment->get_start_time() + _get_offset() : 0.0;
	}
	const double scale = valid && times[1] != times[0] ? (targets[1] - targets[0]) / (times[1] - times[0]) : 0.0;
	if (!(scale > 0.0)) {
		sync_factor_label->set_text(TTR("Pick two segments with different starts, and targets in the same order."));
		return;
	}
	const double shift = targets[0] - times[0];
	sync_factor_label->set_text(vformat(TTR("Stretch: %s %%, first segment moves %s%s."), String::num(scale * 100.0, 3), shift < 0.0 ? "-" : "+", SubtitleEditorHelper::format_short_time(Math::abs(shift))));
}

void SubtitlesDock::_warn_negative_starts(const Ref<Subtitle> &p_copy) {
	int count = 0;
	for (const Ref<SubtitleSegment> &segment : p_copy->get_segment_list()) {
		if (segment.is_valid() && segment->get_start_time() < 0.0) {
			count++;
		}
	}
	if (count > 0) {
		EditorToaster::get_singleton()->popup_str(vformat(TTRN("%d subtitle segment now starts before 0:00: it is listed under the problems of the Subtitles dock.", "%d subtitle segments now start before 0:00: they are listed under the problems of the Subtitles dock.", count), count), EditorToaster::SEVERITY_WARNING);
	}
}

void SubtitlesDock::_timing_menu_id_pressed(int p_id) {
	if (subtitle.is_null()) {
		return;
	}
	switch (p_id) {
		case TIMING_SHIFT: {
			_popup_shift();
		} break;
		case TIMING_SYNC: {
			_popup_sync();
		} break;
		case TIMING_SPLIT: {
			_split_segment();
		} break;
		case TIMING_MERGE: {
			_merge_segments();
		} break;
		case TIMING_ADD: {
			_add_segment();
		} break;
		case TIMING_REMOVE: {
			_remove_segment();
		} break;
		case TIMING_SET_START: {
			_spot(false);
		} break;
		case TIMING_SET_END: {
			_spot(true);
		} break;
		case TIMING_INSPECT: {
			const Ref<SubtitleSegment> segment = selected >= 0 ? subtitle->get_segment(selected) : Ref<SubtitleSegment>();
			if (segment.is_valid()) {
				EditorInterface::get_singleton()->edit_resource(segment);
			}
		} break;
		case TIMING_SNAP: {
			snap = !snap;
			PopupMenu *popup = timing_menu->get_popup();
			popup->set_item_checked(popup->get_item_index(TIMING_SNAP), snap);
			EditorSettings::get_singleton()->set_project_metadata("subtitles_dock", "snap", snap);
		} break;
	}
}

// Snapping

void SubtitlesDock::_update_levels() {
	if (stream.is_null() || stream->get_length() <= 0.0) {
		levels.clear();
		levels_stream = ObjectID();
		return;
	}
	const Ref<AudioStreamPreview> preview = AudioStreamPreviewGenerator::get_singleton()->generate_preview(stream);
	if (preview.is_null() || (levels_stream == stream->get_instance_id() && levels_version == preview->get_version())) {
		return;
	}
	// The preview is generated on a thread and grows: rebuilt whenever its version changes.
	levels_stream = stream->get_instance_id();
	levels_version = preview->get_version();
	levels.resize(MAX(0, int(Math::ceil(preview->get_length() / LEVEL_WINDOW))));
	float peak = 0.0f;
	for (int i = 0; i < levels.size(); i++) {
		const float from = float(i * LEVEL_WINDOW);
		const float to = float((i + 1) * LEVEL_WINDOW);
		const float level = MAX(Math::abs(preview->get_max(from, to)), Math::abs(preview->get_min(from, to)));
		levels.write[i] = level;
		peak = MAX(peak, level);
	}
	// Speech is far louder than the room tone between lines: a tenth of the loudest moment separates them.
	level_threshold = peak * 0.1f;
}

double SubtitlesDock::_snap(double p_time, bool p_onset, double p_radius, int p_exclude) {
	if (!snap || subtitle.is_null()) {
		return p_time;
	}
	double best = p_time;
	double best_distance = p_radius;
	// Edges of the other segments, so segments can follow each other without a gap.
	for (int i = 0; i < subtitle->get_segment_count(); i++) {
		const Ref<SubtitleSegment> segment = subtitle->get_segment(i);
		if (i == p_exclude || segment.is_null()) {
			continue;
		}
		for (const double edge : { segment->get_start_time(), segment->get_end_time() }) {
			if (Math::abs(edge - p_time) <= best_distance) {
				best = edge;
				best_distance = Math::abs(edge - p_time);
			}
		}
	}
	// Where speech starts or stops in the waveform (in stream time); the table is updated when a drag starts.
	const double offset = _get_offset();
	double silence_edge = 0.0;
	if (!levels.is_empty() && find_silence_edge(levels, LEVEL_WINDOW, p_time + offset, p_radius, p_onset, level_threshold, silence_edge) && Math::abs(silence_edge - offset - p_time) < best_distance) {
		best = silence_edge - offset;
	}
	return best;
}

// Playback

void SubtitlesDock::_play_pause() {
	if (subtitle.is_null()) {
		return;
	}
	playing = !playing;
	if (playing) {
		if (playhead >= _get_length()) {
			playhead = 0.0;
		}
		if (stream.is_valid()) {
			player->play(playhead);
		} else if (video.is_valid()) {
			_seek_video();
			video_player->set_paused(false);
		}
	} else {
		player->stop();
		if (video.is_valid()) {
			video_player->set_paused(true);
		}
	}
	play_button->set_button_icon(get_editor_theme_icon(playing ? SNAME("Pause") : SNAME("MainPlay")));
	set_process(playing);
}

void SubtitlesDock::_seek(double p_time) {
	playhead = CLAMP(p_time, 0.0, _get_length());
	if (playing && stream.is_valid()) {
		player->seek(playhead);
	}
	if (video.is_valid()) {
		_seek_video();
	}
	_update_time_label();
	_update_video_text();
	timeline->queue_redraw();
}

void SubtitlesDock::_seek_video() {
	// A paused, playing video draws the frame it seeks to; a stopped one (after its end) draws nothing.
	if (!video_player->is_inside_tree()) {
		return; // The dock is closed: done when it opens.
	}
	// A Theora seek decodes from the previous keyframe: while scrubbing, at most one per VIDEO_SCRUB_MSEC, the last
	// position follows on release.
	const uint64_t now = OS::get_singleton()->get_ticks_msec();
	if (drag_mode == DRAG_SEEK && now - last_video_seek_msec < VIDEO_SCRUB_MSEC) {
		video_seek_pending = true;
		return;
	}
	video_seek_pending = false;
	last_video_seek_msec = now;
	if (!video_player->is_playing()) {
		video_player->play();
		video_player->set_paused(!playing);
	}
	video_player->set_stream_position(playhead);
}

void SubtitlesDock::_player_finished() {
	if (playing) {
		playhead = _get_length();
		_play_pause();
		_update_time_label();
		timeline->queue_redraw();
	}
}

void SubtitlesDock::_set_stream(const Ref<Resource> &p_stream) {
	if (playing) {
		_play_pause();
	}
	stream = p_stream;
	video = p_stream;
	player->set_stream(stream);
	video_player->set_stream(video);
	stream_picker->set_edited_resource(p_stream);
	playhead = MIN(playhead, _get_length());
	if (video.is_valid()) {
		_seek_video(); // Paused at the playhead: shows its frame while spotting.
	}
	_queue_update();
}

void SubtitlesDock::_stream_picked(const Ref<Resource> &p_stream) {
	_set_stream(p_stream);
}

void SubtitlesDock::_mute_toggled(bool p_muted) {
	video_player->set_volume(p_muted ? 0.0f : 1.0f);
}

void SubtitlesDock::_preview_updated(ObjectID p_id) {
	if (stream.is_valid() && stream->get_instance_id() == p_id) {
		timeline->queue_redraw();
	}
}

// Timeline

void SubtitlesDock::_timeline_draw() {
	const Size2 size = timeline->get_size();
	const Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
	const int font_size = int(get_theme_font_size(SceneStringName(font_size), SNAME("Label")) * 0.85);
	const Color font_color = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const Color accent_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color warning_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const real_t ruler = _get_ruler_height();
	const real_t lane_top = size.height - _get_lane_height();
	const double length = _get_length();

	timeline->draw_rect(Rect2(Point2(), size), get_theme_color(SNAME("dark_color_2"), EditorStringName(Editor)));
	timeline->draw_rect(Rect2(0, lane_top, size.width, _get_lane_height()), get_theme_color(SNAME("dark_color_1"), EditorStringName(Editor)));
	const real_t end_x = _time_to_x(length);
	if (end_x < size.width) {
		timeline->draw_rect(Rect2(end_x, 0, size.width - end_x, size.height), get_theme_color(SNAME("dark_color_3"), EditorStringName(Editor)));
	}

	// Waveform between ruler and segment lane.
	if (stream.is_valid() && stream->get_length() > 0.0) {
		const Ref<AudioStreamPreview> preview = AudioStreamPreviewGenerator::get_singleton()->generate_preview(stream);
		const float preview_length = preview.is_valid() ? preview->get_length() : 0.0f;
		const real_t wave_height = lane_top - ruler;
		Vector<Vector2> points;
		for (int x = 0; x < int(MIN(size.width, end_x)); x++) {
			const float from = float(_x_to_time(x));
			const float to = float(_x_to_time(x + 1));
			if (from >= preview_length) {
				break;
			}
			const float max = preview->get_max(from, to) * 0.5f + 0.5f;
			const float min = preview->get_min(from, to) * 0.5f + 0.5f;
			points.push_back(Vector2(x + 0.5, ruler + (1.0f - max) * wave_height));
			points.push_back(Vector2(x + 0.5, ruler + (1.0f - min) * wave_height + 1.0));
		}
		if (!points.is_empty()) {
			timeline->draw_multiline(points, get_theme_color(SNAME("contrast_color_2"), EditorStringName(Editor)));
		}
	}

	// Ruler: a step that keeps labels apart.
	static const double steps[] = { 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800 };
	double step = steps[std_size(steps) - 1];
	for (double candidate : steps) {
		if (candidate * pixels_per_second >= 80 * EDSCALE) {
			step = candidate;
			break;
		}
	}
	const Color tick_color = font_color * Color(1, 1, 1, 0.35);
	for (double t = Math::floor(view_start / step) * step; t <= _x_to_time(size.width); t += step) {
		const real_t x = _time_to_x(t);
		timeline->draw_line(Point2(x, 0), Point2(x, ruler), tick_color, Math::round(EDSCALE));
		timeline->draw_string(font, Point2(x + 3 * EDSCALE, ruler - 4 * EDSCALE), SubtitleEditorHelper::format_short_time(t), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_color * Color(1, 1, 1, 0.7));
	}

	// Segments: sorted by start, so stop after the first one past the right edge.
	if (subtitle.is_valid()) {
		const real_t padding = 4 * EDSCALE;
		for (int i = 0; i < subtitle->get_segment_count(); i++) {
			const Rect2 rect = _get_segment_rect(i);
			if (rect.position.x > size.width) {
				break;
			}
			if (rect.size.width <= 0 || rect.get_end().x < 0) {
				continue;
			}
			const bool is_selected = i == selected;
			const Color base = problems.has(i) ? warning_color : accent_color;
			timeline->draw_rect(rect, base * Color(1, 1, 1, is_selected ? 0.55 : 0.25));
			timeline->draw_rect(rect, is_selected ? font_color : base, false, Math::round(EDSCALE * (is_selected ? 2 : 1)));
			const Ref<SubtitleSegment> segment = subtitle->get_segment(i);
			const real_t text_width = rect.size.width - padding * 2;
			if (segment.is_valid() && text_width > 8 * EDSCALE) {
				const Point2 text_pos = rect.position + Point2(padding, (rect.size.height + font->get_ascent(font_size) - font->get_descent(font_size)) * 0.5);
				timeline->draw_string(font, text_pos, SubtitleEditorHelper::get_row_text(subtitle, segment), HORIZONTAL_ALIGNMENT_LEFT, text_width, font_size, font_color);
			}
		}
	}

	// Where a dragged edge snapped to.
	if (snap_line_visible) {
		const real_t snap_x = Math::round(_time_to_x(snap_line_time + _get_offset()));
		timeline->draw_dashed_line(Point2(snap_x, 0), Point2(snap_x, size.height), get_theme_color(SNAME("success_color"), EditorStringName(Editor)), Math::round(2 * EDSCALE), 4 * EDSCALE);
	}

	// Playhead.
	const real_t playhead_x = Math::round(_time_to_x(playhead));
	timeline->draw_line(Point2(playhead_x, 0), Point2(playhead_x, size.height), accent_color, Math::round(2 * EDSCALE));
	const Ref<Texture2D> indicator = get_editor_theme_icon(SNAME("TimelineIndicator"));
	timeline->draw_texture(indicator, Point2(playhead_x - indicator->get_width() * 0.5, 0), accent_color);
}

void SubtitlesDock::_cancel_drag() {
	const DragMode mode = drag_mode;
	drag_mode = DRAG_NONE;
	snap_line_visible = false;
	timeline->queue_redraw();
	const Ref<SubtitleSegment> segment = subtitle.is_valid() && selected >= 0 ? subtitle->get_segment(selected) : Ref<SubtitleSegment>();
	if (mode >= DRAG_MOVE && segment.is_valid()) {
		segment->set_start_time(drag_old_start);
		segment->set_end_time(drag_old_end);
	}
}

void SubtitlesDock::_timeline_gui_input(const Ref<InputEvent> &p_event) {
	if (subtitle.is_null()) {
		return;
	}
	const Ref<InputEventKey> key = p_event;
	if (key.is_valid() && key->is_pressed() && key->get_keycode() == Key::ESCAPE && drag_mode != DRAG_NONE) {
		_cancel_drag();
		timeline->accept_event();
		return;
	}
	const Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid() && mb->is_pressed() && (mb->get_button_index() == MouseButton::WHEEL_UP || mb->get_button_index() == MouseButton::WHEEL_DOWN)) {
		const bool up = mb->get_button_index() == MouseButton::WHEEL_UP;
		if (mb->is_command_or_control_pressed()) {
			// Zoom around the mouse.
			const double time = _x_to_time(mb->get_position().x);
			pixels_per_second = CLAMP(pixels_per_second * (up ? 1.25 : 0.8), 2.0 * EDSCALE, 2000.0 * EDSCALE);
			view_start = time - mb->get_position().x / pixels_per_second;
		} else {
			view_start += (up ? -0.1 : 0.1) * timeline->get_size().width / pixels_per_second;
		}
		_update_scroll();
		timeline->queue_redraw();
		timeline->accept_event();
		return;
	}
	if (mb.is_valid() && mb->get_button_index() == MouseButton::LEFT) {
		if (mb->is_pressed()) {
			timeline->grab_focus();
			DragMode mode;
			const int index = _get_segment_at(mb->get_position(), mode);
			drag_mode = mode;
			drag_from_time = _x_to_time(mb->get_position().x);
			if (index >= 0) {
				_select(index);
				const Ref<SubtitleSegment> segment = subtitle->get_segment(index);
				drag_old_start = segment->get_start_time();
				drag_old_end = segment->get_end_time();
				if (snap && mode >= DRAG_MOVE) {
					_update_levels(); // Once per drag, not per mouse move.
				}
				if (mb->is_double_click()) {
					drag_mode = DRAG_NONE;
					EditorInterface::get_singleton()->edit_resource(segment);
				}
			} else {
				_seek(drag_from_time);
			}
		} else if (drag_mode != DRAG_NONE) {
			const DragMode mode = drag_mode;
			drag_mode = DRAG_NONE;
			snap_line_visible = false;
			timeline->queue_redraw();
			if (mode == DRAG_SEEK && video_seek_pending && video.is_valid()) {
				_seek_video(); // The last scrub position.
			}
			const Ref<SubtitleSegment> segment = selected >= 0 ? subtitle->get_segment(selected) : Ref<SubtitleSegment>();
			if (mode >= DRAG_MOVE && segment.is_valid()) {
				const double start = segment->get_start_time();
				const double end = segment->get_end_time();
				if (start != drag_old_start || end != drag_old_end) {
					static const char *const actions[] = { "", "", TTRC("Move Subtitle Segment"), TTRC("Trim Subtitle Segment Start"), TTRC("Trim Subtitle Segment End") };
					_commit_times(TTR(actions[mode]), selected, start, end, drag_old_start, drag_old_end);
				}
			}
		}
		timeline->accept_event();
		return;
	}

	const Ref<InputEventMouseMotion> mm = p_event;
	if (mm.is_null()) {
		return;
	}
	if (drag_mode == DRAG_NONE) {
		DragMode mode;
		_get_segment_at(mm->get_position(), mode);
		timeline->set_default_cursor_shape(mode == DRAG_START || mode == DRAG_END ? CURSOR_HSIZE : (mode == DRAG_MOVE ? CURSOR_MOVE : CURSOR_ARROW));
		return;
	}
	const double time = _x_to_time(mm->get_position().x);
	if (drag_mode == DRAG_SEEK) {
		_seek(time);
		return;
	}
	const Ref<SubtitleSegment> segment = selected >= 0 ? subtitle->get_segment(selected) : Ref<SubtitleSegment>();
	if (segment.is_null()) {
		return;
	}
	// Times in segment time, snapped to milliseconds like the subtitle formats, kept within the stream.
	const double length = _get_length() - _get_offset();
	double delta = Math::round((time - drag_from_time) * 1000.0) / 1000.0;
	const double radius = mm->is_shift_pressed() ? 0.0 : DRAG_SNAP_RADIUS * EDSCALE / pixels_per_second;
	snap_line_visible = false;
	switch (drag_mode) {
		case DRAG_MOVE: {
			// The edge nearer to a snap target wins.
			const double start_snap = _snap(drag_old_start + delta, true, radius, selected) - (drag_old_start + delta);
			const double end_snap = _snap(drag_old_end + delta, false, radius, selected) - (drag_old_end + delta);
			const bool use_end = start_snap == 0.0 || (end_snap != 0.0 && Math::abs(end_snap) < Math::abs(start_snap));
			const double adjust = use_end ? end_snap : start_snap;
			delta = CLAMP(delta + adjust, -drag_old_start, MAX(0.0, length - drag_old_end));
			segment->set_start_time(drag_old_start + delta);
			segment->set_end_time(drag_old_end + delta);
			snap_line_visible = adjust != 0.0;
			snap_line_time = use_end ? drag_old_end + delta : drag_old_start + delta;
		} break;
		case DRAG_START: {
			const double new_start = _snap(drag_old_start + delta, true, radius, selected);
			segment->set_start_time(MAX(0.0, MIN(new_start, drag_old_end - MIN_DURATION)));
			snap_line_visible = new_start != drag_old_start + delta;
			snap_line_time = segment->get_start_time();
		} break;
		case DRAG_END: {
			const double new_end = _snap(drag_old_end + delta, false, radius, selected);
			segment->set_end_time(MIN(MAX(new_end, drag_old_start + MIN_DURATION), MAX(length, drag_old_end)));
			snap_line_visible = new_end != drag_old_end + delta;
			snap_line_time = segment->get_end_time();
		} break;
		default:
			break;
	}
}

void SubtitlesDock::_scroll_changed(double p_value) {
	view_start = p_value;
	timeline->queue_redraw();
}

// Table

void SubtitlesDock::_tree_selected() {
	if (updating_tree) {
		return;
	}
	TreeItem *item = tree->get_selected();
	if (item == nullptr) {
		return;
	}
	_select(item->get_metadata(0), false);
	// While playing, a click to edit a cell must not move the playhead.
	if (!playing) {
		_jump_to(selected);
	}
}

void SubtitlesDock::_tree_mouse_selected(const Vector2 &p_position, MouseButton p_button) {
	if (p_button != MouseButton::RIGHT || subtitle.is_null()) {
		return;
	}
	const int count = subtitle->get_segment_count();
	for (int id : { TIMING_REMOVE, TIMING_SET_START, TIMING_SET_END, TIMING_SPLIT, TIMING_INSPECT }) {
		context_menu->set_item_disabled(context_menu->get_item_index(id), selected < 0 || selected >= count);
	}
	context_menu->set_item_disabled(context_menu->get_item_index(TIMING_MERGE), selected < 0 || selected + 1 >= count);
	context_menu->set_position(tree->get_screen_position() + p_position);
	context_menu->reset_size();
	context_menu->popup();
}

void SubtitlesDock::_tree_edited() {
	TreeItem *item = tree->get_edited();
	if (item == nullptr || subtitle.is_null()) {
		return;
	}
	const int column = tree->get_edited_column();
	const int index = item->get_metadata(0);
	const Ref<SubtitleSegment> segment = subtitle->get_segment(index);
	ERR_FAIL_COND(segment.is_null());
	if (column == COLUMN_START || column == COLUMN_END) {
		double time = 0.0;
		if (!SubtitleEditorHelper::parse_short_time(item->get_text(column), time)) {
			status_label->set_text(TTR("Enter a time as seconds (5.5), minutes:seconds (0:05.500) or hours:minutes:seconds."));
			changed_rows.insert(index);
			_queue_update();
			return;
		}
		time = MAX(0.0, time - _get_offset());
		const double start = segment->get_start_time();
		const double end = segment->get_end_time();
		if (column == COLUMN_START) {
			_commit_times(TTR("Set Subtitle Segment Start"), index, time, end, start, end);
		} else {
			_commit_times(TTR("Set Subtitle Segment End"), index, start, time, start, end);
		}
		return;
	}
	StringName property;
	Variant value;
	switch (column) {
		case COLUMN_SPEAKER: {
			property = SNAME("speaker");
			value = StringName(item->get_text(column).strip_edges());
		} break;
		case COLUMN_KIND: {
			property = SNAME("kind");
			value = int(item->get_range(column));
		} break;
		case COLUMN_TEXT: {
			property = SNAME("text");
			value = item->get_text(column);
		} break;
		default:
			return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Edit Subtitle Segment"), UndoRedo::MERGE_DISABLE, subtitle.ptr());
	undo_redo->force_fixed_history(); // The segment, without a path, would count as built into the edited scene.
	undo_redo->add_do_property(segment.ptr(), property, value);
	undo_redo->add_undo_property(segment.ptr(), property, segment->get(property));
	undo_redo->add_do_method(subtitle.ptr(), "emit_changed");
	undo_redo->add_undo_method(subtitle.ptr(), "emit_changed");
	undo_redo->commit_action();
}

void SubtitlesDock::_tree_button_clicked(TreeItem *p_item, int p_column, int p_id, MouseButton p_button) {
	if (p_column != COLUMN_SPEAKER || p_button != MouseButton::LEFT || subtitle.is_null()) {
		return;
	}
	speaker_picker_row = p_item->get_metadata(0);
	const Ref<SubtitleSegment> segment = subtitle->get_segment(speaker_picker_row);
	ERR_FAIL_COND(segment.is_null());
	if (speaker_picker == nullptr) {
		speaker_picker = memnew(EditorSpeakerPicker);
		speaker_picker->connect(SNAME("speaker_picked"), callable_mp(this, &SubtitlesDock::_speaker_picked));
		add_child(speaker_picker);
	}
	speaker_picker->popup_picker(segment->get_speaker());
}

void SubtitlesDock::_speaker_picked(const StringName &p_id) {
	if (subtitle.is_null() || speaker_picker_row < 0 || speaker_picker_row >= subtitle->get_segment_count()) {
		return;
	}
	const Ref<SubtitleSegment> segment = subtitle->get_segment(speaker_picker_row);
	ERR_FAIL_COND(segment.is_null());
	// The id, the stable key; the shown name follows from it.
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Set Subtitle Segment Speaker"), UndoRedo::MERGE_DISABLE, subtitle.ptr());
	undo_redo->force_fixed_history(); // The segment, without a path, would count as built into the edited scene.
	undo_redo->add_do_property(segment.ptr(), SNAME("speaker"), p_id);
	undo_redo->add_undo_property(segment.ptr(), SNAME("speaker"), segment->get_speaker());
	undo_redo->add_do_method(subtitle.ptr(), "emit_changed");
	undo_redo->add_undo_method(subtitle.ptr(), "emit_changed");
	undo_redo->commit_action();
}

void SubtitlesDock::_tree_activated() {
	TreeItem *item = tree->get_selected();
	if (item == nullptr || subtitle.is_null()) {
		return;
	}
	// The # column is not edited in place: all properties of the segment in the Inspector.
	if (tree->get_selected_column() == COLUMN_INDEX) {
		const Ref<SubtitleSegment> segment = subtitle->get_segment(item->get_metadata(0));
		if (segment.is_valid()) {
			EditorInterface::get_singleton()->edit_resource(segment);
		}
	}
}

void SubtitlesDock::_file_removed(const String &p_file) {
	if (subtitle.is_valid() && subtitle->get_path() == p_file) {
		// Save would write the deleted file again.
		edit(Ref<Resource>());
		close();
	}
}

void SubtitlesDock::_resources_reloaded(const Vector<String> &p_files) {
	if (subtitle.is_null() || !p_files.has(subtitle->get_path())) {
		return;
	}
	// EditorNode has reloaded the resource in place by now (its segments are new): show them, keep the view.
	_cancel_drag();
	const bool had_changes = subtitle->is_edited();
	// The reload becomes an undo step back to the segments shown before: the older steps of the history (which
	// refer to those segments) stay valid, and unsaved edits can be brought back.
	if (shown_segments != subtitle->get_segment_list()) {
		TypedArray<SubtitleSegment> old_segments;
		for (const Ref<SubtitleSegment> &segment : shown_segments) {
			old_segments.push_back(segment);
		}
		EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
		undo_redo->create_action(TTR("Reload Subtitle from Disk"), UndoRedo::MERGE_DISABLE, subtitle.ptr());
		undo_redo->add_do_property(subtitle.ptr(), "segments", subtitle->get_segments());
		undo_redo->add_undo_property(subtitle.ptr(), "segments", old_segments);
		undo_redo->commit_action();
	}
	subtitle->set_edited(false); // The resource matches the file again.
	_update();
	// Deferred: the update queued by the reload itself runs first and would replace the message.
	callable_mp(status_label, &Label::set_text).call_deferred(TTR("The file changed on disk and was reloaded. Undo brings back the segments from before."));
	if (had_changes) {
		EditorToaster::get_singleton()->popup_str(vformat(TTR("%s changed on disk and was reloaded, replacing the unsaved changes in the Subtitles dock. Undo brings them back."), subtitle->get_path().get_file()), EditorToaster::SEVERITY_WARNING);
	}
}

// Shortcuts

void SubtitlesDock::input(const Ref<InputEvent> &p_event) {
	// Before the focused control: the table and the buttons would take Space and Delete themselves.
	const Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || subtitle.is_null() || !is_visible_in_tree() || drag_mode != DRAG_NONE) {
		return;
	}
	const Control *focus = get_viewport()->gui_get_focus_owner();
	if (focus == nullptr || !is_ancestor_of(focus) || Object::cast_to<LineEdit>(focus) || Object::cast_to<TextEdit>(focus) || tree->is_editing()) {
		return;
	}
	if (ED_IS_SHORTCUT("subtitles_dock/play_pause", p_event)) {
		_play_pause();
	} else if (ED_IS_SHORTCUT("subtitles_dock/spot_start", p_event)) {
		_spot(false);
	} else if (ED_IS_SHORTCUT("subtitles_dock/spot_end", p_event)) {
		_spot(true);
	} else if (ED_IS_SHORTCUT("subtitles_dock/back", p_event)) {
		_seek(playhead - 1.0);
	} else if (ED_IS_SHORTCUT("subtitles_dock/forward", p_event)) {
		_seek(playhead + 1.0);
	} else if (ED_IS_SHORTCUT("subtitles_dock/add_segment", p_event)) {
		_add_segment();
	} else if (ED_IS_SHORTCUT("subtitles_dock/remove_segment", p_event)) {
		_remove_segment();
	} else if (ED_IS_SHORTCUT("subtitles_dock/split_segment", p_event)) {
		_split_segment();
	} else if (ED_IS_SHORTCUT("subtitles_dock/merge_segments", p_event)) {
		_merge_segments();
	} else {
		return;
	}
	get_viewport()->set_input_as_handled();
}

void SubtitlesDock::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			AudioStreamPreviewGenerator::get_singleton()->connect(SNAME("preview_updated"), callable_mp(this, &SubtitlesDock::_preview_updated));
			EditorUndoRedoManager::get_singleton()->connect(SNAME("history_changed"), callable_mp(this, &SubtitlesDock::_update_file_label));
			EditorNode::get_singleton()->connect(SNAME("resource_saved"), callable_mp(this, &SubtitlesDock::_update_file_label).unbind(1));
			if (FileSystemDock::get_singleton()) {
				FileSystemDock::get_singleton()->connect(SNAME("file_removed"), callable_mp(this, &SubtitlesDock::_file_removed));
				// A moved subtitle keeps its resource with the new path: only the label changes.
				FileSystemDock::get_singleton()->connect(SNAME("files_moved"), callable_mp(this, &SubtitlesDock::_queue_update).unbind(2));
			}
			// Connected after EditorNode, which reloads changed resources in place first.
			EditorFileSystem::get_singleton()->connect(SNAME("resources_reload"), callable_mp(this, &SubtitlesDock::_resources_reloaded));
		} break;
		case NOTIFICATION_THEME_CHANGED: {
			play_button->set_button_icon(get_editor_theme_icon(playing ? SNAME("Pause") : SNAME("MainPlay")));
			add_button->set_button_icon(get_editor_theme_icon(SNAME("Add")));
			remove_button->set_button_icon(get_editor_theme_icon(SNAME("Remove")));
			save_button->set_button_icon(get_editor_theme_icon(SNAME("Save")));
			video_text_label->add_theme_color_override(SceneStringName(font_color), Color(1, 1, 1));
			video_text_label->add_theme_color_override("font_outline_color", Color(0, 0, 0));
			time_label->add_theme_font_override(SceneStringName(font), get_theme_font(SNAME("status_source"), EditorStringName(EditorFonts)));
			shown_segments.clear(); // Rebuild the rows with the new colors.
			_queue_update();
		} break;
		case NOTIFICATION_PROCESS: {
			if (!playing) {
				break;
			}
			if (stream.is_valid() && player->is_playing()) {
				playhead = player->get_playback_position();
			} else if (video.is_valid()) {
				if (video_player->is_playing()) {
					playhead = video_player->get_stream_position();
				}
			} else if (stream.is_null()) {
				playhead += get_process_delta_time(); // No stream: a clock, to spot segments against video or speech elsewhere.
				if (playhead >= _get_length()) {
					_player_finished();
				}
			}
			// Follow the playhead.
			const double visible_seconds = timeline->get_size().width / pixels_per_second;
			if (playhead > view_start + visible_seconds * 0.9 || playhead < view_start) {
				view_start = playhead - visible_seconds * 0.1;
				_update_scroll();
			}
			_update_time_label();
			_update_video_text();
			timeline->queue_redraw();
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (video.is_valid() && video_player->is_inside_tree()) {
				// Hidden: no decoder and frame kept alive; shown: the frame at the playhead again.
				if (is_visible_in_tree()) {
					_seek_video();
				} else {
					video_player->stop();
				}
			}
			if (!is_visible_in_tree()) {
				if (playing) {
					_play_pause();
				}
				_cancel_drag();
			}
		} break;
	}
}

void SubtitlesDock::edit(const Ref<Resource> &p_resource) {
	Ref<Subtitle> new_subtitle = Ref<Subtitle>(Object::cast_to<Subtitle>(p_resource.ptr()));
	Ref<Resource> new_stream;
	if (Object::cast_to<AudioStream>(p_resource.ptr()) || Object::cast_to<VideoStream>(p_resource.ptr())) {
		new_stream = p_resource;
	}
	if (new_stream.is_valid()) {
		const String path = get_stream_subtitle_path(new_stream.ptr());
		new_subtitle = path.is_empty() ? Ref<Subtitle>() : Ref<Subtitle>(ResourceLoader::load(path, "Subtitle"));
	}
	if (new_subtitle.is_valid() && new_subtitle == subtitle) {
		// The same subtitle again (back from inspecting a segment, or the Inspector button): keep the view.
		if (new_stream.is_valid() && new_stream.ptr() != stream.ptr() && new_stream.ptr() != video.ptr()) {
			_set_stream(new_stream);
		}
		return;
	}
	if (playing) {
		_play_pause();
	}
	const Callable callback = callable_mp(this, &SubtitlesDock::_subtitle_changed);
	if (subtitle.is_valid() && subtitle->is_connected(CoreStringName(changed), callback)) {
		subtitle->disconnect_changed(callback);
	}
	if (new_stream.is_null() && new_subtitle.is_valid()) {
		const String sidecar = find_sidecar_stream(new_subtitle->get_path());
		if (!sidecar.is_empty()) {
			new_stream = ResourceLoader::load(sidecar);
		}
	}
	subtitle = new_subtitle;
	if (subtitle.is_valid()) {
		subtitle->connect_changed(callback);
	}
	selected = -1;
	playhead = 0.0;
	view_start = 0.0;
	fit_pending = true;
	_set_stream(new_stream);
}

// Toolbar buttons: flat like Godot's other bottom panels, no keyboard focus (Space and Delete stay with the dock),
// the live shortcut in the tooltip.
static Button *_subtitles_dock_make_button(Control *p_context, const String &p_text, const String &p_tooltip, const String &p_shortcut) {
	Button *button = memnew(Button(p_text));
	button->set_theme_type_variation(SceneStringName(FlatButton));
	button->set_focus_mode(Control::FOCUS_ACCESSIBILITY);
	button->set_tooltip_text(p_tooltip);
	if (!p_shortcut.is_empty()) {
		button->set_shortcut(ED_GET_SHORTCUT(p_shortcut));
		button->set_shortcut_context(p_context);
		button->set_shortcut_in_tooltip(true);
	}
	return button;
}

SubtitlesDock::SubtitlesDock() {
	singleton = this;
	set_name(TTRC("Subtitles"));
	set_icon_name("Subtitle");
	set_dock_shortcut(ED_SHORTCUT_AND_COMMAND("bottom_panels/toggle_subtitles_bottom_panel", TTRC("Toggle Subtitles Dock")));
	set_default_slot(EditorDock::DOCK_SLOT_BOTTOM);
	set_available_layouts(EditorDock::DOCK_LAYOUT_HORIZONTAL | EditorDock::DOCK_LAYOUT_FLOATING);
	set_global(false);
	set_transient(true);
	set_custom_minimum_size(Size2(0, 300) * EDSCALE);
	set_process_input(true);
	pixels_per_second = 100.0 * EDSCALE;

	ED_SHORTCUT_ARRAY("subtitles_dock/play_pause", TTRC("Play/Pause"), { int32_t(Key::SPACE), int32_t(Key::K) });
	ED_SHORTCUT("subtitles_dock/spot_start", TTRC("Set Segment Start at Playhead"), Key::I);
	ED_SHORTCUT("subtitles_dock/spot_end", TTRC("Set Segment End at Playhead"), Key::O);
	ED_SHORTCUT("subtitles_dock/back", TTRC("Back 1 Second"), Key::J);
	ED_SHORTCUT("subtitles_dock/forward", TTRC("Forward 1 Second"), Key::L);
	ED_SHORTCUT("subtitles_dock/add_segment", TTRC("Add Segment at Playhead"), Key::N);
	ED_SHORTCUT("subtitles_dock/remove_segment", TTRC("Remove Segment"), Key::KEY_DELETE);
	ED_SHORTCUT_OVERRIDE("subtitles_dock/remove_segment", "macos", KeyModifierMask::META | Key::BACKSPACE);
	ED_SHORTCUT("subtitles_dock/split_segment", TTRC("Split Segment at Playhead"), Key::S);
	ED_SHORTCUT("subtitles_dock/merge_segments", TTRC("Merge Segment with Next"), Key::M);
	snap = EditorSettings::get_singleton()->get_project_metadata("subtitles_dock", "snap", true);

	player = memnew(AudioStreamPlayer);
	player->connect(SceneStringName(finished), callable_mp(this, &SubtitlesDock::_player_finished));
	add_child(player);

	VBoxContainer *main_vbox = memnew(VBoxContainer);
	add_child(main_vbox);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	main_vbox->add_child(toolbar);
	file_label = memnew(Label);
	file_label->set_mouse_filter(MOUSE_FILTER_PASS);
	toolbar->add_child(file_label);
	toolbar->add_child(memnew(VSeparator));

	stream_picker = memnew(EditorResourcePicker);
	stream_picker->set_base_type("AudioStream,VideoStream");
	stream_picker->set_custom_minimum_size(Size2(100, 0) * EDSCALE);
	stream_picker->set_tooltip_text(TTR("The audio for the waveform and playback. Found as an audio file with the subtitle's name, or the stream the dock was opened for; a stream picked here is not saved."));
	stream_picker->connect("resource_changed", callable_mp(this, &SubtitlesDock::_stream_picked));
	toolbar->add_child(stream_picker);
	stream_label = memnew(Label);
	stream_label->set_mouse_filter(MOUSE_FILTER_PASS);
	toolbar->add_child(stream_label);
	play_button = _subtitles_dock_make_button(this, String(), TTR("Play or pause from the playhead. The Back 1 Second and Forward 1 Second shortcuts move the playhead."), "subtitles_dock/play_pause");
	play_button->set_accessibility_name(TTRC("Play/Pause"));
	play_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_play_pause));
	toolbar->add_child(play_button);
	time_label = memnew(Label);
	toolbar->add_child(time_label);
	toolbar->add_child(memnew(VSeparator));

	add_button = _subtitles_dock_make_button(this, TTR("Add Segment"), TTR("Add a two-second segment at the playhead."), "subtitles_dock/add_segment");
	add_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_add_segment));
	toolbar->add_child(add_button);
	remove_button = _subtitles_dock_make_button(this, TTR("Remove Segment"), TTR("Remove the selected segment."), "subtitles_dock/remove_segment");
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_remove_segment));
	toolbar->add_child(remove_button);
	spot_start_button = _subtitles_dock_make_button(this, TTR("Set Start"), TTR("Set the start of the selected segment at the playhead. Without a selected segment, add one there."), "subtitles_dock/spot_start");
	spot_start_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_spot).bind(false));
	toolbar->add_child(spot_start_button);
	spot_end_button = _subtitles_dock_make_button(this, TTR("Set End"), TTR("Set the end of the selected segment at the playhead and select the next segment."), "subtitles_dock/spot_end");
	spot_end_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_spot).bind(true));
	toolbar->add_child(spot_end_button);
	split_button = _subtitles_dock_make_button(this, TTR("Split"), TTR("Split the selected segment at the playhead."), "subtitles_dock/split_segment");
	split_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_split_segment));
	toolbar->add_child(split_button);
	merge_button = _subtitles_dock_make_button(this, TTR("Merge"), TTR("Merge the selected segment with the next one."), "subtitles_dock/merge_segments");
	merge_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_merge_segments));
	toolbar->add_child(merge_button);
	timing_menu = memnew(MenuButton);
	timing_menu->set_text(TTR("Timing"));
	timing_menu->set_flat(true);
	timing_menu->set_tooltip_text(TTR("Shift, sync, split and merge segments, and snapping to silence."));
	PopupMenu *timing_popup = timing_menu->get_popup();
	timing_popup->add_item(TTR("Shift All Segments..."), TIMING_SHIFT);
	timing_popup->set_item_tooltip(-1, TTR("Move every segment earlier or later by the same time."));
	timing_popup->add_item(TTR("Two-Point Sync..."), TIMING_SYNC);
	timing_popup->set_item_tooltip(-1, TTR("Move and stretch all times so that two segments start where they should: fixes subtitles that drift."));
	timing_popup->add_separator();
	timing_popup->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/split_segment"), TIMING_SPLIT);
	timing_popup->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/merge_segments"), TIMING_MERGE);
	timing_popup->add_separator();
	timing_popup->add_check_item(TTR("Snap to Silence"), TIMING_SNAP);
	timing_popup->set_item_checked(-1, snap);
	timing_popup->set_item_tooltip(-1, TTR("Dragged edges snap to where speech starts or stops in the waveform, and to the edges of other segments. Hold Shift while dragging to place an edge freely."));
	timing_popup->connect(SceneStringName(id_pressed), callable_mp(this, &SubtitlesDock::_timing_menu_id_pressed));
	toolbar->add_child(timing_menu);
	problems_button = _subtitles_dock_make_button(this, TTR("Problems (0)"), TTR("Show or hide the list of problems beside the table."), String());
	problems_button->set_toggle_mode(true);
	toolbar->add_child(problems_button);
	toolbar->add_child(memnew(VSeparator));
	save_button = _subtitles_dock_make_button(this, TTR("Save"), String(), String());
	save_button->connect(SceneStringName(pressed), callable_mp(this, &SubtitlesDock::_save));
	toolbar->add_child(save_button);

	status_label = memnew(Label);
	status_label->set_h_size_flags(SIZE_EXPAND_FILL);
	status_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	status_label->set_mouse_filter(MOUSE_FILTER_PASS);
	toolbar->add_child(status_label);

	// The picture of a video stream left of the timeline and table, with the segments at the playhead over it.
	HSplitContainer *media_split = memnew(HSplitContainer);
	media_split->set_v_size_flags(SIZE_EXPAND_FILL);
	main_vbox->add_child(media_split);
	VBoxContainer *video_vbox = memnew(VBoxContainer);
	video_vbox->set_custom_minimum_size(Size2(240, 0) * EDSCALE);
	video_vbox->hide();
	media_split->add_child(video_vbox);
	video_box = video_vbox;
	video_panel = memnew(AspectRatioContainer);
	video_panel->set_v_size_flags(SIZE_EXPAND_FILL);
	video_panel->set_stretch_mode(AspectRatioContainer::STRETCH_FIT);
	video_vbox->add_child(video_panel);
	Button *mute_button = memnew(Button(TTR("Mute")));
	mute_button->set_toggle_mode(true);
	mute_button->set_theme_type_variation(SceneStringName(FlatButton));
	mute_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	mute_button->set_tooltip_text(TTR("Mute the sound of the video."));
	mute_button->connect(SceneStringName(toggled), callable_mp(this, &SubtitlesDock::_mute_toggled));
	video_vbox->add_child(mute_button);
	video_player = memnew(VideoStreamPlayer);
	video_player->set_expand(true);
	video_player->set_mouse_filter(MOUSE_FILTER_IGNORE);
	video_player->connect(SceneStringName(finished), callable_mp(this, &SubtitlesDock::_player_finished));
	video_panel->add_child(video_player);
	video_text_label = memnew(Label);
	video_text_label->set_anchors_and_offsets_preset(PRESET_BOTTOM_WIDE);
	video_text_label->set_grow_direction_preset(PRESET_BOTTOM_WIDE);
	video_text_label->set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	video_text_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	video_text_label->add_theme_constant_override("outline_size", int(4 * EDSCALE));
	video_player->add_child(video_text_label);

	VSplitContainer *split = memnew(VSplitContainer);
	split->set_v_size_flags(SIZE_EXPAND_FILL);
	split->set_h_size_flags(SIZE_EXPAND_FILL);
	media_split->add_child(split);

	VBoxContainer *timeline_vbox = memnew(VBoxContainer);
	timeline_vbox->add_theme_constant_override("separation", 0);
	split->add_child(timeline_vbox);
	timeline = memnew(Control);
	timeline->set_custom_minimum_size(Size2(0, 130) * EDSCALE);
	timeline->set_v_size_flags(SIZE_EXPAND_FILL);
	timeline->set_focus_mode(FOCUS_ALL);
	timeline->set_clip_contents(true);
	timeline->set_accessibility_name(TTRC("Subtitle Timeline"));
	timeline->set_tooltip_text(TTR("Click to move the playhead. Drag a segment to move it, its edges to trim it (Escape cancels); double-click a segment to inspect it. Ctrl+Wheel zooms."));
	timeline->connect(SceneStringName(draw), callable_mp(this, &SubtitlesDock::_timeline_draw));
	timeline->connect(SceneStringName(gui_input), callable_mp(this, &SubtitlesDock::_timeline_gui_input));
	timeline->connect(SceneStringName(resized), callable_mp(this, &SubtitlesDock::_update_scroll));
	timeline_vbox->add_child(timeline);
	timeline_scroll = memnew(HScrollBar);
	timeline_scroll->connect(SceneStringName(value_changed), callable_mp(this, &SubtitlesDock::_scroll_changed));
	timeline_vbox->add_child(timeline_scroll);

	tree = memnew(Tree);
	tree->set_v_size_flags(SIZE_EXPAND_FILL);
	tree->set_custom_minimum_size(Size2(0, 100) * EDSCALE);
	tree->set_columns(COLUMN_MAX);
	tree->set_hide_root(true);
	tree->set_select_mode(Tree::SELECT_ROW);
	tree->set_column_titles_visible(true);
	static const char *const titles[] = { "#", TTRC("Start"), TTRC("End"), TTRC("Speaker"), TTRC("Kind"), TTRC("Text") };
	static const int widths[] = { 50, 100, 100, 120, 120, 0 };
	for (int column = 0; column < COLUMN_MAX; column++) {
		tree->set_column_title(column, column == COLUMN_INDEX ? String(titles[column]) : TTR(titles[column]));
		if (widths[column] > 0) {
			tree->set_column_expand(column, false);
			tree->set_column_custom_minimum_width(column, widths[column] * EDSCALE);
		}
	}
	tree->set_tooltip_text(TTR("Click a cell of a selected row to edit it. Double-click # to inspect all properties of the segment."));
	tree->connect(SceneStringName(item_selected), callable_mp(this, &SubtitlesDock::_tree_selected));
	tree->connect("item_edited", callable_mp(this, &SubtitlesDock::_tree_edited));
	tree->connect("button_clicked", callable_mp(this, &SubtitlesDock::_tree_button_clicked));
	// Deferred: inspecting the segment rebuilds editors while the tree emits.
	tree->connect("item_activated", callable_mp(this, &SubtitlesDock::_tree_activated), CONNECT_DEFERRED);
	tree->set_allow_rmb_select(true);
	tree->connect("item_mouse_selected", callable_mp(this, &SubtitlesDock::_tree_mouse_selected));
	context_menu = memnew(PopupMenu);
	context_menu->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/add_segment"), TIMING_ADD);
	context_menu->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/remove_segment"), TIMING_REMOVE);
	context_menu->add_separator();
	context_menu->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/spot_start"), TIMING_SET_START);
	context_menu->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/spot_end"), TIMING_SET_END);
	context_menu->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/split_segment"), TIMING_SPLIT);
	context_menu->add_shortcut(ED_GET_SHORTCUT("subtitles_dock/merge_segments"), TIMING_MERGE);
	context_menu->add_separator();
	context_menu->add_item(TTR("Shift Segments..."), TIMING_SHIFT);
	context_menu->add_item(TTR("Inspect Segment"), TIMING_INSPECT);
	context_menu->connect(SceneStringName(id_pressed), callable_mp(this, &SubtitlesDock::_timing_menu_id_pressed));
	add_child(context_menu);
	split->add_child(tree);

	// The validation problems beside timeline and table: clicking a problem jumps to its segment.
	VBoxContainer *problem_vbox = memnew(VBoxContainer);
	problem_vbox->set_custom_minimum_size(Size2(220, 0) * EDSCALE);
	media_split->add_child(problem_vbox);
	problem_box = problem_vbox;
	const bool show_problems = EditorSettings::get_singleton()->get_project_metadata("subtitles_dock", "show_problems", true);
	problem_vbox->set_visible(show_problems);
	problems_button->set_pressed_no_signal(show_problems);
	problems_button->connect(SceneStringName(toggled), callable_mp(this, &SubtitlesDock::_toggle_problems));
	problem_header = memnew(Label(TTR("No problems")));
	problem_header->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	problem_vbox->add_child(problem_header);
	problem_view = memnew(ItemList);
	problem_view->set_v_size_flags(SIZE_EXPAND_FILL);
	problem_view->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	problem_view->set_accessibility_name(TTRC("Subtitle Problems"));
	problem_view->set_tooltip_text(TTR("Problems found by Subtitle.validate(). Click one to select its segment and move the playhead to it."));
	problem_view->connect("item_clicked", callable_mp(this, &SubtitlesDock::_problem_clicked));
	problem_vbox->add_child(problem_view);

	shift_dialog = memnew(ConfirmationDialog);
	shift_dialog->set_title(TTR("Shift All Segments"));
	VBoxContainer *shift_vbox = memnew(VBoxContainer);
	shift_dialog->add_child(shift_vbox);
	shift_vbox->add_child(memnew(Label(TTR("Add this time to every segment (negative: earlier).\nPrefilled to start the selected segment at the playhead."))));
	shift_edit = memnew(LineEdit);
	shift_edit->set_accessibility_name(TTRC("Shift"));
	shift_edit->set_placeholder(TTR("Seconds (-1.5) or minutes:seconds (-0:01.500)"));
	shift_vbox->add_child(shift_edit);
	shift_scope = memnew(OptionButton);
	shift_scope->set_accessibility_name(TTRC("Segments to Shift"));
	shift_scope->add_item(TTR("All segments"), SHIFT_ALL);
	shift_scope->add_item(TTR("The selected segment"), SHIFT_SELECTED);
	shift_scope->add_item(TTR("From the selected segment on"), SHIFT_FROM_SELECTED);
	shift_vbox->add_child(shift_scope);
	shift_dialog->register_text_enter(shift_edit);
	shift_dialog->connect(SceneStringName(confirmed), callable_mp(this, &SubtitlesDock::_shift_confirmed));
	add_child(shift_dialog);

	sync_dialog = memnew(ConfirmationDialog);
	sync_dialog->set_title(TTR("Two-Point Sync"));
	VBoxContainer *sync_vbox = memnew(VBoxContainer);
	sync_dialog->add_child(sync_vbox);
	sync_vbox->add_child(memnew(Label(TTR("Pick an early and a late segment and the times at which they should start.\nAll times in between are moved and stretched to match."))));
	static const char *const point_names[] = { TTRC("First segment"), TTRC("Second segment") };
	for (int point = 0; point < 2; point++) {
		HBoxContainer *row = memnew(HBoxContainer);
		sync_vbox->add_child(row);
		row->add_child(memnew(Label(TTR(point_names[point]))));
		sync_segments[point] = memnew(SpinBox);
		sync_segments[point]->set_min(1);
		sync_segments[point]->set_prefix("#");
		sync_segments[point]->set_accessibility_name(TTR(point_names[point]));
		sync_segments[point]->connect(SceneStringName(value_changed), callable_mp(this, &SubtitlesDock::_update_sync_preview).unbind(1));
		row->add_child(sync_segments[point]);
		sync_current[point] = memnew(Label);
		row->add_child(sync_current[point]);
		row->add_child(memnew(Label(TTR("should start at"))));
		sync_targets[point] = memnew(LineEdit);
		sync_targets[point]->set_h_size_flags(SIZE_EXPAND_FILL);
		sync_targets[point]->set_accessibility_name(TTRC("Start Time"));
		row->add_child(sync_targets[point]);
		sync_targets[point]->connect(SceneStringName(text_changed), callable_mp(this, &SubtitlesDock::_update_sync_preview).unbind(1));
		sync_dialog->register_text_enter(sync_targets[point]);
	}
	sync_factor_label = memnew(Label);
	sync_vbox->add_child(sync_factor_label);
	sync_dialog->connect(SceneStringName(confirmed), callable_mp(this, &SubtitlesDock::_sync_confirmed));
	add_child(sync_dialog);
}

SubtitlesDock::~SubtitlesDock() {
	_watch_segments(false);
	if (singleton == this) {
		singleton = nullptr;
	}
}
