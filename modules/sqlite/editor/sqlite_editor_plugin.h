/**************************************************************************/
/*  sqlite_editor_plugin.h                                                */
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

#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"

class Button;
class DatabaseEditor;
class Label;
class Tree;
class TreeItem;

// The tables of a database file in the Inspector: name, column count and row count (the quick path).
// "Open in Database" and the edit button of a table open the Database dock (#437).
class SQLiteTableOverview : public VBoxContainer {
	GDCLASS(SQLiteTableOverview, VBoxContainer);

	String path;
	Label *summary_label = nullptr;
	Button *refresh_button = nullptr;
	Button *open_button = nullptr;
	Tree *tree = nullptr;

	void _update();
	void _count_rows();
	void _open_in_dock(const String &p_table);
	void _edit_table(TreeItem *p_item, int p_column, int p_id, MouseButton p_button);

protected:
	void _notification(int p_what);

public:
	SQLiteTableOverview(const String &p_path);
};

class EditorInspectorPluginSQLite : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginSQLite, EditorInspectorPlugin);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};

// Opens database files in the Database dock; adds the table overview to the Inspector.
class SQLiteEditorPlugin : public EditorPlugin {
	GDCLASS(SQLiteEditorPlugin, EditorPlugin);

	DatabaseEditor *database_editor = nullptr;

public:
	virtual String get_plugin_name() const override { return "SQLite"; }
	virtual void edit(Object *p_object) override;
	virtual bool handles(Object *p_object) const override;
	virtual void make_visible(bool p_visible) override;
	virtual String get_unsaved_status(const String &p_for_scene) const override;
	virtual void save_external_data() override;

	SQLiteEditorPlugin();
};
