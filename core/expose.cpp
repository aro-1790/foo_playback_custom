// The four statistics as title-formatting fields and as a Properties group, read
// from the in-memory copy of the table. A real tag wins over the database.

#include "config.h"
#include "db.h"
#include "statistics.h"

#include <SDK/foobar2000.h>
#include <SDK/track_property.h>

namespace psc {
namespace {

enum field_index : t_uint32 {
    field_play_counter = 0,
    field_first_played,
    field_last_played,
    field_play_stamp,
    field_count,
};

constexpr const char * property_group = "Playback Statistics Custom";

// Database mode answers to the configured tag names, so %fields% resolve alike.
// The display-field backend binds names at startup and uses indexes afterwards,
// so a rename reaches %fields% only on the next start. The Properties group has
// no such binding and builds its own copy per call.
class field_names {
public:
    field_names() {
        set(field_play_counter, config::tag_play_counter(), config::default_tag_play_counter);
        set(field_first_played, config::tag_first_played(), config::default_tag_first_played);
        set(field_last_played, config::tag_last_played(), config::default_tag_last_played);
        set(field_play_stamp, config::tag_play_stamp(), config::default_tag_play_stamp);
    }

    const pfc::string8 & operator[](t_uint32 index) const { return m_names[index]; }

private:
    void set(t_uint32 index, pfc::string8 value, const char * fallback) {
        m_names[index] = value.is_empty() ? pfc::string8(fallback) : value;
    }

    pfc::string8 m_names[field_count];
};

const field_names & names() {
    static const field_names value;
    return value;
}

// A statistic switched off in the page is not shown at all: its column still
// holds a zero or an older value, and a disabled field should read as absent.
// A file tag of the same name still wins - this only gates the database's answer.
bool enabled(t_uint32 index) {
    switch (index) {
    case field_play_counter:
        return config::write_play_counter();
    case field_first_played:
        return config::write_first_played();
    case field_last_played:
        return config::write_last_played();
    case field_play_stamp:
        return config::write_play_stamp();
    default:
        return false;
    }
}

// One field's text from a stored row; false leaves the field empty exactly as its
// tag would be.
bool field_value(t_uint32 index, const db::stored & row, pfc::string8 & out) {
    if (!enabled(index)) return false;

    switch (index) {
    case field_play_counter:
        out << static_cast<t_uint64>(row.play_count);
        return true;
    case field_first_played:
        out = stats::format_local_time(row.first_played);
        return !out.is_empty();
    case field_last_played:
        out = stats::format_local_time(row.last_played);
        return !out.is_empty();
    case field_play_stamp:
        out = row.play_stamp;
        return !out.is_empty();
    default:
        return false;
    }
}

class field_provider : public metadb_display_field_provider_v2 {
public:
    t_uint32 get_field_count() override { return field_count; }

    void get_field_name(t_uint32 index, pfc::string_base & out) override {
        if (index < field_count) out = names()[index];
    }

    bool process_field(t_uint32 index, metadb_handle * handle, titleformat_text_out * out) override {
        return process_field_v2(index, handle, handle->query_v2_(), out);
    }

    bool process_field_v2(t_uint32 index, metadb_handle * handle, metadb_v2::rec_t const & rec,
                          titleformat_text_out * out) override {
        if (index >= field_count) return false;

        const pfc::string8 & name = names()[index];

        // A tag is the tag-mode value, and it wins in the other modes too.
        if (rec.info.is_valid()) {
            const file_info & info = rec.info->info();
            if (info.meta_exists(name.get_ptr())) {
                const char * tag = info.meta_get(name.get_ptr(), 0);
                if (tag == nullptr) return false;
                out->write(titleformat_inputtypes::meta, tag);
                return true;
            }
        }

        // The database only speaks for the modes that store plays there.
        if (config::storage() == config::storage_mode::tags) return false;

        db::stored row;
        if (!db::lookup(handle->get_location(), row)) return false;

        pfc::string8 value;
        if (!field_value(index, row, value)) return false;
        out->write(titleformat_inputtypes::meta, value.get_ptr(), value.length());
        return true;
    }
};

class property_provider : public track_property_provider_v3 {
public:
    bool is_our_tech_info(const char *) override { return false; }

    void enumerate_properties_v3(trackListRef items, track_property_provider_v3_info_source & info,
                                 track_property_callback_v2 & callback) override {
        if (config::storage() == config::storage_mode::tags) return;
        if (!callback.is_group_wanted(property_group)) return;

        // Unlike the display fields, this is called on demand, on the main thread
        // (the track_property_provider_v3 contract), so the names are read now: a
        // rename shows up in the Properties dialog without a restart.
        const field_names current;

        for (t_size i = 0; i < items.get_size(); ++i) {
            const metadb_handle_ptr track = items[i];

            // Same precedence as the fields: the file's own tags win.
            const file_info * meta = nullptr;
            trackInfoContainer::ptr container = info.get_info(i);
            if (container.is_valid()) meta = &container->info();

            db::stored row;
            if (!db::lookup(track->get_location(), row)) continue;

            for (t_uint32 field = 0; field < field_count; ++field) {
                const pfc::string8 & name = current[field];
                if (meta != nullptr && meta->meta_exists(name.get_ptr())) continue;

                pfc::string8 value;
                if (!field_value(field, row, value)) continue;
                callback.set_property(property_group, 0, name.get_ptr(), value.get_ptr());
            }
        }
    }
};

static service_factory_single_t<field_provider> g_field_provider;
static service_factory_single_t<property_provider> g_property_provider;

} // namespace
} // namespace psc
