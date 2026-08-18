# What Remains of Edith Finch Head Tracking

6DOF head tracking for What Remains of Edith Finch: your head moves the camera while the mouse or controller still drives look and interaction, with no VR headset required.

<!-- ![Mod GIF](https://raw.githubusercontent.com/itsloopyo/what-remains-of-edith-finch-headtracking/main/assets/readme-clip.gif) -->

## Features

- **Decoupled look and aim** - head tracking moves what you see; the mouse or controller still drives where the game thinks you are looking, so interaction prompts and line traces are unchanged.
- **6DOF positional tracking** - lean, peek, and move closer to look around the scene.
- **Field of view control** - the game ships no FOV setting, so the mod adds one.

## Requirements

- [What Remains of Edith Finch on Steam](https://store.steampowered.com/app/501300/) (Steam build).
- A head tracking source that speaks the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack), AITrack, or a phone app.
- Windows 10 or 11, 64-bit.

## Installation

1. Download the latest `EdithFinchHeadTracking-v<version>-installer.zip` from the [Releases](../../releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the install folder yourself. Either pass the path as an argument:

```powershell
install.cmd "D:\Games\Steam\steamapps\common\EdithFinch"
```

or set the environment variable before running it:

```powershell
$env:EDITH_FINCH_PATH = "D:\Games\Steam\steamapps\common\EdithFinch"
```

### Manual Installation

To place the files by hand, use the Nexus ZIP (it contains only the deploy tree) or the same files from the installer ZIP:

1. Copy `vendor\ultimate-asi-loader\dinput8.dll` into `<game>\FinchGame\Binaries\Win64\`, renamed to `winmm.dll`. That is the ASI loader. The game EXE imports `winmm.dll` directly, and `dwmapi.dll` is already taken by the UE4SS install this game ships with.
2. Copy `EdithFinchHeadTracking.asi` into the same folder, alongside `FinchGame.exe`.
3. Launch the game once. `HeadTracking.ini` and `HeadTracking.log` are written into that folder.

## Setting Up OpenTrack

In OpenTrack, set **Output** to **UDP over network**, address `127.0.0.1`, port `4242`. Any sample rate works; the mod estimates the incoming rate per stream and interpolates it up to your frame rate.

### VR Headset Setup

1. Connect the headset to the PC over Air Link or Virtual Desktop.
2. Start SteamVR so the headset is tracked.
3. In OpenTrack, set **Input** to **SteamVR**, and **Output** to UDP on `127.0.0.1:4242`.

### Webcam Setup

1. In OpenTrack, set **Input** to **neuralnet tracker**, which tracks your face from a plain webcam with no markers.
2. Set **Output** to UDP on `127.0.0.1:4242`.
3. Sit at your normal playing distance and press Home in-game to recenter.

### Phone App Setup

Any OpenTrack-compatible phone tracker works, for example [Headcam](https://headcam.app).

- If the app does its own smoothing, point it straight at your PC's LAN IP on port `4242`.
- If you want OpenTrack's curve mapping and filters, have the app feed OpenTrack instead and let OpenTrack output UDP to `127.0.0.1:4242`.

A phone on WiFi is classed as a remote connection and uses `RemoteSmoothing`; a tracker sending to `127.0.0.1` uses `LocalSmoothing`.

## Controls

Two equivalent binding sets. Use whichever your keyboard has; the chords exist for keyboards without a nav cluster.

| Action                        | Nav-cluster | Chord          |
|-------------------------------|-------------|----------------|
| Recenter                      | `Home`      | `Ctrl+Shift+T` |
| Toggle tracking               | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode           | `Page Up`   | `Ctrl+Shift+G` |
| Toggle yaw mode (world/local) | `Page Down` | `Ctrl+Shift+H` |

Cycling the tracking mode steps through: normal head tracking, rotation only, position only, and back to normal.

## Configuration

`HeadTracking.ini` is written on first launch into `<game>\FinchGame\Binaries\Win64\`, next to `FinchGame.exe`. Edit it and restart the game to apply. Any key you leave out keeps its default, so a partial file is valid.

```ini
[Network]
; UDP port the mod listens on. Must match your tracker's output port.
UdpPort=4242

[General]
EnableOnStartup=1
; Yaw mode: 1 = horizon-locked yaw about the world up-axis (default),
; 0 = yaw about the camera's own up-axis. Toggle in-game with Page Down.
WorldSpaceYaw=1

[Hotkeys]
; Virtual-key code for the yaw-mode toggle. 0x22 = Page Down.
; The Ctrl+Shift+H chord always toggles it as well.
YawModeKey=0x22

[Rotation]
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=0
InvertPitch=0
InvertRoll=0
; Smoothing applied when the tracker runs on this machine (loopback).
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
LocalSmoothing=0.0
; Smoothing applied when the tracker is a remote device on the network.
; 0 = no smoothing, 1 = heavy. Covers rotation and position.
RemoteSmoothing=0.15

[View]
; Degrees added to the field of view the game asks for. 0 = untouched.
; Every view measured so far runs at 80, so FovOffset=25 renders at 105.
; It is an offset so a chapter with its own framing keeps the difference.
; The result is capped at 170. Head tracking stays 1:1 at any FOV.
FovOffset=0.0

[Position]
Enabled=1
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
; Movement limits in metres. Z is asymmetric: more range leaning forward
; than back, to stop the camera clipping through the player.
LimitX=0.30
LimitY=0.20
LimitZ=0.40
LimitZBack=0.10
```

## Troubleshooting

**Mod not loading**

- Confirm `winmm.dll` and `EdithFinchHeadTracking.asi` are both in `<game>\FinchGame\Binaries\Win64\`.
- Check `HeadTracking.log` in that folder. No log file at all means the ASI loader is not being loaded.
- "Staying dormant" in the log means the game build did not match a known profile. File an issue with the log attached.

**No tracking response**

- Confirm your tracker is sending UDP to `127.0.0.1:4242` (or your PC's LAN IP for a phone app).
- "Failed to bind UDP port 4242" in the log means another program already holds the port, usually a game left running. Close it; the mod picks the port up within about half a second and logs "tracking is live". No restart needed.
- Press `Home` (or `Ctrl+Shift+T`) to recenter, and check tracking is not toggled off with `End`.

**Jittery or unstable tracking**

- Raise `LocalSmoothing` (tracker on this PC) or `RemoteSmoothing` (phone or network tracker) toward 0.3.
- For a webcam tracker, improve the lighting on your face; the neuralnet tracker gets noisy in the dark.

**Wrong yaw at steep angles**

- Toggle between world-locked and camera-local yaw with `Page Down` (or `Ctrl+Shift+H`). World-locked is horizon-stable; camera-local follows the camera's current up-axis and leans on steeply pitched turns.
- Set `WorldSpaceYaw` in the INI to make your preference the startup default.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod files. The ASI loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio 2022 or newer with the C++ workload, and [pixi](https://pixi.sh).

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/what-remains-of-edith-finch-headtracking
cd what-remains-of-edith-finch-headtracking
pixi run build
pixi run test
pixi run package
```

Outputs land in `release/`.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- What Remains of Edith Finch by [Giant Sparrow](https://store.steampowered.com/app/501300/), published by Annapurna Interactive.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, the loader shim this mod ships with.
- [OpenTrack](https://github.com/opentrack/opentrack) for the head tracking wire protocol.
- [MinHook](https://github.com/TsudaKageyu/minhook) for runtime function hooking.
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core) for the shared tracking pipeline.

Full third-party attribution is in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Giant Sparrow or Annapurna Interactive. Use at your own risk.
