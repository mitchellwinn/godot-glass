/**************************************************************************/
/*  gdscript_cache.h                                                      */
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

#include "gdscript.h"

#include "core/object/ref_counted.h"
#include "core/os/safe_binary_mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class GDScriptAnalyzer;
class GDScriptParser;

class GDScriptParserRef : public RefCounted {
	GDSOFTCLASS(GDScriptParserRef, RefCounted);

public:
	enum Status {
		EMPTY,
		PARSED,
		INHERITANCE_SOLVED,
		INTERFACE_SOLVED,
		FULLY_SOLVED,
	};

private:
	GDScriptParser *parser = nullptr;
	GDScriptAnalyzer *analyzer = nullptr;
	Status status = EMPTY;
	Error result = OK;
	String path;
	uint32_t source_hash = 0;
	bool clearing = false;
	bool abandoned = false;

	friend class GDScriptCache;
	friend class GDScript;

public:
	Status get_status() const;
	String get_path() const;
	uint32_t get_source_hash() const;
	GDScriptParser *get_parser();
	GDScriptAnalyzer *get_analyzer();
	Error raise_status(Status p_new_status);
	void clear();

	GDScriptParserRef() {}
	~GDScriptParserRef();
};

namespace GDScriptTests {
class TestGDScriptCacheAccessor;
}

class GDScriptCache {
	// String key is full path.
	HashMap<String, GDScriptParserRef *> parser_map;
	HashMap<String, Vector<ObjectID>> abandoned_parser_map;
	HashMap<String, Ref<GDScript>> shallow_gdscript_cache;
	HashMap<String, Ref<GDScript>> full_gdscript_cache;
	HashMap<String, Ref<GDScript>> static_gdscript_cache;
	HashMap<String, HashSet<String>> dependencies;
	HashMap<String, HashSet<String>> parser_inverse_dependencies;

	friend class GDScript;
	friend class GDScriptParserRef;
	friend class GDScriptInstance;
	friend class GDScriptTests::TestGDScriptCacheAccessor;

	static GDScriptCache *singleton;

	// Glass: keep bundled parser refs alive for the whole editor session. parser_map
	// only stores raw GDScriptParserRef* and a ref's destructor erases its own
	// parser_map entry, so without an owning Ref here a bundled script's seeded parser
	// would be freed the moment add_bundled_script() returns and a later sibling could
	// no longer resolve its class_name from cache. (Scripts themselves stay alive via
	// full_gdscript_cache / static_gdscript_cache.)
	Vector<Ref<GDScriptParserRef>> bundled_parser_refs;

	bool cleared = false;

public:
	static const int BINARY_MUTEX_TAG = 2;

private:
	static SafeBinaryMutex<BINARY_MUTEX_TAG> mutex;
	friend SafeBinaryMutex<BINARY_MUTEX_TAG> &_get_gdscript_cache_mutex();

public:
	static void move_script(const String &p_from, const String &p_to);
	static void remove_script(const String &p_path);
	static Ref<GDScriptParserRef> get_parser(const String &p_path, GDScriptParserRef::Status status, Error &r_error, const String &p_owner = String());
	static bool has_parser(const String &p_path);
	static void remove_parser(const String &p_path);
	static String get_source_code(const String &p_path);
	static Vector<uint8_t> get_binary_tokens(const String &p_path);
	static Ref<GDScript> get_shallow_script(const String &p_path, Error &r_error, const String &p_owner = String());
	/**
	 * Returns a fully loaded GDScript using an already cached script if one exists.
	 *
	 * The returned instance is present in GDScriptCache and ResourceCache.
	 */
	static Ref<GDScript> get_full_script(const String &p_path, Error &r_error, const String &p_owner = String(), bool p_update_from_disk = false);
	static Ref<GDScript> get_cached_script(const String &p_path);
	static Error finish_compiling(const String &p_owner);
	static void add_static_script(Ref<GDScript> p_script);
	static void remove_static_script(const String &p_fqcn);

	// Glass: compile an engine-bundled GDScript from an in-memory source string under
	// a synthetic (non-res://) path, register its `class_name` as a global class, and
	// pre-seed the path-keyed caches (parser_map + full_gdscript_cache) so LATER bundled
	// scripts that reference this one by class_name resolve to this exact in-memory
	// instance with ZERO FileAccess/pack hits. This is what makes a MULTI-file bundled
	// plugin viable even though packs are disabled in the editor (see main.cpp where the
	// editor calls PackedData::set_disabled(true)). Scripts must be added in dependency
	// order (a class before anything that references it). Returns the compiled script, or
	// a null Ref on failure (errors are reported via ERR_PRINT).
	static Ref<GDScript> add_bundled_script(const String &p_path, const StringName &p_class_name, const StringName &p_base, const String &p_source, bool p_is_tool);

	static void clear();

	GDScriptCache();
	~GDScriptCache();
};
