# Native aimbot

This fork contains an optional client-side aiming module. It is disabled by default and only
runs during live, non-spectator gameplay. Normal keyboard and mouse input is processed first;
the result is then adjusted before it is copied into the outgoing user command.

## Building on Windows

Install Visual Studio with C/C++ support, CMake, Git, and PowerShell. From a Developer
PowerShell in the repository root:

```powershell
powershell -File bootstrap.ps1
cmake --preset msbuild-x64
cmake --build --preset msbuild-x64-release
```

`bootstrap.ps1` may be launched from any working directory; it resolves `version.json` and
the `vcpkg` checkout relative to its own repository location. The repository pins vcpkg to
`2025.06.13`, so bootstrap does not depend on whichever revision happens to be current.

If an older checkout reports `Unable to checkout correct version of vcpkg without
'version.json'`, update the checkout and confirm that `version.json` exists next to
`bootstrap.ps1`. If a failed attempt left a partial `vcpkg` directory, remove that directory
and run `powershell -File bootstrap.ps1` again.

The bootstrap also initializes the `src/qwprot` protocol-header dependency. If CMake reports
that `src/qwprot/src/protocol.h` is missing, rerun `bootstrap.ps1`; do not continue directly
to `cmake --build`, because CMake did not generate a usable Visual Studio project. Current
bootstrap versions recover this dependency even from source distributions whose Git
submodule metadata is incomplete.

The normal 64-bit Windows executable is produced under
`build-msbuild-x64/Release/ezquake.exe`. The supported cross-build flow is:

If the resource compiler reports `RC2104: undefined keyword or key name` followed by a short
Git hash, update the checkout and delete `build-msbuild-x64` before configuring again. Older
versions incorrectly used an untagged checkout's commit abbreviation as the numeric Windows
`FILEVERSION`; current versions fall back to `0.0.0` when no semantic-version tag is available.

```sh
./bootstrap.sh
cmake --preset mingw64-x64-cross
cmake --build build-mingw64-x64-cross --config Release
```

## Installing

Back up the existing client executable, then copy the generated `ezquake.exe` into the nQuake
directory containing the existing game data. No PAK or other asset changes are required.

## Commands

| Command | Description |
| --- | --- |
| `aimbot_toggle` | Toggle persistent activation. |
| `aimbot_status` | Print configuration and current target. |
| `+aimbot` / `-aimbot` | Hold-to-aim activation, independent of the persistent setting. |

## CVARs

| CVAR | Default | Range / meaning |
| --- | ---: | --- |
| `aimbot_enabled` | `0` | Persistent activation (`0`/`1`). |
| `aimbot_fov` | `30` | Maximum angular distance, clamped to 1–180 degrees. |
| `aimbot_smooth` | `0.25` | `0` snaps; values up to `1` progressively slow aiming. |
| `aimbot_target_mode` | `0` | `0`: angular distance; `1`: world distance. |
| `aimbot_visibility` | `1` | Require a client collision trace to reach the target. |
| `aimbot_teamcheck` | `1` | Ignore same-team players in team modes. |
| `aimbot_lock` | `1` | Retain a valid target (with a 25% FOV grace margin). |
| `aimbot_height` | `0.5` | Normalized player-bounds aim height, clamped to 0–1. |
| `aimbot_prediction` | `1` | Enable rocket intercept prediction. |
| `aimbot_prediction_scale` | `1.0` | Target-motion multiplier, clamped to 0–2. |
| `aimbot_debug` | `0` | Print target diagnostics at most once per second. |
| `aimbot_marker` | `0` | Reserved; see limitations below. |

Example configuration:

```text
bind F6 aimbot_toggle
bind mouse4 +aimbot

aimbot_fov 20
aimbot_smooth 0.4
aimbot_visibility 1
aimbot_prediction 1
```

## Architecture and limitations

Target data comes from the current parsed `player_state_t` frame. The aim point prefers the
client-interpolated/antilag origin when present and otherwise uses the current state origin.
Hitscan weapons aim at that current visible position. Rockets use the state velocity and the
same 1000 unit/second speed used by ezQuake weapon prediction to solve the quadratic intercept
equation; impossible intercepts fall back to the current position.

Visibility uses ezQuake's `PM_TraceLine`, which includes the locally known BSP, brush models,
and configured player collision hulls. It cannot account for geometry or player state not yet
received from the QuakeWorld server. A hit within 32 units of the center-mass endpoint counts
as reaching the target because the line may enter its collision hull before reaching its
center.

`aimbot_marker` is registered for configuration compatibility but is not drawn. A reliable
marker belongs in the renderer/HUD projection stage; adding that dependency to the input-side
module would make this otherwise isolated feature invasive. Grenade ballistic prediction is
also not implemented because gravity requires a different trajectory solver than rockets.
