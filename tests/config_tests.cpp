// Characterization tests for the INI contract: the keys the mod reads, the
// defaults it falls back to, and the agreement between the file it writes on
// first run and the defaults compiled into Config. A key renamed on one side
// only would otherwise ship as a setting that silently does nothing.

#include "config.h"

#include <cstdio>
#include <string>

#include <windows.h>

#include "test_support.h"

namespace {

using finch_tests::Check;
using finch_tests::NearEqual;

std::string MakeTempDir()
{
    char tempRoot[MAX_PATH]{};
    GetTempPathA(MAX_PATH, tempRoot);
    std::string dir = std::string(tempRoot) + "finch-ht-config-tests";
    CreateDirectoryA(dir.c_str(), nullptr);
    DeleteFileA((dir + "\\HeadTracking.ini").c_str());
    return dir;
}

void WriteIni(const std::string& dir, const char* body)
{
    FILE* file = nullptr;
    fopen_s(&file, (dir + "\\HeadTracking.ini").c_str(), "w");
    if (file == nullptr) return;
    std::fputs(body, file);
    std::fclose(file);
}

void MissingFileTests(int& failures)
{
    const std::string dir = MakeTempDir();

    finch_ht::Config config;
    finch_ht::LoadConfig(dir, config);

    Check(failures, config.udp_port == 4242, "a missing INI leaves the OpenTrack port at 4242");
    Check(failures, config.enable_on_startup, "a missing INI leaves tracking enabled on startup");
    Check(failures, config.world_space_yaw, "a missing INI leaves yaw horizon-locked");
    Check(failures, NearEqual(config.local_smoothing, 0.0f),
          "local smoothing defaults to 0 - a tracker on this machine is already stable");
    Check(failures, NearEqual(config.remote_smoothing, 0.15f),
          "remote smoothing defaults to 0.15 for a phone over WiFi");
    Check(failures, NearEqual(config.fov_offset, 0.0f), "the FOV is untouched by default");
}

void ParsingTests(int& failures)
{
    const std::string dir = MakeTempDir();
    WriteIni(dir,
        "[Network]\nUdpPort=5252\n"
        "[General]\nEnableOnStartup=0\nWorldSpaceYaw=0\n"
        "[Hotkeys]\nYawModeKey=0x51\n"
        "[Rotation]\nYawSensitivity=1.5\nPitchSensitivity=0.5\nRollSensitivity=2.0\n"
        "InvertYaw=1\nInvertPitch=1\nInvertRoll=1\n"
        "LocalSmoothing=0.25\nRemoteSmoothing=0.75\n"
        "[View]\nFovOffset=25.0\n"
        "[Position]\nEnabled=0\nSensitivityX=2.0\nSensitivityY=3.0\nSensitivityZ=4.0\n"
        "LimitX=0.11\nLimitY=0.22\nLimitZ=0.33\nLimitZBack=0.44\n");

    finch_ht::Config config;
    finch_ht::LoadConfig(dir, config);

    Check(failures, config.udp_port == 5252, "UdpPort is read from [Network]");
    Check(failures, !config.enable_on_startup && !config.world_space_yaw,
          "the [General] booleans are read");
    Check(failures, config.yaw_mode_key == 0x51, "YawModeKey is read as hex");
    Check(failures, NearEqual(config.yaw_sensitivity, 1.5f)
                 && NearEqual(config.pitch_sensitivity, 0.5f)
                 && NearEqual(config.roll_sensitivity, 2.0f),
          "the rotation sensitivities are read");
    Check(failures, config.invert_yaw && config.invert_pitch && config.invert_roll,
          "the inversion flags are read");
    Check(failures, NearEqual(config.local_smoothing, 0.25f)
                 && NearEqual(config.remote_smoothing, 0.75f),
          "both smoothing parameters are read");
    Check(failures, NearEqual(config.fov_offset, 25.0f), "FovOffset is read from [View]");
    Check(failures, !config.position_enabled, "position tracking can be switched off");
    Check(failures, NearEqual(config.position_sensitivity_x, 2.0f)
                 && NearEqual(config.position_sensitivity_y, 3.0f)
                 && NearEqual(config.position_sensitivity_z, 4.0f),
          "the position sensitivities are read");
    Check(failures, NearEqual(config.limit_x, 0.11f) && NearEqual(config.limit_y, 0.22f)
                 && NearEqual(config.limit_z, 0.33f) && NearEqual(config.limit_z_back, 0.44f),
          "the position limits are read");
}

void RetiredSmoothingKeyTests(int& failures)
{
    const std::string dir = MakeTempDir();
    WriteIni(dir, "[Rotation]\nSmoothing=0.9\n");

    finch_ht::Config config;
    finch_ht::LoadConfig(dir, config);

    Check(failures, NearEqual(config.local_smoothing, 0.0f)
                 && NearEqual(config.remote_smoothing, 0.15f),
          "the retired single Smoothing key is ignored, not migrated");
}

void WrittenDefaultsMatchCompiledDefaultsTests(int& failures)
{
    const std::string dir = MakeTempDir();
    finch_ht::WriteDefaultConfigIfMissing(dir);

    finch_ht::Config written;
    finch_ht::LoadConfig(dir, written);
    const finch_ht::Config compiled;

    const bool same =
        written.udp_port == compiled.udp_port &&
        written.enable_on_startup == compiled.enable_on_startup &&
        written.world_space_yaw == compiled.world_space_yaw &&
        written.yaw_mode_key == compiled.yaw_mode_key &&
        NearEqual(written.yaw_sensitivity, compiled.yaw_sensitivity) &&
        NearEqual(written.pitch_sensitivity, compiled.pitch_sensitivity) &&
        NearEqual(written.roll_sensitivity, compiled.roll_sensitivity) &&
        written.invert_yaw == compiled.invert_yaw &&
        written.invert_pitch == compiled.invert_pitch &&
        written.invert_roll == compiled.invert_roll &&
        NearEqual(written.local_smoothing, compiled.local_smoothing) &&
        NearEqual(written.remote_smoothing, compiled.remote_smoothing) &&
        NearEqual(written.fov_offset, compiled.fov_offset) &&
        written.position_enabled == compiled.position_enabled &&
        NearEqual(written.position_sensitivity_x, compiled.position_sensitivity_x) &&
        NearEqual(written.position_sensitivity_y, compiled.position_sensitivity_y) &&
        NearEqual(written.position_sensitivity_z, compiled.position_sensitivity_z) &&
        NearEqual(written.limit_x, compiled.limit_x) &&
        NearEqual(written.limit_y, compiled.limit_y) &&
        NearEqual(written.limit_z, compiled.limit_z) &&
        NearEqual(written.limit_z_back, compiled.limit_z_back);
    Check(failures, same, "the INI written on first run round-trips to the compiled defaults");

    // Second call must not clobber a user's edits.
    WriteIni(dir, "[Network]\nUdpPort=9999\n");
    finch_ht::WriteDefaultConfigIfMissing(dir);
    finch_ht::Config afterSecondCall;
    finch_ht::LoadConfig(dir, afterSecondCall);
    Check(failures, afterSecondCall.udp_port == 9999,
          "an existing INI is never overwritten");
}

// Nothing downstream of LoadConfig rejects a bad value, so the INI is the only
// place a hostile or fat-fingered number can be stopped. Each case below has a
// failure mode that reaches the player with no diagnostic: a port that binds
// somewhere the tracker never reaches, a NaN that poisons the pose pipeline for
// the rest of the session, or a negative limit that pins the camera off-centre.
void PortValidationTests(int& failures)
{
    const std::string dir = MakeTempDir();

    // GetPrivateProfileIntA yields 0 - not the default - for text it cannot
    // parse, and bind(0) succeeds on an OS-assigned ephemeral port.
    WriteIni(dir, "[Network]\nUdpPort=not-a-number\n");
    finch_ht::Config garbage;
    finch_ht::LoadConfig(dir, garbage);
    Check(failures, garbage.udp_port == 4242,
          "a non-numeric UdpPort falls back to 4242 instead of binding port 0");

    // 70000 & 0xFFFF == 4464: a raw cast to uint16_t would bind a wrong port.
    WriteIni(dir, "[Network]\nUdpPort=70000\n");
    finch_ht::Config tooBig;
    finch_ht::LoadConfig(dir, tooBig);
    Check(failures, tooBig.udp_port == 4242,
          "a UdpPort above 65535 falls back rather than truncating to 4464");

    WriteIni(dir, "[Network]\nUdpPort=-1\n");
    finch_ht::Config negative;
    finch_ht::LoadConfig(dir, negative);
    Check(failures, negative.udp_port == 4242, "a negative UdpPort falls back");

    WriteIni(dir, "[Network]\nUdpPort=80\n");
    finch_ht::Config privileged;
    finch_ht::LoadConfig(dir, privileged);
    Check(failures, privileged.udp_port == 4242,
          "a port below the OpenTrack 1024 floor falls back");

    WriteIni(dir, "[Network]\nUdpPort=65535\n");
    finch_ht::Config edge;
    finch_ht::LoadConfig(dir, edge);
    Check(failures, edge.udp_port == 65535, "the top of the valid range is accepted");
}

void NonFiniteFloatTests(int& failures)
{
    const std::string dir = MakeTempDir();

    // strtod parses "nan"/"inf" happily and overflows 1e400 to +inf. A NaN
    // reaching the sensitivity multiply poisons the smoothing state
    // permanently; a NaN FovOffset builds a degenerate projection matrix.
    WriteIni(dir,
        "[Rotation]\nYawSensitivity=nan\nPitchSensitivity=inf\nRollSensitivity=-inf\n"
        "LocalSmoothing=nan\nRemoteSmoothing=1e400\n"
        "[View]\nFovOffset=nan\n"
        "[Position]\nSensitivityX=nan\nLimitZ=inf\n");

    finch_ht::Config config;
    finch_ht::LoadConfig(dir, config);

    Check(failures, NearEqual(config.yaw_sensitivity, 1.0f)
                 && NearEqual(config.pitch_sensitivity, 1.0f)
                 && NearEqual(config.roll_sensitivity, 1.0f),
          "nan and inf sensitivities fall back to 1.0");
    Check(failures, NearEqual(config.local_smoothing, 0.0f)
                 && NearEqual(config.remote_smoothing, 0.15f),
          "nan and overflowed smoothing values fall back to their defaults");
    Check(failures, NearEqual(config.fov_offset, 0.0f),
          "a nan FovOffset falls back to 0 - ClampFov passes NaN straight through");
    Check(failures, NearEqual(config.position_sensitivity_x, 1.0f)
                 && NearEqual(config.limit_z, 0.40f),
          "nan and inf position values fall back to their defaults");
}

void OutOfRangeValueTests(int& failures)
{
    const std::string dir = MakeTempDir();

    // PositionProcessor clamps with [-limit, +limit]; a negative limit inverts
    // the bounds, so every input comes back as one edge or the other and the
    // camera snaps between two extremes instead of following the head.
    WriteIni(dir,
        "[Rotation]\nYawSensitivity=1e30\nLocalSmoothing=-0.5\nRemoteSmoothing=5\n"
        "[View]\nFovOffset=1e9\n"
        "[Position]\nLimitX=-0.3\nLimitY=99\n");

    finch_ht::Config config;
    finch_ht::LoadConfig(dir, config);

    Check(failures, config.yaw_sensitivity <= 10.0f && config.yaw_sensitivity > 0.0f,
          "an absurd sensitivity is clamped into range");
    Check(failures, NearEqual(config.local_smoothing, 0.0f)
                 && NearEqual(config.remote_smoothing, 1.0f),
          "smoothing is clamped into 0-1 at the boundary");
    Check(failures, config.fov_offset <= 160.0f,
          "an absurd FovOffset is clamped rather than reaching the projection matrix");
    Check(failures, config.limit_x >= 0.0f,
          "a negative position limit is clamped to 0 - it would invert the clamp bounds");
    Check(failures, config.limit_y <= 5.0f, "an absurd position limit is clamped");

    // A negative sensitivity is a legitimate way to invert an axis and must
    // survive validation untouched.
    WriteIni(dir, "[Rotation]\nYawSensitivity=-1.5\n");
    finch_ht::Config inverted;
    finch_ht::LoadConfig(dir, inverted);
    Check(failures, NearEqual(inverted.yaw_sensitivity, -1.5f),
          "a negative sensitivity still inverts the axis");
}

void YawModeKeyValidationTests(int& failures)
{
    const std::string dir = MakeTempDir();

    // GetAsyncKeyState reports nothing outside 0x01-0xFE, so an out-of-range
    // code leaves the yaw-mode toggle dead with nothing said about it.
    WriteIni(dir, "[Hotkeys]\nYawModeKey=0x1FF\n");
    finch_ht::Config tooBig;
    finch_ht::LoadConfig(dir, tooBig);
    Check(failures, tooBig.yaw_mode_key == 0x22,
          "a YawModeKey above 0xFE falls back to Page Down");

    WriteIni(dir, "[Hotkeys]\nYawModeKey=0\n");
    finch_ht::Config zero;
    finch_ht::LoadConfig(dir, zero);
    Check(failures, zero.yaw_mode_key == 0x22, "YawModeKey=0 is not a key and falls back");

    WriteIni(dir, "[Hotkeys]\nYawModeKey=0x51\n");
    finch_ht::Config valid;
    finch_ht::LoadConfig(dir, valid);
    Check(failures, valid.yaw_mode_key == 0x51, "a valid virtual-key code is kept");
}

}  // namespace

int RunConfigTests()
{
    int failures = 0;
    std::cout << "Config tests\n";
    MissingFileTests(failures);
    ParsingTests(failures);
    RetiredSmoothingKeyTests(failures);
    WrittenDefaultsMatchCompiledDefaultsTests(failures);
    PortValidationTests(failures);
    NonFiniteFloatTests(failures);
    OutOfRangeValueTests(failures);
    YawModeKeyValidationTests(failures);
    return finch_tests::Report("Config tests", failures);
}
