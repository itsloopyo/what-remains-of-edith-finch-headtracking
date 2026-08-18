#include "config.h"

#include <cstdio>
#include <windows.h>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/math/finite_utils.h"
#include "cameraunlock/protocol/port_utils.h"

#include "logging.h"

namespace finch_ht {

namespace {

const char* kIniName = "HeadTracking.ini";

// Bounds for the INI numbers. Deliberately far wider than anything a user
// would choose - they exist to stop a typo reaching the maths, not to
// second-guess a setting. A negative sensitivity inverts the axis, which is a
// legitimate thing to want, so those ranges stay symmetric.
constexpr float kMaxSensitivity = 10.0f;
constexpr float kMaxPositionLimit = 5.0f;   // metres
constexpr float kMaxFovOffset = 160.0f;     // ClampFov bounds the result to [10, 170]

// Nothing downstream of the INI rejects a bad float. strtod accepts "nan" and
// "inf" and overflows a literal like 1e400 to +inf; a NaN sensitivity then
// poisons the smoothing state for the rest of the session, and a NaN FovOffset
// builds a degenerate projection matrix. Both present as the view simply being
// gone, so the substitution is logged with the key that caused it instead of
// being applied quietly.
float ReadFloatChecked(const cameraunlock::IniReader& reader, const char* section,
                       const char* key, float fallback, float lo, float hi) {
    const float raw = reader.ReadFloat(section, key, fallback);
    const float value = cameraunlock::math::SanitizeFinite(raw, fallback, lo, hi);
    if (value != raw)
        Log::Line("WARNING: config [%s] %s = %g is not a number in [%g, %g] - using %g.",
            section, key, static_cast<double>(raw), static_cast<double>(lo),
            static_cast<double>(hi), static_cast<double>(value));
    return value;
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (reader.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "WARNING: Config key [%s] %s has been retired and is IGNORED. Smoothing is "
        "now two keys: LocalSmoothing (default 0, applies to a tracker on this "
        "machine) and RemoteSmoothing (default 0.15, applies to a tracker on the "
        "network). The old value is not migrated because the semantics changed - it "
        "carried a hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

}  // namespace

void LoadConfig(const std::string& exeDir, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path(exeDir))) return;

    // ReadInt yields 0 for a present-but-non-numeric value rather than the
    // default (see ini_reader.h rule 4), and a raw cast to uint16_t turns 70000
    // into port 4464. Either one binds a socket the tracker never reaches, and
    // both look exactly like "head tracking just doesn't work" from the game.
    const int rawPort = ini.ReadInt("Network", "UdpPort", out.udp_port);
    bool portValid = false;
    const std::uint16_t port = cameraunlock::NormalizeUdpPort(
        rawPort, static_cast<std::uint16_t>(out.udp_port), portValid);
    if (!portValid)
        Log::Line("WARNING: config [Network] UdpPort = %d is not in 1024-65535 (a "
                  "non-numeric value reads as 0) - using %u.", rawPort, port);
    out.udp_port = port;

    out.enable_on_startup  = ini.ReadBool ("General",  "EnableOnStartup",  out.enable_on_startup);
    out.world_space_yaw    = ini.ReadBool ("General",  "WorldSpaceYaw",    out.world_space_yaw);

    // GetAsyncKeyState reports nothing for a code outside 0x01-0xFE, so an
    // out-of-range key would leave the yaw toggle silently dead.
    const int rawYawKey = ini.ReadHex("Hotkeys", "YawModeKey", out.yaw_mode_key);
    if (rawYawKey >= 0x01 && rawYawKey <= 0xFE)
        out.yaw_mode_key = rawYawKey;
    else
        Log::Line("WARNING: config [Hotkeys] YawModeKey = 0x%X is not a virtual-key "
                  "code (0x01-0xFE) - using 0x%02X.", rawYawKey, out.yaw_mode_key);

    out.yaw_sensitivity    = ReadFloatChecked(ini, "Rotation", "YawSensitivity",
                                              out.yaw_sensitivity, -kMaxSensitivity, kMaxSensitivity);
    out.pitch_sensitivity  = ReadFloatChecked(ini, "Rotation", "PitchSensitivity",
                                              out.pitch_sensitivity, -kMaxSensitivity, kMaxSensitivity);
    out.roll_sensitivity   = ReadFloatChecked(ini, "Rotation", "RollSensitivity",
                                              out.roll_sensitivity, -kMaxSensitivity, kMaxSensitivity);
    out.invert_yaw         = ini.ReadBool ("Rotation", "InvertYaw",        out.invert_yaw);
    out.invert_pitch       = ini.ReadBool ("Rotation", "InvertPitch",      out.invert_pitch);
    out.invert_roll        = ini.ReadBool ("Rotation", "InvertRoll",       out.invert_roll);

    out.local_smoothing    = ReadFloatChecked(ini, "Rotation", "LocalSmoothing",
                                              out.local_smoothing, 0.0f, 1.0f);
    out.remote_smoothing   = ReadFloatChecked(ini, "Rotation", "RemoteSmoothing",
                                              out.remote_smoothing, 0.0f, 1.0f);
    WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");

    out.fov_offset         = ReadFloatChecked(ini, "View", "FovOffset",
                                              out.fov_offset, -kMaxFovOffset, kMaxFovOffset);

    out.position_enabled   = ini.ReadBool ("Position", "Enabled",          out.position_enabled);
    out.position_sensitivity_x = ReadFloatChecked(ini, "Position", "SensitivityX",
                                                  out.position_sensitivity_x,
                                                  -kMaxSensitivity, kMaxSensitivity);
    out.position_sensitivity_y = ReadFloatChecked(ini, "Position", "SensitivityY",
                                                  out.position_sensitivity_y,
                                                  -kMaxSensitivity, kMaxSensitivity);
    out.position_sensitivity_z = ReadFloatChecked(ini, "Position", "SensitivityZ",
                                                  out.position_sensitivity_z,
                                                  -kMaxSensitivity, kMaxSensitivity);
    // PositionProcessor clamps each axis with [-limit, +limit], so a negative
    // limit inverts the bounds and every input comes back as one edge or the
    // other - the camera snaps between two extremes instead of following the
    // head. Zero is legitimate (that axis stops moving), negative never is.
    out.limit_x            = ReadFloatChecked(ini, "Position", "LimitX",
                                              out.limit_x, 0.0f, kMaxPositionLimit);
    out.limit_y            = ReadFloatChecked(ini, "Position", "LimitY",
                                              out.limit_y, 0.0f, kMaxPositionLimit);
    out.limit_z            = ReadFloatChecked(ini, "Position", "LimitZ",
                                              out.limit_z, 0.0f, kMaxPositionLimit);
    out.limit_z_back       = ReadFloatChecked(ini, "Position", "LimitZBack",
                                              out.limit_z_back, 0.0f, kMaxPositionLimit);
    WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
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
