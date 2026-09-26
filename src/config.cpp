#include "config.h"

#include <cstdio>
#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace finch_ht {

namespace {

const char* kIniName = "HeadTracking.ini";

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

}  // namespace

void LoadConfig(const std::string& exeDir, Config& out) {
    legacy::Config read;
    legacy::Load(exeDir, read);

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.yaw_mode_key = read.yaw_mode_key;
    out.yaw_sensitivity = read.yaw_sensitivity;
    out.pitch_sensitivity = read.pitch_sensitivity;
    out.roll_sensitivity = read.roll_sensitivity;
    out.invert_yaw = read.invert_yaw;
    out.invert_pitch = read.invert_pitch;
    out.invert_roll = read.invert_roll;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.fov_offset = read.fov_offset;
    out.position_enabled = read.position_enabled;
    out.position_sensitivity_x = read.position_sensitivity_x;
    out.position_sensitivity_y = read.position_sensitivity_y;
    out.position_sensitivity_z = read.position_sensitivity_z;
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;
    out.collision_enabled = read.collision_enabled;
    out.collision_radius = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
}

void WriteDefaultConfigIfMissing(const std::string& exeDir) {
    const std::string p = ini_path(exeDir);
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    FILE* f = nullptr;
    fopen_s(&f, p.c_str(), "w");
    if (!f) {
        Log::Line("config: could not write %s - the mod runs on built-in defaults "
                  "and there is no file to edit.", p.c_str());
        return;
    }
    std::fprintf(f,
        "; What Remains of Edith Finch Head Tracking - configuration\n"
        "; Edit values, restart the game to apply.\n\n"
        "[Network]\n"
        "UdpPort=4242\n\n"
        "[General]\n"
        "EnableOnStartup=1\n"
        "; Yaw mode: 1 = horizon-locked yaw about the world up-axis (default),\n"
        "; 0 = yaw about the camera's own up-axis. Toggle in-game with Page Down.\n"
        "WorldSpaceYaw=1\n\n"
        "[Hotkeys]\n"
        "; Virtual-key code for the yaw-mode toggle. 0x22 = Page Down.\n"
        "; The Ctrl+Shift+H chord always toggles it as well.\n"
        "YawModeKey=0x22\n\n"
        "[Rotation]\n"
        "YawSensitivity=1.0\n"
        "PitchSensitivity=1.0\n"
        "RollSensitivity=1.0\n"
        "InvertYaw=0\n"
        "InvertPitch=0\n"
        "InvertRoll=0\n"
        "; Smoothing applied when the tracker runs on this machine (loopback).\n"
        "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
        "LocalSmoothing=0.0\n"
        "; Smoothing applied when the tracker is a remote device on the network.\n"
        "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
        "RemoteSmoothing=0.15\n\n"
        "[View]\n"
        "; Degrees added to the game's own field of view. 0 = untouched.\n"
        "; The game ships no FOV setting and authors a value per camera (every\n"
        "; view measured so far runs at 80), so this widens or narrows what the\n"
        "; game asks for instead of pinning one number over the top - a chapter\n"
        "; that picks its own framing keeps it. The result is capped at 170.\n"
        "; Try 10 to 20 for a wider view; head tracking stays 1:1 with your head\n"
        "; at any FOV. HeadTracking.log reports the game's value and any change.\n"
        "FovOffset=0.0\n\n"
        "[Position]\n"
        "Enabled=1\n"
        "SensitivityX=1.0\n"
        "SensitivityY=1.0\n"
        "SensitivityZ=1.0\n"
        "LimitX=0.30\n"
        "LimitY=0.20\n"
        "LimitZ=0.40\n"
        "LimitZBack=0.10\n");
    std::fclose(f);
}

}
