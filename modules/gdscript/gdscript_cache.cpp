/**************************************************************************/
/*  gdscript_cache.cpp                                                    */
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

#include "gdscript_cache.h"

#include "gdscript.h"
#include "gdscript_analyzer.h"
#include "gdscript_compiler.h"
#include "gdscript_parser.h"

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/templates/vector.h"

GDScriptParserRef::Status GDScriptParserRef::get_status() const {
	return status;
}

String GDScriptParserRef::get_path() const {
	return path;
}

uint32_t GDScriptParserRef::get_source_hash() const {
	return source_hash;
}

GDScriptParser *GDScriptParserRef::get_parser() {
	if (parser == nullptr) {
		parser = memnew(GDScriptParser);
	}
	return parser;
}

GDScriptAnalyzer *GDScriptParserRef::get_analyzer() {
	if (analyzer == nullptr) {
		analyzer = memnew(GDScriptAnalyzer(get_parser()));
	}
	return analyzer;
}

Error GDScriptParserRef::raise_status(Status p_new_status) {
	ERR_FAIL_COND_V(clearing, ERR_BUG);
	ERR_FAIL_COND_V(parser == nullptr && status != EMPTY, ERR_BUG);

	if (p_new_status < status) {
		return OK;
	}

	while (result == OK && p_new_status > status) {
		switch (status) {
			case EMPTY: {
				// Calling parse will clear the parser, which can destruct another GDScriptParserRef which can clear the last reference to the script with this path, calling remove_script, which clears this GDScriptParserRef.
				// It's ok if its the first thing done here.
				get_parser()->clear();
				status = PARSED;
				String remapped_path = ResourceLoader::path_remap(path);
				if (remapped_path.has_extension("gdc")) {
					Vector<uint8_t> tokens = GDScriptCache::get_binary_tokens(remapped_path);
					source_hash = hash_djb2_buffer(tokens.ptr(), tokens.size());
					result = get_parser()->parse_binary(tokens, path);
				} else {
					String source = GDScriptCache::get_source_code(remapped_path);
					source_hash = source.hash();
					result = get_parser()->parse(source, path, false);
				}
			} break;
			case PARSED: {
				status = INHERITANCE_SOLVED;
				result = get_analyzer()->resolve_inheritance();
			} break;
			case INHERITANCE_SOLVED: {
				status = INTERFACE_SOLVED;
				result = get_analyzer()->resolve_interface();
			} break;
			case INTERFACE_SOLVED: {
				status = FULLY_SOLVED;
				result = get_analyzer()->resolve_body();
			} break;
			case FULLY_SOLVED: {
				return result;
			}
		}
	}

	return result;
}

void GDScriptParserRef::clear() {
	if (clearing) {
		return;
	}
	clearing = true;

	GDScriptParser *lparser = parser;
	GDScriptAnalyzer *lanalyzer = analyzer;

	parser = nullptr;
	analyzer = nullptr;
	status = EMPTY;
	result = OK;
	source_hash = 0;

	clearing = false;

	if (lanalyzer != nullptr) {
		memdelete(lanalyzer);
	}

	if (lparser != nullptr) {
		memdelete(lparser);
	}
}

GDScriptParserRef::~GDScriptParserRef() {
	clear();

	if (!abandoned) {
		MutexLock lock(GDScriptCache::singleton->mutex);
		GDScriptCache::singleton->parser_map.erase(path);
	}
}

GDScriptCache *GDScriptCache::singleton = nullptr;

SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG> &_get_gdscript_cache_mutex() {
	return GDScriptCache::mutex;
}

template <>
thread_local SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG>::TLSData SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG>::tls_data(_get_gdscript_cache_mutex());
SafeBinaryMutex<GDScriptCache::BINARY_MUTEX_TAG> GDScriptCache::mutex;

void GDScriptCache::move_script(const String &p_from, const String &p_to) {
	if (singleton == nullptr || p_from == p_to || p_from.is_empty()) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

	remove_parser(p_from);

	if (singleton->shallow_gdscript_cache.has(p_from) && !p_from.is_empty()) {
		singleton->shallow_gdscript_cache[p_to] = singleton->shallow_gdscript_cache[p_from];
	}
	singleton->shallow_gdscript_cache.erase(p_from);

	if (singleton->full_gdscript_cache.has(p_from) && !p_from.is_empty()) {
		singleton->full_gdscript_cache[p_to] = singleton->full_gdscript_cache[p_from];
	}
	singleton->full_gdscript_cache.erase(p_from);
}

void GDScriptCache::remove_script(const String &p_path) {
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}

	if (HashMap<String, Vector<ObjectID>>::Iterator E = singleton->abandoned_parser_map.find(p_path)) {
		for (ObjectID parser_ref_id : E->value) {
			Ref<GDScriptParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.erase(p_path);

	if (singleton->parser_map.has(p_path)) {
		singleton->parser_map[p_path]->clear();
	}

	remove_parser(p_path);

	singleton->dependencies.erase(p_path);
	singleton->shallow_gdscript_cache.erase(p_path);
	singleton->full_gdscript_cache.erase(p_path);
}

Ref<GDScriptParserRef> GDScriptCache::get_parser(const String &p_path, GDScriptParserRef::Status p_status, Error &r_error, const String &p_owner) {
	MutexLock lock(singleton->mutex);
	Ref<GDScriptParserRef> ref;
	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
		singleton->parser_inverse_dependencies[p_path].insert(p_owner);
	}
	if (singleton->parser_map.has(p_path)) {
		ref = Ref<GDScriptParserRef>(singleton->parser_map[p_path]);
		if (ref.is_null()) {
			r_error = ERR_INVALID_DATA;
			return ref;
		}
	} else {
		String remapped_path = ResourceLoader::path_remap(p_path);
		if (!FileAccess::exists(remapped_path)) {
			r_error = ERR_FILE_NOT_FOUND;
			return ref;
		}
		ref.instantiate();
		ref->path = p_path;
		singleton->parser_map[p_path] = ref.ptr();
	}
	r_error = ref->raise_status(p_status);

	return ref;
}

bool GDScriptCache::has_parser(const String &p_path) {
	MutexLock lock(singleton->mutex);
	return singleton->parser_map.has(p_path);
}

void GDScriptCache::remove_parser(const String &p_path) {
	MutexLock lock(singleton->mutex);

	if (singleton->parser_map.has(p_path)) {
		GDScriptParserRef *parser_ref = singleton->parser_map[p_path];
		parser_ref->abandoned = true;
		singleton->abandoned_parser_map[p_path].push_back(parser_ref->get_instance_id());
	}

	// Can't clear the parser because some other parser might be currently using it in the chain of calls.
	singleton->parser_map.erase(p_path);

	// Have to copy while iterating, because parser_inverse_dependencies is modified.
	HashSet<String> ideps(singleton->parser_inverse_dependencies[p_path]);
	singleton->parser_inverse_dependencies.erase(p_path);
	for (String idep_path : ideps) {
		remove_parser(idep_path);
	}
}

String GDScriptCache::get_source_code(const String &p_path) {
	Vector<uint8_t> source_file;
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V(err, "");

	uint64_t len = f->get_length();
	source_file.resize(len + 1);
	uint64_t r = f->get_buffer(source_file.ptrw(), len);
	ERR_FAIL_COND_V(r != len, "");
	source_file.write[len] = 0;

	String source;
	if (source.append_utf8((const char *)source_file.ptr(), len) != OK) {
		ERR_FAIL_V_MSG("", "Script '" + p_path + "' contains invalid unicode (UTF-8), so it was not loaded. Please ensure that scripts are saved in valid UTF-8 unicode.");
	}
	return source;
}

Vector<uint8_t> GDScriptCache::get_binary_tokens(const String &p_path) {
	Vector<uint8_t> buffer;
	Error err = OK;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);
	ERR_FAIL_COND_V_MSG(err != OK, buffer, "Failed to open binary GDScript file '" + p_path + "'.");

	uint64_t len = f->get_length();
	buffer.resize(len);
	uint64_t read = f->get_buffer(buffer.ptrw(), buffer.size());
	ERR_FAIL_COND_V_MSG(read != len, Vector<uint8_t>(), "Failed to read binary GDScript file '" + p_path + "'.");

	return buffer;
}

Ref<GDScript> GDScriptCache::get_shallow_script(const String &p_path, Error &r_error, const String &p_owner) {
	MutexLock lock(singleton->mutex);

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}
	if (singleton->full_gdscript_cache.has(p_path)) {
		return singleton->full_gdscript_cache[p_path];
	}
	if (singleton->shallow_gdscript_cache.has(p_path)) {
		return singleton->shallow_gdscript_cache[p_path];
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	Ref<GDScript> script;
	script.instantiate();

	script->set_path_cache(p_path);
	if (remapped_path.has_extension("gdc")) {
		Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
		if (buffer.is_empty()) {
			r_error = ERR_FILE_CANT_READ;
		}
		script->set_binary_tokens_source(buffer);
	} else {
		r_error = script->load_source_code(remapped_path);
	}

	if (r_error) {
		return Ref<GDScript>(); // Returns null and does not cache when the script fails to load.
	}

	Ref<GDScriptParserRef> parser_ref = get_parser(p_path, GDScriptParserRef::PARSED, r_error);
	if (r_error == OK) {
		GDScriptCompiler::make_scripts(script.ptr(), parser_ref->get_parser()->get_tree(), true);
	}

	singleton->shallow_gdscript_cache[p_path] = script;

	return script;
}

Ref<GDScript> GDScriptCache::get_full_script(const String &p_path, Error &r_error, const String &p_owner, bool p_update_from_disk) {
	MutexLock lock(singleton->mutex);

	if (!p_owner.is_empty() && p_path != p_owner) {
		singleton->dependencies[p_owner].insert(p_path);
	}

	Ref<GDScript> script;
	r_error = OK;
	if (singleton->full_gdscript_cache.has(p_path)) {
		script = singleton->full_gdscript_cache[p_path];
		if (!p_update_from_disk) {
			return script;
		}
	}

	if (script.is_null()) {
		script = get_shallow_script(p_path, r_error);
		// Only exit early if script failed to load, otherwise let reload report errors.
		if (script.is_null()) {
			return script;
		}
	}

	const String remapped_path = ResourceLoader::path_remap(p_path);

	if (p_update_from_disk) {
		if (remapped_path.has_extension("gdc")) {
			Vector<uint8_t> buffer = get_binary_tokens(remapped_path);
			if (buffer.is_empty()) {
				r_error = ERR_FILE_CANT_READ;
				goto finish;
			}
			script->set_binary_tokens_source(buffer);
		} else {
			r_error = script->load_source_code(remapped_path);
			if (r_error) {
				goto finish;
			}
		}
	}

	// Allowing lifting the lock might cause a script to be reloaded multiple times,
	// which, as a last resort deadlock prevention strategy, is a good tradeoff.
	{
		uint32_t allowance_id = WorkerThreadPool::thread_enter_unlock_allowance_zone(singleton->mutex);
		r_error = script->reload(true);
		WorkerThreadPool::thread_exit_unlock_allowance_zone(allowance_id);
	}

finish:
	singleton->full_gdscript_cache[p_path] = script;
	singleton->shallow_gdscript_cache.erase(p_path);

	// Add the script to the resource cache. Usually ResourceLoader would take care of it, but cyclic references can break that sometimes so we do it ourselves.
	// Resources don't know whether they are cached, so using `set_path()` after `set_path_cache()` does not add the resource to the cache if the path is the same.
	// We reset the cached path from `get_shallow_script()` so that the subsequent call to `set_path()` caches everything correctly.
	script->set_path_cache(String());
	script->set_path(p_path, true);

	return script;
}

Ref<GDScript> GDScriptCache::get_cached_script(const String &p_path) {
	MutexLock lock(singleton->mutex);

	if (singleton->full_gdscript_cache.has(p_path)) {
		return singleton->full_gdscript_cache[p_path];
	}

	if (singleton->shallow_gdscript_cache.has(p_path)) {
		return singleton->shallow_gdscript_cache[p_path];
	}

	return Ref<GDScript>();
}

Error GDScriptCache::finish_compiling(const String &p_owner) {
	MutexLock lock(singleton->mutex);

	// Mark this as compiled.
	Ref<GDScript> script = get_cached_script(p_owner);
	singleton->full_gdscript_cache[p_owner] = script;
	singleton->shallow_gdscript_cache.erase(p_owner);

	HashSet<String> depends(singleton->dependencies[p_owner]);

	Error err = OK;
	for (const String &E : depends) {
		Error this_err = OK;
		// No need to save the script. We assume it's already referenced in the owner.
		get_full_script(E, this_err);

		if (this_err != OK) {
			err = this_err;
		}
	}

	singleton->dependencies.erase(p_owner);

	return err;
}

void GDScriptCache::add_static_script(Ref<GDScript> p_script) {
	ERR_FAIL_COND_MSG(p_script.is_null(), "Trying to cache empty script as static.");
	ERR_FAIL_COND_MSG(!p_script->is_valid(), "Trying to cache non-compiled script as static.");
	singleton->static_gdscript_cache[p_script->get_fully_qualified_name()] = p_script;
}

Ref<GDScript> GDScriptCache::add_bundled_script(const String &p_path, const StringName &p_class_name, const StringName &p_base, const String &p_source, bool p_is_tool) {
	ERR_FAIL_NULL_V_MSG(singleton, Ref<GDScript>(), "GDScriptCache not initialized; cannot add bundled script.");
	ERR_FAIL_COND_V_MSG(p_path.is_empty(), Ref<GDScript>(), "Bundled script needs a (synthetic) path.");

	MutexLock lock(singleton->mutex);

	// Glass: the synthetic path is the cache key for every resolution route below
	// (parser_map, full_gdscript_cache). Reusing a path would silently overwrite an
	// earlier bundled class's cached parser/script, so a sibling that references the
	// earlier class_name would resolve to the WRONG in-memory instance. The caller
	// (register_types.cpp::_glass_compile_bundled_gdscript) guarantees uniqueness via a
	// monotonic counter; assert it here so any future caller that forgets fails loudly
	// instead of corrupting the cache.
	ERR_FAIL_COND_V_MSG(singleton->parser_map.has(p_path) || singleton->full_gdscript_cache.has(p_path), Ref<GDScript>(),
			vformat("Bundled script path '%s' is already seeded; synthetic paths must be unique.", p_path));

	// 1) Register the global class FIRST so the analyzer of THIS script (and of any
	//    later bundled script) resolves `class_name` references by name. We may not yet
	//    know the precise native base from source, so the caller passes it in; the
	//    analyzer only needs the name->path mapping to exist (path resolution is served
	//    from the caches we seed below).
	if (!p_class_name.is_empty()) {
		ScriptServer::add_global_class(p_class_name, p_base, "GDScript", p_path, false, p_is_tool);
	}

	// 2) Seed a parser for this synthetic path, parsed from the IN-MEMORY source and
	//    advanced through analysis. get_parser()/get_depended_parser_for() return this
	//    cached ref without ever touching FileAccess::exists() (which fails under disabled
	//    packs), and its EMPTY->PARSED disk read in raise_status() is skipped because we
	//    pre-advance the status past EMPTY here.
	Ref<GDScriptParserRef> parser_ref;
	parser_ref.instantiate();
	parser_ref->path = p_path;
	// A freshly instantiated GDScriptParser is already clear; parse the in-memory source
	// directly (this avoids raise_status's EMPTY->PARSED branch, which would read the
	// synthetic path from disk and fail under disabled packs).
	Error perr = parser_ref->get_parser()->parse(p_source, p_path, false);
	if (perr != OK) {
		const List<GDScriptParser::ParserError> &errors = parser_ref->get_parser()->get_errors();
		ERR_PRINT(vformat("[Glass] Bundled script '%s' failed to parse: %s",
				String(p_class_name.is_empty() ? StringName(p_path) : p_class_name),
				errors.is_empty() ? String("unknown parse error") : errors.front()->get().message));
		if (!p_class_name.is_empty()) {
			ScriptServer::remove_global_class(p_class_name);
		}
		return Ref<GDScript>();
	}
	parser_ref->source_hash = p_source.hash();
	parser_ref->status = GDScriptParserRef::PARSED;
	// Register in parser_map BEFORE driving analysis so any self-reference (the class
	// resolving its own class_name -> path) or a later sibling finds this in-progress
	// parser instead of falling through to a disk read. The local `parser_ref` keeps it
	// alive across analysis; we take an OWNING ref below only once it fully solves.
	singleton->parser_map[p_path] = parser_ref.ptr();
	// Drive the analyzer the rest of the way; earlier bundled classes resolve from cache.
	Error aerr = parser_ref->raise_status(GDScriptParserRef::FULLY_SOLVED);
	if (aerr != OK) {
		String details;
		const List<GDScriptParser::ParserError> &errs = parser_ref->get_parser()->get_errors();
		for (const GDScriptParser::ParserError &e : errs) {
			details += "\n  - " + e.message;
		}
		ERR_PRINT(vformat("[Glass] Bundled script '%s' failed analysis (err %d):%s",
				String(p_class_name.is_empty() ? StringName(p_path) : p_class_name), (int)aerr, details));
		singleton->parser_map.erase(p_path);
		if (!p_class_name.is_empty()) {
			ScriptServer::remove_global_class(p_class_name);
		}
		return Ref<GDScript>();
	}
	// Fully solved: take an owning ref so the seeded parser outlives this call.
	singleton->bundled_parser_refs.push_back(parser_ref);

	// 3) Compile the actual GDScript from the same in-memory source under the same
	//    synthetic path. GDScript::reload() finds our seeded parser via has_parser()
	//    (same source hash -> kept, not re-read from disk) and seeds
	//    shallow_gdscript_cache[path] = this. class_name references resolve via the
	//    parser/script caches we seeded for earlier bundled scripts.
	Ref<GDScript> script;
	script.instantiate();
	script->set_path_cache(p_path);
	script->set_source_code(p_source);
	Error rerr = script->reload();
	if (rerr != OK || !script->is_valid()) {
		ERR_PRINT(vformat("[Glass] Bundled script '%s' failed to compile (err %d).",
				String(p_class_name.is_empty() ? StringName(p_path) : p_class_name), (int)rerr));
		// Leave the parser cached; remove_parser would cascade. Drop the global mapping.
		if (!p_class_name.is_empty()) {
			ScriptServer::remove_global_class(p_class_name);
		}
		return Ref<GDScript>();
	}

	// 4) Promote into full_gdscript_cache (checked FIRST by get_shallow_script) so later
	//    bundled scripts referencing this class_name resolve to this exact instance, and
	//    pin it in the static cache so a cache clear can't drop it mid-session.
	singleton->full_gdscript_cache[p_path] = script;
	singleton->shallow_gdscript_cache.erase(p_path);
	add_static_script(script);

	return script;
}

void GDScriptCache::remove_static_script(const String &p_fqcn) {
	singleton->static_gdscript_cache.erase(p_fqcn);
}

void GDScriptCache::clear() {
	if (singleton == nullptr) {
		return;
	}

	MutexLock lock(singleton->mutex);

	if (singleton->cleared) {
		return;
	}
	singleton->cleared = true;

	singleton->parser_inverse_dependencies.clear();

	for (const KeyValue<String, Vector<ObjectID>> &KV : singleton->abandoned_parser_map) {
		for (ObjectID parser_ref_id : KV.value) {
			Ref<GDScriptParserRef> parser_ref = { ObjectDB::get_instance(parser_ref_id) };
			if (parser_ref.is_valid()) {
				parser_ref->clear();
			}
		}
	}

	singleton->abandoned_parser_map.clear();

	RBSet<Ref<GDScriptParserRef>> parser_map_refs;
	for (KeyValue<String, GDScriptParserRef *> &E : singleton->parser_map) {
		parser_map_refs.insert(E.value);
	}

	singleton->parser_map.clear();

	for (Ref<GDScriptParserRef> &E : parser_map_refs) {
		if (E.is_valid()) {
			E->clear();
		}
	}

	parser_map_refs.clear();
	singleton->shallow_gdscript_cache.clear();
	singleton->full_gdscript_cache.clear();
	singleton->static_gdscript_cache.clear();

	// Glass: release the owning refs to bundled-script parsers HERE, while `singleton` is
	// still valid and `parser_map` is already empty. Each held Ref is the last owner of
	// its GDScriptParserRef, so dropping it runs ~GDScriptParserRef(), whose non-abandoned
	// branch does `GDScriptCache::singleton->parser_map.erase(path)`. Done here that is a
	// harmless no-op on a live singleton with an empty parser_map. If we instead let these
	// refs die during ~GDScriptCache member teardown, the dtor body has already set
	// `singleton = nullptr`, so ~GDScriptParserRef() would null-deref the singleton and
	// crash the editor on shutdown (SIGSEGV) in every project that loaded a bundled plugin.
	singleton->bundled_parser_refs.clear();
}

GDScriptCache::GDScriptCache() {
	singleton = this;
}

GDScriptCache::~GDScriptCache() {
	if (!cleared) {
		clear();
	}
	singleton = nullptr;
}
