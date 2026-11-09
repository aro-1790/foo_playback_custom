// Preferences -> Tools -> Playback Statistics Custom: the original's page with
// its foo_custominfo selector replaced by the storage choice. Plain Win32.

#include <SDK/foobar2000.h>
#include <SDK/coreDarkMode.h>
#include <SDK/preferences_page.h>
#include <SDK/service_impl.h>

#include "config.h"
#include "db.h"
#include "resource.h"

namespace psc {
namespace {

const GUID guid_preferences_page = { 0x8f2a1d4c, 0x7b3e, 0x4a96, { 0x9d, 0x21, 0x5c, 0x48, 0x6e, 0xb7, 0x13, 0xa0 } };

bool text_differs(const pfc::string8 & a, const pfc::string8 & b) {
    return strcmp(a.get_ptr(), b.get_ptr()) != 0;
}

class config_page : public preferences_page_instance {
public:
    config_page(HWND parent, preferences_page_callback::ptr callback) : m_callback(callback) {
        m_wnd = ::CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_CONFIG), parent,
                                     &config_page::dialog_proc, reinterpret_cast<LPARAM>(this));
        PFC_ASSERT(m_wnd != nullptr);
    }

    ~config_page() {
        if (m_wnd != nullptr) ::DestroyWindow(m_wnd);
    }

    t_uint32 get_state() override {
        // The host only themes a page that claims dark_mode_supported.
        t_uint32 state = preferences_state::resettable | preferences_state::dark_mode_supported;
        if (m_wnd != nullptr && has_changed()) state |= preferences_state::changed;
        return state;
    }

    fb2k::hwnd_t get_wnd() override { return m_wnd; }

    void apply() override {
        if (m_wnd == nullptr) return;

        config::set_update_timing(static_cast<config::update_mode>(current_timing_mode()));
        config::set_update_percentage(slider_position());
        config::set_update_seconds(static_cast<int>(::GetDlgItemInt(m_wnd, IDC_SECONDS_EDIT, nullptr, FALSE)));
        config::set_only_media_library(is_checked(IDC_ONLY_LIBRARY));
        config::set_storage(storage_from_index(current_storage()));

        config::set_write_play_counter(is_checked(IDC_WRITE_PLAY_COUNTER));
        config::set_write_first_played(is_checked(IDC_WRITE_FIRST_PLAYED));
        config::set_write_last_played(is_checked(IDC_WRITE_LAST_PLAYED));
        config::set_write_play_stamp(is_checked(IDC_WRITE_PLAY_STAMP));

        config::set_tag_play_counter(tag_text(IDC_TAG_PLAY_COUNTER, config::default_tag_play_counter));
        config::set_tag_first_played(tag_text(IDC_TAG_FIRST_PLAYED, config::default_tag_first_played));
        config::set_tag_last_played(tag_text(IDC_TAG_LAST_PLAYED, config::default_tag_last_played));
        config::set_tag_play_stamp(tag_text(IDC_TAG_PLAY_STAMP, config::default_tag_play_stamp));

        // Show what was actually stored: the setters clamp, and leaving the
        // dialog on the typed value would report the page changed again.
        m_initialising = true;
        ::SetDlgItemInt(m_wnd, IDC_SECONDS_EDIT, config::update_seconds(), FALSE);
        ::SendDlgItemMessageW(m_wnd, IDC_PERCENT_SLIDER, TBM_SETPOS, TRUE, config::update_percentage());
        update_percent_text();
        update_enabled_state();
        m_initialising = false;

        m_callback->on_state_changed();
    }

    void reset() override {
        if (m_wnd == nullptr) return;

        m_initialising = true;

        ::CheckRadioButton(m_wnd, IDC_TIMING_DISABLE, IDC_TIMING_SECONDS,
                           timing_control(static_cast<int>(config::default_update_timing)));
        ::SendDlgItemMessageW(m_wnd, IDC_PERCENT_SLIDER, TBM_SETPOS, TRUE, config::default_percentage);
        update_percent_text();
        ::SetDlgItemInt(m_wnd, IDC_SECONDS_EDIT, config::default_seconds, FALSE);

        set_text(IDC_TAG_PLAY_COUNTER, config::default_tag_play_counter);
        set_text(IDC_TAG_FIRST_PLAYED, config::default_tag_first_played);
        set_text(IDC_TAG_LAST_PLAYED, config::default_tag_last_played);
        set_text(IDC_TAG_PLAY_STAMP, config::default_tag_play_stamp);

        set_checked(IDC_WRITE_PLAY_COUNTER, config::default_write_play_counter);
        set_checked(IDC_WRITE_FIRST_PLAYED, config::default_write_first_played);
        set_checked(IDC_WRITE_LAST_PLAYED, config::default_write_last_played);
        set_checked(IDC_WRITE_PLAY_STAMP, config::default_write_play_stamp);

        set_checked(IDC_ONLY_LIBRARY, config::default_only_media_library);

        ::CheckRadioButton(m_wnd, IDC_STORAGE_TAGS, IDC_STORAGE_FALLBACK,
                           storage_control(static_cast<int>(config::default_storage)));

        update_enabled_state();
        m_initialising = false;
        m_callback->on_state_changed();
    }

private:
    static INT_PTR CALLBACK dialog_proc(HWND wnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_INITDIALOG) {
            config_page * self = reinterpret_cast<config_page *>(lparam);
            self->m_wnd = wnd;
            ::SetWindowLongPtrW(wnd, DWLP_USER, reinterpret_cast<LONG_PTR>(self));
            self->on_init();
            // As the SDK's sample page does: the page does not decide where the
            // focus goes, so the host's own choice is left alone.
            return FALSE;
        }

        config_page * self = reinterpret_cast<config_page *>(::GetWindowLongPtrW(wnd, DWLP_USER));

        switch (msg) {
        case WM_COMMAND:
            // A button click is an action, not an edit: it must not mark the page
            // changed, and it does not move the focus.
            if (self != nullptr && HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == IDC_PRUNE_DATABASE) {
                self->on_prune();
                return TRUE;
            }
            if (self != nullptr && HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == IDC_CLEAR_DATABASE) {
                self->on_clear();
                return TRUE;
            }
            // The slider and its readout sit between two of the timing radios, which
            // ends the dialog manager's automatic group: without this, two of them
            // can stay checked. CheckRadioButton works on the ID range instead.
            if (self != nullptr && HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) >= IDC_TIMING_DISABLE &&
                LOWORD(wparam) <= IDC_TIMING_SECONDS) {
                ::CheckRadioButton(wnd, IDC_TIMING_DISABLE, IDC_TIMING_SECONDS, static_cast<int>(LOWORD(wparam)));
            }
            if (self != nullptr && (HIWORD(wparam) == BN_CLICKED || HIWORD(wparam) == EN_CHANGE)) {
                self->update_enabled_state();
                self->on_changed();
            }
            return FALSE;
        case WM_HSCROLL:
            if (self != nullptr && reinterpret_cast<HWND>(lparam) == ::GetDlgItem(wnd, IDC_PERCENT_SLIDER)) {
                self->update_percent_text();
                self->on_changed();
            }
            return FALSE;
        case WM_NCDESTROY:
            if (self != nullptr) {
                self->m_wnd = nullptr;
                ::SetWindowLongPtrW(wnd, DWLP_USER, 0);
            }
            return FALSE;
        default:
            return FALSE;
        }
    }

    void on_init() {
        // foobar2000's own dark mode hooks, so no libPPUI is linked in.
        m_dark.AddDialogWithControls(m_wnd);

        m_initialising = true;

        ::SendDlgItemMessageW(m_wnd, IDC_PERCENT_SLIDER, TBM_SETRANGE, TRUE, MAKELONG(1, 99));
        ::SendDlgItemMessageW(m_wnd, IDC_PERCENT_SLIDER, TBM_SETPOS, TRUE, config::update_percentage());
        update_percent_text();
        ::SetDlgItemInt(m_wnd, IDC_SECONDS_EDIT, config::update_seconds(), FALSE);

        ::CheckRadioButton(m_wnd, IDC_TIMING_DISABLE, IDC_TIMING_SECONDS,
                           timing_control(static_cast<int>(config::update_timing())));

        set_checked(IDC_WRITE_PLAY_COUNTER, config::write_play_counter());
        set_checked(IDC_WRITE_FIRST_PLAYED, config::write_first_played());
        set_checked(IDC_WRITE_LAST_PLAYED, config::write_last_played());
        set_checked(IDC_WRITE_PLAY_STAMP, config::write_play_stamp());

        set_text(IDC_TAG_PLAY_COUNTER, config::tag_play_counter());
        set_text(IDC_TAG_FIRST_PLAYED, config::tag_first_played());
        set_text(IDC_TAG_LAST_PLAYED, config::tag_last_played());
        set_text(IDC_TAG_PLAY_STAMP, config::tag_play_stamp());

        set_checked(IDC_ONLY_LIBRARY, config::only_media_library());

        ::CheckRadioButton(m_wnd, IDC_STORAGE_TAGS, IDC_STORAGE_FALLBACK,
                           storage_control(static_cast<int>(config::storage())));

        update_enabled_state();
        m_initialising = false;
    }

    void on_changed() const {
        if (!m_initialising) m_callback->on_state_changed();
    }

    // Both radio groups are consecutive in resource.h and the enum values are
    // the offsets, as in the original's "1000 + mode" dialog code.
    static int timing_control(int mode) {
        return (mode >= 1 && mode <= 4) ? IDC_TIMING_DISABLE + mode : IDC_TIMING_DISABLE;
    }

    int current_timing_mode() const {
        for (int mode = 0; mode < 5; ++mode) {
            if (::IsDlgButtonChecked(m_wnd, timing_control(mode)) == BST_CHECKED) return mode;
        }
        return 0;
    }

    // 0 = file tags, 1 = database, 2 = database fallback: config::storage_mode's values.
    static int storage_control(int index) {
        return (index >= 1 && index <= 2) ? IDC_STORAGE_TAGS + index : IDC_STORAGE_TAGS;
    }

    int current_storage() const {
        for (int index = 0; index < 3; ++index) {
            if (::IsDlgButtonChecked(m_wnd, storage_control(index)) == BST_CHECKED) return index;
        }
        return 0;
    }

    static config::storage_mode storage_from_index(int index) {
        if (index == 1) return config::storage_mode::database;
        if (index == 2) return config::storage_mode::database_fallback;
        return config::storage_mode::tags;
    }

    int slider_position() const {
        return static_cast<int>(::SendDlgItemMessageW(m_wnd, IDC_PERCENT_SLIDER, TBM_GETPOS, 0, 0));
    }

    bool is_checked(int id) const {
        return ::IsDlgButtonChecked(m_wnd, id) == BST_CHECKED;
    }

    void set_checked(int id, bool value) {
        ::CheckDlgButton(m_wnd, id, value ? BST_CHECKED : BST_UNCHECKED);
    }

    pfc::string8 get_text(int id) const {
        wchar_t buffer[1024] = {};
        ::GetDlgItemTextW(m_wnd, id, buffer, static_cast<int>(std::size(buffer)));

        const t_size bytes = pfc::stringcvt::estimate_wide_to_utf8(buffer, SIZE_MAX);
        pfc::string8 text;
        char * target = text.lock_buffer(bytes);
        pfc::stringcvt::convert_wide_to_utf8(target, bytes, buffer, SIZE_MAX);
        text.unlock_buffer();
        return text;
    }

    void set_text(int id, const char * text) const {
        pfc::stringcvt::string_wide_from_utf8 wide(text);
        ::SetDlgItemTextW(m_wnd, id, wide);
    }

    void set_text(int id, const pfc::string8 & text) const { set_text(id, text.get_ptr()); }

    // An empty tag name means the column's default; apply() also puts the default
    // back, so the dialog shows what was stored.
    pfc::string8 tag_text(int id, const char * fallback) {
        pfc::string8 text = get_text(id);
        if (text.is_empty()) {
            set_text(id, fallback);
            text = fallback;
        }
        return text;
    }

    pfc::string8 effective_tag_text(int id, const char * fallback) const {
        pfc::string8 text = get_text(id);
        if (text.is_empty()) text = fallback;
        return text;
    }

    void update_percent_text() const {
        ::SetDlgItemInt(m_wnd, IDC_PERCENT_TEXT, slider_position(), FALSE);
    }

    // A row is the only place some counts exist, so the page asks first and says
    // which entries survive.
    void on_prune() const {
        if (m_wnd == nullptr) return;

        const bool outside_library = is_checked(IDC_PRUNE_OUTSIDE_LIBRARY);

        pfc::string8 message;
        message = outside_library
                      ? "Delete the statistics of every track whose file no longer exists, and of every "
                        "track that is not in the media library?"
                      : "Delete the statistics of every track whose file no longer exists?";
        message << "\n\nEntries for files inside archives, streams, and files on a drive that is not "
                   "currently connected are kept. Statistics stored in file tags are not affected. "
                   "A deleted count cannot be restored.";

        popup_message_v3::query_t question;
        question.wndParent = m_wnd;
        question.title = "Playback Statistics Custom";
        question.msg = message.get_ptr();
        question.buttons = popup_message_v3::buttonOK | popup_message_v3::buttonCancel;
        question.defButton = popup_message_v3::buttonCancel;
        question.icon = popup_message_v3::iconQuestion;
        if (popup_message_v3::get()->show_query_modal(question) != popup_message_v3::buttonOK) return;

        db::prune_missing(outside_library);
    }

    // Every row goes; tags are untouched. The outside-library checkbox only widens
    // a prune, so it does not apply here.
    void on_clear() const {
        if (m_wnd == nullptr) return;

        popup_message_v3::query_t question;
        question.wndParent = m_wnd;
        question.title = "Playback Statistics Custom";
        question.msg = "Delete every entry in playback_stats.db?\n\nStatistics stored in file tags are "
                       "not affected. A deleted count cannot be restored.";
        question.buttons = popup_message_v3::buttonOK | popup_message_v3::buttonCancel;
        question.defButton = popup_message_v3::buttonCancel;
        question.icon = popup_message_v3::iconQuestion;
        if (popup_message_v3::get()->show_query_modal(question) != popup_message_v3::buttonOK) return;

        db::clear_all();
    }

    void update_enabled_state() const {
        if (m_wnd == nullptr) return;

        // The original's timing handler disables every timing control on each radio
        // click, then re-enables the selected mode's.
        const int mode = current_timing_mode();
        const bool percentage = mode == 3;
        const bool seconds = mode == 4;
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_PERCENT_SLIDER), percentage ? TRUE : FALSE);
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_PERCENT_TEXT), percentage ? TRUE : FALSE);
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_PERCENT_LABEL), percentage ? TRUE : FALSE);
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_SECONDS_EDIT), seconds ? TRUE : FALSE);
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_SECONDS_LABEL), seconds ? TRUE : FALSE);

        // As in the original, storage does not take the names away: editing them in
        // database mode only sets them up for a later switch to tags.
        sync_tag_edit(IDC_WRITE_PLAY_COUNTER, IDC_TAG_PLAY_COUNTER);
        sync_tag_edit(IDC_WRITE_FIRST_PLAYED, IDC_TAG_FIRST_PLAYED);
        sync_tag_edit(IDC_WRITE_LAST_PLAYED, IDC_TAG_LAST_PLAYED);
        sync_tag_edit(IDC_WRITE_PLAY_STAMP, IDC_TAG_PLAY_STAMP);

        // Both database buttons follow the storage choice: with tags only, the
        // plugin reads no table and there is nothing to remove.
        const bool can_edit_table = current_storage() != 0 && db::available();
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_PRUNE_DATABASE), can_edit_table ? TRUE : FALSE);
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_CLEAR_DATABASE), can_edit_table ? TRUE : FALSE);
        ::EnableWindow(::GetDlgItem(m_wnd, IDC_PRUNE_OUTSIDE_LIBRARY), can_edit_table ? TRUE : FALSE);
    }

    void sync_tag_edit(int checkbox, int edit, bool enabled = true) const {
        ::EnableWindow(::GetDlgItem(m_wnd, edit), enabled && is_checked(checkbox) ? TRUE : FALSE);
    }

    bool has_changed() {
        if (current_timing_mode() != static_cast<int>(config::update_timing())) return true;
        if (slider_position() != config::update_percentage()) return true;
        if (static_cast<int>(::GetDlgItemInt(m_wnd, IDC_SECONDS_EDIT, nullptr, FALSE)) != config::update_seconds()) return true;
        if (is_checked(IDC_ONLY_LIBRARY) != config::only_media_library()) return true;
        if (current_storage() != static_cast<int>(config::storage())) return true;

        if (is_checked(IDC_WRITE_PLAY_COUNTER) != config::write_play_counter()) return true;
        if (is_checked(IDC_WRITE_FIRST_PLAYED) != config::write_first_played()) return true;
        if (is_checked(IDC_WRITE_LAST_PLAYED) != config::write_last_played()) return true;
        if (is_checked(IDC_WRITE_PLAY_STAMP) != config::write_play_stamp()) return true;

        if (text_differs(effective_tag_text(IDC_TAG_PLAY_COUNTER, config::default_tag_play_counter), config::tag_play_counter())) return true;
        if (text_differs(effective_tag_text(IDC_TAG_FIRST_PLAYED, config::default_tag_first_played), config::tag_first_played())) return true;
        if (text_differs(effective_tag_text(IDC_TAG_LAST_PLAYED, config::default_tag_last_played), config::tag_last_played())) return true;
        if (text_differs(effective_tag_text(IDC_TAG_PLAY_STAMP, config::default_tag_play_stamp), config::tag_play_stamp())) return true;

        return false;
    }

    const preferences_page_callback::ptr m_callback;
    // Must outlive the window: it subclasses the dialog.
    fb2k::CCoreDarkModeHooks m_dark;
    HWND m_wnd = nullptr;
    bool m_initialising = false;
};

class config_page_factory : public preferences_page_v3 {
public:
    const char * get_name() override { return "Playback Statistics Custom"; }
    GUID get_guid() override { return guid_preferences_page; }
    GUID get_parent_guid() override { return preferences_page::guid_tools; }

    preferences_page_instance::ptr instantiate(fb2k::hwnd_t parent, preferences_page_callback::ptr callback) override {
        return preferences_page_instance::ptr(
            new service_impl_t<config_page>(reinterpret_cast<HWND>(parent), callback));
    }
};

preferences_page_factory_t<config_page_factory> g_preferences_page;

} // namespace
} // namespace psc
