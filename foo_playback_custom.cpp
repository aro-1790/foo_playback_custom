// foo_playback_custom - playback statistics for foobar2000.
//
// 0.9 component recreated for the 2.0 API: tags, this build's playback_stats.db,
// or tags with the database as a fallback.

#include <SDK/foobar2000.h>

// Stamped from resource/version.txt into this literal by ./version.sh - do not
// edit by hand. This is the string foobar2000 shows in its component list.
DECLARE_COMPONENT_VERSION("Playback Statistics Custom", "1.5.2",
    "Stores playback statistics (play count, first played, last played) in file "
    "tags or a local database.\n"
    "\n"
    "Recreation of the 0.9 component for foobar2000 2.0+.\n"
    "Unofficial rebuild - report bugs on GitHub.\n"
    "\n"
    "Original: (C) Kitahei. All rights reserved.");

// A component must keep its file name, and only one component may live per DLL.
VALIDATE_COMPONENT_FILENAME("foo_playback_custom.dll");
