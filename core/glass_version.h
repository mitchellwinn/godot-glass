/**************************************************************************/
/*  glass_version.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT GLASS                               */
/*              (a Godot Engine fork — https://godotengine.org)           */
/**************************************************************************/

#pragma once

// Glass build identity used by the Project Manager auto-update check.
//
// CI stamps `core/glass_version.gen.h` from the pushed release tag before
// building (see misc/scripts/glass_stamp_version.py, invoked by
// .github/workflows/glass-build.yml). That gen header is gitignored, so local
// and dev builds have none — they fall back to the sentinel below, which the
// updater treats as "dev build, don't nag".
#if defined(__has_include)
#if __has_include("core/glass_version.gen.h")
#include "core/glass_version.gen.h"
#endif
#endif

#ifndef GLASS_VERSION_TAG
// Sentinel for un-stamped (local/dev) builds. Parses to 0.0.0 → no update nag.
#define GLASS_VERSION_TAG "v0.0.0-dev"
#endif

// Where the updater looks for releases, and where "click to download" opens.
#define GLASS_RELEASES_API "https://api.github.com/repos/mitchellwinn/godot-glass/releases/latest"
#define GLASS_RELEASES_PAGE "https://github.com/mitchellwinn/godot-glass/releases/latest"
