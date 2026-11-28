/**************************************************************************/
/*  test_subtitle_label.cpp                                               */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_subtitle_label)

#ifndef ADVANCED_GUI_DISABLED

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/message_queue.h"
#include "core/string/translation_server.h"
#include "scene/gui/subtitle_label.h"
#include "scene/main/scene_tree.h"
#include "scene/main/subtitle_player.h"
#include "scene/main/window.h"
#include "servers/settings/accessibility_settings_server.h"
#include "servers/subtitle/subtitle_server.h"
#include "tests/signal_watcher.h"

namespace TestSubtitleLabel {

static Ref<SubtitleSegment> make_segment(double p_start, double p_end, const String &p_text, const StringName &p_speaker = StringName(), SubtitleSegment::Kind p_kind = SubtitleSegment::KIND_DIALOG) {
	Ref<SubtitleSegment> segment;
	segment.instantiate();
	segment->set_start_time(p_start);
	segment->set_end_time(p_end);
	segment->set_text(p_text);
	segment->set_speaker(p_speaker);
	segment->set_kind(p_kind);
	return segment;
}

// 0-1 "Hello" (Anna), 2-4 "Applause" (sound), 3-4 "Run!" (Ben, overlaps the sound).
static Ref<Subtitle> make_subtitle() {
	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(0.0, 1.0, "Hello", "Anna"));
	subtitle->add_segment(make_segment(2.0, 4.0, "Applause", StringName(), SubtitleSegment::KIND_SOUND));
	subtitle->add_segment(make_segment(3.0, 4.0, "Run!", "Ben"));
	return subtitle;
}

static String sized(const SubtitleLabel *p_label, const String &p_text) {
	return vformat("[font_size=%d]", p_label->get_display_font_size()) + p_text + "[/font_size]";
}

TEST_CASE("[SceneTree][SubtitleLabel] Follows the player, colors speakers, honors captions_enabled") {
	Window *root = SceneTree::get_singleton()->get_root().ptr();
	SubtitlePlayer *player = memnew(SubtitlePlayer);
	player->set_name("Player");
	player->set_subtitle(make_subtitle());
	SubtitleLabel *label = memnew(SubtitleLabel);
	label->set_follow_accessibility_settings(false);
	label->set_player(NodePath("../Player"));
	Node *scene = memnew(Node);
	scene->add_child(label); // Before the player: the path is resolved when the label is ready.
	scene->add_child(player);
	root->add_child(scene);
	CHECK(label->get_text().is_empty());

	player->play(0.5);
	CHECK(label->get_text() == sized(label, "Anna: Hello"));
	CHECK(label->is_visible());
	CHECK(label->get_display_font_size() == 32);

	TypedDictionary<StringName, Color> colors;
	colors[StringName("Anna")] = Color(1, 0, 0);
	label->set_speaker_colors(colors);
	CHECK(label->get_text() == sized(label, "[color=#ff0000ff]Anna: Hello[/color]"));
	label->set_show_speaker_names(false);
	CHECK(label->get_text() == sized(label, "[color=#ff0000ff]Hello[/color]"));
	label->set_show_speaker_names(true);

	player->seek(1.5); // Between the segments: nothing shown, the label hides.
	CHECK(label->get_text().is_empty());
	CHECK_FALSE(label->is_visible());

	player->seek(3.5); // A sound and a line: two lines in start order, the caption without speaker.
	CHECK(label->get_text() == sized(label, "[lb]Applause[rb]\nBen: Run!"));
	SubtitleServer *server = SubtitleServer::get_singleton();
	const bool captions = server->is_captions_enabled();
	server->set_captions_enabled(true);
	CHECK(label->get_text() == sized(label, "[lb]Applause[rb]\nBen: Run!"));
	server->set_captions_enabled(false); // Signal: the label updates at once.
	CHECK(label->get_text() == sized(label, "Ben: Run!"));
	server->set_captions_enabled(true);
	server->set_enabled(false);
	CHECK(label->get_text().is_empty());
	server->set_enabled(true);
	CHECK(label->get_text() == sized(label, "[lb]Applause[rb]\nBen: Run!"));

	label->set_font_size(20);
	CHECK(label->get_text() == "[font_size=20][lb]Applause[rb]\nBen: Run![/font_size]");

	player->stop();
	CHECK(label->get_text().is_empty());

	// The text is written by the label: not saved with the scene. It is translated in the subtitles domain only.
	CHECK(label->get_auto_translate_mode() == Node::AUTO_TRANSLATE_MODE_DISABLED);
	List<PropertyInfo> properties;
	label->get_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.name == "text" || property.name == "auto_translate_mode") {
			CHECK_FALSE((property.usage & PROPERTY_USAGE_STORAGE) != 0);
		}
	}

	memdelete(scene);
	server->set_captions_enabled(captions);
}

TEST_CASE("[SceneTree][SubtitleLabel] Follows AccessibilitySettings") {
	AccessibilitySettingsServer *settings = AccessibilitySettingsServer::get_singleton();
	REQUIRE(settings != nullptr);
	const Variant autosave = ProjectSettings::get_singleton()->get_setting("settings/general/autosave");
	ProjectSettings::get_singleton()->set_setting("settings/general/autosave", false);
	const bool enabled = settings->is_subtitles_enabled();
	const bool captions = settings->is_captions_enabled();
	const float scale = settings->get_subtitle_scale();
	settings->set_subtitles_enabled(true);
	settings->set_captions_enabled(true);
	settings->set_subtitle_scale(1.0);

	Window *root = SceneTree::get_singleton()->get_root().ptr();
	SubtitlePlayer *player = memnew(SubtitlePlayer);
	player->set_subtitle(make_subtitle());
	root->add_child(player);
	SubtitleLabel *label = memnew(SubtitleLabel);
	root->add_child(label);
	label->set_player(label->get_path_to(player));
	player->play(0.5);
	CHECK(label->get_text() == sized(label, "Anna: Hello"));

	settings->set_subtitle_scale(2.0); // Signal: the label updates at once.
	CHECK(label->get_display_font_size() == settings->scale_subtitle_font_size(32));
	CHECK(label->get_text() == sized(label, "Anna: Hello"));

	settings->set_subtitles_enabled(false);
	CHECK(label->get_text().is_empty());
	CHECK_FALSE(label->is_visible());
	label->set_follow_accessibility_settings(false);
	CHECK(label->get_text() == "[font_size=32]Anna: Hello[/font_size]");
	label->set_follow_accessibility_settings(true);
	settings->set_subtitles_enabled(true);
	CHECK(label->get_text() == sized(label, "Anna: Hello"));

	player->seek(3.5); // The player's captions setting drops the sound, speech stays.
	CHECK(label->get_text() == sized(label, "[lb]Applause[rb]\nBen: Run!"));
	settings->set_captions_enabled(false);
	CHECK(label->get_text() == sized(label, "Ben: Run!"));
	label->set_follow_accessibility_settings(false);
	CHECK(label->get_text() == "[font_size=32][lb]Applause[rb]\nBen: Run![/font_size]");
	label->set_follow_accessibility_settings(true);
	settings->set_captions_enabled(true);
	CHECK(label->get_text() == sized(label, "[lb]Applause[rb]\nBen: Run!"));

	memdelete(label);
	memdelete(player);
	settings->set_subtitles_enabled(enabled);
	settings->set_captions_enabled(captions);
	settings->set_subtitle_scale(scale);
	ProjectSettings::get_singleton()->set_setting("settings/general/autosave", autosave);
}

TEST_CASE("[SceneTree][SubtitleLabel] A locale change of the subtitles channel translates the active line") {
	TranslationServer *translation_server = TranslationServer::get_singleton();
	Ref<Translation> german;
	german.instantiate();
	german->set_locale("de");
	german->add_message("LINE_1", "Wo sind wir?");
	const Ref<TranslationDomain> domain = translation_server->get_or_add_domain("subtitles");
	domain->add_translation(german);
	const String locale = translation_server->get_locale();
	translation_server->set_locale("en");

	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->set_auto_translate(true);
	subtitle->add_segment(make_segment(0.0, 1.0, "LINE_1"));
	Window *root = SceneTree::get_singleton()->get_root().ptr();
	SubtitlePlayer *player = memnew(SubtitlePlayer);
	player->set_subtitle(subtitle);
	root->add_child(player);
	SubtitleLabel *label = memnew(SubtitleLabel);
	label->set_follow_accessibility_settings(false);
	root->add_child(label);
	label->set_player(label->get_path_to(player));
	player->play(0.5);
	CHECK(label->get_text() == sized(label, "LINE_1"));

	translation_server->set_locale_for("subtitles", "de"); // No segment change: the label follows the locale.
	CHECK(label->get_text() == sized(label, "Wo sind wir?"));
	translation_server->clear_locale_for("subtitles");
	CHECK(label->get_text() == sized(label, "LINE_1"));

	memdelete(label);
	memdelete(player);
	domain->remove_translation(german);
	translation_server->set_locale(locale);
}

TEST_CASE("[SceneTree][SubtitleLabel] Speaker names are translated in the subtitles domain with the context speaker") {
	TranslationServer *translation_server = TranslationServer::get_singleton();
	Ref<Translation> german;
	german.instantiate();
	german->set_locale("de");
	german->add_message("Anna", "Annchen", "speaker");
	const Ref<TranslationDomain> domain = translation_server->get_or_add_domain("subtitles");
	domain->add_translation(german);
	const String locale = translation_server->get_locale();
	translation_server->set_locale("en");

	Ref<Subtitle> subtitle;
	subtitle.instantiate();
	subtitle->add_segment(make_segment(0.0, 1.0, "Hello", "Anna"));
	Ref<SubtitleSegment> segment = subtitle->get_segment(0);
	SubtitleServer *server = SubtitleServer::get_singleton();
	CHECK(server->get_display_speaker(subtitle, segment) == "Anna"); // auto_translate is off.
	subtitle->set_auto_translate(true);
	CHECK(server->get_display_speaker(subtitle, segment) == "Anna"); // No English entry.

	Window *root = SceneTree::get_singleton()->get_root().ptr();
	SubtitlePlayer *player = memnew(SubtitlePlayer);
	player->set_subtitle(subtitle);
	root->add_child(player);
	SubtitleLabel *label = memnew(SubtitleLabel);
	label->set_follow_accessibility_settings(false);
	root->add_child(label);
	label->set_player(label->get_path_to(player));
	player->play(0.5);
	CHECK(label->get_text() == sized(label, "Anna: Hello"));

	translation_server->set_locale_for("subtitles", "de");
	CHECK(server->get_display_speaker(subtitle, segment) == "Annchen");
	CHECK(label->get_text() == sized(label, "Annchen: Hello"));
	translation_server->clear_locale_for("subtitles");

	memdelete(label);
	memdelete(player);
	domain->remove_translation(german);
	translation_server->set_locale(locale);
}

#ifdef TOOLS_ENABLED
TEST_CASE("[SceneTree][SubtitleLabel] The editor shows a preview line while nothing plays") {
	Engine *engine = Engine::get_singleton();
	engine->set_editor_hint(true);
	Window *root = SceneTree::get_singleton()->get_root().ptr();
	SubtitleLabel *label = memnew(SubtitleLabel);
	label->set_follow_accessibility_settings(false);
	root->add_child(label);
	CHECK(label->get_text() == sized(label, "Speaker: The subtitle line appears here."));
	CHECK(label->get_subtitle_text().is_empty()); // The preview is not a real line.
	CHECK(label->is_visible());

	label->set_preview_speaker("Anna");
	label->set_preview_text("Hello [there]");
	CHECK(label->get_text() == sized(label, "Anna: Hello [lb]there]"));
	TypedDictionary<StringName, Color> colors;
	colors[StringName("Anna")] = Color(1, 0, 0);
	label->set_speaker_colors(colors);
	label->set_show_speaker_names(false);
	CHECK(label->get_text() == sized(label, "[color=#ff0000ff]Hello [lb]there][/color]"));

	engine->set_editor_hint(false);
	label->refresh(); // At runtime there is no preview.
	CHECK(label->get_text().is_empty());
	CHECK_FALSE(label->is_visible());
	memdelete(label);
}
#endif // TOOLS_ENABLED

TEST_CASE("[SceneTree][SubtitleLabel] Clears the line when the player is freed, keeps one ready on reparent") {
	Window *root = SceneTree::get_singleton()->get_root().ptr();
	SubtitlePlayer *player = memnew(SubtitlePlayer);
	player->set_name("Player");
	player->set_subtitle(make_subtitle());
	SubtitleLabel *label = memnew(SubtitleLabel);
	label->set_follow_accessibility_settings(false);
	label->set_player(NodePath("../Player"));
	Node *scene = memnew(Node);
	scene->add_child(player);
	scene->add_child(label);
	root->add_child(scene);
	player->play(0.5);
	CHECK(label->get_text() == sized(label, "Anna: Hello"));

	// Reparent: no second ready (user `_ready()` scripts run once), the path is resolved again.
	SIGNAL_WATCH(label, "ready");
	scene->remove_child(label);
	scene->add_child(label);
	MessageQueue::get_singleton()->flush();
	SIGNAL_CHECK_FALSE("ready");
	SIGNAL_UNWATCH(label, "ready");
	player->seek(3.5);
	CHECK(label->get_text() == sized(label, "[lb]Applause[rb]\nBen: Run!"));

	// The player is freed mid-line: no segment signal, the label clears itself.
	memdelete(player);
	MessageQueue::get_singleton()->flush();
	CHECK(label->get_text().is_empty());
	CHECK_FALSE(label->is_visible());

	memdelete(scene);
}

} // namespace TestSubtitleLabel

#endif // ADVANCED_GUI_DISABLED
