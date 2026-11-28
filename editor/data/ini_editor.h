/**************************************************************************/
/*  ini_editor.h                                                          */
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

#include "core/io/ini_format.h"
#include "editor/docks/editor_dock.h"
#include "editor/plugins/editor_plugin.h"

class Button;
class Label;
class TextEditorBase;
class Tree;

// Edits an INI file (`.ini`, `.cfg`) in a bottom dock: the sections on the left, the keys of the selected
// section with typed values on the right (#424). Every edit is one EditorUndoRedoManager action that replaces
// `data` with a copy in the same order, and shows up right away in an open text editor of the same file.
class INIEditor : public EditorDock {
	GDCLASS(INIEditor, EditorDock);

	static inline INIEditor *singleton = nullptr;

	// Edits of the kept text, one line per item, so comments and formatting stay (#424 review).
	enum TextChange {
		TEXT_CHANGE_SET_VALUE,
		TEXT_CHANGE_RENAME_KEY,
		TEXT_CHANGE_REMOVE_KEYS,
		TEXT_CHANGE_ADD_KEY,
		TEXT_CHANGE_ADD_SECTION,
		TEXT_CHANGE_RENAME_SECTION,
		TEXT_CHANGE_REMOVE_SECTION,
	};

	Ref<INI> ini;
	String section; // Selected section; "" holds the keys before the first section header.
	String previous_section; // Selected before the last edit; selected again after its undo.
	uint32_t reloaded_version = UINT32_MAX; // Version of the text editor's text right after the dock reloaded it.
	bool update_queued = false;
	String rendered_key; // _get_render_key() when the dock rendered.
	bool edit_section = false; // Starts editing the selected section's name after the next render.
	String edit_key; // Starts editing this key's name after the next render.

	Label *file_label = nullptr;
	Button *add_section_button = nullptr;
	Button *save_button = nullptr;
	Button *remove_section_button = nullptr;
	Button *add_key_button = nullptr;
	Button *remove_key_button = nullptr;
	Tree *section_tree = nullptr;
	Tree *key_tree = nullptr;
	Label *empty_label = nullptr;
	Label *status_label = nullptr;

	TextEditorBase *_get_text_editor() const;
	bool _can_edit();
	String _get_text() const;
	String _get_render_key() const;
	// p_key: the edited key; p_to: the new key or section name; p_value: the value, or the keys to remove.
	void _edit_text(const String &p_action, TextChange p_change, const String &p_key = String(), const String &p_to = String(), const Variant &p_value = Variant());

	void _ini_changed();
	void _update();
	void _render();
	void _edit_added();
	void _section_selected();
	void _section_edited();
	void _key_edited();
	void _add_section();
	void _remove_section();
	void _add_key();
	void _remove_keys();
	void _save();
	void _resource_saved(Object *p_resource);
	void _tree_gui_input(const Ref<InputEvent> &p_event, bool p_sections);
	void _file_removed(const String &p_file);

protected:
	void _notification(int p_what);

public:
	static INIEditor *get_singleton() { return singleton; }
	// Opened with the file (double-click, edit_resource()): an INI resource with a recognized path (.ini, .cfg).
	static bool can_edit(const Object *p_object);

	void edit(const Ref<INI> &p_ini);

	INIEditor();
	~INIEditor();
};

class INIEditorPlugin : public EditorPlugin {
	GDCLASS(INIEditorPlugin, EditorPlugin);

	INIEditor *ini_editor = nullptr;

public:
	virtual String get_plugin_name() const override { return "INIEditor"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	// The dock stays open while other objects are edited; it closes with its close button.
	virtual bool can_auto_hide() const override { return false; }
	virtual void make_visible(bool p_visible) override;

	INIEditorPlugin();
};
