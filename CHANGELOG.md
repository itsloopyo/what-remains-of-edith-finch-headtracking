# Changelog

## [1.1.0] - 2026-08-20

### Added

- drop mod-side recentring, keep one previous log generation

## [Unreleased]

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
