#pragma once

#include <string>

// The pre-canonical HeadTracking.ini reader, frozen. It reads a file the way
// the last build before the canonical config format did, so a player's old file
// is carried over as that build read it. Never edit anything in this folder:
// tests/config_differential/ pins every file here by hash.
//
// Frozen from src/config.h and src/config.cpp at c010822, whose reader is byte
// for byte v1.1.1's (LoadConfig, ReadFloatChecked and WarnRetiredSmoothingKey),
// with three changes: it fills this frozen copy of that commit's Config and its
// defaults instead of the runtime type, it writes nothing, and it lives in
// namespace finch_ht::legacy. The defaults are written as the literals the code
// held then (cameraunlock-core's PositionSettings limits and its smoothing
// defaults), so a later change to core cannot move what an old file means.
namespace finch_ht::legacy {

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

    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

    // Degrees added to the field of view the game asks for.
    float fov_offset = 0.0f;

    // The tracking mode at startup: rotation and position, or rotation only.
    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    // LimitY bounded both vertical directions, up and down.
    float limit_x = 0.30f;
    float limit_y = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;

    bool collision_enabled = false;
    // The swept sphere's radius in cm, which is the standoff from a surface.
    float collision_radius = 10.0f;
    int collision_channel = 0;
    float collision_release_smoothing = 0.9f;
};

// Takes the directory holding the game EXE; HeadTracking.ini sits beside it.
// Keys absent from the file keep the values `out` holds, so a partial INI is
// valid. A float that is not a finite number keeps the value `out` held and one
// outside its range is clamped into it; a port, yaw key or collision channel
// outside its range keeps the value `out` held. Each substitution is logged.
void Load(const std::string& exeDir, Config& out);

}  // namespace finch_ht::legacy
