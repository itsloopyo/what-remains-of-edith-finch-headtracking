# Changelog

## [1.2.0] - 2026-09-30

### Changed

- Settings move to `FinchGame\Binaries\Win64\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
  - The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.
  - A hotkey set to Ctrl, Shift or Alt on its own. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord where it has one.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. The toggle, tracking mode and yaw mode keys can all be changed now, chords included; before, only the yaw key could, as a virtual-key code (`YawModeKey=0x22`), and a code you set there is carried over as its key name, or as the same `0x` code where the key has no name.
- Keys that moved or were renamed: `[Rotation] LocalSmoothing` and `RemoteSmoothing` are in `[Smoothing]`, `[Position] LimitX`, `LimitY`, `LimitZ` and `LimitZBack` are `PositionLimitX`, `PositionLimitY`, `PositionLimitZ` and `PositionLimitZBack`, and `[Position] CollisionRadius` is `CollisionMargin`. `LimitY` bounded both raising and lowering your head; `PositionLimitY` and the new `PositionLimitYDown` set each one, and your old `LimitY` is carried into both. `[Position] Enabled` is now the startup tracking mode, `[General] RotationEnabled` and `[Position] PositionEnabled`: `Enabled=0` imports as rotation only.
- The tracking mode you pick with Page Up / Ctrl+Shift+G and the yaw mode you pick with Page Down / Ctrl+Shift+H are saved to `CameraUnlock.ini`, and the game starts in them next time. End still turns head tracking on or off for the current session only.
- The lean collision clamp is on by default. It shipped off in 1.1.1 until it had been checked in game, and an imported `CollisionEnabled` is not carried over (the untested feature above). `CollisionMargin` (10 cm) and `CollisionChannel` (0) keep this game's own values and are never read from `Defaults.ini`.
- `CollisionChannel` takes any number in `CameraUnlock.ini`. A channel outside 0 to 31 turns the wall check off for the session, with a line in the log, where 1.1.1 fell back to channel 0.
- `uninstall.cmd` keeps `CameraUnlock.ini` and `HeadTracking.ini`, so your settings survive a reinstall; it used to delete `HeadTracking.ini`. No release ZIP or launcher install carries a config file; the mod creates `CameraUnlock.ini` the first time the game starts.

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Removed

- The sensitivity and axis inversion settings (`[Rotation] YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch`, `InvertRoll`, `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`). Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.

### Fixed

- Switching tracking mode with Page Up / Ctrl+Shift+G no longer resets the lean's smoothing while the game is part way through a frame. The new mode now takes effect at the start of the next frame.
- A wall that held your lean back no longer carries over once the lean stops, for example after turning tracking off with End, switching to rotation only, or looking straight down during the comic and cannery sequences. Before this fix the lean eased back out from where the wall had stopped it when it resumed.

## [1.1.1] - 2026-09-03

### Added

- optional lean collision clamp, off by default

### Fixed

- mirror the vertical limit and restore the MIT grant
- re-sync THIRD-PARTY-NOTICES.md before cutting the tag

## [1.1.0] - 2026-08-20

### Changed

- Removed recentring from the mod. The `Home` / `Ctrl+Shift+T` hotkey is gone and
  the tracker pose is applied as sent. Every tracker app centres itself, so a
  mod-side centre sat in series with the tracker's own and the two drifted apart.
  Centre in your tracker app instead: OpenTrack's Center bind, or the CENTER
  button in Headcam.

### Added

- Added a single previous log generation: the launch before the current one is
  kept as `HeadTracking.prev.log`. The crash handler asks the user to send the
  log, and relaunching to go find it used to truncate away the crash being
  reported.

## [1.0.0] - 2026-08-18

### Added

- Added 6DOF head tracking for What Remains of Edith Finch (UE4), built as an
  Ultimate ASI Loader plugin that hooks the player view point in the render
  path only, so look and aim stay decoupled.
- Added an OpenTrack UDP receiver (port 4242) with per-axis sensitivity,
  inversion and smoothing, and a 6DOF position offset applied in the clean
  camera basis.
- Added nav-cluster hotkeys (Home/End/PageUp/PageDown) plus Ctrl+Shift+T/Y/G/H
  chord alternatives.
- Added a PE-fingerprint build profile failsafe: the mod stays dormant on any
  build it does not recognise, so the game always runs vanilla on an unknown
  patch.
- Added `[View] FovOffset`, a field-of-view control for a game that ships none.
  The render-path caller hands the view point out of a single
  FMinimalViewInfo, so the hook reads the FOV the frame is about to be built
  with and can widen it in place. The offset is added to whatever the current
  camera asks for, so an authored framing keeps its shape; measured 105 degrees
  rendered from the game's 80 at `FovOffset=25`. Game logic never sees the
  change, and the log reports the game's own value and any change to it.

### Changed

- Changed head tracking to hold still on views pinned straight down. Lewis'
  cannery chapter parks the player camera on the daydream's top-down 2D map
  while the cannery fills the screen, and Barbara's comic chapter parks it
  above the open comic book. In both, the view the player is actually watching
  is drawn outside the player-camera path, so driving the pinned camera only
  swung the map or the book about. Tracking resumes on its own once the game
  hands back a camera with a horizon.
- Changed smoothing to two keys in `[Rotation]`: `LocalSmoothing` (default 0.0)
  for a tracker running on this machine and `RemoteSmoothing` (default 0.15)
  for a remote device on the network, selected per connection from the packet
  source address.

### Removed

- Removed `[Rotation] Smoothing` and `[Position] Smoothing`; rotation and
  position both use the new smoothing pair.
- Removed the hidden 0.15 baseline smoothing floor, so a local tracker gets
  zero-latency tracking by default.
