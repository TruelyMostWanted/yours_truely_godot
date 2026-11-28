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

#include "resource_format_yaml.h"
#include "yaml.h"

#include "core/io/resource_format_text_data.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_text_serializer.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/yaml_inspector_plugin.h"
#include "editor/yaml_syntax_highlighter.h"
#include "editor/yaml_tree_adapter.h"

static void _register_yaml_syntax_highlighter() {
	Ref<EditorYAMLSyntaxHighlighter> highlighter;
	highlighter.instantiate();
	ScriptEditor::get_singleton()->register_syntax_highlighter(highlighter);
}

static void _register_yaml_inspector_plugin() {
	Ref<EditorInspectorPluginYAML> plugin;
	plugin.instantiate();
	EditorInspector::add_inspector_plugin(plugin);
}
#endif // TOOLS_ENABLED

static Ref<ResourceFormatLoaderResourceYAML> resource_loader_yaml;
static Ref<ResourceFormatSaverResourceYAML> resource_saver_yaml;
static Ref<ResourceFormatLoaderTextData> resource_loader_text_data_yaml;
static Ref<ResourceFormatSaverTextData> resource_saver_text_data_yaml;
static Ref<ResourceFormatLoaderResourceYAML> resource_loader_yml;
static Ref<ResourceFormatSaverResourceYAML> resource_saver_yml;

void initialize_yaml_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorNode::add_init_callback(&_register_yaml_syntax_highlighter);
		EditorNode::add_init_callback(&_register_yaml_inspector_plugin);
		return;
	}
#endif // TOOLS_ENABLED
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(YAML);
#ifdef TOOLS_ENABLED
	DataTreeAdapter::register_adapter("YAML", &YAMLTreeAdapter::create);
#endif // TOOLS_ENABLED

	resource_loader_yaml.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_yaml);
	resource_saver_yaml.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_yaml);

	resource_loader_yml.instantiate();
	resource_loader_yml->set_extension("yml");
	ResourceLoader::add_resource_format_loader(resource_loader_yml);
	resource_saver_yml.instantiate();
	resource_saver_yml->set_extension("yml");
	ResourceSaver::add_resource_format_saver(resource_saver_yml);

	// Plain YAML files (no type marker) as YAML resources, after the type-marked loaders above.
	resource_loader_text_data_yaml.instantiate("YAML", Vector<String>{ "yaml", "yml" });
	ResourceLoader::add_resource_format_loader(resource_loader_text_data_yaml);
	resource_saver_text_data_yaml.instantiate("YAML");
	ResourceSaver::add_resource_format_saver(resource_saver_text_data_yaml);

	// ResourceTextSerializer.serialize(resource, FORMAT_YAML) and deserialize(text, FORMAT_YAML).
	ResourceTextSerializer::register_format(ResourceTextSerializer::FORMAT_YAML, &YAML::write_resource, &YAML::parse_format);
}

void uninitialize_yaml_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	ResourceTextSerializer::unregister_format(ResourceTextSerializer::FORMAT_YAML);
	ResourceLoader::remove_resource_format_loader(resource_loader_text_data_yaml);
	resource_loader_text_data_yaml.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_text_data_yaml);
	resource_saver_text_data_yaml.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_yaml);
	resource_loader_yaml.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_yaml);
	resource_saver_yaml.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_yml);
	resource_loader_yml.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_yml);
	resource_saver_yml.unref();
}
