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

#include "sqlite_database.h"
#include "sqlite_statement.h"
#include "sqlite_vfs.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/database_editor.h"
#include "editor/database_table_dialog.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/sqlite_editor_plugin.h"
#endif

static Ref<ResourceFormatLoaderSQLite> resource_loader_sqlite;
static Ref<ResourceFormatSaverSQLite> resource_saver_sqlite;

void initialize_sqlite_module(ModuleInitializationLevel p_level) {
#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		// Internal: no class reference, no scripting API; ClassDB knows their signals (the dialog connects to its own "confirmed").
		GDREGISTER_INTERNAL_CLASS(DatabaseEditor);
		GDREGISTER_INTERNAL_CLASS(DatabaseTableDialog);
		EditorPlugins::add_by_type<SQLiteEditorPlugin>();
	}
#endif

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	if (!SQLiteVFS::initialize()) {
		ERR_PRINT("SQLite: initialization failed.");
	}

	GDREGISTER_CLASS(SQLiteColumn);
	GDREGISTER_CLASS(SQLiteDatabase);
	GDREGISTER_CLASS(SQLiteStatement);

	resource_loader_sqlite.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_sqlite);
	resource_saver_sqlite.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_sqlite);
}

void uninitialize_sqlite_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	ResourceLoader::remove_resource_format_loader(resource_loader_sqlite);
	resource_loader_sqlite.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_sqlite);
	resource_saver_sqlite.unref();

	SQLiteVFS::finalize();
}
