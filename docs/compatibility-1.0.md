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
