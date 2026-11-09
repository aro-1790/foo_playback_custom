// The playback_stats.db backend. One worker thread owns the only connection, so
// a slow profile folder cannot stall the player; a failure is reported once and
// then disables the backend instead of retrying per track.

#include "db.h"
#include "config.h"
#include "statistics.h"

#include <SDK/initquit.h>
#include <SDK/main_thread_callback.h>

#include <string.h>

#include <atomic>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace psc {
namespace db {

namespace {

// The subset of the SQLite C API used here. The real types are opaque to us;
// only the pointer sizes matter across the DLL boundary.
struct sqlite3;
struct sqlite3_stmt;

using sqlite3_destructor_type = void (*)(void *);

using fn_open_v2 = int (*)(const char *, sqlite3 **, int, const char *);
using fn_close_v2 = int (*)(sqlite3 *);
using fn_exec = int (*)(sqlite3 *, const char *, int (*)(void *, int, char **, char **), void *, char **);
using fn_prepare_v2 = int (*)(sqlite3 *, const char *, int, sqlite3_stmt **, const char *);
using fn_step = int (*)(sqlite3_stmt *);
using fn_finalize = int (*)(sqlite3_stmt *);
using fn_reset = int (*)(sqlite3_stmt *);
using fn_bind_int64 = int (*)(sqlite3_stmt *, int, long long);
using fn_bind_text = int (*)(sqlite3_stmt *, int, const char *, int, sqlite3_destructor_type);
using fn_bind_null = int (*)(sqlite3_stmt *, int);
using fn_busy_timeout = int (*)(sqlite3 *, int);
using fn_free = void (*)(void *);
using fn_column_int64 = long long (*)(sqlite3_stmt *, int);
using fn_column_text = const unsigned char * (*)(sqlite3_stmt *, int);

// Return codes and open flags from sqlite3.h; the values are part of its ABI.
constexpr int sqlite_ok = 0;
constexpr int sqlite_cantopen = 14;
constexpr int sqlite_row = 100;
constexpr int sqlite_done = 101;
constexpr int open_readwrite = 0x00000002;
constexpr int open_create = 0x00000004;

struct api {
    HINSTANCE module = nullptr;
    fn_open_v2 open_v2 = nullptr;
    fn_close_v2 close_v2 = nullptr;
    fn_exec exec = nullptr;
    fn_prepare_v2 prepare_v2 = nullptr;
    fn_step step = nullptr;
    fn_finalize finalize = nullptr;
    fn_reset reset = nullptr;
    fn_bind_int64 bind_int64 = nullptr;
    fn_bind_text bind_text = nullptr;
    fn_bind_null bind_null = nullptr;
    fn_busy_timeout busy_timeout = nullptr;
    fn_free free = nullptr;
    fn_column_int64 column_int64 = nullptr;
    fn_column_text column_text = nullptr;
};

api g_api;

template <typename T>
bool resolve(T & target, const char * name) {
    target = reinterpret_cast<T>(::GetProcAddress(g_api.module, name));
    return target != nullptr;
}

// sqlite3.dll sits beside foobar2000.exe; the path is spelled out because a host
// may restrict the DLL search order.
bool try_load() {
    std::wstring beside_exe(MAX_PATH, L'\0');
    const DWORD length = ::GetModuleFileNameW(nullptr, beside_exe.data(), static_cast<DWORD>(beside_exe.size()));
    if (length > 0 && length < beside_exe.size()) {
        beside_exe.resize(length);
        const size_t slash = beside_exe.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            beside_exe.resize(slash + 1);
            beside_exe += L"sqlite3.dll";
            g_api.module = ::LoadLibraryW(beside_exe.c_str());
        }
    }

    if (g_api.module == nullptr) g_api.module = ::LoadLibraryW(L"sqlite3.dll");
    if (g_api.module == nullptr) return false;

    const bool complete =
        resolve(g_api.open_v2, "sqlite3_open_v2") && resolve(g_api.close_v2, "sqlite3_close_v2") &&
        resolve(g_api.exec, "sqlite3_exec") && resolve(g_api.prepare_v2, "sqlite3_prepare_v2") &&
        resolve(g_api.step, "sqlite3_step") && resolve(g_api.finalize, "sqlite3_finalize") &&
        resolve(g_api.reset, "sqlite3_reset") && resolve(g_api.bind_int64, "sqlite3_bind_int64") &&
        resolve(g_api.bind_text, "sqlite3_bind_text") && resolve(g_api.bind_null, "sqlite3_bind_null") &&
        resolve(g_api.busy_timeout, "sqlite3_busy_timeout") && resolve(g_api.free, "sqlite3_free") &&
        resolve(g_api.column_int64, "sqlite3_column_int64") && resolve(g_api.column_text, "sqlite3_column_text");

    if (!complete) {
        ::FreeLibrary(g_api.module);
        g_api = api();
        return false;
    }

    return true;
}

// Probed once, from whichever thread gets here first.
bool load() {
    static std::once_flag once;
    static bool loaded = false;
    std::call_once(once, [] { loaded = try_load(); });
    return loaded;
}

void report(const pfc::string8 & text) {
    ::console::printf("Playback Statistics Custom: %s", text.get_ptr());
}

void report_unavailable() {
    static std::atomic<bool> reported = false;
    if (!reported.exchange(true)) {
        report("disabled: sqlite3.dll was not found beside foobar2000.exe");
    }
}

const char * schema_sql =
    "CREATE TABLE IF NOT EXISTS statistics("
    " path TEXT NOT NULL,"
    " subsong INTEGER NOT NULL DEFAULT 0,"
    " play_count INTEGER NOT NULL DEFAULT 0,"
    " first_played INTEGER,"
    " last_played INTEGER,"
    " play_stamp TEXT,"
    " PRIMARY KEY(path, subsong));";

// One play, as captured on the main thread. First/last/stamp are present only
// when the matching setting is on, so a disabled column is left untouched.
struct pending {
    metadb_handle_ptr track;
    pfc::string8 path;
    t_uint32 subsong = 0;
    int count_increment = 0;
    bool has_first = false;
    bool has_last = false;
    bool has_stamp = false;
    long long first = 0;
    long long last = 0;
    pfc::string8 stamp;
};

// Resolved in on_init(), so the worker never calls core_api itself; the lazy
// fallback keeps the writer usable if a play arrives before init.
pfc::string8 g_path;

// get_profile_path() returns a URL, so the folder goes through the filesystem
// service first; pathInProfile() would join the name onto the URL and fail.
bool resolve_database_path(pfc::string8 & out) {
    pfc::string8 folder;
    if (!filesystem::g_get_native_path(core_api::get_profile_path(), folder)) return false;
    if (folder.is_empty()) return false;

    // add_char, not <<: pfc::string8 has no operator<< for a char, so a character
    // literal would bind to the integer formatter and append its code page number.
    const char last = folder[folder.length() - 1];
    if (last != '\\' && last != '/') folder.add_char('\\');
    folder << file_name;

    out = folder;
    return true;
}

const pfc::string8 & database_path() {
    if (g_path.is_empty()) resolve_database_path(g_path);
    return g_path;
}

// Only local files can be checked, and only a file/path-not-found error counts:
// an unreachable drive is not evidence the track was deleted.
bool file_is_gone(const char * url) {
    if (strncmp(url, "file://", 7) != 0) return false;

    pfc::string8 native;
    if (!filesystem::g_get_native_path(url, native)) return false;
    if (::GetFileAttributesA(native.get_ptr()) != INVALID_FILE_ATTRIBUTES) return false;

    const DWORD error = ::GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_NAME;
}

// The table mirrored in memory, so the display providers never touch the
// worker's connection.
std::unordered_map<std::string, stored> g_rows;
std::mutex g_rows_mutex;
std::atomic<bool> g_rows_loaded = false;

// One key format for the mirror; the separator cannot occur in a path.
std::string make_key(const char * path, t_uint32 subsong) {
    std::string key(path);
    key += '\x1f';
    key += std::to_string(subsong);
    return key;
}

// What a sweep may delete: a row whose file is gone, and optionally any row the
// media library does not list. Built on the caller's thread; the worker only reads
// it, so the library itself is never walked off the main thread.
struct prune_scope {
    bool outside_library = false;
    std::unordered_set<std::string> library;
};

class writer {
public:
    void enqueue(pending && item) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stopping) return;
        m_queue.push_back(std::move(item));
        if (!m_thread.joinable()) m_thread = std::thread([this] { run(); });
        m_ready.notify_one();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) return;
            m_stopping = true;
        }
        m_ready.notify_all();
        if (m_thread.joinable()) m_thread.join();
        close();
    }

    // Asks the worker to refresh the in-memory copy from the table.
    void request_reload() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) return;
            m_reload = true;
            if (!m_thread.joinable()) m_thread = std::thread([this] { run(); });
        }
        m_ready.notify_one();
    }

    // Asks the worker to drop the rows the scope allows. Unlike a play, this is a
    // sweep of the whole table, so it is queued rather than done on the click.
    void request_prune(prune_scope scope) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) return;
            m_prune = true;
            m_scope = std::move(scope);
            if (!m_thread.joinable()) m_thread = std::thread([this] { run(); });
        }
        m_ready.notify_one();
    }

    // Wipes the table; the page asks first, since a deleted count is gone.
    void request_clear() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) return;
            m_clear = true;
            if (!m_thread.joinable()) m_thread = std::thread([this] { run(); });
        }
        m_ready.notify_one();
    }

private:
    void run() {
        for (;;) {
            pending item;
            prune_scope scope;
            bool have_item = false;
            bool do_reload = false;
            bool do_prune = false;
            bool do_clear = false;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_ready.wait(lock,
                             [this] { return m_stopping || m_reload || m_prune || m_clear || !m_queue.empty(); });
                if (m_prune) {
                    m_prune = false;
                    do_prune = true;
                    scope = std::move(m_scope);
                } else if (m_clear) {
                    m_clear = false;
                    do_clear = true;
                } else if (m_reload) {
                    m_reload = false;
                    do_reload = true;
                } else if (!m_queue.empty()) {
                    item = std::move(m_queue.front());
                    m_queue.pop_front();
                    have_item = true;
                } else {
                    return; // stopping, and the queue is drained
                }
            }
            if (do_prune) prune(scope);
            if (do_clear) clear_all();
            if (do_reload) reload(true);
            if (have_item) write(item);
        }
    }

    // `create` separates the callers: preload() only reads, the first write creates
    // the file and schema. Absence is not a failure, so a later play can create it.
    bool open(bool create) {
        if (m_db != nullptr) return true;
        if (m_failed) return false;
        // Every sqlite3 entry point below is resolved by load(), so it has to run
        // first: an unresolved api is a table of null pointers.
        if (!load()) {
            m_failed = true;
            return false;
        }

        const pfc::string8 & path = database_path();
        if (path.is_empty()) {
            report("cannot resolve the profile folder to a file path");
            m_failed = true;
            return false;
        }

        const bool exists = ::GetFileAttributesA(path.get_ptr()) != INVALID_FILE_ATTRIBUTES;
        // Nothing stored yet, and nothing to read: not an error, just no rows.
        if (!exists && !create) return false;

        sqlite3 * handle = nullptr;
        const int flags = create ? (open_readwrite | open_create) : open_readwrite;
        const int rc = g_api.open_v2(path.get_ptr(), &handle, flags, nullptr);
        if (rc != sqlite_ok || handle == nullptr) {
            pfc::string8 text = "cannot open ";
            text << path << " (SQLite error " << rc << ")";
            report(text);
            if (handle != nullptr) g_api.close_v2(handle);
            m_failed = true;
            return false;
        }

        m_db = handle;
        g_api.busy_timeout(m_db, 5000);

        char * error = nullptr;
        if (g_api.exec(m_db, schema_sql, nullptr, nullptr, &error) != sqlite_ok) {
            pfc::string8 text = "cannot create the statistics table: ";
            if (error != nullptr) {
                text << error;
                g_api.free(error);
            } else {
                text << "SQLite error";
            }
            report(text);
            close();
            m_failed = true;
            return false;
        }

        if (!exists) {
            pfc::string8 text = "created playback_stats.db at ";
            text << path;
            report(text);
        }

        return true;
    }

    bool prepare() {
        if (m_stmt != nullptr) return true;

        // first_played keeps the earliest value, as the tag writer only sets it
        // when missing; the others take the newest.
        static const char * upsert_sql =
            "INSERT INTO statistics(path, subsong, play_count, first_played, last_played, play_stamp)"
            " VALUES(?1, ?2, ?3, ?4, ?5, ?6)"
            " ON CONFLICT(path, subsong) DO UPDATE SET"
            "  play_count = play_count + excluded.play_count,"
            "  first_played = COALESCE(first_played, excluded.first_played),"
            "  last_played = COALESCE(excluded.last_played, last_played),"
            "  play_stamp = COALESCE(excluded.play_stamp, play_stamp);";

        const int rc = g_api.prepare_v2(m_db, upsert_sql, -1, &m_stmt, nullptr);
        if (rc != sqlite_ok || m_stmt == nullptr) {
            pfc::string8 text = "cannot prepare the statistics statement (SQLite error ";
            text << rc << ")";
            report(text);
            m_stmt = nullptr;
            m_failed = true;
            close();
            return false;
        }

        return true;
    }

    void write(pending & item) {
        if (!open(true) || !prepare()) return;

        // The bindings are static: every string here outlives the step() below.
        g_api.bind_text(m_stmt, 1, item.path.get_ptr(), -1, nullptr);
        g_api.bind_int64(m_stmt, 2, static_cast<long long>(item.subsong));
        g_api.bind_int64(m_stmt, 3, item.count_increment);

        if (item.has_first) {
            g_api.bind_int64(m_stmt, 4, item.first);
        } else {
            g_api.bind_null(m_stmt, 4);
        }

        if (item.has_last) {
            g_api.bind_int64(m_stmt, 5, item.last);
        } else {
            g_api.bind_null(m_stmt, 5);
        }

        if (item.has_stamp) {
            g_api.bind_text(m_stmt, 6, item.stamp.get_ptr(), -1, nullptr);
        } else {
            g_api.bind_null(m_stmt, 6);
        }

        const int rc = g_api.step(m_stmt);
        if (rc != sqlite_done) {
            pfc::string8 text = "cannot write a play to ";
            text << file_name << " (SQLite error " << rc << ")";
            report(text);
            m_failed = true;
            close();
            return;
        }

        g_api.reset(m_stmt);

        update_cache(item);

        // Title formatting caches provider output, so it has to be told.
        const metadb_handle_ptr track = item.track;
        if (!track.is_empty()) {
            fb2k::inMainThread([track] { metadb_io::get()->dispatch_refresh(track); });
        }
    }

    // The whole table into memory: everything the display providers read comes
    // from here. No file yet means an empty table, which is the truth.
    size_t reload(bool announce) {
        if (!open(false)) {
            // Either nothing stored yet or a database that cannot be used: there
            // is nothing to mirror, and open() has said why if it was an error.
            g_rows_loaded = true;
            return 0;
        }

        static const char * select_sql =
            "SELECT path, subsong, play_count, first_played, last_played, play_stamp FROM statistics;";

        sqlite3_stmt * stmt = nullptr;
        if (g_api.prepare_v2(m_db, select_sql, -1, &stmt, nullptr) != sqlite_ok || stmt == nullptr) {
            report("cannot read the statistics table");
            return 0;
        }

        std::unordered_map<std::string, stored> rows;
        while (g_api.step(stmt) == sqlite_row) {
            const char * path = reinterpret_cast<const char *>(g_api.column_text(stmt, 0));
            if (path == nullptr) continue;

            stored row;
            row.play_count = g_api.column_int64(stmt, 2);
            row.first_played = g_api.column_int64(stmt, 3);
            row.last_played = g_api.column_int64(stmt, 4);

            const char * stamp = reinterpret_cast<const char *>(g_api.column_text(stmt, 5));
            if (stamp != nullptr) row.play_stamp = stamp;

            rows[make_key(path, static_cast<t_uint32>(g_api.column_int64(stmt, 1)))] = std::move(row);
        }

        g_api.finalize(stmt);

        const size_t count = rows.size();
        {
            std::lock_guard<std::mutex> lock(g_rows_mutex);
            g_rows = std::move(rows);
        }
        g_rows_loaded = true;

        // Once per start, and only when there is a database to read: the file is
        // in the profile folder, which is not the install folder for a normal
        // installation, and that is the first thing to check when a play seems to
        // go nowhere. A prune reloads silently; its own line carries the count.
        if (announce) {
            pfc::string8 text = "playback_stats.db at ";
            text << database_path() << " (" << pfc::format_uint(count) << " tracks)";
            report(text);
        }

        return count;
    }

    // Deletes the rows whose file is gone, then rebuilds the mirror rather than
    // editing it row by row: a half-updated copy would answer for the session.
    void prune(const prune_scope & scope) {
        if (!open(false)) {
            // No file yet is the ordinary case. open() has already reported an
            // actual failure, so this does not claim one happened.
            notify("No playback_stats.db yet: nothing to prune.");
            return;
        }

        sqlite3_stmt * select = nullptr;
        if (g_api.prepare_v2(m_db, "SELECT path, subsong FROM statistics;", -1, &select, nullptr) != sqlite_ok ||
            select == nullptr) {
            report("cannot read the statistics table");
            notify("Could not read playback_stats.db: see the console for the SQLite error.");
            return;
        }

        std::vector<std::pair<std::string, long long>> stale;
        size_t scanned = 0;
        while (g_api.step(select) == sqlite_row) {
            ++scanned;

            const char * path = reinterpret_cast<const char *>(g_api.column_text(select, 0));
            if (path == nullptr) continue;

            const long long subsong = g_api.column_int64(select, 1);
            // Two scopes: a file that is gone, and - when the page asked for it -
            // a track the media library does not list.
            const bool outside = scope.outside_library &&
                                 scope.library.find(make_key(path, static_cast<t_uint32>(subsong))) ==
                                     scope.library.end();
            if (!outside && !file_is_gone(path)) continue;

            stale.emplace_back(path, subsong);
        }
        g_api.finalize(select);

        // An empty table has nothing to say about missing files, and "nothing
        // pruned" followed by "now empty" reads as a contradiction.
        if (scanned == 0) {
            notify("playback_stats.db has no entries: nothing to prune.");
            return;
        }

        size_t pruned = 0;
        if (!stale.empty()) {
            sqlite3_stmt * remove = nullptr;
            if (g_api.prepare_v2(m_db, "DELETE FROM statistics WHERE path = ?1 AND subsong = ?2;", -1, &remove, nullptr) !=
                    sqlite_ok ||
                remove == nullptr) {
                report("cannot prepare the delete statement");
                notify("Could not delete from playback_stats.db: see the console for the SQLite error.");
                return;
            }

            for (const auto & row : stale) {
                g_api.bind_text(remove, 1, row.first.c_str(), -1, nullptr);
                g_api.bind_int64(remove, 2, row.second);
                if (g_api.step(remove) == sqlite_done) {
                    ++pruned;
                } else {
                    report("a stale row could not be deleted");
                }
                g_api.reset(remove);
            }

            g_api.finalize(remove);
        }

        const size_t remaining = reload(false);

        pfc::string8 text;
        if (pruned == 0) {
            text = scope.outside_library
                       ? "Nothing pruned: no entry names a missing file or a track outside the media library."
                       : "Nothing pruned: every file in playback_stats.db still exists.";
        } else {
            text << "Removed " << pfc::format_uint(pruned) << " stale entries.";
        }
        if (remaining == 0) {
            text << " The database is now empty.";
        } else {
            text << " " << pfc::format_uint(remaining) << " entries remain.";
        }

        report(text);
        notify(text);
    }

    // Every row, whatever its file. The mirror is rebuilt, or this session would
    // keep answering lookups from rows the file no longer has.
    void clear_all() {
        if (!open(false)) {
            notify("No playback_stats.db yet: nothing to remove.");
            return;
        }

        // Whose caches to invalidate, read before the rows are gone. Only these
        // items can be displaying a count, so this is smaller than a global sweep.
        std::vector<std::pair<std::string, t_uint32>> removed;
        sqlite3_stmt * select = nullptr;
        if (g_api.prepare_v2(m_db, "SELECT path, subsong FROM statistics;", -1, &select, nullptr) != sqlite_ok ||
            select == nullptr) {
            report("cannot read the statistics table");
            notify("Could not read playback_stats.db: see the console for the SQLite error.");
            return;
        }

        while (g_api.step(select) == sqlite_row) {
            const char * path = reinterpret_cast<const char *>(g_api.column_text(select, 0));
            if (path == nullptr) continue;
            removed.emplace_back(path, static_cast<t_uint32>(g_api.column_int64(select, 1)));
        }
        g_api.finalize(select);

        if (removed.empty()) {
            notify("playback_stats.db has no entries: nothing to remove.");
            return;
        }

        if (g_api.exec(m_db, "DELETE FROM statistics;", nullptr, nullptr, nullptr) != sqlite_ok) {
            report("cannot empty the statistics table");
            notify("Could not empty playback_stats.db: see the console for the SQLite error.");
            return;
        }

        reload(false);

        const pfc::string8 text = "Removed every entry in playback_stats.db.";
        report(text);
        refresh(removed);
        notify(text);
    }

    // Title formatting caches per item, so the removed locations have to be told.
    // The handles are made on the main thread, where the metadb API wants them;
    // the paths come from the table, so a row whose file is gone still resolves.
    static void refresh(const std::vector<std::pair<std::string, t_uint32>> & locations) {
        if (locations.empty()) return;

        fb2k::inMainThread([locations] {
            metadb_handle_list items;
            for (const auto & location : locations) {
                items.add_item(metadb::get()->handle_create(location.first.c_str(), location.second));
            }
            metadb_io::get()->dispatch_refresh(items);
        });
    }

    // The page has no status line, so the result comes back as a message box. No
    // refresh follows: the deleted rows named files that no longer exist.
    void notify(const pfc::string8 & text) {
        fb2k::inMainThread([text] { popup_message::g_show(text.get_ptr(), "Playback Statistics Custom"); });
    }

    // Mirrors the upsert on the in-memory copy: no second table read.
    void update_cache(const pending & item) {
        const std::string key = make_key(item.path.get_ptr(), item.subsong);

        std::lock_guard<std::mutex> lock(g_rows_mutex);
        stored & row = g_rows[key];
        row.play_count += item.count_increment;
        if (item.has_first && row.first_played == 0) row.first_played = item.first;
        if (item.has_last) row.last_played = item.last;
        if (item.has_stamp) row.play_stamp = item.stamp;
    }

    void close() {
        if (m_stmt != nullptr) {
            g_api.finalize(m_stmt);
            m_stmt = nullptr;
        }
        if (m_db != nullptr) {
            g_api.close_v2(m_db);
            m_db = nullptr;
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::deque<pending> m_queue;
    std::thread m_thread;
    bool m_stopping = false;
    bool m_reload = false;
    bool m_prune = false;
    bool m_clear = false;
    prune_scope m_scope;

    // Owned by the worker thread.
    sqlite3 * m_db = nullptr;
    sqlite3_stmt * m_stmt = nullptr;
    bool m_failed = false;
};

writer g_writer;

} // namespace

bool available() { return load(); }

void preload() {
    // Reads what an earlier session left, so the display providers have their rows
    // from the first refresh. Nothing is created here: the first recorded play
    // writes the file and the schema, so a profile that never stores a play never
    // grows an empty database.
    if (config::storage() == config::storage_mode::tags) {
        report("storing in file tags only; playback_stats.db is not used");
        return;
    }
    if (!load()) {
        report_unavailable();
        return;
    }
    g_writer.request_reload();
}

bool lookup(const playable_location & location, stored & out) {
    const std::string key = make_key(location.get_path(), location.get_subsong_index());
    std::lock_guard<std::mutex> lock(g_rows_mutex);

    const auto found = g_rows.find(key);
    if (found == g_rows.end()) return false;

    out = found->second;
    return true;
}

void record_play(metadb_handle_ptr track) {
    if (track.is_empty()) return;

    // Read before load(): with every statistic off there is nothing to write, so
    // the backend is neither probed nor reported as missing.
    const bool want_count = config::write_play_counter();
    const bool want_first = config::write_first_played();
    const bool want_last = config::write_last_played();
    const bool want_stamp = config::write_play_stamp();
    if (!want_count && !want_first && !want_last && !want_stamp) return;

    if (!load()) {
        report_unavailable();
        return;
    }

    const long long now = static_cast<long long>(::time(nullptr));

    pending item;
    item.track = track;
    item.path = track->get_path();
    item.subsong = track->get_subsong_index();
    item.count_increment = want_count ? 1 : 0;
    item.has_first = want_first;
    item.has_last = want_last;
    item.first = now;
    item.last = now;
    item.has_stamp = want_stamp;

    // A mode switch this session has left the table unread; load it now.
    if (!g_rows_loaded) g_writer.request_reload();
    // One instant for the stamp and the columns, so they cannot disagree.
    if (want_stamp) item.stamp = stats::format_local_time(now);

    g_writer.enqueue(std::move(item));
}

void prune_missing(bool outside_library) {
    if (!load()) {
        report_unavailable();
        return;
    }

    prune_scope scope;
    scope.outside_library = outside_library;
    if (outside_library) {
        // Read here, on the main thread: walking the library is not the worker's
        // job, and one snapshot is enough for a sweep that has not started yet.
        metadb_handle_list items;
        library_manager::get()->get_all_items(items);
        for (t_size i = 0; i < items.get_size(); ++i) {
            const metadb_handle_ptr & item = items[i];
            scope.library.insert(make_key(item->get_path(), item->get_subsong_index()));
        }
    }

    g_writer.request_prune(std::move(scope));
}

void clear_all() {
    if (!load()) {
        report_unavailable();
        return;
    }
    g_writer.request_clear();
}

void shutdown() { g_writer.stop(); }

} // namespace db

namespace db {
namespace {

// Joined before the DLL unloads, and the profile path resolved here on the main
// thread so the worker never asks core_api.
class db_lifecycle : public initquit {
public:
    void on_init() override {
        resolve_database_path(g_path);
        preload();
    }
    void on_quit() override { shutdown(); }
};
initquit_factory_t<db_lifecycle> g_lifecycle;

} // namespace
} // namespace db

} // namespace psc
