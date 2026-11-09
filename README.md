# foo_playback_custom — Playback Statistics Custom for foobar2000

> **Disclaimer first:** Huffing those Freebuff AI fumes and done in a day.
> * Not affiliated with, endorsed by, or approved by **Kitahei**- please don't bother them about this build (if you're able to get a hold of them, even)
> * They are the creator of the original Playback Statistics Custom from which this heavily derives
> * The original last shipped in 2009 for fb2k 0.9, so it's a super dead component- I had to abandon it for x86 2.0 as it doesn't work right on there

> **Expect rough edges.** This is a quick decomp job: behavior was reversed from the binary, and I can't know every single intent Kitahei had in the code. If something misbehaves, please report it, you unpaid testers! (I'm using it myself, but I'll be using a fixed configuration, so I might not run into what you do).

> **Important:** The original kept its database through **foo_custominfo**, another dead 32-bit plugin. Statistics stored in the original plugin's custominfo mode cannot be recovered currently. Tags handling is the same as ever. The `.dll.cfg` settings do migrate automatically (for applicable fields) on first load, so **back that file up** if you even **think** you'll consider reverting.

---

### What it is

A drop-in decomp-to-recreation of **Playback Statistics Custom** for foobar2000 2.0+. It ships with slightly different defaults, a refined configuration page, and a new database mode handler via foobar2000's built-in sqlite3. In database mode, chosen details are written to `playback_stats.db` (at the profile folder root). The original default tag-to-file mode is available too, as is the database-fallback mode for when tag writes fail! I can only hope all permutations work. I'm using the database mode with last_played on, and using it to seed Case's [Skip Track](https://www.foobar2000.org/components/view/foo_skip) plugin so my shuffle knows to skip recently played things. A lot of people would be better served by Peter's plugin- this is just a matter of weird tastes and preferences, and my historical use of this plugin and expectations surrounding it.

### Building

Visual Studio 2022 with the C++ toolchain; the SDK is fetched by the build script.

> ```
> build.bat fetch     fetch the pinned dependencies
> build.bat build     build release\foo_playback_custom.dll and release\x64\foo_playback_custom.dll
> ```

### Credits

The original component is **Kitahei**'s work- last build 1.5.1, 2009, and long gone from the web (the DLL had to be dug out of an archive to work from)- they are unaffiliated with this remake and most certainly not endorsing me. My part is just making it run again on modern foobar2000. Hopefully also a good baseline for improvements, forks, or whatever.
