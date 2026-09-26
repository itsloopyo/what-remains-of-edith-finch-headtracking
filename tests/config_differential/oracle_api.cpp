// Built with finch_ht and cameraunlock renamed on the command line
// (tests/CMakeLists.txt), so the published reader and the core code it calls are
// a separate copy from today's, and nothing here can resolve to a symbol of the
// mod under test.

#include "oracle_api.h"

#include "config.h"

namespace finch_oracle {

PublishedConfig Load(const std::string& exe_dir) {
    finch_ht::Config c;
    finch_ht::LoadConfig(exe_dir, c);
    return PublishedConfig{
        c.udp_port,
        c.enable_on_startup,
        c.world_space_yaw,
        c.yaw_mode_key,
        c.yaw_sensitivity,
        c.pitch_sensitivity,
        c.roll_sensitivity,
        c.invert_yaw,
        c.invert_pitch,
        c.invert_roll,
        c.local_smoothing,
        c.remote_smoothing,
        c.fov_offset,
        c.position_enabled,
        c.position_sensitivity_x,
        c.position_sensitivity_y,
        c.position_sensitivity_z,
        c.limit_x,
        c.limit_y,
        c.limit_z,
        c.limit_z_back,
        c.collision_enabled,
        c.collision_radius,
        c.collision_channel,
        c.collision_release_smoothing,
    };
}

void WriteDefaultConfigIfMissing(const std::string& exe_dir) { finch_ht::WriteDefaultConfigIfMissing(exe_dir); }

}  // namespace finch_oracle
