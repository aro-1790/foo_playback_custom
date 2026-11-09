#pragma once

// <profile>/playback_stats.db, keyed by (path, subsong). sqlite3.dll is resolved
// at runtime, and the file exists only once a play has been stored in it.

#include <SDK/foobar2000.h>

namespace psc {
namespace db {

// Name of the database file in the profile folder.
constexpr const char * file_name = "playback_stats.db";

// One row of the statistics table: times in unix seconds, plus the local-time
// text the tag writer would have put in play_stamp.
struct stored {
    long long play_count = 0;
    long long first_played = 0;
    long long last_played = 0;
    pfc::string8 play_stamp;
};

// True when sqlite3.dll loaded and every entry point was found. Probes once.
bool available();

// Looks a track up in the in-memory copy, so the display providers never wait
// for the worker's connection. Safe from any thread.
bool lookup(const playable_location & location, stored & out);

// Reads an existing database into memory on the worker. Never creates the file.
void preload();

// Queues one play for the worker. Main thread only; does no file I/O itself.
void record_play(metadb_handle_ptr track);

// Queues a sweep deleting the rows whose file is gone, then reports in a message
// box. Main thread only. Archives, streams, unplugged drives and denied folders
// are kept: none of those say the track was deleted. `outside_library` also drops
// rows the media library does not list; the library is read here.
void prune_missing(bool outside_library);

// Queues a wipe of the table: every row, whatever its file. Main thread only.
// File tags are untouched.
void clear_all();

// Stops the worker and closes the connection, so the DLL can be unloaded.
void shutdown();

} // namespace db
} // namespace psc
