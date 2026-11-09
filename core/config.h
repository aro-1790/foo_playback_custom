#pragma once

// Settings: when a play updates, what it writes, and where it is stored.

#include <SDK/foobar2000.h>

namespace psc {
namespace config {

// When a play is recorded. Values match the original's config integers.
enum class update_mode : int {
    disabled = 0,
    on_track_start = 1,
    on_track_end = 2,
    on_percentage = 3,
    on_seconds = 4,
};

// Where a play is recorded. The values are the original's combo box order.
enum class storage_mode : int {
    tags = 0,               // file tags, under the names below
    database = 1,           // <profile>/playback_stats.db, through the player's sqlite3.dll
    database_fallback = 2,  // tags, and the database only when writing the tag fails
};

// The original's defaults, except database and the media-library filter. Stored
// values win, so this reaches fresh profiles and Reset only.
constexpr int default_percentage = 50;
constexpr int default_seconds = 60;

constexpr update_mode default_update_timing = update_mode::on_seconds;
constexpr storage_mode default_storage = storage_mode::database;
constexpr bool default_only_media_library = true;
constexpr bool default_write_play_counter = true;
constexpr bool default_write_first_played = true;
constexpr bool default_write_last_played = true;
constexpr bool default_write_play_stamp = false;

// Lowercase is only the spelling a fresh profile and Reset show; a stored name
// wins, and tag names are matched case-insensitively anyway.
constexpr const char * default_tag_play_counter = "play_count";
constexpr const char * default_tag_first_played = "first_played";
constexpr const char * default_tag_last_played = "last_played";
constexpr const char * default_tag_play_stamp = "play_stamp";

update_mode update_timing();
int update_percentage();  // 1..99
int update_seconds();     // 1..
bool only_media_library();

storage_mode storage();

bool write_play_counter();
bool write_first_played();
bool write_last_played();
bool write_play_stamp();

pfc::string8 tag_play_counter();
pfc::string8 tag_first_played();
pfc::string8 tag_last_played();
pfc::string8 tag_play_stamp();

void set_update_timing(update_mode value);
void set_update_percentage(int value);
void set_update_seconds(int value);
void set_only_media_library(bool value);

void set_storage(storage_mode value);

void set_write_play_counter(bool value);
void set_write_first_played(bool value);
void set_write_last_played(bool value);
void set_write_play_stamp(bool value);

void set_tag_play_counter(const char * value);
void set_tag_first_played(const char * value);
void set_tag_last_played(const char * value);
void set_tag_play_stamp(const char * value);

} // namespace config
} // namespace psc
