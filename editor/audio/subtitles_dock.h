/**************************************************************************/
/*  subtitles_dock.h                                                      */
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
#include "editor/docks/editor_dock.h"
#include "servers/subtitle/subtitle.h"

class AudioStream;
class AudioStreamPlayer;
class AspectRatioContainer;
class Button;
class ConfirmationDialog;
class EditorResourcePicker;
class HScrollBar;
class ItemList;
class Label;
class LineEdit;
class MenuButton;
class OptionButton;
class PopupMenu;
class SpinBox;
class Tree;
class TreeItem;
class VideoStream;
class VideoStreamPlayer;

// Edits the segments of a `Subtitle` in a bottom dock: a table and a timeline with the waveform of the audio
// stream (or the picture of a video stream), where segments are moved and trimmed with the mouse, snapped to
// silence and spotted at the playhead with the keyboard. Timing tools shift, sync, split and merge segments;
// a list shows the validation problems. Opened for a Subtitle resource, or from the Inspector of an AudioStream
// with a subtitle file; the Inspector table is the quick path.
class SubtitlesDock : public EditorDock {
	GDCLASS(SubtitlesDock, EditorDock);

	enum Column {
		COLUMN_INDEX,
		COLUMN_START,
		COLUMN_END,
		COLUMN_SPEAKER,
		COLUMN_KIND,
		COLUMN_TEXT,
		COLUMN_MAX,
	};

	enum TimingMenu {
		TIMING_SHIFT,
		TIMING_SYNC,
		TIMING_SPLIT,
		TIMING_MERGE,
		TIMING_SNAP,
		// Only in the table's context menu.
		TIMING_ADD,
		TIMING_REMOVE,
		TIMING_SET_START,
		TIMING_SET_END,
		TIMING_INSPECT,
	};

	enum ShiftScope {
		SHIFT_ALL,
		SHIFT_SELECTED,
		SHIFT_FROM_SELECTED,
	};

	enum DragMode {
		DRAG_NONE,
		DRAG_SEEK,
		DRAG_MOVE,
		DRAG_START,
		DRAG_END,
	};

	static constexpr double MIN_DURATION = 0.1;
	static constexpr double NEW_DURATION = 2.0;
	static constexpr double LEVEL_WINDOW = 0.01; // Seconds per entry of `levels`.
	static constexpr real_t DRAG_SNAP_RADIUS = 8; // Pixels, for dragged edges.
	static constexpr uint64_t VIDEO_SCRUB_MSEC = 80; // At most one video seek per this time while scrubbing.
	static inline SubtitlesDock *singleton = nullptr;

	Ref<Subtitle> subtitle;
	Ref<AudioStream> stream;
	Ref<VideoStream> video;
	int selected = -1;
	HashMap<int, PackedStringArray> problems; // Of the last update: segment index -> messages.
	Vector<Ref<SubtitleSegment>> shown_segments; // The segments the table rows were built for.
	HashSet<int> changed_rows; // Rows whose segment changed since the last update.
	bool update_queued = false;
	bool updating_tree = false;

	// Timeline view, in stream seconds (segment times plus the time offset).
	double view_start = 0.0;
	double pixels_per_second = 100.0;
	bool fit_pending = false; // Zoom out to the whole stream once the timeline has a size.
	double playhead = 0.0;
	bool playing = false;
	bool snap = true;

	// Loudness of the stream per LEVEL_WINDOW, for snapping to silence; rebuilt when the preview changes.
	Vector<float> levels;
	float level_threshold = 0.0f;
	ObjectID levels_stream;
	uint64_t levels_version = 0;
	bool snap_line_visible = false; // Drawn while a dragged edge sits on a snap target.
	double snap_line_time = 0.0; // In segment time.
	uint64_t last_video_seek_msec = 0;
	bool video_seek_pending = false;
	Size2i video_size;

	DragMode drag_mode = DRAG_NONE;
	double drag_from_time = 0.0;
	double drag_old_start = 0.0;
	double drag_old_end = 0.0;

	Label *file_label = nullptr;
	EditorResourcePicker *stream_picker = nullptr;
	Label *stream_label = nullptr;
	Button *play_button = nullptr;
	Label *time_label = nullptr;
	Button *add_button = nullptr;
	Button *remove_button = nullptr;
	Button *spot_start_button = nullptr;
	Button *spot_end_button = nullptr;
	Button *save_button = nullptr;
	Label *status_label = nullptr;
	Button *split_button = nullptr;
	Button *merge_button = nullptr;
	Button *problems_button = nullptr;
	MenuButton *timing_menu = nullptr;
	PopupMenu *context_menu = nullptr;
	Control *video_box = nullptr;
	AspectRatioContainer *video_panel = nullptr;
	VideoStreamPlayer *video_player = nullptr;
	Label *video_text_label = nullptr;
	Control *problem_box = nullptr;
	Label *problem_header = nullptr;
	ItemList *problem_view = nullptr;
	ConfirmationDialog *shift_dialog = nullptr;
	LineEdit *shift_edit = nullptr;
	OptionButton *shift_scope = nullptr;
	ConfirmationDialog *sync_dialog = nullptr;
	SpinBox *sync_segments[2] = {};
	LineEdit *sync_targets[2] = {};
	Label *sync_current[2] = {};
	Label *sync_factor_label = nullptr;
	Control *timeline = nullptr;
	HScrollBar *timeline_scroll = nullptr;
	Tree *tree = nullptr;
	AudioStreamPlayer *player = nullptr;

	static real_t _get_ruler_height();
	static real_t _get_lane_height();
	double _get_offset() const;
	double _get_length() const;
	double _x_to_time(double p_x) const;
	double _time_to_x(double p_time) const;
	Rect2 _get_segment_rect(int p_index) const;
	int _get_segment_at(const Point2 &p_pos, DragMode &r_mode) const;

	void _queue_update();
	void _update();
	void _update_file_label();
	void _update_tree();
	void _fill_row(TreeItem *p_item, int p_index, const String &p_kinds);
	void _watch_segments(bool p_watch);
	void _update_scroll();
	void _update_time_label();
	void _subtitle_changed();
	void _segment_changed(int p_index);
	void _select(int p_index, bool p_scroll_tree = true);
	void _jump_to(int p_index);
	void _update_problem_view();
	void _problem_clicked(int p_item, const Vector2 &p_position, MouseButton p_button);
	void _toggle_problems(bool p_visible);
	void _update_video_text();

	void _commit_segments(const String &p_action, const TypedArray<SubtitleSegment> &p_segments, int p_select);
	void _commit_times(const String &p_action, int p_index, double p_start, double p_end, double p_old_start, double p_old_end);
	void _add_segment();
	void _remove_segment();
	void _spot(bool p_end);
	void _save();
	Ref<Subtitle> _make_copy(int p_from, int p_to) const;
	void _split_segment();
	void _merge_segments();
	void _popup_shift();
	void _shift_confirmed();
	void _popup_sync();
	void _sync_confirmed();
	void _update_sync_preview();
	void _warn_negative_starts(const Ref<Subtitle> &p_copy);
	void _timing_menu_id_pressed(int p_id);
	void _update_levels();
	double _snap(double p_time, bool p_onset, double p_radius, int p_exclude);

	void _play_pause();
	void _seek(double p_time);
	void _player_finished();
	void _stream_picked(const Ref<Resource> &p_stream);
	void _set_stream(const Ref<Resource> &p_stream);
	void _seek_video();
	void _mute_toggled(bool p_muted);
	void _preview_updated(ObjectID p_id);

	void _timeline_draw();
	void _timeline_gui_input(const Ref<InputEvent> &p_event);
	void _cancel_drag();
	void _scroll_changed(double p_value);
	void _tree_selected();
	void _tree_edited();
	void _tree_activated();
	void _tree_mouse_selected(const Vector2 &p_position, MouseButton p_button);
	void _file_removed(const String &p_file);
	void _resources_reloaded(const Vector<String> &p_files);

protected:
	void _notification(int p_what);
	virtual void input(const Ref<InputEvent> &p_event) override;

public:
	static SubtitlesDock *get_singleton() { return singleton; }
	// A Subtitle resource (audio streams open through the Inspector button).
	static bool can_edit(const Object *p_object);
	// The subtitle file of an AudioStream or VideoStream: its `subtitle_path`, else an .srt or .vtt next to the
	// stream file.
	static String get_stream_subtitle_path(const Resource *p_stream);
	// An audio file with the base name of `p_subtitle_path` (voice.srt -> voice.wav, .ogg, ...), else a video
	// file (intro.srt -> intro.ogv), or empty.
	static String find_sidecar_stream(const String &p_subtitle_path);
	// The time of the silence edge nearest to `p_time` within `p_radius`: where `p_levels` (one loudness value
	// per `p_window` seconds) rises above `p_threshold` (`p_onset`, the start of speech) or falls below it.
	static bool find_silence_edge(const Vector<float> &p_levels, double p_window, double p_time, double p_radius, bool p_onset, float p_threshold, double &r_time);
	// Reads a time with an optional sign ("-1.5", "+0:02.000") as seconds.
	static bool parse_signed_time(const String &p_text, double &r_seconds);

	// Edits a Subtitle (finding a sidecar audio or video stream) or an AudioStream or VideoStream (loading its
	// subtitle file).
	// The same subtitle again keeps selection, zoom, playhead and stream.
	void edit(const Ref<Resource> &p_resource);
	Ref<Subtitle> get_subtitle() const { return subtitle; }

	SubtitlesDock();
	~SubtitlesDock();
};
