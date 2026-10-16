# Critic review — staged 1.0 core preview

Scope: read-only review of staging `src/dllmain.cpp` and `src/client_profile.h`, 2026-10-16. No game process execution or runtime rendering evidence.

## Findings

- No confirmed new crash blocker identified in the reviewed core profile. Three complete instruction prefixes (camera 18, present 16, Vulkan init 20 bytes) match the installed executable independently via the PE harness. The profile requires on-disk full SHA-256 plus named ECS registration and live memory entry bytes before hook construction. Unknown layouts do not reach the legacy pattern scan unless legacy named registrations match.
- Vulkan init now retains a pending candidate, validates it before active publication, and validates function pointers before table hooks. This avoids treating entry-time uninitialized memory as a complete device table.
- Configuration initially continued to allow clock/weather/remote marker flags despite those sources being unavailable in the preview. Root agent notified; gate unavailable indicators or ensure UI never presents defaults as live game state. Fog gating was already being added.
- `Unload` does not restore new profile globals/offsets to legacy defaults. Limited practical impact while the executable remains the same, but a same-DLL repeated-load lifecycle test is appropriate.
- Existing renderer fallback uses format 37 and screen dimensions when swapchain creation was missed. Pending table publication may occur after initial swapchain creation; actual first-start and resize/recreate behavior needs runtime verification. Static evidence cannot establish actual Vulkan swapchain format/extent.

## Meaningful checks

1. A changed executable hash or changed expected entry byte rejects the 1.0 profile without installing any hooks.
2. Incomplete pending Vulkan table never triggers writes; a fully initialized candidate can later publish and patch successfully.
3. All core hook activations failing leaves the mod inactive; partial activation behavior should be explicit in diagnostics.
4. Disabled preview features cannot show a fabricated time/weather or stale players/waypoints.
5. Actual game test: initial launch, joining a world, moving/turning the local player, resizing or recreating swapchain, toggling, unloading/reloading. Confirm overlay and compare its position/heading to the game separately from successful DLL loading.

Evidence files: `installed-pe-observed.json`, `staged-profile-regression.json`, `staged-profile-check.json`. The extracted regression profile is a check that staged constants match the binary; its origin is the staging source, not an independently researched runtime compatibility specification.

## Final follow-up: completed-init CALL wrapper

- Reviewed CaptureRelease10VulkanInit: it calls the original four-argument function, preserves its boolean return, and only installs table hooks after successful initialization and table validation. The verified CALL relay keeps the caller return address and stack/shadow-space convention; the original init entry remains intact.
- Added vulkan_callsite expected E8 F1 15 02 00 at RVA 0xFBC36A to the staging regression profile. Independent PE read passes all SHA/prologue/CALL byte comparisons; this displacement targets RVA 0xFDD960.
- Confirmed Load resets profile and dynamic offsets, and preview config gates world/remote/ping/waypoint/death/clock/weather/fog flags.
- Confirmed tests cover wrapper argument forwarding and both bool results, rejection of incomplete table, known SHA-256 abc vector, wrong/missing file hash, and old/new table offset fallback fixtures. These are meaningful isolation tests, not runtime game validation.
- Residual safety detail sent to root: skipping only an absent swapchain snapshot does not cover an images-only snapshot whose format/extent remain zero. RememberSwapchainImages can create that record without RememberSwapchainCreate. Require nonzero width/height/format for 1.0 before allowing renderer creation; otherwise legacy guessed defaults remain reachable.
