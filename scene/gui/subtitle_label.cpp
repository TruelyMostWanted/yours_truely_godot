/**************************************************************************/
/*  subtitle_label.cpp                                                    */
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

#include "subtitle_label.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/subtitle_player.h"
#include "servers/settings/accessibility_settings_server.h"
#include "servers/subtitle/subtitle_server.h"

// `segment_changed` is left out: it follows `segment_started` or `segment_ended`, one rebuild each is enough.
static const char *const _SUBTITLE_LABEL_PLAYER_SIGNALS[] = { "segment_started", "segment_ended" };
static const char *const _SUBTITLE_LABEL_SETTINGS_SIGNALS[] = { "subtitles_enabled_changed", "captions_enabled_changed", "subtitle_scale_changed", "font_size_factor_changed" };
static const char *const _SUBTITLE_LABEL_SERVER_SIGNALS[] = { "enabled_changed", "captions_enabled_changed", "speakers_changed" };

SubtitlePlayer *SubtitleLabel::_get_player() const {
	return ObjectDB::get_instance<SubtitlePlayer>(player_id);
}

void SubtitleLabel::_connect_player(bool p_connect) {
	const Callable callback = callable_mp(this, &SubtitleLabel::refresh).unbind(1);
	// The player leaves the tree or is freed: resolve the path again, which clears the line if it is gone.
	const Callable exiting_callback = callable_mp(this, &SubtitleLabel::_reconnect);
	SubtitlePlayer *subtitle_player = _get_player();
	if (subtitle_player != nullptr) {
		for (const char *signal_name : _SUBTITLE_LABEL_PLAYER_SIGNALS) {
			if (subtitle_player->is_connected(signal_name, callback)) {
				subtitle_player->disconnect(signal_name, callback);
			}
		}
		if (subtitle_player->is_connected(SceneStringName(tree_exiting), exiting_callback)) {
			subtitle_player->disconnect(SceneStringName(tree_exiting), exiting_callback);
		}
	}
	player_id = ObjectID();
	if (!p_connect || !is_inside_tree() || player.is_empty()) {
		return;
	}
	SubtitlePlayer *found = Object::cast_to<SubtitlePlayer>(get_node_or_null(player));
	if (found == nullptr) {
		return;
	}
	player_id = found->get_instance_id();
	for (const char *signal_name : _SUBTITLE_LABEL_PLAYER_SIGNALS) {
		found->connect(signal_name, callback);
	}
	found->connect(SceneStringName(tree_exiting), exiting_callback, CONNECT_DEFERRED);
}

void SubtitleLabel::_reconnect() {
	if (!is_inside_tree()) {
		return; // Connected again when the label re-enters the tree.
	}
	_connect_player(true);
	_connect_settings(true);
	refresh();
}

template <size_t SIZE>
static void _subtitle_label_connect(Object *p_source, const char *const (&p_signals)[SIZE], const Callable &p_callback, bool p_connect) {
	if (p_source == nullptr) {
		return;
	}
	for (const char *signal_name : p_signals) {
		const bool connected = p_source->is_connected(signal_name, p_callback);
		if (p_connect && !connected) {
			p_source->connect(signal_name, p_callback);
		} else if (!p_connect && connected) {
			p_source->disconnect(signal_name, p_callback);
		}
	}
}

void SubtitleLabel::_connect_settings(bool p_connect) {
	const Callable callback = callable_mp(this, &SubtitleLabel::refresh).unbind(1);
	_subtitle_label_connect(AccessibilitySettingsServer::get_singleton(), _SUBTITLE_LABEL_SETTINGS_SIGNALS, callback, p_connect);
	_subtitle_label_connect(SubtitleServer::get_singleton(), _SUBTITLE_LABEL_SERVER_SIGNALS, callback, p_connect);
}

int SubtitleLabel::get_display_font_size() const {
	const AccessibilitySettingsServer *settings = AccessibilitySettingsServer::get_singleton();
	return (follow_accessibility_settings && settings != nullptr) ? settings->scale_subtitle_font_size(font_size) : font_size;
}

String SubtitleLabel::get_subtitle_text() const {
	const SubtitleServer *server = SubtitleServer::get_singleton();
	const SubtitlePlayer *subtitle_player = _get_player();
	if (server == nullptr || subtitle_player == nullptr || !server->is_enabled()) {
		return String();
	}
	bool show_captions = server->is_captions_enabled();
	if (follow_accessibility_settings) {
		const AccessibilitySettingsServer *settings = AccessibilitySettingsServer::get_singleton();
		if (settings != nullptr && !settings->is_subtitles_enabled()) {
			return String();
		}
		show_captions = show_captions && (settings == nullptr || settings->is_captions_enabled());
	}
	const Ref<SubtitlePlayback> playback = subtitle_player->get_playback();
	const Ref<Subtitle> subtitle = playback->get_subtitle();
	const TypedArray<SubtitleSegment> segments = playback->get_active_segments();
	PackedStringArray lines;
	for (int idx = 0; idx < segments.size(); idx++) {
		const Ref<SubtitleSegment> segment = segments[idx];
		if (segment.is_null() || (segment->is_caption() && !show_captions)) {
			continue;
		}
		String line = server->get_display_text(subtitle, segment, true);
		if (!segment->is_caption()) {
			// The name is translated like the text (domain `subtitles`, context `speaker`).
			line = _decorate_line(line, segment->get_speaker(), server->get_display_speaker(subtitle, segment));
		}
		lines.push_back(line);
	}
	return _wrap_lines(lines);
}

String SubtitleLabel::_decorate_line(const String &p_line, const StringName &p_speaker, const String &p_display_speaker) const {
	String line = p_line;
	if (p_speaker != StringName()) {
		if (show_speaker_names) {
			line = p_display_speaker.replace("[", "[lb]") + ": " + line;
		}
		// The project's speakers (#483). A color with alpha 0 (their default) keeps the label's font color.
		const SubtitleServer *server = SubtitleServer::get_singleton();
		const Ref<DialogueSpeaker> known = server != nullptr ? server->get_speakers()->get_speaker(p_speaker) : Ref<DialogueSpeaker>();
		if (known.is_valid() && known->get_color().a > 0.0f) {
			line = "[color=#" + known->get_color().to_html() + "]" + line + "[/color]";
		}
	}
	return line;
}

String SubtitleLabel::_wrap_lines(const PackedStringArray &p_lines) const {
	if (p_lines.is_empty()) {
		return String();
	}
	// A BBCode tag rather than a theme override: nothing of it is saved with the scene.
	return vformat("[font_size=%d]", get_display_font_size()) + String("\n").join(p_lines) + "[/font_size]";
}

void SubtitleLabel::refresh() {
	String bbcode = get_subtitle_text();
	const bool editor_hint = Engine::get_singleton()->is_editor_hint();
	if (bbcode.is_empty() && editor_hint) {
		// In the editor a sample line stays visible, so the label can be placed and styled without playing anything.
		const String line = preview_text.replace("[", "[lb]");
		const SubtitleServer *server = SubtitleServer::get_singleton();
		const Ref<DialogueSpeaker> known = server != nullptr ? server->get_speakers()->get_speaker(preview_speaker) : Ref<DialogueSpeaker>();
		const String display_speaker = known.is_valid() ? known->get_translated_name() : String(preview_speaker);
		bbcode = _wrap_lines(PackedStringArray({ _decorate_line(line, preview_speaker, display_speaker) }));
	}
	if (bbcode != get_text()) {
		set_text(bbcode);
	}
	if (!editor_hint) {
		set_visible(!bbcode.is_empty());
	}
}

void SubtitleLabel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			// Re-entering after a reparent: no second `_ready()`. Once more deferred if the player comes later.
			if (is_ready()) {
				_reconnect();
				if (player_id.is_null()) {
					callable_mp(this, &SubtitleLabel::_reconnect).call_deferred();
				}
			}
		} break;
		case NOTIFICATION_READY: {
			// Ready, not enter tree: the player may be a later sibling.
			_reconnect();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_connect_player(false);
			_connect_settings(false);
		} break;
		case NOTIFICATION_TRANSLATION_CHANGED: {
			// The subtitles channel locale changed: the active lines are translated again. Also sent on every enter
			// tree and by `set_auto_translate_mode()` in the constructor: the first refresh is at ready.
			if (is_ready()) {
				refresh();
			}
		} break;
	}
}

void SubtitleLabel::_validate_property(PropertyInfo &p_property) const {
	// Written by the label itself: not stored, not editable.
	// `auto_translate_mode` too: the lines are translated in the `subtitles` domain, not again in the node's domain.
	if (p_property.name == "text" || p_property.name == "bbcode_enabled" || p_property.name == "auto_translate_mode") {
		p_property.usage = (p_property.usage & ~PROPERTY_USAGE_STORAGE) | PROPERTY_USAGE_READ_ONLY;
	} else if (p_property.name == "preview_speaker") {
		SubtitleServer::add_speaker_id_hint(p_property);
	}
}

void SubtitleLabel::set_player(const NodePath &p_path) {
	if (player == p_path) {
		return;
	}
	player = p_path;
	if (is_ready()) {
		_connect_player(true);
		refresh();
	}
}

NodePath SubtitleLabel::get_player() const {
	return player;
}

void SubtitleLabel::set_show_speaker_names(bool p_enabled) {
	show_speaker_names = p_enabled;
	refresh();
}

bool SubtitleLabel::is_showing_speaker_names() const {
	return show_speaker_names;
}

void SubtitleLabel::set_font_size(int p_size) {
	font_size = MAX(1, p_size);
	refresh();
}

int SubtitleLabel::get_font_size() const {
	return font_size;
}

void SubtitleLabel::set_follow_accessibility_settings(bool p_enabled) {
	follow_accessibility_settings = p_enabled;
	refresh();
}

bool SubtitleLabel::is_following_accessibility_settings() const {
	return follow_accessibility_settings;
}

void SubtitleLabel::set_preview_speaker(const StringName &p_speaker) {
	preview_speaker = p_speaker;
	refresh();
}

StringName SubtitleLabel::get_preview_speaker() const {
	return preview_speaker;
}

void SubtitleLabel::set_preview_text(const String &p_text) {
	preview_text = p_text;
	refresh();
}

String SubtitleLabel::get_preview_text() const {
	return preview_text;
}

void SubtitleLabel::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_player", "path"), &SubtitleLabel::set_player);
	ClassDB::bind_method(D_METHOD("get_player"), &SubtitleLabel::get_player);
	ClassDB::bind_method(D_METHOD("set_show_speaker_names", "enabled"), &SubtitleLabel::set_show_speaker_names);
	ClassDB::bind_method(D_METHOD("is_showing_speaker_names"), &SubtitleLabel::is_showing_speaker_names);
	ClassDB::bind_method(D_METHOD("set_font_size", "size"), &SubtitleLabel::set_font_size);
	ClassDB::bind_method(D_METHOD("get_font_size"), &SubtitleLabel::get_font_size);
	ClassDB::bind_method(D_METHOD("set_follow_accessibility_settings", "enabled"), &SubtitleLabel::set_follow_accessibility_settings);
	ClassDB::bind_method(D_METHOD("is_following_accessibility_settings"), &SubtitleLabel::is_following_accessibility_settings);
	ClassDB::bind_method(D_METHOD("set_preview_speaker", "speaker"), &SubtitleLabel::set_preview_speaker);
	ClassDB::bind_method(D_METHOD("get_preview_speaker"), &SubtitleLabel::get_preview_speaker);
	ClassDB::bind_method(D_METHOD("set_preview_text", "text"), &SubtitleLabel::set_preview_text);
	ClassDB::bind_method(D_METHOD("get_preview_text"), &SubtitleLabel::get_preview_text);
	ClassDB::bind_method(D_METHOD("get_display_font_size"), &SubtitleLabel::get_display_font_size);
	ClassDB::bind_method(D_METHOD("get_subtitle_text"), &SubtitleLabel::get_subtitle_text);
	ClassDB::bind_method(D_METHOD("refresh"), &SubtitleLabel::refresh);

	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "player", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "SubtitlePlayer"), "set_player", "get_player");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "show_speaker_names"), "set_show_speaker_names", "is_showing_speaker_names");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "font_size", PROPERTY_HINT_RANGE, "1,256,1,or_greater,suffix:px"), "set_font_size", "get_font_size");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_accessibility_settings"), "set_follow_accessibility_settings", "is_following_accessibility_settings");
	ADD_GROUP("Preview", "preview_");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "preview_speaker"), "set_preview_speaker", "get_preview_speaker");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "preview_text", PROPERTY_HINT_MULTILINE_TEXT), "set_preview_text", "get_preview_text");
}

SubtitleLabel::SubtitleLabel() {
	set_use_bbcode(true);
	set_fit_content(true);
	set_horizontal_alignment(HORIZONTAL_ALIGNMENT_CENTER);
	set_mouse_filter(MOUSE_FILTER_IGNORE);
	set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
}
