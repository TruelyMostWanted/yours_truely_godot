/**************************************************************************/
/*  subtitle_label.h                                                      */
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

#include "scene/gui/rich_text_label.h"
#include "servers/subtitle/subtitle.h"

class SubtitlePlayer;

// A ready-made subtitle line: shows the active segments of a `SubtitlePlayer`, names and colors speakers
// (`SubtitleServer.speakers`) and follows
// `AccessibilitySettingsServer` (`subtitles_enabled`, `captions_enabled`, `subtitle_scale`, `font_size_factor`). It writes `text` itself.
class SubtitleLabel : public RichTextLabel {
	GDCLASS(SubtitleLabel, RichTextLabel);

	NodePath player;
	bool show_speaker_names = true;
	int font_size = 32;
	bool follow_accessibility_settings = true;
	StringName preview_speaker = StringName("Speaker");
	String preview_text = "The subtitle line appears here.";

	ObjectID player_id;

	SubtitlePlayer *_get_player() const;
	void _connect_player(bool p_connect);
	void _connect_settings(bool p_connect);
	void _reconnect();
	String _decorate_line(const String &p_line, const StringName &p_speaker, const String &p_display_speaker) const;
	String _wrap_lines(const PackedStringArray &p_lines) const;

protected:
	void _notification(int p_what);
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	void set_player(const NodePath &p_path);
	NodePath get_player() const;
	void set_show_speaker_names(bool p_enabled);
	bool is_showing_speaker_names() const;
	void set_font_size(int p_size);
	int get_font_size() const;
	void set_follow_accessibility_settings(bool p_enabled);
	bool is_following_accessibility_settings() const;
	void set_preview_speaker(const StringName &p_speaker);
	StringName get_preview_speaker() const;
	void set_preview_text(const String &p_text);
	String get_preview_text() const;

	// `font_size`, scaled by `AccessibilitySettingsServer.scale_subtitle_font_size()` when following the settings.
	int get_display_font_size() const;
	// The BBCode of the lines to show now; empty while nothing is shown.
	String get_subtitle_text() const;
	// Rebuilds `text` from the player's active segments. Called on every segment and settings change.
	void refresh();

	SubtitleLabel();
};
