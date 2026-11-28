/**************************************************************************/
/*  transition_template_editor_plugin.cpp                                 */
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

#include "transition_template_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_toaster.h"
#include "scene/gui/dialogs.h"
#include "scene/resources/packed_scene.h"

static const char *const BOOT_SPLASH_SCENE_SETTING = "application/boot_splash/scene";

void TransitionTemplateEditorPlugin::_create_splash() {
	_popup(TransitionScreen::TEMPLATE_SPLASH);
}

void TransitionTemplateEditorPlugin::_create_loading() {
	_popup(TransitionScreen::TEMPLATE_LOADING);
}

void TransitionTemplateEditorPlugin::_popup(TransitionScreen::Template p_template) {
	pending_template = p_template;
	file_dialog->set_title(p_template == TransitionScreen::TEMPLATE_SPLASH ? TTR("Create Splash Screen Scene") : TTR("Create Loading Screen Scene"));
	file_dialog->set_current_dir("res://");
	file_dialog->set_current_file(p_template == TransitionScreen::TEMPLATE_SPLASH ? "splash_screen.tscn" : "loading_screen.tscn");
	file_dialog->popup_file_dialog();
}

void TransitionTemplateEditorPlugin::_file_selected(const String &p_path) {
	const Ref<PackedScene> scene = TransitionScreen::create_template(pending_template);
	if (scene.is_null()) {
		EditorToaster::get_singleton()->popup_str(TTR("Could not create the transition screen scene."), EditorToaster::SEVERITY_ERROR);
		return;
	}
	const Error err = ResourceSaver::save(scene, p_path);
	if (err != OK) {
		EditorToaster::get_singleton()->popup_str(vformat(TTR("Could not save the scene '%s'."), p_path), EditorToaster::SEVERITY_ERROR);
		return;
	}
	EditorFileSystem::get_singleton()->update_file(p_path);
	EditorInterface::get_singleton()->open_scene_from_path(p_path);
	if (pending_template == TransitionScreen::TEMPLATE_SPLASH && String(ProjectSettings::get_singleton()->get_setting(BOOT_SPLASH_SCENE_SETTING, String())) != p_path) {
		created_path = p_path;
		splash_dialog->set_text(vformat(TTR("Use '%s' as the boot splash scene (Application > Boot Splash > Scene)?"), p_path));
		splash_dialog->popup_centered();
	}
}

void TransitionTemplateEditorPlugin::_use_as_boot_splash() {
	ProjectSettings::get_singleton()->set_setting(BOOT_SPLASH_SCENE_SETTING, created_path);
	ProjectSettings::get_singleton()->save();
}

void TransitionTemplateEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_tool_menu_item(TTR("Create Splash Screen Scene..."), callable_mp(this, &TransitionTemplateEditorPlugin::_create_splash));
			add_tool_menu_item(TTR("Create Loading Screen Scene..."), callable_mp(this, &TransitionTemplateEditorPlugin::_create_loading));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_tool_menu_item(TTR("Create Splash Screen Scene..."));
			remove_tool_menu_item(TTR("Create Loading Screen Scene..."));
		} break;
	}
}

TransitionTemplateEditorPlugin::TransitionTemplateEditorPlugin() {
	file_dialog = memnew(EditorFileDialog);
	file_dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
	file_dialog->set_access(EditorFileDialog::ACCESS_RESOURCES);
	file_dialog->add_filter("*.tscn", TTR("Scene"));
	file_dialog->connect("file_selected", callable_mp(this, &TransitionTemplateEditorPlugin::_file_selected));
	EditorNode::get_singleton()->get_gui_base()->add_child(file_dialog);

	splash_dialog = memnew(ConfirmationDialog);
	splash_dialog->set_title(TTR("Boot Splash Scene"));
	splash_dialog->connect("confirmed", callable_mp(this, &TransitionTemplateEditorPlugin::_use_as_boot_splash));
	EditorNode::get_singleton()->get_gui_base()->add_child(splash_dialog);
}
