# 0.5.13 death markers — 2026-10-19

Cause: the pinned 1.0 profile intentionally disabled the legacy death reader and show_death_markers at runtime. Steam build 25988036 retains the death UI list but moves it to state+0x3E6A78 and uses 0xA0 entries instead of the legacy 0x80.

Scope: a separate, byte-validated clear_map_markers_for_ui entry hook copies only the completed previous-frame death list. No game state is written. The POI support flag remains disabled; existing camera, friend, waypoint, Vulkan and rendering hooks retain their addresses and code paths. Only death visibility is enabled for the validated current build. Older profiles retain prior behavior.

Safety: bounded copied snapshots, independent freshness/session checks, empty/read-failed snapshot clears, world-exit cleanup, narrow SEH around native iterator and existing protected memory reads, C++ exception logging. The existing skull renderer is reused. Read-only PE analysis and synthetic regression results do not prove in-game display/recovery or map-closed feed updates.

Install: close the game and use install.ps1 -GameDir <enshrouded.exe folder> -SkipBuild. Installer verifies backup and installed DLL hashes while preserving shroudtopia.json. Rollback: close the game and restore minimap-backups/<timestamp>/minimap_mod to mods/minimap_mod. Saves are untouched.
