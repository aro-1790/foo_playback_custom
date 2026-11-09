#pragma once

// Tag writes through a file_info_filter (the original's CFileInfoData): the
// filter sees the file's own tags, so the counter increments what is stored.

#include <SDK/foobar2000.h>
#include <SDK/file_info_filter.h>

namespace psc {
namespace stats {

// Copied before the update is queued: the filter runs on the tag updater thread.
struct settings {
    bool write_play_counter = false;
    bool write_first_played = false;
    bool write_last_played = false;
    bool write_play_stamp = false;

    pfc::string8 tag_play_counter;
    pfc::string8 tag_first_played;
    pfc::string8 tag_last_played;
    pfc::string8 tag_play_stamp;

    // Reads the live configuration. Main thread only, which is where
    // update_track() is called from.
    static settings current();
};

// Local time, formatted the way the original wrote it: "YYYY-MM-DD HH:MM:SS".
pfc::string8 format_now();

// The same text for a unix timestamp, so the columns and the tags agree.
pfc::string8 format_local_time(long long unix_seconds);

// Queues a tag update for one track. Main thread only (metadb_io_v2 rule).
void update_track(metadb_handle_ptr track);

} // namespace stats
} // namespace psc
