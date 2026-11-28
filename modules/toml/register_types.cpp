/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "resource_format_toml.h"
#include "toml.h"

#include "core/io/resource_format_text_data.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_text_serializer.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/toml_inspector_plugin.h"
#include "editor/toml_syntax_highlighter.h"
#include "editor/toml_tree_adapter.h"

static void _register_toml_syntax_highlighter() {
	Ref<EditorTOMLSyntaxHighlighter> highlighter;
	highlighter.instantiate();
	ScriptEditor::get_singleton()->register_syntax_highlighter(highlighter);
}

static void _register_toml_inspector_plugin() {
	Ref<EditorInspectorPluginTOML> plugin;
	plugin.instantiate();
	EditorInspector::add_inspector_plugin(plugin);
}
#endif // TOOLS_ENABLED

static Ref<ResourceFormatLoaderResourceTOML> resource_loader_toml;
static Ref<ResourceFormatSaverResourceTOML> resource_saver_toml;
static Ref<ResourceFormatLoaderTextData> resource_loader_text_data_toml;
static Ref<ResourceFormatSaverTextData> resource_saver_text_data_toml;

static Error _parse_toml_format(const String &p_text, const String &p_type_hint, const ResourceTextSerializer::Options &p_options, Ref<Resource> &r_resource, String &r_error) {
	return TOML::parse_resource(p_text, p_type_hint, r_resource, r_error);
}

void initialize_toml_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorNode::add_init_callback(&_register_toml_syntax_highlighter);
		EditorNode::add_init_callback(&_register_toml_inspector_plugin);
		return;
	}
#endif // TOOLS_ENABLED
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(TOML);
#ifdef TOOLS_ENABLED
	DataTreeAdapter::register_adapter("TOML", &TOMLTreeAdapter::create);
#endif // TOOLS_ENABLED

	resource_loader_toml.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_toml);
	resource_saver_toml.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_toml);

	// Plain TOML files (no type marker) as TOML resources, after the type-marked loaders above.
	resource_loader_text_data_toml.instantiate("TOML", Vector<String>{ "toml" });
	ResourceLoader::add_resource_format_loader(resource_loader_text_data_toml);
	resource_saver_text_data_toml.instantiate("TOML");
	ResourceSaver::add_resource_format_saver(resource_saver_text_data_toml);

	// ResourceTextSerializer.serialize(resource, FORMAT_TOML) and deserialize(text, FORMAT_TOML).
	ResourceTextSerializer::register_format(ResourceTextSerializer::FORMAT_TOML, &TOML::write_resource, &_parse_toml_format);
}

void uninitialize_toml_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	ResourceTextSerializer::unregister_format(ResourceTextSerializer::FORMAT_TOML);
	ResourceLoader::remove_resource_format_loader(resource_loader_text_data_toml);
	resource_loader_text_data_toml.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_text_data_toml);
	resource_saver_text_data_toml.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_toml);
	resource_loader_toml.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_toml);
	resource_saver_toml.unref();
}
