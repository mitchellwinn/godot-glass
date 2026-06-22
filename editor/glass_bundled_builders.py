"""Glass: build-time generator for engine-bundled GDScript editor plugins.

Reads each bundled plugin's `manifest.json` (which declares the STRICT
dependency load order + per-script class_name/base/flags) and the referenced
`.gd` sources, then emits a single header-only `.gen.h` containing:

  * one zlib-compressed byte array per source (same scheme as
    editor/editor_builders.py::make_doc_header — `methods.compress_buffer` +
    `methods.format_buffer`, decompressed at load with Compression::MODE_DEFLATE);
  * a `GlassBundledScript` struct table, in manifest order, that the loader
    (EditorNode::_load_glass_bundled_plugins) iterates: it decompresses each
    source, compiles it via the bundled-script compiler IN ORDER (so each
    class_name is registered before the next source resolves it), and for the
    single `is_plugin` entry does the instantiate + set_script +
    add_editor_plugin dance.

This mirrors the engine's existing gen-header conventions so editing any `.gd`
or a manifest re-triggers the codegen (the SCsub lists them as sources) and
recompiles editor_node.cpp. Keeping the real `.gd` files on disk (instead of
raw R"(...)" literals in C++) means they stay lintable / formattable GDScript.
"""

import json
import os

import methods


# Absolute root holding every bundled plugin's subdir. Anchored to THIS file's
# location (editor/glass_bundled_builders.py -> editor/glass/bundled_plugins),
# so it is independent of both SCons's cwd AND the generated target's path. The
# previous code derived plugins_root from dirname(str(target[0])), which only
# resolved correctly because SCsub targets '#editor/glass/...'; computing it
# here decouples the two. Keep in sync with the SCsub Glob that lists the
# .gd/manifest sources for incremental rebuilds.
_EDITOR_DIR = os.path.dirname(os.path.abspath(__file__))  # .../editor
BUNDLED_PLUGINS_ROOT = os.path.join(_EDITOR_DIR, "glass", "bundled_plugins")

# Each bundled plugin lives in its own subdir of BUNDLED_PLUGINS_ROOT with a
# manifest.json. Add new plugins here; the generator concatenates their tables
# into one header.
BUNDLED_PLUGIN_DIRS = [
    "cutscene_camera",
]


def _c_ident(plugin: str, idx: int) -> str:
    """Stable C identifier for a source's compressed-byte array."""
    return f"_glass_bundled_{plugin}_src_{idx}_compressed"


def _escape_c_string(value: str) -> str:
    """Escape a Python string for use as a C string literal."""
    return value.replace("\\", "\\\\").replace('"', '\\"')


def glass_bundled_plugins_builder(target, source, env):
    # Resolved from the module-level constant, NOT from dirname(target[0]) — the
    # generated header's location and the plugin sources' location are decoupled.
    plugins_root = BUNDLED_PLUGINS_ROOT

    # (plugin, idx, class_name, base, is_tool, is_plugin, comp_size, decomp_size, ident)
    table_entries = []
    array_blocks = []

    for plugin in BUNDLED_PLUGIN_DIRS:
        plugin_dir = os.path.join(plugins_root, plugin)
        manifest_path = os.path.join(plugin_dir, "manifest.json")
        with open(manifest_path, "r", encoding="utf-8") as mf:
            manifest = json.load(mf)

        scripts = manifest.get("scripts", [])
        if not scripts:
            methods.print_warning(f"[Glass] bundled plugin '{plugin}' has no scripts; skipping.")
            continue

        plugin_had_entry = False
        for idx, entry in enumerate(scripts):
            gd_path = os.path.join(plugin_dir, entry["file"])
            buffer = methods.get_buffer(gd_path)
            decomp_size = len(buffer)
            comp = methods.compress_buffer(buffer)
            ident = _c_ident(plugin, idx)

            array_blocks.append(
                f"static const unsigned char {ident}[] = {{\n"
                f"{methods.format_buffer(comp, 1)}\n"
                f"}};\n"
            )

            is_plugin = bool(entry.get("is_plugin", False))
            plugin_had_entry = plugin_had_entry or is_plugin
            table_entries.append(
                {
                    "plugin": plugin,
                    "class_name": entry.get("class_name", ""),
                    "base": entry["base"],
                    "is_tool": bool(entry.get("is_tool", True)),
                    "is_plugin": is_plugin,
                    "comp_size": len(comp),
                    "decomp_size": decomp_size,
                    "ident": ident,
                }
            )

        if not plugin_had_entry:
            methods.print_warning(
                f"[Glass] bundled plugin '{plugin}' has no is_plugin entry; it will compile helpers but add no EditorPlugin."
            )

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(
            """\
// Engine-bundled GDScript editor plugins, embedded as zlib byte arrays.
// Consumed by EditorNode::_load_glass_bundled_plugins(). Each source is
// decompressed (Compression::MODE_DEFLATE) and compiled IN TABLE ORDER via
// the bundled-script compiler; the single is_plugin row is instanced and
// add_editor_plugin'd.

struct GlassBundledScript {
\tconst char *plugin; // Which bundled plugin this source belongs to.
\tconst char *class_name; // GDScript class_name to register (empty for the anonymous EditorPlugin).
\tconst char *base; // Native base type passed to the bundled-script compiler.
\tbool is_tool; // All bundled editor sources are @tool.
\tbool is_plugin; // The single EditorPlugin entrypoint per plugin (instanced + add_editor_plugin'd).
\tint compressed_size;
\tint uncompressed_size;
\tconst unsigned char *data;
};

"""
        )

        for block in array_blocks:
            file.write(block)
            file.write("\n")

        file.write("static const GlassBundledScript _glass_bundled_scripts[] = {\n")
        for e in table_entries:
            file.write(
                '\t{{ "{plugin}", "{class_name}", "{base}", {is_tool}, {is_plugin}, {comp}, {decomp}, {ident} }},\n'.format(
                    plugin=_escape_c_string(e["plugin"]),
                    class_name=_escape_c_string(e["class_name"]),
                    base=_escape_c_string(e["base"]),
                    is_tool="true" if e["is_tool"] else "false",
                    is_plugin="true" if e["is_plugin"] else "false",
                    comp=e["comp_size"],
                    decomp=e["decomp_size"],
                    ident=e["ident"],
                )
            )
        file.write("\t{ nullptr, nullptr, nullptr, false, false, 0, 0, nullptr },\n")
        file.write("};\n")
