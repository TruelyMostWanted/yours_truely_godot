/**************************************************************************/
/*  register_core_types.cpp                                               */
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

#include "register_core_types.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/core_bind.h"
#include "core/crypto/aes_context.h"
#include "core/crypto/crypto.h"
#include "core/crypto/crypto_resource_format.h"
#include "core/crypto/hashing_context.h"
#include "core/debugger/engine_profiler.h"
#include "core/extension/gdextension.h"
#include "core/extension/gdextension_manager.h"
#include "core/extension/gdextension_resource_format.h"
#include "core/extension/godot_instance.h"
#include "core/input/input.h"
#include "core/input/input_action.h"
#include "core/input/input_actions.h"
#include "core/input/input_map.h"
#include "core/input/shortcut.h"
#include "core/io/config_file.h"
#include "core/io/csv_format.h"
#include "core/io/csv_reader.h"
#include "core/io/data_schema.h"
#include "core/io/data_table.h"
#include "core/io/dir_access.h"
#include "core/io/dtls_server.h"
#include "core/io/files_manifest.h"
#include "core/io/files_manifest_server.h"
#include "core/io/files_manifest_validation.h"
#include "core/io/http_client.h"
#include "core/io/image_loader.h"
#include "core/io/image_resource_format.h"
#include "core/io/ini_format.h"
#include "core/io/json.h"
#include "core/io/json_resource_format.h"
#include "core/io/load_progress.h"
#include "core/io/marshalls.h"
#include "core/io/missing_resource.h"
#include "core/io/packet_peer.h"
#include "core/io/packet_peer_dtls.h"
#include "core/io/packet_peer_udp.h"
#include "core/io/path_prefix_server.h"
#include "core/io/pck_build_plan.h"
#include "core/io/pck_extract_result.h"
#include "core/io/pck_package.h"
#include "core/io/pck_package_server.h"
#include "core/io/pck_packer.h"
#include "core/io/resource_database.h"
#include "core/io/resource_error_capture.h"
#include "core/io/resource_format_binary.h"
#include "core/io/resource_format_pck.h"
#include "core/io/resource_format_text_data.h"
#include "core/io/resource_format_text_tree.h"
#include "core/io/resource_importer.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_result.h"
#include "core/io/resource_saver.h"
#include "core/io/resource_text_serializer.h"
#include "core/io/resource_uid.h"
#include "core/io/serializer_options.h"
#include "core/io/stream_peer_gzip.h"
#include "core/io/stream_peer_tls.h"
#include "core/io/streaming_assets_server.h"
#include "core/io/tcp_server.h"
#include "core/io/text_data_format.h"
#include "core/io/translation_loader_po.h"
#include "core/io/udp_server.h"
#include "core/io/uds_server.h"
#include "core/io/xml_format.h"
#include "core/io/xml_parser.h"
#include "core/io/xml_writer.h"
#include "core/math/a_star.h"
#include "core/math/a_star_grid_2d.h"
#include "core/math/expression.h"
#include "core/math/random_number_generator.h"
#include "core/math/seed_server.h"
#include "core/math/triangle_mesh.h"
#include "core/object/class_db.h"
#include "core/object/script_backtrace.h"
#include "core/object/script_language_extension.h"
#include "core/object/undo_redo.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/main_loop.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/fuzzy_search.h"
#include "core/string/localization_catalog.h"
#include "core/string/optimized_translation.h"
#include "core/string/regex.h"
#include "core/string/resource_localization_server.h"
#include "core/string/text_insertion_server.h"
#include "core/string/text_localization_server.h"
#include "core/string/translation.h"
#include "core/string/translation_server.h"
#ifndef DISABLE_DEPRECATED
#include "core/io/packed_data_container.h"
#endif

static Ref<ResourceFormatSaverBinary> resource_saver_binary;
static Ref<ResourceFormatLoaderBinary> resource_loader_binary;
static Ref<ResourceFormatImporter> resource_format_importer;
static Ref<ResourceFormatImporterSaver> resource_format_importer_saver;
static Ref<ResourceFormatLoaderImage> resource_format_image;
static Ref<TranslationLoaderPO> resource_format_po;
static Ref<ResourceFormatLoaderLocalizationCatalog> resource_loader_localization_catalog;
static Ref<ResourceFormatSaverLocalizationCatalog> resource_saver_localization_catalog;
static Ref<ResourceFormatSaverCrypto> resource_format_saver_crypto;
static Ref<ResourceFormatLoaderCrypto> resource_format_loader_crypto;
static Ref<GDExtensionResourceLoader> resource_loader_gdextension;
static Ref<ResourceFormatSaverJSON> resource_saver_json;
static Ref<ResourceFormatLoaderJSON> resource_loader_json;
static Ref<ResourceFormatLoaderResourceJSON> resource_loader_resource_json;
static Ref<ResourceFormatSaverResourceJSON> resource_saver_resource_json;
static Ref<ResourceFormatLoaderResourceINI> resource_loader_resource_ini;
static Ref<ResourceFormatSaverResourceINI> resource_saver_resource_ini;
static Ref<ResourceFormatLoaderResourceCSV> resource_loader_resource_csv;
static Ref<ResourceFormatSaverResourceCSV> resource_saver_resource_csv;
static Ref<ResourceFormatLoaderResourceXML> resource_loader_resource_xml;
static Ref<ResourceFormatSaverResourceXML> resource_saver_resource_xml;
static Ref<ResourceFormatLoaderTextData> resource_loader_text_data_ini;
static Ref<ResourceFormatSaverTextData> resource_saver_text_data_ini;
static Ref<ResourceFormatLoaderTextData> resource_loader_text_data_csv;
static Ref<ResourceFormatSaverTextData> resource_saver_text_data_csv;
static Ref<ResourceFormatLoaderTextData> resource_loader_text_data_xml;
static Ref<ResourceFormatSaverTextData> resource_saver_text_data_xml;
static Ref<ResourceFormatLoaderPck> resource_loader_pck;
static Ref<ResourceFormatSaverPck> resource_saver_pck;
static ResourceTextSerializer *_resource_text_serializer = nullptr;

static CoreBind::ResourceLoader *_resource_loader = nullptr;
static PathPrefixServer *_path_prefix_server = nullptr;
static FilesManifestServer *_files_manifest_server = nullptr;
static PCKPackageServer *_pck_package_server = nullptr;
static StreamingAssetsServer *_streaming_assets_server = nullptr;
static ResourceDatabase *_resource_database = nullptr;
static CoreBind::ResourceSaver *_resource_saver = nullptr;
static CoreBind::OS *_os = nullptr;
static CoreBind::Engine *_engine = nullptr;
static CoreBind::Special::ClassDB *_classdb = nullptr;
static CoreBind::Marshalls *_marshalls = nullptr;
static CoreBind::EngineDebugger *_engine_debugger = nullptr;

static IP *ip = nullptr;
static Time *_time = nullptr;

static CoreBind::Geometry2D *_geometry_2d = nullptr;
static CoreBind::Geometry3D *_geometry_3d = nullptr;

static WorkerThreadPool *worker_thread_pool = nullptr;

extern Mutex _global_mutex;

static GDExtensionManager *gdextension_manager = nullptr;

extern void register_global_constants();
extern void unregister_global_constants();

static ResourceUID *resource_uid = nullptr;
static SeedServer *seed_server = nullptr;
static TextInsertionServer *text_insertion_server = nullptr;
static TextLocalizationServer *text_localization_server = nullptr;
static ResourceLocalizationServer *resource_localization_server = nullptr;

static bool _is_core_extensions_registered = false;

void register_core_types() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Register Types");

	//consistency check
	static_assert(sizeof(Callable) <= 16);

	CryptoCore::initialize();

	ObjectDB::setup();
	StringName::setup();
	register_global_constants();
	CoreStringNames::create();

	GDREGISTER_CLASS(Object);
	GDREGISTER_CLASS(RefCounted);
	GDREGISTER_CLASS(CoreBind::WeakRef);
	GDREGISTER_CLASS(Resource);

	GDREGISTER_CLASS(Time);
	_time = memnew(Time);
	ResourceLoader::initialize();
	ResourceErrorCapture::initialize();

	Variant::register_types();

	GDREGISTER_CLASS(ResourceFormatLoader);
	GDREGISTER_CLASS(ResourceFormatSaver);

	if constexpr (GD_IS_CLASS_ENABLED(Translation)) {
		resource_format_po.instantiate();
		ResourceLoader::add_resource_format_loader(resource_format_po);
	}

	resource_saver_binary.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_binary);
	resource_loader_binary.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_binary);

	resource_format_importer.instantiate();
	ResourceLoader::add_resource_format_loader(resource_format_importer);

	resource_format_importer_saver.instantiate();
	ResourceSaver::add_resource_format_saver(resource_format_importer_saver);

	if constexpr (GD_IS_CLASS_ENABLED(Image)) {
		resource_format_image.instantiate();
		ResourceLoader::add_resource_format_loader(resource_format_image);
	}

	GDREGISTER_ABSTRACT_CLASS(Script);
	GDREGISTER_ABSTRACT_CLASS(ScriptLanguage);
	GDREGISTER_CLASS(ScriptBacktrace);
	GDREGISTER_VIRTUAL_CLASS(ScriptExtension);
	GDREGISTER_VIRTUAL_CLASS(ScriptLanguageExtension);

	GDREGISTER_CLASS(MissingResource);
	GDREGISTER_CLASS(Image);

	GDREGISTER_CLASS(Shortcut);
	GDREGISTER_ABSTRACT_CLASS(InputEvent);
	GDREGISTER_ABSTRACT_CLASS(InputEventFromWindow);
	GDREGISTER_ABSTRACT_CLASS(InputEventWithModifiers);
	GDREGISTER_CLASS(InputEventKey);
	GDREGISTER_CLASS(InputEventShortcut);
	GDREGISTER_ABSTRACT_CLASS(InputEventMouse);
	GDREGISTER_CLASS(InputEventMouseButton);
	GDREGISTER_CLASS(InputEventMouseMotion);
	GDREGISTER_CLASS(InputEventJoypadButton);
	GDREGISTER_CLASS(InputEventJoypadMotion);
	GDREGISTER_CLASS(InputEventScreenDrag);
	GDREGISTER_CLASS(InputEventScreenTouch);
	GDREGISTER_CLASS(InputEventAction);
	GDREGISTER_ABSTRACT_CLASS(InputEventGesture);
	GDREGISTER_CLASS(InputEventMagnifyGesture);
	GDREGISTER_CLASS(InputEventPanGesture);
	GDREGISTER_CLASS(InputEventMIDI);
	GDREGISTER_CLASS(InputAction);
	GDREGISTER_CLASS(InputActions);

	// Network
	GDREGISTER_ABSTRACT_CLASS(StreamPeer);
	GDREGISTER_ABSTRACT_CLASS(StreamPeerSocket);
	GDREGISTER_ABSTRACT_CLASS(SocketServer);
	GDREGISTER_CLASS(StreamPeerExtension);
	GDREGISTER_CLASS(StreamPeerBuffer);
	GDREGISTER_CLASS(StreamPeerGZIP);
	GDREGISTER_CLASS(StreamPeerTCP);
	GDREGISTER_CLASS(TCPServer);

	// IPC using UNIX domain sockets.
	GDREGISTER_CLASS(StreamPeerUDS);
	GDREGISTER_CLASS(UDSServer);

	GDREGISTER_ABSTRACT_CLASS(PacketPeer);
	GDREGISTER_CLASS(PacketPeerExtension);
	GDREGISTER_CLASS(PacketPeerStream);
	GDREGISTER_CLASS(PacketPeerUDP);
	GDREGISTER_CLASS(UDPServer);

	GDREGISTER_ABSTRACT_CLASS(WorkerThreadPool);

	ClassDB::register_custom_instance_class<HTTPClient>();

	// Crypto
	GDREGISTER_CLASS(HashingContext);
	GDREGISTER_CLASS(AESContext);
	ClassDB::register_custom_instance_class<X509Certificate>();
	ClassDB::register_custom_instance_class<CryptoKey>();
	GDREGISTER_ABSTRACT_CLASS(TLSOptions);
	ClassDB::register_custom_instance_class<HMACContext>();
	ClassDB::register_custom_instance_class<Crypto>();
	ClassDB::register_custom_instance_class<StreamPeerTLS>();
	ClassDB::register_custom_instance_class<PacketPeerDTLS>();
	ClassDB::register_custom_instance_class<DTLSServer>();

	if constexpr (GD_IS_CLASS_ENABLED(Crypto)) {
		resource_format_saver_crypto.instantiate();
		ResourceSaver::add_resource_format_saver(resource_format_saver_crypto);

		resource_format_loader_crypto.instantiate();
		ResourceLoader::add_resource_format_loader(resource_format_loader_crypto);
	}

	// Resource text formats (JSON, INI, CSV). The JSON loader comes before ResourceFormatLoaderJSON,
	// which takes all JSON files without a "$type" key.
	resource_loader_resource_json.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_resource_json);
	resource_loader_resource_ini.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_resource_ini);
	resource_loader_resource_csv.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_resource_csv);
	resource_loader_resource_xml.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_resource_xml);

	if constexpr (GD_IS_CLASS_ENABLED(JSON)) {
		resource_saver_json.instantiate();
		ResourceSaver::add_resource_format_saver(resource_saver_json);

		resource_loader_json.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_json);
	}

	resource_saver_resource_json.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_resource_json);
	resource_saver_resource_ini.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_resource_ini);
	resource_saver_resource_csv.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_resource_csv);
	resource_saver_resource_xml.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_resource_xml);

	// Plain data files (no type marker) as INI, CSV and XML resources, after the text-tree loaders above.
	resource_loader_text_data_ini.instantiate("INI", Vector<String>{ "ini", "cfg" });
	ResourceLoader::add_resource_format_loader(resource_loader_text_data_ini);
	resource_saver_text_data_ini.instantiate("INI");
	ResourceSaver::add_resource_format_saver(resource_saver_text_data_ini);
	resource_loader_text_data_csv.instantiate("CSV", Vector<String>{ "csv", "tsv" });
	ResourceLoader::add_resource_format_loader(resource_loader_text_data_csv);
	resource_saver_text_data_csv.instantiate("CSV");
	ResourceSaver::add_resource_format_saver(resource_saver_text_data_csv);
	resource_loader_text_data_xml.instantiate("XML", Vector<String>{ "xml" });
	ResourceLoader::add_resource_format_loader(resource_loader_text_data_xml);
	resource_saver_text_data_xml.instantiate("XML");
	ResourceSaver::add_resource_format_saver(resource_saver_text_data_xml);

	// `.pck` files as `PCKPackage` resources: loading opens a pack without mounting it, saving builds one.
	resource_loader_pck.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_pck);
	resource_saver_pck.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_pck);

	GDREGISTER_CLASS(MainLoop);
	GDREGISTER_CLASS(Translation);
	GDREGISTER_CLASS(TranslationDomain);
	GDREGISTER_CLASS(TextInsertionValues);
	GDREGISTER_CLASS(TextInsertionTemplate);
	GDREGISTER_ABSTRACT_CLASS(TextInsertionServer);
	text_insertion_server = memnew(TextInsertionServer);
	GDREGISTER_CLASS(OptimizedTranslation);
	GDREGISTER_ABSTRACT_CLASS(LocalizationCatalog);
	GDREGISTER_CLASS(TextLocalizationCatalog);
	GDREGISTER_CLASS(ResourceLocalizationCatalog);
	resource_loader_localization_catalog.instantiate();
	ResourceLoader::add_resource_format_loader(resource_loader_localization_catalog, true); // Before the text resource loaders (#357).
	resource_saver_localization_catalog.instantiate();
	ResourceSaver::add_resource_format_saver(resource_saver_localization_catalog, true); // Before the text resource savers (#346).
	GDREGISTER_ABSTRACT_CLASS(LocalizationServer);
	GDREGISTER_ABSTRACT_CLASS(TextLocalizationServer);
	text_localization_server = memnew(TextLocalizationServer);
	GDREGISTER_ABSTRACT_CLASS(ResourceLocalizationServer);
	resource_localization_server = memnew(ResourceLocalizationServer);
	GDREGISTER_CLASS(UndoRedo);
	GDREGISTER_CLASS(TriangleMesh);

	GDREGISTER_ABSTRACT_CLASS(FileAccess);
	GDREGISTER_ABSTRACT_CLASS(DirAccess);
	GDREGISTER_CLASS(CoreBind::Thread);
	GDREGISTER_CLASS(CoreBind::Mutex);
	GDREGISTER_CLASS(CoreBind::Semaphore);
	GDREGISTER_VIRTUAL_CLASS(CoreBind::Logger);

	GDREGISTER_CLASS(XMLParser);
	GDREGISTER_CLASS(XMLWriter);
	GDREGISTER_CLASS(JSON);
	GDREGISTER_ABSTRACT_CLASS(TextDataFormat);
	GDREGISTER_CLASS(XMLElement);
	GDREGISTER_CLASS(XML);
	GDREGISTER_CLASS(INI);
	GDREGISTER_CLASS(CSV);
	GDREGISTER_CLASS(CSVReader);
	GDREGISTER_CLASS(DataField);
	GDREGISTER_CLASS(DataSchema);
	GDREGISTER_CLASS(DataValidationOptions);
	GDREGISTER_CLASS(DataValidation);
	GDREGISTER_CLASS(DataTable);
	GDREGISTER_CLASS(RegExMatch);
	GDREGISTER_CLASS(RegEx);

	GDREGISTER_CLASS(ConfigFile);

	GDREGISTER_CLASS(PCKPacker);

	GDREGISTER_CLASS(AStar3D);
	GDREGISTER_CLASS(AStar2D);
	GDREGISTER_CLASS(AStarGrid2D);
	GDREGISTER_CLASS(EncodedObjectAsID);
	GDREGISTER_CLASS(RandomNumberGenerator);
	GDREGISTER_ABSTRACT_CLASS(SeedServer);
	seed_server = memnew(SeedServer);
	GDREGISTER_CLASS(SerializerOptions);
	GDREGISTER_ABSTRACT_CLASS(ResourceTextSerializer);
	_resource_text_serializer = memnew(ResourceTextSerializer);
#ifndef DISABLE_DEPRECATED
	GDREGISTER_CLASS(PackedDataContainer);
	GDREGISTER_ABSTRACT_CLASS(PackedDataContainerRef);
#endif

	GDREGISTER_ABSTRACT_CLASS(ImageFormatLoader);
	GDREGISTER_CLASS(ImageFormatLoaderExtension);
	GDREGISTER_ABSTRACT_CLASS(ResourceImporter);

	GDREGISTER_CLASS(GDExtension);

	GDREGISTER_ABSTRACT_CLASS(GodotInstance);

	GDREGISTER_ABSTRACT_CLASS(GDExtensionManager);

	GDREGISTER_ABSTRACT_CLASS(ResourceUID);

	GDREGISTER_CLASS(EngineProfiler);

	GDREGISTER_CLASS(FuzzySearch);
	GDREGISTER_CLASS(FuzzySearchMatch);

	resource_uid = memnew(ResourceUID);

	gdextension_manager = memnew(GDExtensionManager);

	if constexpr (GD_IS_CLASS_ENABLED(GDExtension)) {
		resource_loader_gdextension.instantiate();
		ResourceLoader::add_resource_format_loader(resource_loader_gdextension);
	}

	GDREGISTER_ABSTRACT_CLASS(IP);
	GDREGISTER_CLASS(CoreBind::Geometry2D);
	GDREGISTER_CLASS(CoreBind::Geometry3D);
	GDREGISTER_CLASS(FilesManifest);
	GDREGISTER_CLASS(PathInfo);
	GDREGISTER_CLASS(PathPrefix);
	GDREGISTER_CLASS(PathPrefixServer);
	GDREGISTER_ABSTRACT_CLASS(FilesManifestServer);
	GDREGISTER_CLASS(FilesManifestValidation);
	GDREGISTER_CLASS(PCKExtractResult);
	GDREGISTER_CLASS(PCKPackage);
	GDREGISTER_CLASS(PCKPackageEntry);
	GDREGISTER_CLASS(PCKBuildPlan);
	GDREGISTER_ABSTRACT_CLASS(PCKPackageServer);
	GDREGISTER_ABSTRACT_CLASS(StreamingAssetsServer);
	GDREGISTER_CLASS(Preloads);
	GDREGISTER_CLASS(LoadProgress);
#ifndef DISABLE_DEPRECATED
	// Renamed in 0.2.2 (naming rule: no container type in a name); saved resources keep loading.
	ClassDB::add_compatibility_class("PreloadList", "Preloads");
#endif // DISABLE_DEPRECATED
	GDREGISTER_ABSTRACT_CLASS(ResourceDatabase);
	GDREGISTER_CLASS(CoreBind::ResourceLoader);
	GDREGISTER_CLASS(CoreBind::ResourceSaver);
	GDREGISTER_CLASS(ResourceLoadResult);
	GDREGISTER_CLASS(ResourceSaveResult);
	GDREGISTER_CLASS(CoreBind::OS);
	GDREGISTER_CLASS(CoreBind::Engine);
	GDREGISTER_CLASS(CoreBind::Special::ClassDB);
	GDREGISTER_CLASS(CoreBind::Marshalls);
	GDREGISTER_CLASS(CoreBind::EngineDebugger);

	GDREGISTER_CLASS(TranslationServer);
	GDREGISTER_ABSTRACT_CLASS(Input);
	GDREGISTER_CLASS(InputMap);
	GDREGISTER_CLASS(Expression);
	GDREGISTER_CLASS(ProjectSettings);

	ip = IP::create();

	_geometry_2d = memnew(CoreBind::Geometry2D);
	_geometry_3d = memnew(CoreBind::Geometry3D);

	// Before anything resolves a path: registers the locked engine prefixes.
	_path_prefix_server = memnew(PathPrefixServer);
	_resource_loader = memnew(CoreBind::ResourceLoader);
	_resource_saver = memnew(CoreBind::ResourceSaver);
	_files_manifest_server = memnew(FilesManifestServer);
	_pck_package_server = memnew(PCKPackageServer);
	// Registers the locked `streaming://` prefix; `setup()` points it at the files later.
	_streaming_assets_server = memnew(StreamingAssetsServer);
	_resource_database = memnew(ResourceDatabase);
	_os = memnew(CoreBind::OS);
	_engine = memnew(CoreBind::Engine);
	_classdb = memnew(CoreBind::Special::ClassDB);
	_marshalls = memnew(CoreBind::Marshalls);
	_engine_debugger = memnew(CoreBind::EngineDebugger);

	GDREGISTER_NATIVE_STRUCT(ObjectID, "uint64_t id = 0");
	GDREGISTER_NATIVE_STRUCT(ScriptLanguageExtensionProfilingInfo, "StringName signature;uint64_t call_count;uint64_t total_time;uint64_t self_time");

	worker_thread_pool = memnew(WorkerThreadPool);

	OS::get_singleton()->benchmark_end_measure("Core", "Register Types");
}

void register_core_settings() {
	// Since in register core types, globals may not be present.
	GLOBAL_DEF(PropertyInfo(Variant::DICTIONARY, "filesystem/manifests/autoload", PROPERTY_HINT_DICTIONARY_TYPE, "String;String"), Dictionary());
	GLOBAL_DEF(PropertyInfo(Variant::PACKED_STRING_ARRAY, "filesystem/resources/preloads", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:*.tres,*.res", Variant::STRING, PROPERTY_HINT_FILE)), PackedStringArray());
#ifndef DISABLE_DEPRECATED
	GLOBAL_DEF(PropertyInfo(Variant::PACKED_STRING_ARRAY, "filesystem/resources/preload_lists", PROPERTY_HINT_TYPE_STRING, vformat("%d/%d:*.tres,*.res", Variant::STRING, PROPERTY_HINT_FILE)), PackedStringArray());
#endif
	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/tcp/connect_timeout_seconds", PROPERTY_HINT_RANGE, "1,1800,1"), (30));
	GLOBAL_DEF(PropertyInfo(Variant::INT, "network/limits/unix/connect_timeout_seconds", PROPERTY_HINT_RANGE, "1,1800,1"), (30));
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "network/limits/packet_peer_stream/max_buffer_po2", PROPERTY_HINT_RANGE, "8,64,1,or_greater"), (16));
	GLOBAL_DEF(PropertyInfo(Variant::STRING, "network/tls/certificate_bundle_override", PROPERTY_HINT_FILE, "*.crt"), "");

	GLOBAL_DEF("threading/worker_pool/max_threads", -1);
	GLOBAL_DEF("threading/worker_pool/low_priority_thread_ratio", 0.3);

	GLOBAL_DEF_RST("filesystem/path_prefixes/defaults", true);
	ResourceTextSerializer::register_settings();
	StreamingAssetsServer::register_settings();
	TextInsertionServer::register_settings();
	LocalizationServer::register_settings();
}

void register_early_core_singletons() {
	Engine::get_singleton()->add_singleton(Engine::Singleton("Engine", CoreBind::Engine::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ProjectSettings", ProjectSettings::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("OS", CoreBind::OS::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Time", Time::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ClassDB", _classdb));
}

void register_core_singletons() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Register Singletons");

	Engine::get_singleton()->add_singleton(Engine::Singleton("IP", IP::get_singleton(), "IP"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Geometry2D", CoreBind::Geometry2D::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Geometry3D", CoreBind::Geometry3D::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("SeedServer", SeedServer::get_singleton(), "SeedServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceLoader", CoreBind::ResourceLoader::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceSaver", CoreBind::ResourceSaver::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Marshalls", CoreBind::Marshalls::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("TranslationServer", TranslationServer::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("TextInsertionServer", TextInsertionServer::get_singleton(), "TextInsertionServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("TextLocalizationServer", TextLocalizationServer::get_singleton(), "TextLocalizationServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceLocalizationServer", ResourceLocalizationServer::get_singleton(), "ResourceLocalizationServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("Input", Input::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("InputMap", InputMap::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("EngineDebugger", CoreBind::EngineDebugger::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("GDExtensionManager", GDExtensionManager::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceUID", ResourceUID::get_singleton()));
	Engine::get_singleton()->add_singleton(Engine::Singleton("PathPrefixServer", PathPrefixServer::get_singleton(), "PathPrefixServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("FilesManifestServer", FilesManifestServer::get_singleton(), "FilesManifestServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("PCKPackageServer", PCKPackageServer::get_singleton(), "PCKPackageServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("StreamingAssetsServer", StreamingAssetsServer::get_singleton(), "StreamingAssetsServer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceDatabase", ResourceDatabase::get_singleton(), "ResourceDatabase"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("ResourceTextSerializer", ResourceTextSerializer::get_singleton(), "ResourceTextSerializer"));
	Engine::get_singleton()->add_singleton(Engine::Singleton("WorkerThreadPool", worker_thread_pool));

	OS::get_singleton()->benchmark_end_measure("Core", "Register Singletons");
}

void register_core_extensions() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Register Extensions");

	// The project settings are loaded here: register the default path prefixes before extensions can replace them.
	PathPrefixServer::get_singleton()->register_default_prefixes();
	// The main pack is loaded too: resolve `streaming://` and mount the streaming pack before anything loads resources.
	StreamingAssetsServer::get_singleton()->setup();

	// Hardcoded for now.
	GDExtension::initialize_gdextensions();
	gdextension_manager->load_extensions();
	gdextension_manager->initialize_extensions(GDExtension::INITIALIZATION_LEVEL_CORE);
	_is_core_extensions_registered = true;

	OS::get_singleton()->benchmark_end_measure("Core", "Register Extensions");
}

void unregister_core_extensions() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Unregister Extensions");

	if (_is_core_extensions_registered) {
		gdextension_manager->deinitialize_extensions(GDExtension::INITIALIZATION_LEVEL_CORE);
	}
	GDExtension::finalize_gdextensions();

	OS::get_singleton()->benchmark_end_measure("Core", "Unregister Extensions");
}

void unregister_core_types() {
	OS::get_singleton()->benchmark_begin_measure("Core", "Unregister Types");

	// Destroy singletons in reverse order to ensure dependencies are not broken.

	// Waits for preloads on worker threads, so before the pool goes away.
	memdelete(_resource_database);
	memdelete(worker_thread_pool);

	memdelete(_engine_debugger);
	memdelete(_marshalls);
	memdelete(_classdb);
	memdelete(_engine);
	memdelete(_os);
	memdelete(_resource_saver);
	memdelete(_resource_loader);
	memdelete(_streaming_assets_server);
	memdelete(_pck_package_server);
	memdelete(_files_manifest_server);
	memdelete(_path_prefix_server);

	memdelete(_geometry_3d);
	memdelete(_geometry_2d);

	memdelete(gdextension_manager);

	memdelete(resource_uid);

	memdelete(seed_server);
	memdelete(resource_localization_server);
	memdelete(text_localization_server);
	memdelete(text_insertion_server);
	memdelete(_resource_text_serializer);

	memdelete(ip);

	if constexpr (GD_IS_CLASS_ENABLED(Image)) {
		ResourceLoader::remove_resource_format_loader(resource_format_image);
		resource_format_image.unref();
	}

	ResourceSaver::remove_resource_format_saver(resource_saver_binary);
	resource_saver_binary.unref();

	ResourceLoader::remove_resource_format_loader(resource_loader_binary);
	resource_loader_binary.unref();

	ResourceLoader::remove_resource_format_loader(resource_format_importer);
	resource_format_importer.unref();

	ResourceSaver::remove_resource_format_saver(resource_format_importer_saver);
	resource_format_importer_saver.unref();

	if constexpr (GD_IS_CLASS_ENABLED(Translation)) {
		ResourceLoader::remove_resource_format_loader(resource_format_po);
		resource_format_po.unref();
	}

	ResourceLoader::remove_resource_format_loader(resource_loader_localization_catalog);
	resource_loader_localization_catalog.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_localization_catalog);
	resource_saver_localization_catalog.unref();
	LocalizationCatalog::finish_plural_rules();

	if constexpr (GD_IS_CLASS_ENABLED(Crypto)) {
		ResourceSaver::remove_resource_format_saver(resource_format_saver_crypto);
		resource_format_saver_crypto.unref();

		ResourceLoader::remove_resource_format_loader(resource_format_loader_crypto);
		resource_format_loader_crypto.unref();
	}

	ResourceLoader::remove_resource_format_loader(resource_loader_resource_json);
	resource_loader_resource_json.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_resource_json);
	resource_saver_resource_json.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_resource_ini);
	resource_loader_resource_ini.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_resource_ini);
	resource_saver_resource_ini.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_resource_csv);
	resource_loader_resource_csv.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_resource_csv);
	resource_saver_resource_csv.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_resource_xml);
	resource_loader_resource_xml.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_text_data_ini);
	resource_loader_text_data_ini.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_text_data_ini);
	resource_saver_text_data_ini.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_text_data_csv);
	resource_loader_text_data_csv.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_text_data_csv);
	resource_saver_text_data_csv.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_text_data_xml);
	resource_loader_text_data_xml.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_text_data_xml);
	resource_saver_text_data_xml.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_resource_xml);
	resource_saver_resource_xml.unref();
	ResourceLoader::remove_resource_format_loader(resource_loader_pck);
	resource_loader_pck.unref();
	ResourceSaver::remove_resource_format_saver(resource_saver_pck);
	resource_saver_pck.unref();

	if constexpr (GD_IS_CLASS_ENABLED(JSON)) {
		ResourceSaver::remove_resource_format_saver(resource_saver_json);
		resource_saver_json.unref();

		ResourceLoader::remove_resource_format_loader(resource_loader_json);
		resource_loader_json.unref();
	}

	if constexpr (GD_IS_CLASS_ENABLED(GDExtension)) {
		ResourceLoader::remove_resource_format_loader(resource_loader_gdextension);
		resource_loader_gdextension.unref();
	}

	ResourceErrorCapture::finalize();
	ResourceLoader::finalize();

	ClassDB::cleanup_defaults();
	memdelete(_time);
	ObjectDB::cleanup();

	CryptoCore::finalize();

	Variant::unregister_types();

	unregister_global_constants();

	ResourceCache::clear();
	ClassDB::cleanup();
	CoreStringNames::free();
	StringName::cleanup();

	OS::get_singleton()->benchmark_end_measure("Core", "Unregister Types");
}
