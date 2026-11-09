// The tag rewrite: read the file's tags, apply the configured statistics, write
// them back. play_count is decimal; the others are local "YYYY-MM-DD HH:MM:SS".
// first_played is stamped once, last_played and play_stamp are replaced.

#include "statistics.h"
#include "config.h"
#include "db.h"

#include <ctime>

namespace psc {
namespace stats {

namespace {

pfc::string8 format_system_time(const SYSTEMTIME & value) {
    pfc::string8 out;
    out << pfc::format_uint(value.wYear, 4) << "-" << pfc::format_uint(value.wMonth, 2) << "-"
        << pfc::format_uint(value.wDay, 2) << " " << pfc::format_uint(value.wHour, 2) << ":"
        << pfc::format_uint(value.wMinute, 2) << ":" << pfc::format_uint(value.wSecond, 2);
    return out;
}

// First value of a metadata field, if the field exists at all.
bool first_value(const file_info & info, const pfc::string8 & name, pfc::string8 & out) {
    if (name.is_empty() || !info.meta_exists(name)) return false;
    const char * value = info.meta_get(name, 0);
    if (value == nullptr) return false;
    out = value;
    return true;
}

unsigned long long text_to_count(const char * raw) {
    if (raw == nullptr || *raw == 0) return 0;
    return pfc::atoui64_ex(raw, SIZE_MAX);
}

// One pass over a file's tags from the snapshot only: this runs on the tag
// updater thread.
bool apply_statistics(file_info & info, const settings & cfg, const pfc::string8 & now) {
    bool changed = false;

    if (cfg.write_play_counter && !cfg.tag_play_counter.is_empty()) {
        pfc::string8 value;
        const unsigned long long count =
            first_value(info, cfg.tag_play_counter, value) ? text_to_count(value.get_ptr()) : 0;
        // The original tested "existing > 0x7ffffffe": 0x7fffffff was the
        // largest count written, and anything above blanked the tag.
        if (count >= 0x7fffffffull) {
            // A blanked field: a removal, not a zero-length value.
            info.meta_remove_field(cfg.tag_play_counter);
        } else {
            pfc::string8 text;
            text << pfc::format_uint(count + 1);
            info.meta_set(cfg.tag_play_counter, text);
        }
        changed = true;
    }

    if (cfg.write_first_played && !cfg.tag_first_played.is_empty()) {
        if (!info.meta_exists(cfg.tag_first_played)) {
            info.meta_set(cfg.tag_first_played, now);
            changed = true;
        }
    }
    if (cfg.write_last_played && !cfg.tag_last_played.is_empty()) {
        info.meta_set(cfg.tag_last_played, now);
        changed = true;
    }
    if (cfg.write_play_stamp && !cfg.tag_play_stamp.is_empty()) {
        info.meta_set(cfg.tag_play_stamp, now);
        changed = true;
    }

    return changed;
}

} // namespace

settings settings::current() {
    settings cfg;
    cfg.write_play_counter = config::write_play_counter();
    cfg.write_first_played = config::write_first_played();
    cfg.write_last_played = config::write_last_played();
    cfg.write_play_stamp = config::write_play_stamp();

    cfg.tag_play_counter = config::tag_play_counter();
    cfg.tag_first_played = config::tag_first_played();
    cfg.tag_last_played = config::tag_last_played();
    cfg.tag_play_stamp = config::tag_play_stamp();
    return cfg;
}

pfc::string8 format_local_time(long long unix_seconds) {
    if (unix_seconds <= 0) return pfc::string8();

    const time_t value = static_cast<time_t>(unix_seconds);
    tm local = {};
    if (localtime_s(&local, &value) != 0) return pfc::string8();

    SYSTEMTIME parts = {};
    parts.wYear = static_cast<WORD>(local.tm_year + 1900);
    parts.wMonth = static_cast<WORD>(local.tm_mon + 1);
    parts.wDay = static_cast<WORD>(local.tm_mday);
    parts.wHour = static_cast<WORD>(local.tm_hour);
    parts.wMinute = static_cast<WORD>(local.tm_min);
    parts.wSecond = static_cast<WORD>(local.tm_sec);
    return format_system_time(parts);
}

pfc::string8 format_now() {
    return format_local_time(static_cast<long long>(::time(nullptr)));
}

void update_track(metadb_handle_ptr track) {
    if (track.is_empty()) return;

    const config::storage_mode storage = config::storage();
    if (storage == config::storage_mode::database) {
        db::record_play(track);
        return;
    }

    const settings cfg = settings::current();
    if (!cfg.write_play_counter && !cfg.write_first_played && !cfg.write_last_played &&
        !cfg.write_play_stamp) {
        return;
    }

    const pfc::string8 now = format_now();

    // "tags and database": the database is the safety net for files that cannot be
    // tagged, so a play reaches it only when the tag update fails - never twice.
    service_ptr_t<completion_notify> notify;
    if (storage == config::storage_mode::database_fallback) {
        notify = fb2k::makeCompletionNotify([track](unsigned status) {
            if (status != metadb_io::update_info_success) db::record_play(track);
        });
    }

    // The filter only ever sees this one track, and carries both the timestamp
    // and the settings, so apply_filter() reads nothing shared.
    service_ptr_t<file_info_filter> filter = file_info_filter::create(
        [now, cfg](trackRef, t_filestats, file_info & info) { return apply_statistics(info, cfg, now); });

    metadb_handle_list tracks;
    tracks.add_item(track);

    // Delay the progress dialog like the original did: short updates should not
    // flash UI, but a slow disk still shows what it is doing.
    metadb_io_v2::get()->update_info_async(tracks, filter, nullptr, metadb_io_v2::op_flag_delay_ui, notify);
}

} // namespace stats
} // namespace psc
