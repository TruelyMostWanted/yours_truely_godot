/**************************************************************************/
/*  transition_template_editor_plugin.h                                   */
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

#include "editor/plugins/editor_plugin.h"
#include "scene/main/transition_screen.h"

class ConfirmationDialog;
class EditorFileDialog;

// Project > Tools > "Create Splash Screen Scene..." and "Create Loading Screen Scene...": saves a ready-made
// `TransitionScreen.create_template()` scene where the user chooses and opens it. A splash scene can be set as
// `application/boot_splash/scene` right away.
class TransitionTemplateEditorPlugin : public EditorPlugin {
	GDCLASS(TransitionTemplateEditorPlugin, EditorPlugin);

	EditorFileDialog *file_dialog = nullptr;
	ConfirmationDialog *splash_dialog = nullptr;
	TransitionScreen::Template pending_template = TransitionScreen::TEMPLATE_SPLASH;
	String created_path;

	void _create_splash();
	void _create_loading();
	void _popup(TransitionScreen::Template p_template);
	void _file_selected(const String &p_path);
	void _use_as_boot_splash();

protected:
	void _notification(int p_what);

public:
	virtual String get_plugin_name() const override { return "TransitionTemplates"; }

	TransitionTemplateEditorPlugin();
};
