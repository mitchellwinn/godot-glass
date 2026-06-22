/**************************************************************************/
/*  engine_update_label.cpp                                               */
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

#include "engine_update_label.h"

#include "core/glass_version.h"
#include "core/io/json.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/version.h"
#include "editor/editor_string_names.h"
#include "editor/settings/editor_settings.h"
#include "scene/main/http_request.h"

bool EngineUpdateLabel::_can_check_updates() const {
	return int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_ONLINE &&
			UpdateMode(int(EDITOR_GET("network/connection/check_for_updates"))) != UpdateMode::DISABLED;
}

void EngineUpdateLabel::_check_update() {
	checked_update = true;
	_set_status(UpdateStatus::BUSY);
	// Glass checks its own GitHub releases, not godotengine.org. The GitHub API
	// rejects requests without a User-Agent, so set one explicitly.
	PackedStringArray headers;
	headers.push_back("User-Agent: godot-glass-updater");
	headers.push_back("Accept: application/vnd.github+json");
	http->request(GLASS_RELEASES_API, headers);
}

void EngineUpdateLabel::_parse_glass_tag(const String &p_tag, int &r_major, int &r_minor, int &r_patch) {
	r_major = 0;
	r_minor = 0;
	r_patch = 0;

	String s = p_tag.strip_edges();
	if (s.begins_with("v") || s.begins_with("V")) {
		s = s.substr(1);
	}
	// Drop any pre-release/build suffix ("0.1.5-rc1" -> "0.1.5").
	const int dash = s.find_char('-');
	if (dash != -1) {
		s = s.substr(0, dash);
	}

	const PackedStringArray bits = s.split(".");
	if (bits.size() > 0) {
		r_major = bits[0].to_int();
	}
	if (bits.size() > 1) {
		r_minor = bits[1].to_int();
	}
	if (bits.size() > 2) {
		r_patch = bits[2].to_int();
	}
}

void EngineUpdateLabel::_http_request_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (p_result != OK) {
		_set_status(UpdateStatus::ERROR);
		_set_message(vformat(TTR("Failed to check for updates. Error: %d."), p_result), theme_cache.error_color);
		return;
	}

	if (p_response_code != 200) {
		_set_status(UpdateStatus::ERROR);
		_set_message(vformat(TTR("Failed to check for updates. Response code: %d."), p_response_code), theme_cache.error_color);
		return;
	}

	// GitHub's /releases/latest returns a single release object.
	const String s = String::utf8((const char *)p_body.ptr(), p_body.size());
	const Variant result = JSON::parse_string(s);
	if (result.get_type() != Variant::DICTIONARY) {
		_set_status(UpdateStatus::ERROR);
		_set_message(TTR("Failed to parse release JSON."), theme_cache.error_color);
		return;
	}

	const Dictionary release = result;
	const String latest_tag = String(release.get("tag_name", "")).strip_edges();
	if (latest_tag.is_empty()) {
		// No published release to compare against — treat as up to date.
		_set_status(UpdateStatus::UP_TO_DATE);
		return;
	}
	release_page_url = release.get("html_url", String(GLASS_RELEASES_PAGE));

	int cur_major, cur_minor, cur_patch;
	_parse_glass_tag(GLASS_VERSION_TAG, cur_major, cur_minor, cur_patch);

	// Un-stamped local/dev builds (sentinel parses to 0.0.0) carry no real
	// version — don't nag them about every release.
	if (cur_major == 0 && cur_minor == 0 && cur_patch == 0) {
		_set_status(UpdateStatus::UP_TO_DATE);
		return;
	}

	int new_major, new_minor, new_patch;
	_parse_glass_tag(latest_tag, new_major, new_minor, new_patch);

	const bool newer = new_major > cur_major ||
			(new_major == cur_major && new_minor > cur_minor) ||
			(new_major == cur_major && new_minor == cur_minor && new_patch > cur_patch);

	if (newer) {
		available_newer_version = latest_tag;
		_set_status(UpdateStatus::UPDATE_AVAILABLE);
		_set_message(vformat(TTR("Glass update available: %s."), latest_tag), theme_cache.update_color);
	} else {
		_set_status(UpdateStatus::UP_TO_DATE);
	}
}

void EngineUpdateLabel::_set_message(const String &p_message, const Color &p_color) {
	if (is_disabled()) {
		add_theme_color_override("font_disabled_color", p_color);
	} else {
		add_theme_color_override(SceneStringName(font_color), p_color);
	}
	set_text(p_message);
}

void EngineUpdateLabel::_set_status(UpdateStatus p_status) {
	status = p_status;
	if (status == UpdateStatus::BUSY || status == UpdateStatus::UP_TO_DATE) {
		// Hide the label to prevent unnecessary distraction.
		hide();
		return;
	} else {
		show();
	}

	switch (status) {
		case UpdateStatus::OFFLINE: {
			set_disabled(false);
			if (int(EDITOR_GET("network/connection/network_mode")) == EditorSettings::NETWORK_OFFLINE) {
				_set_message(TTR("Offline mode, update checks disabled."), theme_cache.disabled_color);
			} else {
				_set_message(TTR("Update checks disabled."), theme_cache.disabled_color);
			}
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_OFF);
			set_tooltip_text("");
			break;
		}

		case UpdateStatus::ERROR: {
			set_disabled(false);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text(TTR("An error has occurred. Click to try again."));
		} break;

		case UpdateStatus::UPDATE_AVAILABLE: {
			set_disabled(false);
			set_accessibility_live(AccessibilityServerEnums::AccessibilityLiveMode::LIVE_POLITE);
			set_tooltip_text(TTR("Click to open download page."));
		} break;

		default: {
		}
	}
}

EngineUpdateLabel::VersionType EngineUpdateLabel::_get_version_type(const String &p_string, int *r_index) const {
	VersionType type = VersionType::UNKNOWN;
	String index_string;

	static HashMap<String, VersionType> type_map;
	if (type_map.is_empty()) {
		type_map["stable"] = VersionType::STABLE;
		type_map["rc"] = VersionType::RC;
		type_map["beta"] = VersionType::BETA;
		type_map["alpha"] = VersionType::ALPHA;
		type_map["dev"] = VersionType::DEV;
	}

	for (const KeyValue<String, VersionType> &kv : type_map) {
		if (p_string.begins_with(kv.key)) {
			index_string = p_string.trim_prefix(kv.key);
			type = kv.value;
			break;
		}
	}

	if (r_index) {
		if (index_string.is_empty()) {
			*r_index = DEV_VERSION;
		} else {
			*r_index = index_string.to_int();
		}
	}
	return type;
}

String EngineUpdateLabel::_extract_sub_string(const String &p_line) const {
	int j = p_line.find_char('"') + 1;
	return p_line.substr(j, p_line.find_char('"', j) - j);
}

void EngineUpdateLabel::_notification(int p_what) {
	switch (p_what) {
		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (!EditorSettings::get_singleton()->check_changed_settings_in_group("network/connection")) {
				break;
			}

			if (_can_check_updates()) {
				_check_update();
			} else {
				_set_status(UpdateStatus::OFFLINE);
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			theme_cache.default_color = get_theme_color(SceneStringName(font_color), "Button");
			theme_cache.disabled_color = get_theme_color("font_disabled_color", "Button");
			theme_cache.error_color = get_theme_color("error_color", EditorStringName(Editor));
			theme_cache.update_color = get_theme_color("warning_color", EditorStringName(Editor));
		} break;

		case NOTIFICATION_READY: {
			if (_can_check_updates()) {
				_check_update();
			} else {
				_set_status(UpdateStatus::OFFLINE);
			}
		} break;
	}
}

void EngineUpdateLabel::_bind_methods() {
	ADD_SIGNAL(MethodInfo("offline_clicked"));
}

void EngineUpdateLabel::pressed() {
	switch (status) {
		case UpdateStatus::OFFLINE: {
			emit_signal("offline_clicked");
		} break;

		case UpdateStatus::ERROR: {
			_check_update();
		} break;

		case UpdateStatus::UPDATE_AVAILABLE: {
			OS::get_singleton()->shell_open(release_page_url.is_empty() ? String(GLASS_RELEASES_PAGE) : release_page_url);
		} break;

		default: {
		}
	}
}

EngineUpdateLabel::EngineUpdateLabel() {
	set_underline_mode(UNDERLINE_MODE_ON_HOVER);

	http = memnew(HTTPRequest);
	http->set_https_proxy(EDITOR_GET("network/http_proxy/host"), EDITOR_GET("network/http_proxy/port"));
	http->set_timeout(10.0);
	add_child(http);
	http->connect("request_completed", callable_mp(this, &EngineUpdateLabel::_http_request_completed));
}
