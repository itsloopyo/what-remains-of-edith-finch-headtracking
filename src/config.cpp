#include "config.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

namespace finch_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for edith-finch.
constexpr const char* kDisplayName = "What Remains of Edith Finch";

// FovOffset's bounds, the ones every earlier build clamped it to. The hook holds
// the resulting field of view to 10 to 170 (view_injection.h ClampFov).
constexpr double kMaxFovOffset = 160.0;

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    // No owner when the bootstrap could not read the game's folder.
    if (!g_owner) {
        Log::Line("config: %s not saved: CameraUnlock.ini has no known folder this session", rows);
        return;
    }
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    // The frozen reader takes the folder and names the file itself.
    const std::string& path = input.ansi_path;
    constexpr const char* kLegacySuffix = "\\HeadTracking.ini";
    const std::size_t suffix_length = std::strlen(kLegacySuffix);
    if (path.size() < suffix_length ||
        _stricmp(path.c_str() + path.size() - suffix_length, kLegacySuffix) != 0) {
        throw std::invalid_argument("the legacy import reads HeadTracking.ini only, not " + path);
    }
    // The test the frozen reader opens the file with: where it fails, the
    // published build ran on its defaults.
    const bool present = GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;

    legacy::Config read;
    legacy::Load(path.substr(0, path.size() - suffix_length), read);

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> pose_shaping;

    // Every sensitivity and inversion shipped at identity, and the position
    // offset is already converted to the game's centimetres in code, so nothing
    // folds: the mod applies the pose as the tracker sends it, and a value the
    // player changed is dropped.
    const auto shaping = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, pose_shaping, dropped);
    };
    shaping(read.yaw_sensitivity, 1.0f, "Rotation", "YawSensitivity");
    shaping(read.pitch_sensitivity, 1.0f, "Rotation", "PitchSensitivity");
    shaping(read.roll_sensitivity, 1.0f, "Rotation", "RollSensitivity");
    shaping(read.invert_yaw, false, "Rotation", "InvertYaw");
    shaping(read.invert_pitch, false, "Rotation", "InvertPitch");
    shaping(read.invert_roll, false, "Rotation", "InvertRoll");
    shaping(read.position_sensitivity_x, 1.0f, "Position", "SensitivityX");
    shaping(read.position_sensitivity_y, 1.0f, "Position", "SensitivityY");
    shaping(read.position_sensitivity_z, 1.0f, "Position", "SensitivityZ");

    // The reader keeps the port inside 1024-65535, every float finite and inside
    // its range, and the channel inside 0-31, so each carries over as it is.
    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.fov_offset = read.fov_offset;

    // [Position] Enabled chose the startup mode and nothing else: the cycle
    // reached every mode either way.
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(
        read.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                              : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = channels.rotation_enabled;
    out.position_enabled = channels.position_enabled;

    // LimitY bounded both vertical directions.
    out.position_limit_x = read.limit_x;
    out.position_limit_y = read.limit_y;
    out.position_limit_y_down = read.limit_y;
    out.position_limit_z = read.limit_z;
    out.position_limit_z_back = read.limit_z_back;

    // The lean clamp shipped switched off until it had been confirmed in game,
    // so it takes the table's default (approved change follows_default).
    out.collision_enabled = Table().defaults().collision_enabled;
    if (read.collision_enabled != out.collision_enabled) {
        dropped.push_back({cfg::DropRule::FollowsDefault, "Position", "CollisionEnabled",
                           read.collision_enabled ? "true" : "false"});
    }
    out.collision_margin = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;

    // End, Page Up and the three Ctrl+Shift chords were bound in code; only the
    // yaw key was in the file, and the reader keeps it inside 0x01-0xFE.
    out.toggle_key = FormatKeyBindings({{KeyModifiers::kNone, VK_END}, {kChord, 'Y'}});
    out.cycle_tracking_mode_key = FormatKeyBindings({{KeyModifiers::kNone, VK_PRIOR}, {kChord, 'G'}});
    out.yaw_mode_key = cfg::LegacyVirtualKeyToBindings(read.yaw_mode_key, "Hotkeys", "YawModeKey", dropped) + ", " +
                       FormatKeyBindings({{kChord, 'H'}});

    return present ? cfg::ImportResult::Imported(std::move(dropped), std::move(pose_shaping))
                   : cfg::ImportResult::Absent(std::move(dropped), std::move(pose_shaping));
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::position_limit_x)
        .Concept<Concept::PositionLimitY>(&Config::position_limit_y)
        .Concept<Concept::PositionLimitYDown>(&Config::position_limit_y_down)
        .Concept<Concept::PositionLimitZ>(&Config::position_limit_z)
        .Concept<Concept::PositionLimitZBack>(&Config::position_limit_z_back)
        .Concept<Concept::CollisionEnabled>(&Config::collision_enabled)
        .Concept<Concept::CollisionMargin>(&Config::collision_margin)
        .Comment("How far, in centimetres, the view is held off a wall when you lean into it.\n"
                 "Keep it above the camera's near clip distance, or the wall is not drawn anyway.")
        .Concept<Concept::CollisionChannel>(&Config::collision_channel)
        .Comment("Which of the game's collision channels the wall check tests against, 0 to 31.\n"
                 "Any other number turns the wall check off.")
        .Engine()
        .Concept<Concept::CollisionReleaseSmoothing>(&Config::collision_release_smoothing)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .Local("View", "FovOffset", &Config::fov_offset, cfg::FloatCodec(),
               "Degrees added to the field of view the game asks for, -160 to 160. 0 leaves it as it is.\n"
               "The game has no field of view setting, and every view measured so far runs at 80,\n"
               "so FovOffset=25 draws at 105. The result is held to 10 to 170.")
        .Range(-kMaxFovOffset, kMaxFovOffset);
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    // Every key the frozen reader takes a value from. The retired [Rotation] and
    // [Position] Smoothing are read only to warn that they are ignored.
    import.keys = {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"General", "WorldSpaceYaw"},
        {"Hotkeys", "YawModeKey"},
        {"Rotation", "YawSensitivity"},
        {"Rotation", "PitchSensitivity"},
        {"Rotation", "RollSensitivity"},
        {"Rotation", "InvertYaw"},
        {"Rotation", "InvertPitch"},
        {"Rotation", "InvertRoll"},
        {"Rotation", "LocalSmoothing"},
        {"Rotation", "RemoteSmoothing"},
        {"View", "FovOffset"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "CollisionEnabled"},
        {"Position", "CollisionRadius"},
        {"Position", "CollisionChannel"},
        {"Position", "CollisionReleaseSmoothing"},
    };
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = exe_dir + L"\\" + kIniName;
    options.table = Table();
    options.import = Import();
    options.legacy_path = exe_dir + L"\\" + kLegacyIniName;
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(exe_dir, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

void SaveWorldSpaceYaw(bool world_space_yaw) {
    Save("[General] WorldSpaceYaw", [world_space_yaw](Config& c) { c.world_space_yaw = world_space_yaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace finch_ht::config
