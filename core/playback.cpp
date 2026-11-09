// The original's CPlayback state machine: snapshot the settings and arm a
// countdown on the new track, decrement it from on_playback_time(), record at
// zero, flush a pending track when playback moves on. The media-library filter is
// applied while arming, and a natural end of stream credits every waiting mode.

#include "config.h"
#include "statistics.h"

namespace psc {
namespace {

class playback_callback : public play_callback_static {
public:
    unsigned get_flags() override {
        return flag_on_playback_new_track | flag_on_playback_stop | flag_on_playback_time | flag_on_playback_seek;
    }

    void on_playback_starting(play_control::t_track_command, bool) override {}

    void on_playback_new_track(metadb_handle_ptr track) override {
        // A track replaced before its update fired still played; the original
        // flushed it for the modes that do not record at start.
        flush_pending();
        arm(track);
    }

    void on_playback_stop(play_control::t_stop_reason reason) override {
        // stop_reason 1 is stop_reason_eof in both enums: only a natural end of
        // stream credits a track that never reached its threshold.
        if (reason == play_control::stop_reason_eof && records_late(m_mode) && !m_track.is_empty() &&
            !m_recorded && !m_skipped) {
            record();
        }
        reset();
    }

    void on_playback_seek(double) override {
        // Modes that measure elapsed playback mark a scrubbed play, so leaving
        // it credits nothing. The countdown keeps running, as it did there.
        if (measures_elapsed(m_mode)) m_skipped = true;
    }

    void on_playback_pause(bool) override {}
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info &) override {}
    void on_playback_dynamic_info_track(const file_info &) override {}

    void on_playback_time(double) override {
        if (m_track.is_empty() || m_recorded) return;
        if (!measures_elapsed(m_mode)) return;

        if (m_remaining > 0) --m_remaining;
        if (m_remaining <= 0) record();
    }

    void on_volume_change(float) override {}

private:
    // Modes that wait for a threshold instead of recording at a track boundary.
    static bool measures_elapsed(config::update_mode mode) {
        return mode == config::update_mode::on_percentage || mode == config::update_mode::on_seconds;
    }

    // Modes whose pending play is recorded by a later event.
    static bool records_late(config::update_mode mode) {
        return mode == config::update_mode::on_track_end || measures_elapsed(mode);
    }

    void arm(metadb_handle_ptr track) {
        reset();
        if (track.is_empty()) return;

        const config::update_mode mode = config::update_timing();

        // Part of arming, not of recording: the original forced the mode to
        // "disabled" for the whole track.
        if (config::only_media_library() && !library_manager::get()->is_item_in_library(track)) return;

        m_track = track;
        m_mode = mode;

        switch (mode) {
        case config::update_mode::disabled:
            reset();
            break;
        case config::update_mode::on_track_start:
            record();
            break;
        case config::update_mode::on_track_end:
            // Recorded by on_playback_stop().
            break;
        case config::update_mode::on_percentage: {
            const double length = track->get_length();
            const double at = length > 0 ? length * config::update_percentage() / 100.0 : 0.0;
            // Half up, like the original; an unknown length waits one tick.
            const int threshold = static_cast<int>(at + 0.5);
            m_remaining = threshold < 1 ? 1 : threshold;
            break;
        }
        case config::update_mode::on_seconds:
            // The first time tick lands at ~1s, so the original waited a
            // second longer than the configured value.
            m_remaining = config::update_seconds() + 1;
            break;
        }
    }

    void flush_pending() {
        if (m_track.is_empty() || m_recorded || m_skipped) return;
        if (records_late(m_mode)) record();
    }

    void record() {
        if (m_recorded || m_track.is_empty()) return;
        m_recorded = true;
        stats::update_track(m_track);
    }

    void reset() {
        m_track.release();
        m_mode = config::update_mode::disabled;
        m_recorded = false;
        m_skipped = false;
        m_remaining = 0;
    }

    metadb_handle_ptr m_track;
    // The mode this track was armed with, as the original snapshotted it.
    config::update_mode m_mode = config::update_mode::disabled;
    int m_remaining = 0;
    bool m_recorded = false;
    bool m_skipped = false;
};

play_callback_static_factory_t<playback_callback> g_playback;

} // namespace
} // namespace psc
