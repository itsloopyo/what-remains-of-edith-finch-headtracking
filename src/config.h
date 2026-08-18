#pragma once

#include <string>

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
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

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
    float limit_x = 0.30f;
    float limit_y = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;
};

// Both take the directory holding the game EXE; the INI sits beside it.
// Keys absent from the file keep the defaults above, so a partial INI is valid.
// LoadConfig validates every value it reads: a key that is out of range or not
// a number keeps its default and the substitution is logged, so nothing here is
// ever NaN, infinite, or outside the range its consumer can take.
void LoadConfig(const std::string& exeDir, Config& out);
void WriteDefaultConfigIfMissing(const std::string& exeDir);

}
