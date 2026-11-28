/**************************************************************************/
/*  localization_catalog_editor_plugin.h                                  */
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

#include "core/string/localization_catalog.h"
#include "editor/export/editor_export_plugin.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"
#include "scene/gui/label.h"
#include "scene/resources/localized_text.h"

class ConfirmationDialog;
class Timer;
class Tree;

// The validation result and the missing keys per locale of a catalog, updated (debounced) when it changes.
class LocalizationCatalogIssues : public VBoxContainer {
	GDCLASS(LocalizationCatalogIssues, VBoxContainer);

	static constexpr int MAX_LISTED_KEYS = 100;

	Ref<LocalizationCatalog> catalog;
	Label *status = nullptr;
	Tree *missing_tree = nullptr;
	Timer *update_timer = nullptr;

	void _queue_update();
	void _update();

protected:
	void _notification(int p_what);

public:
	LocalizationCatalogIssues(const Ref<LocalizationCatalog> &p_catalog = Ref<LocalizationCatalog>());
};

// Missing keys per locale, the validation result and "Save POT..." above the catalog's properties (#346).
// Editing happens in the Localization Table dock, which opens with the catalog.
class EditorInspectorPluginLocalizationCatalog : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginLocalizationCatalog, EditorInspectorPlugin);

	void _save_pot(const String &p_path, const Ref<LocalizationCatalog> &p_catalog);
	static void _open_in_table(const Ref<LocalizationCatalog> &p_catalog);

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
};

// The text of a LocalizedText in the inspector, updated when it changes (#391).
class LocalizedTextPreview : public Label {
	GDCLASS(LocalizedTextPreview, Label);

	Ref<LocalizedText> localized_text;

	void _update();

protected:
	void _notification(int p_what);

public:
	LocalizedTextPreview(const Ref<LocalizedText> &p_localized_text = Ref<LocalizedText>());
};

// LocalizedText: a preview of the text and key suggestions from the loaded text catalogs (#391).
class EditorInspectorPluginLocalizedText : public EditorInspectorPlugin {
	GDCLASS(EditorInspectorPluginLocalizedText, EditorInspectorPlugin);

	static constexpr int MAX_SUGGESTED_KEYS = 1000;

public:
	virtual bool can_handle(Object *p_object) override;
	virtual void parse_begin(Object *p_object) override;
	virtual bool parse_property(Object *p_object, const Variant::Type p_type, const String &p_path, const PropertyHint p_hint, const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage, const bool p_wide = false) override;
};

// Writes the text catalogs of `internationalization/localization/catalog_manifests` as binary `.lcat` into the
// export, remapped from the text file (like FlowGraphExportPlugin).
class LocalizationCatalogExportPlugin : public EditorExportPlugin {
	GDCLASS(LocalizationCatalogExportPlugin, EditorExportPlugin);

	HashMap<String, StringName> catalog_classes; // Text catalog path -> catalog class, from the manifests.

protected:
	virtual void _export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) override;
	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override;

public:
	virtual String get_name() const override { return "LocalizationCatalog"; }
};

// "Use as Text/Resource Localization Catalog" for data files in the FileSystem dock.
class EditorContextMenuPluginLocalizationCatalog : public EditorContextMenuPlugin {
	GDCLASS(EditorContextMenuPluginLocalizationCatalog, EditorContextMenuPlugin);

	void _use_as_catalog(const Dictionary &p_data, const StringName &p_class);

public:
	using EditorContextMenuPlugin::get_options;
	virtual void get_options(const OptionsData &p_data) override;
};

class LocalizationCatalogEditorPlugin : public EditorPlugin {
	GDCLASS(LocalizationCatalogEditorPlugin, EditorPlugin);

	ConfirmationDialog *convert_dialog = nullptr;
	String convert_text; // The dialog text before the list of files.
	String loaded_manifests; // The catalog manifests setting the servers loaded last, as text.

	void _convert_project_translations();
	void _project_settings_changed();
	void _resource_saved(Object *p_resource);
	void _popup_convert_dialog();

protected:
	void _notification(int p_what);

public:
	// Adds the files to the first catalog manifest of the class (one is created if missing), switches CSV files
	// to "Keep File" and saves the project settings.
	static void add_catalog_files(const PackedStringArray &p_paths, const StringName &p_class);
	// The class of the catalog manifest that lists the text file `p_path`, or empty.
	static StringName get_manifest_catalog_class(const String &p_path);
	// The catalog of `p_path` as `p_class`, taking over the path from a resource cached as another type.
	static Ref<Resource> load_catalog(const String &p_path, const StringName &p_class);

	virtual String get_plugin_name() const override { return "LocalizationCatalog"; }

	LocalizationCatalogEditorPlugin();
};
