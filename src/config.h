#pragma once

#include <string>

#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace finch_ht {

struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;
    // true = yaw about the world up-axis (horizon-locked); false = yaw about
    // the camera's own up-axis, which leans on pitched turns.
    bool world_space_yaw = true;
    int yaw_mode_key = 0x22;  // Page Down

    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    // Two smoothing parameters, picked per connection from the packet source
    // address. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // Degrees added to the field of view the game asks for, on the render path
    // only. 0 = leave it alone. The game has no FOV setting of its own and each
    // chapter authors its own value (the comic-book camera runs at 80), so this
    // is additive rather than an absolute number - a widened view keeps the
    // authored differences instead of flattening them to one figure.
    float fov_offset = 0.0f;

    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // Stop the lean putting the eye inside level geometry, by sweeping the
    // engine's own collision from where the game put the camera toward where
    // the head asks it to go. OFF until it has been confirmed in game: it calls
    // into the engine every rendered frame the head is off centre, and an
    // unverified channel either blocks on nothing or blocks on everything.
    bool collision_enabled = false;
    // Radius of the swept sphere, in UE units (cm), and so the standoff the eye
    // keeps from a surface. Must exceed the camera's near clip distance or the
    // wall is culled before the eye reaches it and the player sees through it
    // anyway.
    float collision_radius = 10.0f;
    // ETraceTypeQuery index. Which one the level's geometry blocks is a project
    // setting rather than an engine constant, so it is a value to try and read
    // back out of the log, not a constant to hard-code.
    int collision_channel = 0;
    // How quickly the lean reopens once an obstruction clears, 0 to 1 on the
    // same scale as the smoothing values. Tightening is never smoothed.
    float collision_release_smoothing = 0.9f;
};

// Both take the directory holding the game EXE; the INI sits beside it.
// Keys absent from the file keep the defaults above, so a partial INI is valid.
// LoadConfig validates every value it reads: a key that is out of range or not
// a number keeps its default and the substitution is logged, so nothing here is
// ever NaN, infinite, or outside the range its consumer can take.
void LoadConfig(const std::string& exeDir, Config& out);
void WriteDefaultConfigIfMissing(const std::string& exeDir);

}
