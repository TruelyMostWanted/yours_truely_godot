/**************************************************************************/
/*  editor_speaker_picker.cpp                                             */
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

#include "editor_speaker_picker.h"

#include "core/input/input_event.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/string/fuzzy_search.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/tree.h"
#include "scene/resources/texture.h"
#include "scene/scene_string_names.h"
#include "servers/subtitle/subtitle_server.h"

// EditorSpeakerPicker

void EditorSpeakerPicker::merge_speakers(const Vector<Ref<DialogueSpeaker>> &p_add, const String &p_source, Vector<Ref<DialogueSpeaker>> &r_speakers, Vector<String> &r_sources) {
	for (const Ref<DialogueSpeaker> &speaker : p_add) {
		if (speaker.is_null() || speaker->get_id() == StringName()) {
			continue;
		}
		bool known = false;
		for (const Ref<DialogueSpeaker> &existing : r_speakers) {
			if (DialogueSpeaker::ids_match(existing->get_id(), speaker->get_id())) {
				known = true;
				break;
			}
		}
		if (!known) {
			r_speakers.push_back(speaker);
			r_sources.push_back(p_source);
		}
	}
}

Vector<int> EditorSpeakerPicker::rank_speakers(const Vector<Ref<DialogueSpeaker>> &p_speakers, const String &p_query) {
	Vector<int> result;
	const String query = p_query.strip_edges();
	if (query.is_empty()) {
		for (int idx = 0; idx < p_speakers.size(); idx++) {
			result.push_back(idx);
		}
		return result;
	}
	PackedStringArray targets;
	for (const Ref<DialogueSpeaker> &speaker : p_speakers) {
		targets.push_back(speaker->get_translated_name() + " " + String(speaker->get_id()));
	}
	Ref<FuzzySearch> fuzzy;
	fuzzy.instantiate();
	fuzzy->set_max_results(p_speakers.size());
	for (const Ref<FuzzySearchMatch> &match : fuzzy->search_all(query, targets)) {
		result.push_back(match->get_original_index());
	}
	return result;
}

void EditorSpeakerPicker::_collect_dir(EditorFileSystemDirectory *p_dir) {
	for (int idx = 0; idx < p_dir->get_file_count(); idx++) {
		const StringName type = p_dir->get_file_type(idx);
		const bool one = ClassDB::is_parent_class(type, SNAME("DialogueSpeaker"));
		if (!one && !ClassDB::is_parent_class(type, SNAME("DialogueSpeakers"))) {
			continue;
		}
		const String path = p_dir->get_file_path(idx);
		const Ref<Resource> resource = ResourceLoader::load(path);
		Vector<Ref<DialogueSpeaker>> found;
		const Ref<DialogueSpeaker> single = resource;
		const Ref<DialogueSpeakers> list_resource = resource;
		if (single.is_valid()) {
			found.push_back(single);
		} else if (list_resource.is_valid()) {
			found = list_resource->get_speaker_list();
		}
		merge_speakers(found, path, speakers, sources);
	}
	for (int idx = 0; idx < p_dir->get_subdir_count(); idx++) {
		_collect_dir(p_dir->get_subdir(idx));
	}
}

void EditorSpeakerPicker::_collect() {
	speakers.clear();
	sources.clear();
	const SubtitleServer *server = SubtitleServer::get_singleton();
	if (server != nullptr) {
		const Ref<DialogueSpeakers> project = server->get_speakers();
		merge_speakers(project->get_speaker_list(), project->get_path().is_empty() ? TTR("Project speakers") : project->get_path(), speakers, sources);
	}
	EditorFileSystem *file_system = EditorFileSystem::get_singleton();
	if (file_system != nullptr && file_system->get_filesystem() != nullptr) {
		_collect_dir(file_system->get_filesystem());
	}
}

void EditorSpeakerPicker::_update_list() {
	list->clear();
	TreeItem *root = list->create_item();
	TreeItem *to_select = nullptr;
	for (int idx : rank_speakers(speakers, search->get_text())) {
		const Ref<DialogueSpeaker> &speaker = speakers[idx];
		TreeItem *item = list->create_item(root);
		item->set_metadata(0, speaker->get_id());
		if (speaker->get_color().a > 0.0f) {
			item->set_custom_bg_color(0, speaker->get_color());
		}
		const Ref<Texture2D> portrait = speaker->load_portrait();
		if (portrait.is_valid()) {
			item->set_icon(1, portrait);
			item->set_icon_max_width(1, int(32 * EDSCALE));
		}
		item->set_text(1, speaker->get_translated_name());
		item->set_text(2, speaker->get_id());
		item->set_tooltip_text(1, sources[idx]);
		item->set_tooltip_text(2, sources[idx]);
		if (to_select == nullptr || (current_id != StringName() && DialogueSpeaker::ids_match(speaker->get_id(), current_id) && search->get_text().is_empty())) {
			to_select = item;
		}
	}
	if (to_select != nullptr) {
		to_select->select(1);
		list->ensure_cursor_is_visible();
	}
	get_ok_button()->set_disabled(to_select == nullptr);
}

void EditorSpeakerPicker::_search_input(const Ref<InputEvent> &p_event) {
	const Ref<InputEventKey> key = p_event;
	if (key.is_null() || !key->is_pressed() || (key->get_keycode() != Key::UP && key->get_keycode() != Key::DOWN)) {
		return;
	}
	TreeItem *selected = list->get_selected();
	if (selected != nullptr) {
		TreeItem *next = key->get_keycode() == Key::UP ? selected->get_prev_visible() : selected->get_next_visible();
		if (next != nullptr && next != list->get_root()) {
			next->select(1);
			list->ensure_cursor_is_visible();
		}
	}
	search->accept_event();
}

void EditorSpeakerPicker::_pick() {
	TreeItem *selected = list->get_selected();
	if (selected == nullptr) {
		return;
	}
	const StringName id = selected->get_metadata(0);
	hide();
	emit_signal(SNAME("speaker_picked"), id);
}

void EditorSpeakerPicker::popup_picker(const StringName &p_current_id) {
	current_id = p_current_id;
	_collect();
	search->clear();
	_update_list();
	popup_centered_clamped(Size2(560, 420) * EDSCALE, 0.8f);
	search->grab_focus();
}

void EditorSpeakerPicker::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		search->set_right_icon(get_editor_theme_icon(SNAME("Search")));
	}
}

void EditorSpeakerPicker::_bind_methods() {
	ADD_SIGNAL(MethodInfo("speaker_picked", PropertyInfo(Variant::STRING_NAME, "id")));
}

EditorSpeakerPicker::EditorSpeakerPicker() {
	set_title(TTR("Pick a Speaker"));
	set_ok_button_text(TTR("Pick"));
	set_hide_on_ok(false);
	VBoxContainer *box = memnew(VBoxContainer);
	add_child(box);
	search = memnew(LineEdit);
	search->set_placeholder(TTR("Search speakers by name or id"));
	search->set_clear_button_enabled(true);
	search->connect(SceneStringName(text_changed), callable_mp(this, &EditorSpeakerPicker::_update_list).unbind(1));
	search->connect(SceneStringName(gui_input), callable_mp(this, &EditorSpeakerPicker::_search_input));
	box->add_child(search);
	register_text_enter(search);
	list = memnew(Tree);
	list->set_hide_root(true);
	list->set_columns(3);
	list->set_column_expand(0, false);
	list->set_column_custom_minimum_width(0, int(12 * EDSCALE));
	list->set_column_expand(2, false);
	list->set_column_custom_minimum_width(2, int(140 * EDSCALE));
	list->set_select_mode(Tree::SELECT_ROW);
	list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	list->connect("item_activated", callable_mp(this, &EditorSpeakerPicker::_pick));
	box->add_child(list);
	connect(SceneStringName(confirmed), callable_mp(this, &EditorSpeakerPicker::_pick));
}

// EditorPropertySpeakerId

void EditorPropertySpeakerId::_commit() {
	const StringName id = StringName(line_edit->get_text().strip_edges());
	if (id != StringName(get_edited_property_value())) {
		emit_changed(get_edited_property(), id);
	}
}

void EditorPropertySpeakerId::_text_submitted(const String &p_text) {
	_commit();
}

void EditorPropertySpeakerId::_open_picker() {
	if (picker == nullptr) {
		picker = memnew(EditorSpeakerPicker);
		picker->connect(SNAME("speaker_picked"), callable_mp(this, &EditorPropertySpeakerId::_picked));
		add_child(picker);
	}
	picker->popup_picker(StringName(line_edit->get_text().strip_edges()));
}

void EditorPropertySpeakerId::_picked(const StringName &p_id) {
	line_edit->set_text(p_id);
	_commit();
}

void EditorPropertySpeakerId::update_property() {
	const String id = StringName(get_edited_property_value());
	if (line_edit->get_text() != id) {
		line_edit->set_text(id);
	}
	line_edit->set_editable(!is_read_only());
	pick_button->set_disabled(is_read_only());
}

void EditorPropertySpeakerId::_notification(int p_what) {
	if (p_what == NOTIFICATION_THEME_CHANGED) {
		pick_button->set_button_icon(get_editor_theme_icon(SNAME("Search")));
	}
}

EditorPropertySpeakerId::EditorPropertySpeakerId() {
	HBoxContainer *box = memnew(HBoxContainer);
	add_child(box);
	line_edit = memnew(LineEdit);
	line_edit->set_h_size_flags(SIZE_EXPAND_FILL);
	line_edit->set_placeholder(TTR("Speaker id"));
	line_edit->connect(SceneStringName(text_submitted), callable_mp(this, &EditorPropertySpeakerId::_text_submitted));
	line_edit->connect(SceneStringName(focus_exited), callable_mp(this, &EditorPropertySpeakerId::_commit));
	box->add_child(line_edit);
	add_focusable(line_edit);
	pick_button = memnew(Button);
	pick_button->set_flat(true);
	pick_button->set_tooltip_text(TTR("Pick a speaker"));
	pick_button->connect(SceneStringName(pressed), callable_mp(this, &EditorPropertySpeakerId::_open_picker));
	box->add_child(pick_button);
}
