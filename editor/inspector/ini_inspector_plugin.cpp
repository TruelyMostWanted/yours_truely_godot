/**************************************************************************/
/*  ini_inspector_plugin.cpp                                              */
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

#include "ini_inspector_plugin.h"

#include "core/io/ini_format.h"
#include "core/object/callable_mp.h"
#include "editor/data/ini_editor.h"

String INIInspectorView::get_summary_text(const Variant &p_data) const {
	const Dictionary sections = p_data;
	int key_count = 0;
	for (const KeyValue<Variant, Variant> &kv : sections) {
		key_count += Dictionary(kv.value).size();
	}
	return vformat(TTRN("%d section", "%d sections", sections.size()), sections.size()) + ", " + vformat(TTRN("%d key", "%d keys", key_count), key_count);
}

void INIInspectorView::_open_in_ini_dock() {
	INIEditor *ini_editor = INIEditor::get_singleton();
	if (ini_editor) {
		ini_editor->edit(Ref<INI>(resource));
		ini_editor->make_visible();
	}
}

INIInspectorView::INIInspectorView(const Ref<Resource> &p_resource) :
		DataFileInspectorView(p_resource) {
	_add_dock_button(TTR("Open in INI Dock"), TTR("Edit the sections and keys in the INI dock."), callable_mp(this, &INIInspectorView::_open_in_ini_dock));
}

DataFileInspectorView *EditorInspectorPluginINI::create_view(const Ref<Resource> &p_resource) const {
	return memnew(INIInspectorView(p_resource));
}

bool EditorInspectorPluginINI::can_handle(Object *p_object) {
	return Object::cast_to<INI>(p_object) != nullptr;
}
