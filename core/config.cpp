// Values are clamped on read, so a corrupt config cannot produce a bad mode.

#include "config.h"

namespace psc {
namespace config {

namespace {

// The original's GUIDs, which are the whole legacy migration: the SDK imports a
// leftover <component>.dll.cfg into config.sqlite by GUID.
const GUID guid_update_timing = { 0xf0a9738d, 0x7fe6, 0x4460, { 0xb0, 0xd4, 0x12, 0x61, 0x86, 0x2b, 0x6e, 0x6d } };
const GUID guid_update_percentage = { 0xa850721f, 0x568f, 0x4602, { 0x83, 0x53, 0xf0, 0x90, 0xee, 0xca, 0x08, 0x94 } };
const GUID guid_update_seconds = { 0x978ec4f8, 0x7e91, 0x4524, { 0x96, 0x20, 0x0f, 0xf3, 0x64, 0x67, 0xab, 0xc5 } };
const GUID guid_only_media_library = { 0xc1ecfb59, 0xdd36, 0x46d5, { 0x83, 0xff, 0x64, 0x07, 0x00, 0x41, 0xc3, 0x20 } };
const GUID guid_storage = { 0x38a06876, 0xeafd, 0x475a, { 0xb4, 0xa9, 0xe7, 0x17, 0xa8, 0x22, 0x6b, 0xd8 } };
const GUID guid_write_play_counter = { 0x69f81726, 0x6ebb, 0x4cbb, { 0xbf, 0x67, 0xec, 0xbf, 0x50, 0xbd, 0x6a, 0x5d } };
const GUID guid_write_first_played = { 0x2e5ebc31, 0xcc21, 0x47e0, { 0x91, 0x83, 0x55, 0xbf, 0xfc, 0xde, 0x17, 0x88 } };
const GUID guid_write_last_played = { 0x43863868, 0x50b2, 0x4616, { 0xbb, 0xdd, 0x25, 0x96, 0xd1, 0xbd, 0x47, 0xe9 } };
const GUID guid_write_play_stamp = { 0xc042bdb0, 0x6374, 0x4c2b, { 0x9f, 0x94, 0xb6, 0x20, 0x02, 0xe3, 0x36, 0xbc } };
const GUID guid_tag_play_counter = { 0x30bd004e, 0x2141, 0x4f6d, { 0xa9, 0x1c, 0x1a, 0xf1, 0x4f, 0x26, 0x5c, 0x0c } };
const GUID guid_tag_first_played = { 0x28c95f7c, 0x9c30, 0x4acb, { 0x9c, 0x49, 0x77, 0x9d, 0x4f, 0x0b, 0xbc, 0xbb } };
const GUID guid_tag_last_played = { 0xd2ab912f, 0x4f35, 0x45d3, { 0x90, 0xae, 0x4b, 0xd5, 0xb2, 0x62, 0x32, 0x67 } };
const GUID guid_tag_play_stamp = { 0xef372b70, 0x4c3f, 0x4416, { 0x9a, 0xae, 0xda, 0x31, 0xb5, 0xe1, 0xe7, 0x9b } };

cfg_var_modern::cfg_int g_update_timing(guid_update_timing, static_cast<int64_t>(default_update_timing));
cfg_var_modern::cfg_int g_update_percentage(guid_update_percentage, default_percentage);
cfg_var_modern::cfg_int g_update_seconds(guid_update_seconds, default_seconds);
cfg_var_modern::cfg_bool g_only_media_library(guid_only_media_library, default_only_media_library);
cfg_var_modern::cfg_int g_storage(guid_storage, static_cast<int64_t>(default_storage));
cfg_var_modern::cfg_bool g_write_play_counter(guid_write_play_counter, default_write_play_counter);
cfg_var_modern::cfg_bool g_write_first_played(guid_write_first_played, default_write_first_played);
cfg_var_modern::cfg_bool g_write_last_played(guid_write_last_played, default_write_last_played);
cfg_var_modern::cfg_bool g_write_play_stamp(guid_write_play_stamp, default_write_play_stamp);
cfg_var_modern::cfg_string g_tag_play_counter(guid_tag_play_counter, default_tag_play_counter);
cfg_var_modern::cfg_string g_tag_first_played(guid_tag_first_played, default_tag_first_played);
cfg_var_modern::cfg_string g_tag_last_played(guid_tag_last_played, default_tag_last_played);
cfg_var_modern::cfg_string g_tag_play_stamp(guid_tag_play_stamp, default_tag_play_stamp);

pfc::string8 tag_value(cfg_var_modern::cfg_string & var, const char * fallback) {
    pfc::string8 value = var.get();
    if (value.is_empty()) value = fallback;
    return value;
}

void set_tag_value(cfg_var_modern::cfg_string & var, const char * value, const char * fallback) {
    var = (value != nullptr && *value != 0) ? value : fallback;
}

} // namespace

update_mode update_timing() {
    const int64_t value = g_update_timing.get();
    if (value < static_cast<int64_t>(update_mode::disabled) || value > static_cast<int64_t>(update_mode::on_seconds)) {
        return update_mode::on_seconds;
    }
    return static_cast<update_mode>(value);
}

int update_percentage() {
    const int64_t value = g_update_percentage.get();
    if (value < 1) return 1;
    if (value > 99) return 99;
    return static_cast<int>(value);
}

int update_seconds() {
    const int64_t value = g_update_seconds.get();
    return value < 1 ? 1 : static_cast<int>(value);
}

bool only_media_library() { return g_only_media_library.get(); }

storage_mode storage() {
    const int64_t value = g_storage.get();
    if (value < static_cast<int64_t>(storage_mode::tags) || value > static_cast<int64_t>(storage_mode::database_fallback)) {
        // A corrupt entry must not move someone into the database: tags are the
        // mode that cannot lose a play, so they are the safe read.
        return storage_mode::tags;
    }
    return static_cast<storage_mode>(value);
}

bool write_play_counter() { return g_write_play_counter.get(); }
bool write_first_played() { return g_write_first_played.get(); }
bool write_last_played() { return g_write_last_played.get(); }
bool write_play_stamp() { return g_write_play_stamp.get(); }

pfc::string8 tag_play_counter() { return tag_value(g_tag_play_counter, default_tag_play_counter); }
pfc::string8 tag_first_played() { return tag_value(g_tag_first_played, default_tag_first_played); }
pfc::string8 tag_last_played() { return tag_value(g_tag_last_played, default_tag_last_played); }
pfc::string8 tag_play_stamp() { return tag_value(g_tag_play_stamp, default_tag_play_stamp); }

void set_update_timing(update_mode value) { g_update_timing = static_cast<int64_t>(value); }
void set_update_percentage(int value) {
    if (value < 1) value = 1;
    if (value > 99) value = 99;
    g_update_percentage = value;
}
void set_update_seconds(int value) { g_update_seconds = value < 1 ? 1 : value; }
void set_only_media_library(bool value) { g_only_media_library = value; }
void set_storage(storage_mode value) { g_storage = static_cast<int64_t>(value); }

void set_write_play_counter(bool value) { g_write_play_counter = value; }
void set_write_first_played(bool value) { g_write_first_played = value; }
void set_write_last_played(bool value) { g_write_last_played = value; }
void set_write_play_stamp(bool value) { g_write_play_stamp = value; }

void set_tag_play_counter(const char * value) { set_tag_value(g_tag_play_counter, value, default_tag_play_counter); }
void set_tag_first_played(const char * value) { set_tag_value(g_tag_first_played, value, default_tag_first_played); }
void set_tag_last_played(const char * value) { set_tag_value(g_tag_last_played, value, default_tag_last_played); }
void set_tag_play_stamp(const char * value) { set_tag_value(g_tag_play_stamp, value, default_tag_play_stamp); }

} // namespace config
} // namespace psc
