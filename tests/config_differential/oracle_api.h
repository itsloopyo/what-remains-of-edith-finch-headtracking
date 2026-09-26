#pragma once

#include <string>

// The oracle: the HeadTracking.ini reader of the newest published build, v1.1.1
// (e6a9d7d) against cameraunlock-core 3038291, compiled only into this test.
// oracle/src/ holds that build's config.h, config.cpp and logging.h byte for
// byte as the tag has them. Every core file they include is hash-equal at
// 3038291 and at the pin, so they compile against the pin's headers and sources
// (provenance in differential_tests.cpp). oracle_api.cpp builds them with their
// namespaces renamed, so they link beside today's code.
//
// v1.0.0 (3c02b99, core e1c29b7), v1.1.0 (7cb2cfb, core 3465659) and the dev
// pre-release (a4c0f26, core e1c29b7) read the same file with an older reader
// that had no collision keys, and wrote the same first-run file.
namespace finch_oracle {

// finch_ht::Config as v1.1.1 declared it, field for field.
struct PublishedConfig {
    int udp_port;
    bool enable_on_startup;
    bool world_space_yaw;
    int yaw_mode_key;
    float yaw_sensitivity;
    float pitch_sensitivity;
    float roll_sensitivity;
    bool invert_yaw;
    bool invert_pitch;
    bool invert_roll;
    float local_smoothing;
    float remote_smoothing;
    float fov_offset;
    bool position_enabled;
    float position_sensitivity_x;
    float position_sensitivity_y;
    float position_sensitivity_z;
    float limit_x;
    float limit_y;
    float limit_z;
    float limit_z_back;
    bool collision_enabled;
    float collision_radius;
    int collision_channel;
    float collision_release_smoothing;
};

// The published LoadConfig on a default Config, as its bootstrap called it.
PublishedConfig Load(const std::string& exe_dir);

// The published WriteDefaultConfigIfMissing: that build's first-run file.
void WriteDefaultConfigIfMissing(const std::string& exe_dir);

}  // namespace finch_oracle
