/**************************************************************************/
/*  localization_catalog_editor_plugin.cpp                                */
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

#include "localization_catalog_editor_plugin.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/files_manifest.h"
#include "core/io/resource_loader.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/string/localization_catalog.h"
#include "core/string/localization_server.h"
#include "core/string/resource_localization_server.h"
#include "core/string/text_localization_server.h"
#include "core/string/translation.h"
#include "core/variant/variant_parser.h"
#include "editor/data/csv_table_editor.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/file_system/editor_paths.h"
#include "editor/gui/editor_file_dialog.h"
#include "editor/gui/editor_toaster.h"
#include "editor/inspector/editor_properties.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/tree.h"
#include "scene/main/timer.h"
#include "scene/resources/localized_text.h"

static const char *SETTING_TRANSLATIONS = "internationalization/locale/translations";
static const char *SETTING_TRANSLATION_REMAPS = "internationalization/locale/translation_remaps";

// Text formats a catalog can be read from (not `.lcat`).
static bool _is_text_catalog_path(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	return extension != LocalizationCatalog::BINARY_EXTENSION && LocalizationCatalog::get_file_extensions().has(extension);
}

// The catalog class of a manifest: its type hint, TextLocalizationCatalog without one.
static StringName _get_manifest_class(const Ref<FilesManifest> &p_manifest) {
	return p_manifest->get_type_hint() == StringName() ? StringName("TextLocalizationCatalog") : p_manifest->get_type_hint();
}

// A file imported by an importer (a CSV imported as Translation), not kept as it is.
static bool _is_imported(const String &p_path) {
	Ref<ConfigFile> import_config;
	import_config.instantiate();
	if (import_config->load(p_path + ".import") != OK) {
		return false;
	}
	const String importer = import_config->get_value("remap", "importer", String());
	return importer != "keep" && importer != "skip";
}

// Switches a CSV to "Keep File" like the Import dock: a reimport with "keep" returns before writing `.import`
// (EditorFileSystem::_reimport_file()), so it is written here. Removes the files the CSV importer had written
// (`<name>.<locale>.translation`) and returns them.
static PackedStringArray _keep_csv_file(const String &p_path) {
	Ref<ConfigFile> import_config;
	import_config.instantiate();
	if (import_config->load(p_path + ".import") == OK && String(import_config->get_value("remap", "importer", String())) == "keep") {
		return PackedStringArray();
	}
	const PackedStringArray dest_files = import_config->get_value("deps", "dest_files", PackedStringArray());
	for (const String &dest_file : dest_files) {
		DirAccess::remove_absolute(dest_file);
	}
	import_config->clear();
	import_config->set_value("remap", "importer", "keep");
	import_config->save(p_path + ".import");
	return dest_files;
}

/////////////////////////////////////////////////////////////////////////////
// Inspector.

void LocalizationCatalogIssues::_queue_update() {
	if (update_timer->is_inside_tree()) {
		update_timer->start();
	}
}

void LocalizationCatalogIssues::_update() {
	// Missing translations are gaps, not errors (#346): listed per locale below, the fallback locales fill them.
	const Ref<DataValidation> result = catalog->validate();
	int issue_count = 0;
	String first_issue;
	for (int i = 0; i < result->get_issue_count(); i++) {
		if (result->get_issue_kind(i) != DataValidation::ISSUE_KIND_EMPTY_REQUIRED) {
			first_issue = issue_count == 0 ? result->get_issue_message(i) : first_issue;
			issue_count++;
		}
	}
	status->set_text(issue_count == 0 ? TTR("No issues.") : vformat(TTR("%d issue(s). First: %s"), issue_count, first_issue));

	// Missing keys per locale, collapsed; the Localization Table dock shows them in place.
	missing_tree->clear();
	TreeItem *root = missing_tree->create_item();
	const int key_count = catalog->get_key_count();
	for (const String &locale : catalog->get_locales()) {
		const int missing = key_count - catalog->get_translated_count(locale);
		TreeItem *locale_item = missing_tree->create_item(root);
		locale_item->set_text(0, vformat(TTR("%s: %d missing"), locale, missing));
		locale_item->set_collapsed(true);
		if (missing == 0) {
			continue;
		}
		const PackedInt32Array rows = catalog->get_missing_rows(locale);
		for (int i = 0; i < rows.size() && i < MAX_LISTED_KEYS; i++) {
			const String key = catalog->get_key(rows[i]);
			const String context = catalog->get_key_context(rows[i]);
			missing_tree->create_item(locale_item)->set_text(0, context.is_empty() ? key : vformat("%s @ %s", key, context));
		}
		if (rows.size() > MAX_LISTED_KEYS) {
			missing_tree->create_item(locale_item)->set_text(0, vformat(TTR("... and %d more"), rows.size() - MAX_LISTED_KEYS));
		}
	}
}

void LocalizationCatalogIssues::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE && catalog.is_valid()) {
		_update();
	}
}

LocalizationCatalogIssues::LocalizationCatalogIssues(const Ref<LocalizationCatalog> &p_catalog) {
	catalog = p_catalog;
	ERR_FAIL_COND(catalog.is_null());

	status = memnew(Label);
	status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(status);

	missing_tree = memnew(Tree);
	missing_tree->set_hide_root(true);
	missing_tree->set_custom_minimum_size(Size2(0, 120 * EDSCALE));
	missing_tree->set_tooltip_text(TTR("Missing translations per locale. Edit them in the Localization Table dock."));
	add_child(missing_tree);

	// Large catalogs: validated once after a burst of edits, like the Localization Table dock.
	update_timer = memnew(Timer);
	update_timer->set_one_shot(true);
	update_timer->set_wait_time(0.5);
	update_timer->connect("timeout", callable_mp(this, &LocalizationCatalogIssues::_update));
	add_child(update_timer);
	catalog->connect_changed(callable_mp(this, &LocalizationCatalogIssues::_queue_update));
}

bool EditorInspectorPluginLocalizationCatalog::can_handle(Object *p_object) {
	return Object::cast_to<LocalizationCatalog>(p_object) != nullptr;
}

void EditorInspectorPluginLocalizationCatalog::_save_pot(const String &p_path, const Ref<LocalizationCatalog> &p_catalog) {
	const Ref<TextLocalizationCatalog> catalog = p_catalog;
	ERR_FAIL_COND(catalog.is_null());
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	ERR_FAIL_COND_MSG(file.is_null(), vformat("Cannot write \"%s\".", p_path));
	file->store_string(catalog->to_po());
	file.unref();
	EditorFileSystem::get_singleton()->update_file(p_path); // Listed in the FileSystem dock right away.
}

void EditorInspectorPluginLocalizationCatalog::_open_in_table(const Ref<LocalizationCatalog> &p_catalog) {
	CSVTableEditor *table = CSVTableEditor::get_singleton();
	ERR_FAIL_NULL(table);
	table->edit(p_catalog);
	table->make_visible();
}

void EditorInspectorPluginLocalizationCatalog::parse_begin(Object *p_object) {
	const Ref<LocalizationCatalog> catalog(Object::cast_to<LocalizationCatalog>(p_object));
	VBoxContainer *box = memnew(VBoxContainer);

	Button *table_button = memnew(Button(TTR("Open in Localization Table")));
	table_button->set_tooltip_text(TTR("Edit the keys, locales and notes in the Localization Table dock."));
	table_button->connect(SceneStringName(pressed), callable_mp_static(&EditorInspectorPluginLocalizationCatalog::_open_in_table).bind(catalog));
	box->add_child(table_button);

	box->add_child(memnew(LocalizationCatalogIssues(catalog))); // Follows edits in the Localization Table dock.

	if (Object::cast_to<TextLocalizationCatalog>(p_object)) {
		EditorFileDialog *dialog = memnew(EditorFileDialog);
		dialog->set_file_mode(EditorFileDialog::FILE_MODE_SAVE_FILE);
		dialog->add_filter("*.pot", TTR("gettext Template"));
		dialog->connect("file_selected", callable_mp(this, &EditorInspectorPluginLocalizationCatalog::_save_pot).bind(catalog));
		box->add_child(dialog);
		Button *pot_button = memnew(Button(TTR("Save POT...")));
		pot_button->set_tooltip_text(TTR("Writes a gettext template of the keys, contexts, plural keys and translator notes."));
		pot_button->connect(SceneStringName(pressed), callable_mp((FileDialog *)dialog, &FileDialog::popup_file_dialog));
		box->add_child(pot_button);
	}
	add_custom_control(box);
}

/////////////////////////////////////////////////////////////////////////////
// LocalizedText inspector.

void LocalizedTextPreview::_update() {
	set_text(localized_text.is_valid() ? vformat(TTR("Preview: %s"), localized_text->get_text()) : String());
}

void LocalizedTextPreview::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		_update();
	}
}

LocalizedTextPreview::LocalizedTextPreview(const Ref<LocalizedText> &p_localized_text) {
	localized_text = p_localized_text;
	set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	set_tooltip_text(TTR("The text in the current editor locale, with the values TextInsertionServer has in the editor."));
	if (localized_text.is_valid()) {
		localized_text->connect_changed(callable_mp(this, &LocalizedTextPreview::_update));
	}
}

bool EditorInspectorPluginLocalizedText::can_handle(Object *p_object) {
	return Object::cast_to<LocalizedText>(p_object) != nullptr;
}

void EditorInspectorPluginLocalizedText::parse_begin(Object *p_object) {
	add_custom_control(memnew(LocalizedTextPreview(Ref<LocalizedText>(Object::cast_to<LocalizedText>(p_object)))));
}

bool EditorInspectorPluginLocalizedText::parse_property(Object *p_object, const Variant::Type p_type, const String &p_path, const PropertyHint p_hint, const String &p_hint_text, const BitField<PropertyUsageFlags> p_usage, const bool p_wide) {
	const TextLocalizationServer *server = TextLocalizationServer::get_singleton();
	if (p_path != "key" || !server) {
		return false;
	}
	// Suggestions from the loaded catalogs; any other key can still be typed (#391).
	HashSet<String> seen;
	Vector<String> keys;
	const TypedArray<LocalizationCatalog> catalogs = server->get_catalogs();
	for (int i = 0; i < catalogs.size() && keys.size() < MAX_SUGGESTED_KEYS; i++) {
		const Ref<LocalizationCatalog> catalog = catalogs[i];
		for (int row = 0; catalog.is_valid() && row < catalog->get_key_count() && keys.size() < MAX_SUGGESTED_KEYS; row++) {
			const String key = catalog->get_key(row);
			if (!seen.has(key)) {
				seen.insert(key);
				keys.push_back(key);
			}
		}
	}
	keys.sort();
	EditorPropertyTextEnum *editor = memnew(EditorPropertyTextEnum);
	editor->setup(keys, Vector<String>(), true, true);
	add_property_editor(p_path, editor);
	return true;
}

/////////////////////////////////////////////////////////////////////////////
// Export.

void LocalizationCatalogExportPlugin::_export_begin(const HashSet<String> &p_features, bool p_debug, const String &p_path, int p_flags) {
	catalog_classes.clear();
	const bool convert = GLOBAL_GET(LocalizationServer::SETTING_EXPORT_CONVERT_TEXT_TO_BINARY);
	const Array manifests = GLOBAL_GET(LocalizationServer::SETTING_CATALOG_MANIFESTS);
	for (const Variant &value : manifests) {
		const Ref<FilesManifest> manifest = value;
		if (manifest.is_null()) {
			continue;
		}
		const StringName catalog_class = _get_manifest_class(manifest);
		if (!ClassDB::is_parent_class(catalog_class, "LocalizationCatalog")) {
			continue;
		}
		for (const String &path : LocalizationServer::get_catalog_files(manifest)->get_paths()) {
			// Without conversion too for a CSV still imported as Translation: only its `.translation` files would be
			// exported, and the catalog would be missing at runtime (#403).
			if (_is_text_catalog_path(path) && (convert || _is_imported(path))) {
				catalog_classes[path] = catalog_class;
			}
		}
	}
}

void LocalizationCatalogExportPlugin::_export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) {
	const StringName *catalog_class = catalog_classes.getptr(p_path);
	if (!catalog_class) {
		return;
	}
	const Ref<LocalizationCatalog> catalog(Object::cast_to<LocalizationCatalog>(ClassDB::instantiate(*catalog_class)));
	ERR_FAIL_COND(catalog.is_null());
	const String temp_path = EditorPaths::get_singleton()->get_temp_dir().path_join("localization_catalog_export.lcat");
	ERR_FAIL_COND_MSG(catalog->load_from_file(p_path) != OK || catalog->save_to_file(temp_path) != OK, vformat("Cannot convert the localization catalog \"%s\" to binary; it is exported as text.", p_path));
	// Outside the project folders, so the folder scan of a manifest does not list the catalog a second time.
	add_file("res://.godot/exported/localization/" + p_path.md5_text() + ".lcat", FileAccess::get_file_as_bytes(temp_path), true);
}

/////////////////////////////////////////////////////////////////////////////
// FileSystem context menu.

void EditorContextMenuPluginLocalizationCatalog::_use_as_catalog(const Dictionary &p_data, const StringName &p_class) {
	const PackedStringArray selected = p_data.get("selected_files", PackedStringArray());
	LocalizationCatalogEditorPlugin::add_catalog_files(selected, p_class);
	if (selected.size() != 1 || LocalizationCatalogEditorPlugin::get_manifest_catalog_class(selected[0]) != p_class) {
		return; // Several files, or refused.
	}
	const Ref<Resource> catalog = LocalizationCatalogEditorPlugin::load_catalog(selected[0], p_class);
	if (catalog.is_valid()) {
		EditorNode::get_singleton()->edit_resource(catalog);
	}
}

void EditorContextMenuPluginLocalizationCatalog::get_options(const OptionsData &p_data) {
	EditorContextMenuPlugin::get_options(p_data);
	const PackedStringArray selected = p_data.get("selected_files", PackedStringArray());
	if (selected.is_empty()) {
		return;
	}
	for (const String &path : selected) {
		if (!_is_text_catalog_path(path)) {
			return;
		}
	}
	const Ref<Texture2D> icon = EditorNode::get_singleton()->get_gui_base()->get_editor_theme_icon(SNAME("LocalizationCatalog"));
	add_context_menu_item(TTR("Use as Text Localization Catalog"), callable_mp(this, &EditorContextMenuPluginLocalizationCatalog::_use_as_catalog).bind(StringName("TextLocalizationCatalog")), icon);
	add_context_menu_item(TTR("Use as Resource Localization Catalog"), callable_mp(this, &EditorContextMenuPluginLocalizationCatalog::_use_as_catalog).bind(StringName("ResourceLocalizationCatalog")), icon);
}

/////////////////////////////////////////////////////////////////////////////
// Plugin.

StringName LocalizationCatalogEditorPlugin::get_manifest_catalog_class(const String &p_path) {
	if (!_is_text_catalog_path(p_path)) {
		return StringName(); // `.lcat` names its class itself.
	}
	const Array manifests = GLOBAL_GET(LocalizationServer::SETTING_CATALOG_MANIFESTS);
	for (const Variant &value : manifests) {
		const Ref<FilesManifest> manifest = value;
		if (manifest.is_valid() && ClassDB::is_parent_class(_get_manifest_class(manifest), "LocalizationCatalog") && LocalizationServer::get_catalog_files(manifest)->get_paths().has(p_path)) {
			return _get_manifest_class(manifest);
		}
	}
	return StringName();
}

Ref<Resource> LocalizationCatalogEditorPlugin::load_catalog(const String &p_path, const StringName &p_class) {
	const Ref<Resource> cached = ResourceCache::get_ref(p_path);
	if (cached.is_valid() && cached->is_class(p_class)) {
		return cached;
	}
	// Not from the cache: the path may be cached as another type (JSON, CSV, TextFile).
	const Ref<Resource> catalog = ResourceLoader::load(p_path, p_class, ResourceFormatLoader::CACHE_MODE_IGNORE);
	ERR_FAIL_COND_V_MSG(catalog.is_null(), Ref<Resource>(), vformat("\"%s\" is no %s.", p_path, p_class));
	catalog->set_path(p_path, true);
	return catalog;
}

void LocalizationCatalogEditorPlugin::add_catalog_files(const PackedStringArray &p_paths, const StringName &p_class) {
	Array manifests = GLOBAL_GET(LocalizationServer::SETTING_CATALOG_MANIFESTS);
	manifests = manifests.duplicate();
	Ref<FilesManifest> target;
	PackedStringArray in_other_manifests; // Paths also listed by a manifest of the other class.
	for (const Variant &value : manifests) {
		const Ref<FilesManifest> manifest = value;
		if (manifest.is_null()) {
			continue;
		}
		if (_get_manifest_class(manifest) == p_class) {
			if (target.is_null()) {
				target = manifest;
			}
			continue;
		}
		const PackedStringArray listed = LocalizationServer::get_catalog_files(manifest)->get_paths();
		for (const String &path : p_paths) {
			if (listed.has(path) && !in_other_manifests.has(path)) {
				in_other_manifests.push_back(path);
			}
		}
	}
	if (target.is_null()) {
		target.instantiate();
		target->set_type_hint(p_class);
		manifests.push_back(target);
	}
	const PackedStringArray listed = LocalizationServer::get_catalog_files(target)->get_paths();
	PackedStringArray files = target->get_file_paths();
	PackedStringArray csv_files;
	PackedStringArray removed_translations;
	for (const String &path : p_paths) {
		if (in_other_manifests.has(path)) {
			continue; // Exported with one class only (#346): refused, see the warning below.
		}
		if (!listed.has(path) && !files.has(path)) {
			files.push_back(path);
		}
		// A CSV imported as Translation is no catalog for the loaders: "Keep File" keeps the text, which is loaded
		// as a catalog and converted to `.lcat` on export.
		if (path.get_extension().to_lower() == "csv") {
			csv_files.push_back(path);
			removed_translations.append_array(_keep_csv_file(path));
		}
	}
	target->set_file_paths(files);
	ProjectSettings::get_singleton()->set_setting(LocalizationServer::SETTING_CATALOG_MANIFESTS, manifests);
	if (!removed_translations.is_empty()) {
		// The removed `.translation` files must not stay in the project's translations.
		PackedStringArray translations = GLOBAL_GET(SETTING_TRANSLATIONS);
		for (const String &removed : removed_translations) {
			translations.erase(removed);
		}
		ProjectSettings::get_singleton()->set_setting(SETTING_TRANSLATIONS, translations);
	}
	ProjectSettings::get_singleton()->save();
	if (!csv_files.is_empty()) {
		EditorFileSystem::get_singleton()->reimport_files(csv_files);
		EditorFileSystem::get_singleton()->scan_changes();
	}
	if (!in_other_manifests.is_empty()) {
		EditorToaster::get_singleton()->popup_str(vformat(TTR("Not added: listed in a catalog manifest of the other class. Remove it there first: %s"), String(", ").join(in_other_manifests)), EditorToaster::SEVERITY_WARNING);
	}
}

void LocalizationCatalogEditorPlugin::_project_settings_changed() {
	// The servers load the manifests at startup: a changed list (marked or converted catalogs, the Project Settings)
	// is loaded again, so it works without a restart (#403).
	String manifests;
	VariantWriter::write_to_string(GLOBAL_GET(LocalizationServer::SETTING_CATALOG_MANIFESTS), manifests);
	if (manifests == loaded_manifests) {
		return;
	}
	loaded_manifests = manifests;
	if (TextLocalizationServer::get_singleton()) {
		TextLocalizationServer::get_singleton()->load_project_catalogs();
	}
	if (ResourceLocalizationServer::get_singleton()) {
		ResourceLocalizationServer::get_singleton()->load_project_catalogs();
	}
}

void LocalizationCatalogEditorPlugin::_resource_saved(Object *p_resource) {
	// A catalog saved as a text file outside of the manifests (a new one, "Save As") becomes a catalog of the
	// project: without a manifest the file would open as a data file and the CSV importer would take it (#346).
	const LocalizationCatalog *catalog = Object::cast_to<LocalizationCatalog>(p_resource);
	if (!catalog || !_is_text_catalog_path(catalog->get_path()) || get_manifest_catalog_class(catalog->get_path()) != StringName()) {
		return;
	}
	const StringName catalog_class = Object::cast_to<TextLocalizationCatalog>(p_resource) ? StringName("TextLocalizationCatalog") : StringName("ResourceLocalizationCatalog");
	callable_mp_static(&LocalizationCatalogEditorPlugin::add_catalog_files).call_deferred(PackedStringArray({ catalog->get_path() }), catalog_class);
}

void LocalizationCatalogEditorPlugin::_convert_project_translations() {
	const PackedStringArray translations = GLOBAL_GET(SETTING_TRANSLATIONS);
	const Dictionary remaps = GLOBAL_GET(SETTING_TRANSLATION_REMAPS);
	DirAccess::make_dir_recursive_absolute("res://localization");

	PackedStringArray text_files;
	PackedStringArray kept;
	const String texts_path = "res://localization/translations.csv";
	Ref<TextLocalizationCatalog> texts;
	texts.instantiate();
	if (FileAccess::exists(texts_path)) {
		texts->load_from_file(texts_path); // Converted before: add to it.
	}
	const int previous_keys = texts->get_key_count();
	for (const String &path : translations) {
		// The CSV importer writes `<name>.<locale>.translation` next to `<name>.csv`: the CSV itself becomes the
		// catalog, with its contexts, plural forms and notes.
		const String csv_path = path.get_basename().get_basename() + ".csv";
		if (path.get_extension().to_lower() == "translation" && FileAccess::exists(csv_path)) {
			if (!text_files.has(csv_path)) {
				text_files.push_back(csv_path);
			}
			continue;
		}
		const Ref<Translation> translation = ResourceLoader::load(path, "Translation");
		if (translation.is_valid()) {
			texts->add_translation(translation);
		} else {
			kept.push_back(path);
		}
	}
	const int csv_count = text_files.size();
	if (texts->get_key_count() > 0 && texts->save_to_file(texts_path) == OK) {
		text_files.push_back(texts_path);
	}
	if (!text_files.is_empty()) {
		add_catalog_files(text_files, "TextLocalizationCatalog");
	}

	Dictionary kept_remaps = remaps;
	if (!remaps.is_empty()) {
		const String resources_path = "res://localization/resource_remaps.csv";
		Ref<ResourceLocalizationCatalog> resources;
		resources.instantiate();
		if (FileAccess::exists(resources_path)) {
			resources->load_from_file(resources_path); // Converted before: add to it.
		}
		resources->add_translation_remaps(remaps);
		if (resources->save_to_file(resources_path) == OK) {
			add_catalog_files(PackedStringArray({ resources_path }), "ResourceLocalizationCatalog");
			kept_remaps = Dictionary();
		}
	}

	ProjectSettings::get_singleton()->set_setting(SETTING_TRANSLATIONS, kept);
	ProjectSettings::get_singleton()->set_setting(SETTING_TRANSLATION_REMAPS, kept_remaps);
	ProjectSettings::get_singleton()->save();
	EditorFileSystem::get_singleton()->scan_changes();
	EditorToaster::get_singleton()->popup_str(vformat(TTR("Converted %d CSV file(s), %d other translated key(s) and %d remapped resource(s) to localization catalogs. %d translation(s) could not be converted."), csv_count, texts->get_key_count() - previous_keys, remaps.size() - kept_remaps.size(), kept.size()));
}

void LocalizationCatalogEditorPlugin::_popup_convert_dialog() {
	// The files the conversion changes, so nothing happens unseen (#391).
	const PackedStringArray translations = GLOBAL_GET(SETTING_TRANSLATIONS);
	const Dictionary remaps = GLOBAL_GET(SETTING_TRANSLATION_REMAPS);
	PackedStringArray lines;
	for (const String &path : translations) {
		const String csv_path = path.get_basename().get_basename() + ".csv";
		const String line = path.get_extension().to_lower() == "translation" && FileAccess::exists(csv_path) ? vformat(TTR("%s: Keep File, text catalog"), csv_path) : vformat(TTR("%s: into res://localization/translations.csv"), path);
		if (!lines.has(line)) {
			lines.push_back(line);
		}
	}
	if (!remaps.is_empty()) {
		lines.push_back(vformat(TTR("%d translation remap(s): into res://localization/resource_remaps.csv"), remaps.size()));
	}
	constexpr int MAX_LINES = 20;
	String files = lines.is_empty() ? TTR("Nothing to convert.") : String("\n").join(lines.slice(0, MAX_LINES));
	if (lines.size() > MAX_LINES) {
		files += "\n" + vformat(TTR("... and %d more"), lines.size() - MAX_LINES);
	}
	convert_dialog->set_text(convert_text + "\n\n" + files);
	convert_dialog->get_ok_button()->set_disabled(lines.is_empty());
	convert_dialog->popup_centered();
}

void LocalizationCatalogEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			VariantWriter::write_to_string(GLOBAL_GET(LocalizationServer::SETTING_CATALOG_MANIFESTS), loaded_manifests);
			ProjectSettings::get_singleton()->connect("settings_changed", callable_mp(this, &LocalizationCatalogEditorPlugin::_project_settings_changed));
			EditorNode::get_singleton()->connect("resource_saved", callable_mp(this, &LocalizationCatalogEditorPlugin::_resource_saved));
			add_tool_menu_item(TTR("Convert Project Translations to Catalogs..."), callable_mp(this, &LocalizationCatalogEditorPlugin::_popup_convert_dialog));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			ProjectSettings::get_singleton()->disconnect("settings_changed", callable_mp(this, &LocalizationCatalogEditorPlugin::_project_settings_changed));
			EditorNode::get_singleton()->disconnect("resource_saved", callable_mp(this, &LocalizationCatalogEditorPlugin::_resource_saved));
			remove_tool_menu_item(TTR("Convert Project Translations to Catalogs..."));
		} break;
	}
}

LocalizationCatalogEditorPlugin::LocalizationCatalogEditorPlugin() {
	Ref<EditorInspectorPluginLocalizationCatalog> inspector_plugin;
	inspector_plugin.instantiate();
	add_inspector_plugin(inspector_plugin);
	Ref<EditorInspectorPluginLocalizedText> localized_text_plugin;
	localized_text_plugin.instantiate();
	add_inspector_plugin(localized_text_plugin);

	Ref<LocalizationCatalogExportPlugin> export_plugin;
	export_plugin.instantiate();
	add_export_plugin(export_plugin);

	Ref<EditorContextMenuPluginLocalizationCatalog> context_menu_plugin;
	context_menu_plugin.instantiate();
	add_context_menu_plugin(EditorContextMenuPlugin::CONTEXT_SLOT_FILESYSTEM, context_menu_plugin);

	convert_dialog = memnew(ConfirmationDialog);
	convert_dialog->set_title(TTR("Convert Project Translations"));
	convert_text = TTR("Moves the project's translations to localization catalogs:\n- CSV files imported as translations are switched to \"Keep File\" and used as text catalogs.\n- Other translations (PO, MO, .translation) are merged into res://localization/translations.csv.\n- Translation remaps are written to res://localization/resource_remaps.csv.\nThe files are added to \"internationalization/localization/catalog_manifests\" and removed from \"internationalization/locale/translations\" and \"translation_remaps\". This can't be undone.");
	convert_dialog->connect(SceneStringName(confirmed), callable_mp(this, &LocalizationCatalogEditorPlugin::_convert_project_translations));
	add_child(convert_dialog);
}
