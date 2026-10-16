# Enshrouded 1.0 compatibility investigation

Date: 2026-10-16 Asia/Seoul. Status: implementation in progress; no runtime acceptance.

## Plan / evidence boundary

- Fork main f93ab498bc6200cbb9ea6a57b29ff824c70e6535 is identical to Aerox912 main; community main is 25 commits ahead of elxokker ca881bcff534699ee7d2a068c2c351d1124f06f0.
- Shroudtopia public API header blob 6a845e3ea16990d0a8871447f298973010a29f46 matches the bundled header. No public API drift established. Installed binary ABI remains separate.
- Installed Steam build 25966634. Old release targets client data revision 1076226. Build ID, data revision and mod version are different identifiers.
- Prior 2026-10-16 log sessions reach minimap code but repeatedly report missing Vulkan create/images/present. Latest session does not register minimap; its folder is absent from active mods.
- Old short Vulkan prologue matches 909 .text locations (393 within scan radius); nearest UI match is inside another function. Proximity is not function identity.
- Actual PE named ECS registries identify UI 0x382760, camera 0x2b26a0, waypoints 0x30e650.
- Actual Vulkan API-name xrefs show slots create 0x580, images 0x590, acquire 0x598, present 0x5a0 (previously 0x570/580/588/590). Init 0xfdd960; present 0xfd13d0. Graphics queue state/family move to 0xdd0/0xe34.

## Do / checkpoints

1. Preserve clean checkout and existing loader/config/mod files outside active mods; verify hashes. Work on fix/enshrouded-1.0-compat.
2. Add exact observed-client profile and reject unknown layouts; never select nearest generic prologue on 1.0.
3. Port only disassembly-supported renderer and camera paths. Keep unverified marker/weather/clock/fog layouts disabled and report limitations.
4. Verify candidate tables before overwriting slots; retain bounded fallback discovery.

## Check / harness

Baseline Release x64 build succeeds with VS2022 v143. Existing tracking, renderer, marker and map-feature suites are run before and after edits. Independent PE harness records binary identity, names, entry boundaries and instruction evidence. Synthetic tests are not live gameplay evidence.

## Act / critic and runtime

Independent renderer and loader reviews check guessed addresses, ABI and feature claims. Runtime acceptance requires a new log plus visible terrain, moving center, rotating heading and clean world transitions. No old log is accepted as evidence for a new DLL. Installation and recovery instructions will be completed with final validation.

## Remote access

GitHub connector read succeeds; create_branch returns 403 Resource not accessible by integration. Local commits proceed; push/PR remain blocked until connector write access is granted.

## Implemented core preview (0.5.8)

Exact on-disk executable SHA256: `4d6d43ea4a91e71c82d60b045dde932179c99e32e40657efe374688b2933589a`. The loader's game.version string is not used as a revision oracle. Unknown clients are rejected before guessed hooks.

- Core camera uses the named 1.0 ECS registry, record size 0x50, iterator 0x9ba5a0/0x9b5490 and entity array +0x828. Legacy +0x7e0 is retained for the legacy profile.
- Present entry is 0xfd13d0, with a 16-byte complete-instruction prefix. Device comes from the validated table +8; graphics queue family comes from +0xe34.
- Verified CALL at 0xfbc36a wraps original init 0xfdd960, preserving all four arguments and its bool return. Vulkan hooks are installed after initialization and before the caller's swapchain setup.
- Table validation requires matching Vulkan module/proc identity and executable create-instance/device-resolver/create/images/present functions. Pending legacy candidates do not suppress fallback scanning. Unrelated tables cannot replace a patched table's original pointers.
- 1.0 rendering waits for a captured swapchain with nonzero width, height and format. It never guesses new-client swapchain metadata. If the initial creation was missed, switch window/fullscreen once to recreate it.

| Feature | Preview state |
|---|---|
| Local position / camera heading | Ported; static and synthetic checks pass, gameplay pending |
| Vulkan renderer / zoom / size / toggle | Ported; build and synthetic checks pass, GPU gameplay pending |
| Terrain artwork / original static POIs | Bundled pre-1.0 assets; new areas and alignment unverified |
| Other players / pings / waypoints / live POIs / tombstones | Disabled on 1.0; old layouts not reused |
| Clock / weather / exploration fog | Disabled on 1.0; terrain is shown without exploration masking |
| Existing 1076226 implementation | Retained; synthetic suites pass, no new legacy gameplay test |

## Checks

VS2022 BuildTools AMD64 MSBuild, v143, Release x64: mod builds. Four suites: tracking 29, renderer 52, markers 58, map_features 381 = 520 PASS. Renderer includes 100,000 delayed-GPU submit/skip cycles. Independent standard-library PE harness checks executable hash, named registry, entry bytes, unwind boundaries, API-name references and init CALL target. These do not prove actual GPU rendering or gameplay.

Installed Shroudtopia DLL SHA256 `53ceee2dd51c12b7db1371092eddcd1118abeaeaba6e3b482986a071ab9b1a11` exactly matches the user's extracted Shroudtopia 0.1.1 distribution. This identifies the file; it does not establish 1.0 runtime compatibility for every loader mod.

## Build / package / install

From the repository: `.\build-release.ps1` uses the installed v143 toolset. `.\package-release.ps1 -DllPath .\build\x64\Release\mods\minimap_mod.dll -OutputDirectory <new-output-directory>` packages committed sources and records source commit, DLL SHA256 and the experimental validation state. Package layout is `mods/minimap_mod/minimap_mod.dll`, `mod.json`, runtime assets and the no-op `src/mod.lua`; loader binaries are not bundled.

Close Enshrouded. Run `.\install.ps1 -GameDir <actual-Enshrouded-directory> -SkipBuild` after building. Installer copies a timestamped complete prior mod backup outside mods, preserves shroudtopia.json, refuses installation while the game runs and verifies the installed DLL hash. Do not copy backup DLLs under mods. No server component is installed.

## Recovery / commits

Close the game. Move the preview's `mods/minimap_mod` folder outside mods; restore the prior `minimap_mod` backup if one existed. If `previous-install-absent.txt` exists, there was no active minimap to restore. shroudtopia.json is preserved by installation; its backup is available if later manual changes need reversing. No save files are modified by the installer.

Source milestones are independent commits on `fix/enshrouded-1.0-compat`. Use `git revert <commit>` to undo one milestone; main is untouched. Original checkout/config/loader/mod backup and machine-specific receipts are recorded locally, not embedded with private paths in this public document.

## Runtime acceptance still required

A fresh launch must establish mod registration, exact client profile selection, active camera/present/init hooks and hooked Vulkan create/images/present slots. Then verify terrain, movement, camera turning, F10/F9/F11/zoom, window/fullscreen recreation and exit/rejoin in an actual world. A successful startup log alone is not acceptance. Other installed loader mods have old signature errors in prior logs; they are not changed by this patch.

## Final static critic follow-up

The initial late-swapchain concern was addressed with the verified post-init CALL wrapper. The images-only snapshot concern was addressed by requiring nonzero captured format/width/height before 1.0 drawing. Config flags for all unavailable feeds are gated. Load resets profile offsets. Independent full-function disassembly found no fifth stack argument for init; caller sets RCX/RDX/R8/R9 and tests AL. This supports the four-pointer/bool wrapper ABI statically.

On this machine installation was attempted but refused before any game-file copy because Enshrouded was running. No new DLL-load or gameplay claim is made. Remote branch creation through the requested GitHub connector returned 403; local commits, a Git bundle and a PR draft are available, but no push or PR is claimed.
