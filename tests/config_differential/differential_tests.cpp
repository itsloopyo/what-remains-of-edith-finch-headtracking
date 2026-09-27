// The differential test for the conversion from HeadTracking.ini to
// CameraUnlock.ini.
//
// Three readings of every input, and what may differ between them:
//
//   Oracle     the reader of the newest published build (v1.1.1, e6a9d7d, core
//              3038291), compiled from its own sources (oracle_api.h)
//   Import     the frozen reader in src/legacy_config/
//   Migration  the config owner's Load in a folder holding only the input as
//              HeadTracking.ini, which imports it through config::Import into a
//              new CameraUnlock.ini, then the canonical reader and table on it
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the published build that change how
// the file is read. There are none. src/ was unchanged from v1.1.1 to the commit
// the reader was frozen at, and every core source either reader compiles holds
// the same bytes at 3038291 and at the pin, which SourcesAreThePinnedOnes checks.
//
// Comparison 2, import against migration, is the proof for the conversion. It
// allows the approved changes core's data/config-format.json records, and
// nothing else:
//
//   pose_shaping     a sensitivity or inversion the player set away from the
//                    shipped identity is dropped, and the session runs at
//                    identity. Every shipped value is identity, so nothing folds.
//   follows_default  CollisionEnabled shipped false until the clamp was confirmed
//                    in game, and now takes the table's true, whatever the file
//                    held. This also moves the default, so no file and a file
//                    without the key start with the clamp on.
//
//   N3               a yaw key on Ctrl, Shift or Alt alone imports as unbound,
//                    and the Ctrl+Shift+H chord stays.
//
// The reader clamps every float into a range inside the concept's, replaces a
// value that is not finite, and keeps the yaw key inside 0x01-0xFE, so N1 and N2
// cannot apply.
//
// A row the player never changed from what v1.1.1 shipped follows Defaults.ini:
// the import lists it in follows_defaults_ini and the migration writes it
// `default`, the tracking mode pair as one unit. The test derives that list from
// what the published build ran on, a row whose every observed value is v1.1.1's
// default, and holds the import's list to it on every input. The first-run file,
// the empty file and no file list every row and migrate to the committed file
// byte for byte.
//
// Each input migrates three times: over a Defaults.ini the owner creates with the
// built-in values, from a read-only HeadTracking.ini, and over a Defaults.ini
// that differs from the built-in value on every global row. Over the first two
// the session runs as the import read, since v1.1.1's defaults are the built-in
// values. Over the third a row the player never changed is `default` and takes
// Defaults.ini's value, and a changed row keeps the player's.
//
// Inputs: the published build's first-run file (no build shipped or seeded a
// config, so every player's file started as that one), no file, an empty file,
// core's corpus of mutations of the first-run file, and the first-run file with
// YawModeKey set to each code from 0x01 to 0xFE.

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_api.h"

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

using cameraunlock::TrackingMode;
namespace cfg = cameraunlock::config;
using cfg::schema::Concept;
namespace fs = std::filesystem;
namespace testing = cameraunlock::config::testing;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
}

// ---- Provenance ------------------------------------------------------------
//
// Every source the oracle and the import compile, pinned by the SHA-256 of its
// bytes. The oracle's files are the published build's, taken with
// `git show v1.1.1:src/<file>`. The core files both readers compile are
// hash-equal to `git -C cameraunlock-core show 3038291:<path>`, so the readers
// differ only where the mod's own reader changed. The frozen import is pinned at
// the commit that froze it, so nothing edits it afterwards.

struct Pinned {
    const char* path;
    const char* sha256;
};

constexpr Pinned kPinned[] = {
    // The oracle: v1.1.1:src/...
    {"tests/config_differential/oracle/src/config.cpp", "55c104d4c051c728b064667571ce312ebea7e18275fcc143b7ad815998ab627f"},
    {"tests/config_differential/oracle/src/config.h", "211f479c536c76f054b66da318e9888e7e41ec6cab2937411d2540f267ccc94d"},
    {"tests/config_differential/oracle/src/logging.h", "39be7c1011eac008b9a4061aa6e041b5ec70b0a8845c598b6889b668568729ab"},
    // Both readers: core at the pin, hash-equal to 3038291:cpp/...
    {"cameraunlock-core/cpp/include/cameraunlock/config/ini_reader.h", "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
    {"cameraunlock-core/cpp/src/config/ini_reader.cpp", "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/finite_utils.h", "c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"},
    {"cameraunlock-core/cpp/include/cameraunlock/protocol/port_utils.h", "91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"},
    {"cameraunlock-core/cpp/include/cameraunlock/logging/file_log.h", "43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"},
    {"cameraunlock-core/cpp/src/logging/file_log.cpp", "73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"},
    // The oracle only: the headers v1.1.1's config.h includes for its defaults.
    {"cameraunlock-core/cpp/include/cameraunlock/data/position_settings.h", "b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/smoothing_utils.h", "fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"},
    {"cameraunlock-core/cpp/include/cameraunlock/math/angle_utils.h", "d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"},
    // The import: src/logging.h is v1.1.1's, the legacy folder is frozen.
    {"src/logging.h", "39be7c1011eac008b9a4061aa6e041b5ec70b0a8845c598b6889b668568729ab"},
    {"src/legacy_config/legacy_config.h", "3d620d256f159fa7aef7f37932936175b6f01d2578c4deff4c9de98791a89f21"},
    {"src/legacy_config/legacy_config.cpp", "c15295a501558e05296e7ced6820bbf867e140eeaed12428aeaf9f95be02feb7"},
};

std::string ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string Sha256Hex(const std::string& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
        throw std::runtime_error("BCryptOpenAlgorithmProvider(SHA256) failed");
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32] = {};
    const bool ok =
        BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
        BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                      static_cast<ULONG>(bytes.size()), 0)) &&
        BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0));
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("SHA-256 failed");
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char b : digest) {
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}

std::string SourcePath(const std::string& relative) { return std::string(FINCH_SOURCE_DIR) + "/" + relative; }

void SourcesAreThePinnedOnes() {
    for (const Pinned& p : kPinned) {
        const std::string actual = Sha256Hex(ReadFileBytes(SourcePath(p.path)));
        if (actual != p.sha256) std::printf("  %s is %s\n", p.path, actual.c_str());
        Check(actual == p.sha256, std::string(p.path) + " holds the pinned bytes");
    }
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per input: GetPrivateProfileString, which both readers sit on, is
// free to cache the file it last read. `dir` stands for the folder holding the
// game exe; Defaults.ini sits in `global` beside it.

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        root_ = fs::path(temp) /
                ("finch_ht_diff_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(s_next++));
        Remove();
        fs::create_directories(root_ / "game");
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    // A scanner can still hold a file the test just wrote, and a destructor must
    // not throw, so a folder left behind is reported and the run carries on.
    ~Scratch() {
        try {
            Remove();
        } catch (const fs::filesystem_error& e) {
            std::printf("  scratch folder left behind: %s\n", e.what());
        }
    }

    std::string dir() const { return (root_ / "game").string(); }
    std::wstring wdir() const { return (root_ / "game").wstring(); }
    std::string ini() const { return dir() + "\\HeadTracking.ini"; }
    std::wstring wini() const { return wdir() + L"\\HeadTracking.ini"; }
    fs::path canonical() const { return root_ / "game" / "CameraUnlock.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    // Every file in the game folder, by name, with its bytes.
    std::vector<std::pair<std::string, std::string>> Listing() const {
        std::vector<std::pair<std::string, std::string>> files;
        for (const auto& entry : fs::directory_iterator(root_ / "game")) {
            files.push_back({entry.path().filename().string(), ReadFileBytes(entry.path().string())});
        }
        std::sort(files.begin(), files.end());
        return files;
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(root_ / "game")) names.insert(entry.path().filename().string());
        return names;
    }

    void Write(const std::string& bytes) const {
        std::ofstream out(ini(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + ini());
    }

    void WriteDefaults(const std::string& bytes) const {
        fs::create_directories(defaults().parent_path());
        std::ofstream out(defaults(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + defaults().string());
    }

    cfg::ConfigOwnerOptions<finch_ht::Config> Options() const {
        return finch_ht::config::OwnerOptions(wdir(), cfg::DefaultsFile::At(defaults().wstring()));
    }

private:
    void Remove() const {
        if (!fs::exists(root_)) return;
        for (const auto& entry : fs::recursive_directory_iterator(root_)) {
            SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        }
        fs::remove_all(root_);
    }

    fs::path root_;
};

// ---- What a reading does -------------------------------------------------------

std::uint32_t Bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

enum Action { kToggle, kCycleMode, kYawMode };

// One registered binding: the action, the virtual-key code, and the modifiers
// it needs (0, or Ctrl+Shift as cameraunlock::input::KeyModifiers spells it).
using Hotkey = std::tuple<int, int, unsigned>;
constexpr unsigned kPlain = 0;
constexpr unsigned kCtrlShift = 3;

// Everything a reading decides that the running mod acts on: the settings, the
// state at startup, and the bindings the poller registers.
struct Observed {
    int udp_port = 0;
    bool start_enabled = false;
    bool start_world_yaw = false;
    int start_mode = 0;
    float local_smoothing = 0;
    float remote_smoothing = 0;
    float fov_offset = 0;
    float limit_x = 0;
    float limit_y = 0;
    float limit_y_down = 0;
    float limit_z = 0;
    float limit_z_back = 0;
    bool collision_enabled = false;
    float collision_margin = 0;
    int collision_channel = 0;
    float collision_release_smoothing = 0;
    // The pose shaping the session applied: rotation sensitivity and inversion,
    // then position sensitivity.
    float yaw_sensitivity = 1;
    float pitch_sensitivity = 1;
    float roll_sensitivity = 1;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;
    float position_sensitivity_x = 1;
    float position_sensitivity_y = 1;
    float position_sensitivity_z = 1;
    std::vector<Hotkey> hotkeys;
};

std::vector<std::string> Differences(const Observed& a, const Observed& b) {
    std::vector<std::string> out;
    const auto flt = [&out](float x, float y, const char* what) {
        if (Bits(x) != Bits(y)) out.push_back(what);
    };
    if (a.udp_port != b.udp_port) out.push_back("UDP port");
    if (a.start_enabled != b.start_enabled) out.push_back("tracking on at startup");
    if (a.start_world_yaw != b.start_world_yaw) out.push_back("yaw mode at startup");
    if (a.start_mode != b.start_mode) out.push_back("tracking mode at startup");
    flt(a.local_smoothing, b.local_smoothing, "local smoothing");
    flt(a.remote_smoothing, b.remote_smoothing, "remote smoothing");
    flt(a.fov_offset, b.fov_offset, "FOV offset");
    flt(a.limit_x, b.limit_x, "limit x");
    flt(a.limit_y, b.limit_y, "limit y");
    flt(a.limit_y_down, b.limit_y_down, "limit y down");
    flt(a.limit_z, b.limit_z, "limit z");
    flt(a.limit_z_back, b.limit_z_back, "limit z back");
    if (a.collision_enabled != b.collision_enabled) out.push_back("collision enabled");
    flt(a.collision_margin, b.collision_margin, "collision margin");
    if (a.collision_channel != b.collision_channel) out.push_back("collision channel");
    flt(a.collision_release_smoothing, b.collision_release_smoothing, "collision release smoothing");
    flt(a.yaw_sensitivity, b.yaw_sensitivity, "yaw sensitivity");
    flt(a.pitch_sensitivity, b.pitch_sensitivity, "pitch sensitivity");
    flt(a.roll_sensitivity, b.roll_sensitivity, "roll sensitivity");
    if (a.invert_yaw != b.invert_yaw) out.push_back("invert yaw");
    if (a.invert_pitch != b.invert_pitch) out.push_back("invert pitch");
    if (a.invert_roll != b.invert_roll) out.push_back("invert roll");
    flt(a.position_sensitivity_x, b.position_sensitivity_x, "position sensitivity x");
    flt(a.position_sensitivity_y, b.position_sensitivity_y, "position sensitivity y");
    flt(a.position_sensitivity_z, b.position_sensitivity_z, "position sensitivity z");
    if (a.hotkeys != b.hotkeys) out.push_back("hotkeys");
    return out;
}

// Hand copied, not compiled from the published sources: the oracle library
// exports only the reader. v1.1.1:src/hotkeys.cpp:77-84 (StartHotkeys), which is
// also the frozen reader's commit: End, Page Up and the configured yaw key
// NavGuarded, the Y, G and H chords ChordGuarded. The inject-mode chords on
// lines 89-90 are compiled out of every build but a developer's.
std::vector<Hotkey> LegacyHotkeys(int yaw_mode_key) {
    std::vector<Hotkey> keys = {
        {kToggle, VK_END, kPlain},   {kCycleMode, VK_PRIOR, kPlain}, {kYawMode, yaw_mode_key, kPlain},
        {kToggle, 'Y', kCtrlShift},  {kCycleMode, 'G', kCtrlShift},  {kYawMode, 'H', kCtrlShift},
    };
    std::sort(keys.begin(), keys.end());
    return keys;
}

// Hand copied from v1.1.1:src/headtracking_mod.cpp:298-344 (ApplyConfigToSession),
// unchanged at the frozen reader's commit: tracking on as EnableOnStartup says,
// the yaw mode from WorldSpaceYaw, LimitY on both vertical bounds, the swept
// sphere's radius as the standoff (the clamp's own skin is 0), and rotation and
// position, or rotation only, as [Position] Enabled says.
template <class C>
Observed ObservePublished(const C& c) {
    Observed o;
    o.udp_port = c.udp_port;
    o.start_enabled = c.enable_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(c.position_enabled ? TrackingMode::RotationAndPosition : TrackingMode::RotationOnly);
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.fov_offset = c.fov_offset;
    o.limit_x = c.limit_x;
    o.limit_y = c.limit_y;
    o.limit_y_down = c.limit_y;
    o.limit_z = c.limit_z;
    o.limit_z_back = c.limit_z_back;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_radius;
    o.collision_channel = c.collision_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    o.yaw_sensitivity = c.yaw_sensitivity;
    o.pitch_sensitivity = c.pitch_sensitivity;
    o.roll_sensitivity = c.roll_sensitivity;
    o.invert_yaw = c.invert_yaw;
    o.invert_pitch = c.invert_pitch;
    o.invert_roll = c.invert_roll;
    o.position_sensitivity_x = c.position_sensitivity_x;
    o.position_sensitivity_y = c.position_sensitivity_y;
    o.position_sensitivity_z = c.position_sensitivity_z;
    o.hotkeys = LegacyHotkeys(c.yaw_mode_key);
    return o;
}

Observed ReadOracle(const std::string& dir) { return ObservePublished(finch_oracle::Load(dir)); }

Observed ReadImport(const std::string& dir) {
    finch_ht::legacy::Config c;
    finch_ht::legacy::Load(dir, c);
    return ObservePublished(c);
}

// ---- Inputs --------------------------------------------------------------------

std::string DataPath(const char* name) {
    return SourcePath(std::string("tests/config_differential/data/") + name);
}

// The published build's first-run file, extracted once from the text of its
// WriteDefaultConfigIfMissing and committed. FirstRunFileIsThePublishedBuilds
// holds it to what that function writes.
std::string FirstRunFile() { return ReadFileBytes(DataPath("v1.1.1-first-run.ini")); }

// Every key the frozen reader takes a value from, and how the corpus varies each
// one. The out-of-range values sit either side of the range each key is clamped
// or refused outside. The retired [Rotation] and [Position] Smoothing are read
// only to warn that they are ignored, so they are not among them.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "UdpPort", "5252", {"80", "70000"}},
        {"General", "EnableOnStartup", "0", {}},
        {"General", "WorldSpaceYaw", "0", {}},
        {"Hotkeys", "YawModeKey", "0x51", {"0x1FF"}, true},
        {"Rotation", "YawSensitivity", "1.5", {"-11", "11"}},
        {"Rotation", "PitchSensitivity", "0.5", {"-11", "11"}},
        {"Rotation", "RollSensitivity", "2.0", {"-11", "11"}},
        {"Rotation", "InvertYaw", "1", {}},
        {"Rotation", "InvertPitch", "1", {}},
        {"Rotation", "InvertRoll", "1", {}},
        {"Rotation", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Rotation", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"View", "FovOffset", "25.0", {"-161", "161"}},
        {"Position", "Enabled", "0", {}},
        {"Position", "SensitivityX", "2.0", {"-11", "11"}},
        {"Position", "SensitivityY", "3.0", {"-11", "11"}},
        {"Position", "SensitivityZ", "4.0", {"-11", "11"}},
        {"Position", "LimitX", "0.25", {"-0.1", "5.5"}},
        {"Position", "LimitY", "0.3", {"-0.1", "5.5"}},
        {"Position", "LimitZ", "0.5", {"-0.1", "5.5"}},
        {"Position", "LimitZBack", "0.2", {"-0.1", "5.5"}},
        {"Position", "CollisionEnabled", "1", {}},
        {"Position", "CollisionRadius", "14.0", {"0.5", "101"}},
        {"Position", "CollisionChannel", "2", {"-1", "32"}},
        {"Position", "CollisionReleaseSmoothing", "0.5", {"-0.5", "1.5"}},
    };
}

// The generator refuses the call when these and the descriptors name different
// keys, so the corpus covers every key the import reads.
std::vector<cfg::LegacyKey> CorpusReads() { return finch_ht::config::Import().keys; }

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

// The first-run file with [Hotkeys] YawModeKey set to `code`.
std::string WithYawModeKey(int code) {
    std::string bytes = FirstRunFile();
    const std::string line = "YawModeKey=0x22\r\n";
    const std::size_t at = bytes.find(line);
    if (at == std::string::npos) throw std::runtime_error("the first-run file has no YawModeKey=0x22 line");
    char value[32];
    std::snprintf(value, sizeof value, "YawModeKey=0x%02X\r\n", code);
    return bytes.replace(at, line.size(), value);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {"v1.1.1 first-run file", true, FirstRunFile()},
        {"no file", false, {}},
        {"empty file", true, {}},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRunFile(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    for (int code = 0x01; code <= 0xFE; ++code) {
        char name[48];
        std::snprintf(name, sizeof name, "YawModeKey=0x%02X", code);
        inputs.push_back({name, true, WithYawModeKey(code)});
    }
    return inputs;
}

// ---- Checks --------------------------------------------------------------------

// The first-run file committed as test data is what the published build writes.
void FirstRunFileIsThePublishedBuilds() {
    Scratch s;
    finch_oracle::WriteDefaultConfigIfMissing(s.dir());
    Check(ReadFileBytes(s.ini()) == FirstRunFile(),
          "v1.1.1-first-run.ini is what the published build writes at first run");
}

// Comparison 1. Nothing may differ, floats bit for bit.
void OracleAgainstImport(const std::vector<Input>& inputs) {
    int compared = 0;
    for (const Input& input : inputs) {
        Scratch s;
        if (input.present) s.Write(input.bytes);
        const std::vector<std::string> diff = Differences(ReadOracle(s.dir()), ReadImport(s.dir()));
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", input.name.c_str(), d.c_str());
        Check(diff.empty(), "comparison 1: oracle and import agree on " + input.name);
        ++compared;
    }
    std::printf("comparison 1: %d inputs\n", compared);
}

// What the session runs on from a canonical Config: headtracking_mod.cpp
// ApplyConfigToSession, and hotkeys.cpp StartHotkeys, which puts each list
// through ParseKeyBindings and RegisterKeyBindings. The session keeps the
// processors' identity sensitivity and inversion.
Observed ObserveCanonical(const finch_ht::Config& c) {
    Observed o;
    o.udp_port = c.udp_port;
    o.start_enabled = c.enable_on_startup;
    o.start_world_yaw = c.world_space_yaw;
    o.start_mode = static_cast<int>(finch_ht::config::StartupTrackingMode(c));
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.fov_offset = c.fov_offset;
    o.limit_x = c.position_limit_x;
    o.limit_y = c.position_limit_y;
    o.limit_y_down = c.position_limit_y_down;
    o.limit_z = c.position_limit_z;
    o.limit_z_back = c.position_limit_z_back;
    o.collision_enabled = c.collision_enabled;
    o.collision_margin = c.collision_margin;
    o.collision_channel = c.collision_channel;
    o.collision_release_smoothing = c.collision_release_smoothing;
    const std::pair<Action, const std::string*> lists[] = {
        {kToggle, &c.toggle_key}, {kCycleMode, &c.cycle_tracking_mode_key}, {kYawMode, &c.yaw_mode_key}};
    for (const auto& [action, list] : lists) {
        const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(*list);
        Check(parsed.ok(), "a migrated key list parses: " + *list);
        for (const cameraunlock::input::KeyBinding& b : parsed.bindings) {
            o.hotkeys.push_back({action, b.vk, static_cast<unsigned>(b.modifiers)});
        }
    }
    std::sort(o.hotkeys.begin(), o.hotkeys.end());
    return o;
}

using Drop = std::tuple<cfg::DropRule, std::string, std::string>;

// The drops the approved changes call for, from what the frozen reader read,
// and the settings the session then runs on: comparison 2's whole allowance.
struct Allowed {
    std::vector<Drop> dropped;
    Observed observed;
};

Allowed ApplyApprovedChanges(const finch_ht::legacy::Config& read) {
    Allowed a;
    a.observed = ObservePublished(read);
    Observed& o = a.observed;
    const auto shaping = [&a](auto value, auto shipped, auto& field, const char* section, const char* key) {
        if (value != shipped) a.dropped.push_back({cfg::DropRule::PoseShaping, section, key});
        field = shipped;
    };
    shaping(read.yaw_sensitivity, 1.0f, o.yaw_sensitivity, "Rotation", "YawSensitivity");
    shaping(read.pitch_sensitivity, 1.0f, o.pitch_sensitivity, "Rotation", "PitchSensitivity");
    shaping(read.roll_sensitivity, 1.0f, o.roll_sensitivity, "Rotation", "RollSensitivity");
    shaping(read.invert_yaw, false, o.invert_yaw, "Rotation", "InvertYaw");
    shaping(read.invert_pitch, false, o.invert_pitch, "Rotation", "InvertPitch");
    shaping(read.invert_roll, false, o.invert_roll, "Rotation", "InvertRoll");
    shaping(read.position_sensitivity_x, 1.0f, o.position_sensitivity_x, "Position", "SensitivityX");
    shaping(read.position_sensitivity_y, 1.0f, o.position_sensitivity_y, "Position", "SensitivityY");
    shaping(read.position_sensitivity_z, 1.0f, o.position_sensitivity_z, "Position", "SensitivityZ");
    if (!read.collision_enabled) a.dropped.push_back({cfg::DropRule::FollowsDefault, "Position", "CollisionEnabled"});
    o.collision_enabled = true;
    const int k = read.yaw_mode_key;
    if ((k >= 0x10 && k <= 0x12) || (k >= 0xA0 && k <= 0xA5)) {
        a.dropped.push_back({cfg::DropRule::ModifierKey, "Hotkeys", "YawModeKey"});
        o.hotkeys.erase(std::find(o.hotkeys.begin(), o.hotkeys.end(), Hotkey{kYawMode, k, kPlain}));
    }
    std::sort(a.dropped.begin(), a.dropped.end());
    return a;
}

// ---- Rows that follow Defaults.ini ---------------------------------------------
//
// Every row the table binds that follows Defaults.ini: the global concepts,
// less CollisionMargin and CollisionChannel, which every game keeps for itself.
const std::set<Concept>& AllRows() {
    static const std::set<Concept> all = {
        Concept::UdpPort,           Concept::EnableOnStartup,    Concept::WorldSpaceYaw,
        Concept::RotationEnabled,   Concept::PositionEnabled,    Concept::LocalSmoothing,
        Concept::RemoteSmoothing,   Concept::PositionLimitX,     Concept::PositionLimitY,
        Concept::PositionLimitYDown, Concept::PositionLimitZ,    Concept::PositionLimitZBack,
        Concept::CollisionEnabled,  Concept::CollisionReleaseSmoothing, Concept::ToggleKey,
        Concept::CycleTrackingModeKey, Concept::YawModeKey,
    };
    return all;
}

std::vector<Hotkey> KeysOf(const Observed& o, int action) {
    std::vector<Hotkey> keys;
    for (const Hotkey& h : o.hotkeys) {
        if (std::get<0>(h) == action) keys.push_back(h);
    }
    return keys;
}

// The rows on which two readings differ; the mode pair is both rows or neither.
// Pose shaping has no row.
std::set<Concept> RowsThatDiffer(const Observed& a, const Observed& b) {
    std::set<Concept> rows;
    const auto flt = [&rows](float x, float y, Concept row) {
        if (Bits(x) != Bits(y)) rows.insert(row);
    };
    if (a.udp_port != b.udp_port) rows.insert(Concept::UdpPort);
    if (a.start_enabled != b.start_enabled) rows.insert(Concept::EnableOnStartup);
    if (a.start_world_yaw != b.start_world_yaw) rows.insert(Concept::WorldSpaceYaw);
    if (a.start_mode != b.start_mode) rows.insert({Concept::RotationEnabled, Concept::PositionEnabled});
    flt(a.local_smoothing, b.local_smoothing, Concept::LocalSmoothing);
    flt(a.remote_smoothing, b.remote_smoothing, Concept::RemoteSmoothing);
    flt(a.limit_x, b.limit_x, Concept::PositionLimitX);
    flt(a.limit_y, b.limit_y, Concept::PositionLimitY);
    flt(a.limit_y_down, b.limit_y_down, Concept::PositionLimitYDown);
    flt(a.limit_z, b.limit_z, Concept::PositionLimitZ);
    flt(a.limit_z_back, b.limit_z_back, Concept::PositionLimitZBack);
    if (a.collision_enabled != b.collision_enabled) rows.insert(Concept::CollisionEnabled);
    flt(a.collision_release_smoothing, b.collision_release_smoothing, Concept::CollisionReleaseSmoothing);
    if (KeysOf(a, kToggle) != KeysOf(b, kToggle)) rows.insert(Concept::ToggleKey);
    if (KeysOf(a, kCycleMode) != KeysOf(b, kCycleMode)) rows.insert(Concept::CycleTrackingModeKey);
    if (KeysOf(a, kYawMode) != KeysOf(b, kYawMode)) rows.insert(Concept::YawModeKey);
    return rows;
}

// The rows the player never changed: the published build ran on v1.1.1's
// default for each.
std::set<Concept> UntouchedRows(const finch_ht::legacy::Config& read) {
    const std::set<Concept> changed =
        RowsThatDiffer(ObservePublished(read), ObservePublished(finch_ht::legacy::Config{}));
    std::set<Concept> untouched;
    for (const Concept row : AllRows()) {
        if (!changed.count(row)) untouched.insert(row);
    }
    return untouched;
}

std::string Names(const std::set<Concept>& rows) {
    std::string text;
    for (const Concept row : rows) {
        text += (text.empty() ? "" : ", ") + std::string(cfg::schema::kConcepts[static_cast<std::size_t>(row)].name);
    }
    return text.empty() ? "none" : text;
}

// What the session runs on over a Defaults.ini other than the built-in one:
// `want`, with each row in `follows` as `defaults_ini` gives it.
Observed OverDefaults(Observed want, const std::set<Concept>& follows, const Observed& defaults_ini) {
    const auto take = [&follows](Concept row) { return follows.count(row) != 0; };
    const Observed& d = defaults_ini;
    if (take(Concept::RotationEnabled) != take(Concept::PositionEnabled)) {
        throw std::logic_error("follows_defaults_ini names one half of the tracking mode");
    }
    if (take(Concept::UdpPort)) want.udp_port = d.udp_port;
    if (take(Concept::EnableOnStartup)) want.start_enabled = d.start_enabled;
    if (take(Concept::WorldSpaceYaw)) want.start_world_yaw = d.start_world_yaw;
    if (take(Concept::RotationEnabled)) want.start_mode = d.start_mode;
    if (take(Concept::LocalSmoothing)) want.local_smoothing = d.local_smoothing;
    if (take(Concept::RemoteSmoothing)) want.remote_smoothing = d.remote_smoothing;
    if (take(Concept::PositionLimitX)) want.limit_x = d.limit_x;
    if (take(Concept::PositionLimitY)) want.limit_y = d.limit_y;
    if (take(Concept::PositionLimitYDown)) want.limit_y_down = d.limit_y_down;
    if (take(Concept::PositionLimitZ)) want.limit_z = d.limit_z;
    if (take(Concept::PositionLimitZBack)) want.limit_z_back = d.limit_z_back;
    if (take(Concept::CollisionEnabled)) want.collision_enabled = d.collision_enabled;
    if (take(Concept::CollisionReleaseSmoothing)) want.collision_release_smoothing = d.collision_release_smoothing;
    const std::pair<Concept, int> lists[] = {
        {Concept::ToggleKey, kToggle}, {Concept::CycleTrackingModeKey, kCycleMode}, {Concept::YawModeKey, kYawMode}};
    for (const auto& [row, action] : lists) {
        if (!take(row)) continue;
        std::vector<Hotkey> keys;
        for (const Hotkey& h : want.hotkeys) {
            if (std::get<0>(h) != action) keys.push_back(h);
        }
        for (const Hotkey& h : KeysOf(d, action)) keys.push_back(h);
        std::sort(keys.begin(), keys.end());
        want.hotkeys = keys;
    }
    return want;
}

// The inputs a player cannot have changed anything in.
bool IsUnedited(const std::string& name) {
    return name == "v1.1.1 first-run file" || name == "no file" || name == "empty file";
}

std::vector<std::string> CanonicalDiagnostics(const std::string& bytes, finch_ht::Config& out) {
    std::vector<std::string> found;
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(bytes);
    for (const cfg::CanonicalDiagnostic& d : doc.diagnostics) found.push_back("reader: " + cfg::DescribeCanonicalDiagnostic(d));
    const cfg::ConfigTable<finch_ht::Config> table = finch_ht::config::Table();
    out = table.defaults();
    for (const cfg::CanonicalDiagnostic& d : cfg::ApplyCanonical(doc, table, out).diagnostics) {
        found.push_back("table: " + cfg::DescribeCanonicalDiagnostic(d));
    }
    return found;
}

bool AsciiCrlf(const std::string& bytes) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(bytes[i]);
        if (c > 0x7E) return false;
        if (c == '\r' && (i + 1 == bytes.size() || bytes[i + 1] != '\n')) return false;
        if (c == '\n' && (i == 0 || bytes[i - 1] != '\r')) return false;
        if (c < 0x20 && c != '\r' && c != '\n') return false;
    }
    return !bytes.empty() && bytes.back() == '\n';
}

// A file's bytes, last write time and attributes, which no load may change.
struct FileState {
    std::string bytes;
    unsigned long long written = 0;
    DWORD attributes = 0;
    bool operator==(const FileState& other) const {
        return bytes == other.bytes && written == other.written && attributes == other.attributes;
    }
};

std::optional<FileState> StateOf(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return std::nullopt;
        throw std::runtime_error("cannot read the attributes of " + path.string());
    }
    FileState state;
    state.bytes = ReadFileBytes(path.string());
    state.written = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) |
                    data.ftLastWriteTime.dwLowDateTime;
    state.attributes = data.dwFileAttributes;
    return state;
}

bool LogSays(const std::vector<std::string>& log, const std::string& text) {
    for (const std::string& line : log) {
        if (line.find(text) != std::string::npos) return true;
    }
    return false;
}

// A Defaults.ini holding a value other than the built-in one on every global row
// the table binds, so a migration that wrote `default` where the imported value
// is not what `default` gives would read back differently over it.
const char* const kSkewedDefaults =
    "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n"
    "[Network]\r\nUdpPort=5252\r\n\r\n"
    "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\nRotationEnabled=false\r\n\r\n"
    "[Smoothing]\r\nLocalSmoothing=0.5\r\nRemoteSmoothing=0.5\r\n\r\n"
    "[Position]\r\nPositionEnabled=true\r\nPositionLimitX=0.5\r\nPositionLimitY=0.5\r\nPositionLimitYDown=0.5\r\n"
    "PositionLimitZ=0.5\r\nPositionLimitZBack=0.5\r\nCollisionEnabled=false\r\nCollisionReleaseSmoothing=0.25\r\n\r\n"
    "[Hotkeys]\r\nToggleKey=F8\r\nCycleTrackingModeKey=F9\r\nYawModeKey=F10\r\n";

// The folder beside this executable the migrated files are written to, for
// lint-migrated.mjs, which CTest runs after this test.
fs::path MigratedFolder() {
    std::vector<wchar_t> exe(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
        if (length == 0) throw std::runtime_error("cannot find this executable's path");
        if (length < exe.size()) return fs::path(std::wstring(exe.data(), length)).parent_path() / "migrated";
        exe.resize(exe.size() * 2);
    }
}

// Runs the owner's Load in `s`, whose game folder holds the input as
// HeadTracking.ini or nothing, checks what a load must do beyond comparison 2,
// and returns the settings the session runs on. A file it creates by migrating
// goes into `migrated_files`.
std::optional<finch_ht::Config> Migrate(const Input& input, const Scratch& s, const std::string& label,
                                        std::set<std::string>& migrated_files) {
    const fs::path legacy = fs::path(s.wini());
    const std::optional<FileState> legacy_before = StateOf(legacy);
    const std::set<std::string> both{"CameraUnlock.ini", "HeadTracking.ini"};

    const cfg::ConfigLoadResult<finch_ht::Config> loaded = cfg::ConfigOwner<finch_ht::Config>(s.Options()).Load();
    const cfg::ConfigLoadStatus want = input.present ? cfg::ConfigLoadStatus::Migrated : cfg::ConfigLoadStatus::Created;
    if (loaded.status != want) {
        std::printf("  %s: %s, %s\n", label.c_str(), cfg::ConfigLoadStatusName(loaded.status), loaded.reason.c_str());
    }
    Check(loaded.status == want, label + ": the legacy file imports, or with none the file is created");
    Check(StateOf(legacy) == legacy_before, label + ": a load leaves HeadTracking.ini's bytes, write time and attributes");
    if (loaded.status != want) return std::nullopt;
    Check(s.Names() == (input.present ? both : std::set<std::string>{"CameraUnlock.ini"}),
          label + ": the game folder holds HeadTracking.ini and CameraUnlock.ini and nothing else");

    const std::string migrated = ReadFileBytes(s.canonical().string());
    Check(cfg::HasCanonicalStamp(migrated), label + ": CameraUnlock.ini carries the stamp");
    Check(AsciiCrlf(migrated), label + ": CameraUnlock.ini is ASCII with CRLF line ends");
    finch_ht::Config reread;
    const std::vector<std::string> diagnostics = CanonicalDiagnostics(migrated, reread);
    for (const std::string& d : diagnostics) std::printf("  %s: CameraUnlock.ini, %s\n", label.c_str(), d.c_str());
    Check(diagnostics.empty(), label + ": CameraUnlock.ini reads with no diagnostic");
    if (input.present) migrated_files.insert(migrated);

    // The next start reads CameraUnlock.ini, imports nothing and writes nothing.
    const std::optional<FileState> created = StateOf(s.canonical());
    const cfg::ConfigLoadResult<finch_ht::Config> again = cfg::ConfigOwner<finch_ht::Config>(s.Options()).Load();
    Check(again.status == cfg::ConfigLoadStatus::Canonical, label + ": the next start reads CameraUnlock.ini");
    Check(Differences(ObserveCanonical(again.config), ObserveCanonical(loaded.config)).empty(),
          label + ": the next start runs on the same settings");
    Check(StateOf(s.canonical()) == created && StateOf(legacy) == legacy_before,
          label + ": the next start changes neither file");
    Check(!input.present || LogSays(again.log, "is left as it was and is not read"),
          label + ": the next start logs that HeadTracking.ini is not read");
    return loaded.config;
}

// Comparison 2, and what the migration must do with every input besides.
void ImportAgainstMigration(const std::vector<Input>& inputs) {
    const std::string committed = ReadFileBytes(SourcePath("CameraUnlock.ini"));
    const cfg::ConfigTable<finch_ht::Config> table = finch_ht::config::Table();
    std::set<std::string> migrated_files;
    int compared = 0;
    int touched = 0;
    int modeTouched = 0;

    finch_ht::Config skewedConfig;
    const std::vector<std::string> skewedDiagnostics = CanonicalDiagnostics(kSkewedDefaults, skewedConfig);
    for (const std::string& d : skewedDiagnostics) std::printf("  the skewed Defaults.ini: %s\n", d.c_str());
    Check(skewedDiagnostics.empty(), "the skewed Defaults.ini sets every row with no diagnostic");
    const Observed skewedObserved = ObserveCanonical(skewedConfig);
    Check(RowsThatDiffer(skewedObserved, ObserveCanonical(table.defaults())) == AllRows(),
          "the skewed Defaults.ini differs from the built-in values on every row that follows Defaults.ini");
    for (const Input& input : inputs) {
        const std::string& name = input.name;

        // The import, run as the owner runs it but on a read-only copy: it reads
        // what the frozen reader reads, drops what the approved changes drop, and
        // writes nothing.
        cfg::ImportResult imported;
        finch_ht::legacy::Config read;
        {
            Scratch ro;
            if (input.present) {
                ro.Write(input.bytes);
                SetFileAttributesA(ro.ini().c_str(), FILE_ATTRIBUTE_READONLY);
            }
            const auto before = ro.Listing();
            finch_ht::Config unused = table.defaults();
            imported = finch_ht::config::Import().run({ro.wini(), ro.ini(), false}, unused);
            Check(ro.Listing() == before, name + ": the import leaves a read-only folder as it was");
            finch_ht::legacy::Load(ro.dir(), read);
        }
        Check(imported.status == (input.present ? cfg::ImportStatus::Imported : cfg::ImportStatus::Absent),
              name + ": the import reads every input, as the published build did");

        const Allowed allowed = ApplyApprovedChanges(read);
        std::vector<Drop> dropped;
        for (const cfg::DroppedValue& d : imported.dropped) dropped.push_back({d.rule, d.section, d.key});
        std::sort(dropped.begin(), dropped.end());
        Check(dropped == allowed.dropped, name + ": the import drops exactly what the approved changes drop");
        Check(imported.pose_shaping.size() == 9, name + ": the import records all nine pose-shaping settings");
        for (const cfg::PoseShapingValue& p : imported.pose_shaping) {
            const bool changed = std::find(allowed.dropped.begin(), allowed.dropped.end(),
                                           Drop{cfg::DropRule::PoseShaping, p.section, p.key}) != allowed.dropped.end();
            Check(p.folded != changed, name + ": [" + p.section + "] " + p.key + " folds exactly when it is the shipped value");
        }
        const Observed& want = allowed.observed;

        const std::set<Concept> follows(imported.follows_defaults_ini.begin(), imported.follows_defaults_ini.end());
        Check(follows.size() == imported.follows_defaults_ini.size(), name + ": follows_defaults_ini names each row once");
        const std::set<Concept> untouched = UntouchedRows(read);
        if (follows != untouched) {
            std::printf("  %s: follows Defaults.ini %s, untouched %s\n", name.c_str(), Names(follows).c_str(),
                        Names(untouched).c_str());
        }
        Check(follows == untouched, name + ": the rows left to Defaults.ini are exactly the ones the player never changed");
        if (untouched != AllRows()) ++touched;
        if (!untouched.count(Concept::RotationEnabled)) ++modeTouched;
        if (IsUnedited(name)) Check(untouched == AllRows(), name + ": every row follows Defaults.ini");

        // Over a Defaults.ini the owner creates with the built-in values.
        Scratch s;
        if (input.present) s.Write(input.bytes);
        const std::optional<finch_ht::Config> migrated = Migrate(input, s, name, migrated_files);
        if (migrated) {
            const std::vector<std::string> diff = Differences(want, ObserveCanonical(*migrated));
            for (const std::string& d : diff) std::printf("  comparison 2, %s: %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), "comparison 2: the migration runs as the import read, less the approved changes: " + name);

            // Over the built-in values the table's own defaults stand for Defaults.ini.
            finch_ht::Config reread;
            CanonicalDiagnostics(ReadFileBytes(s.canonical().string()), reread);
            Check(Differences(ObserveCanonical(reread), ObserveCanonical(*migrated)).empty(),
                  name + ": CameraUnlock.ini reads back as the settings the session runs on");

            // Fresh equals upgrade: the published build's first-run file, an
            // empty file and no file at all end as the committed file, `default`
            // on every row.
            if (IsUnedited(name)) {
                Check(ReadFileBytes(s.canonical().string()) == committed,
                      name + ": gives the committed file byte for byte");
            }
        }

        // From a read-only HeadTracking.ini, which keeps its attribute.
        if (input.present) {
            Scratch ro;
            ro.Write(input.bytes);
            SetFileAttributesA(ro.ini().c_str(), FILE_ATTRIBUTE_READONLY);
            const std::optional<finch_ht::Config> c = Migrate(input, ro, name + " (read-only)", migrated_files);
            Check(c && Differences(want, ObserveCanonical(*c)).empty(),
                  name + ": a read-only HeadTracking.ini imports as a writable one does");
            Check((GetFileAttributesA(ro.ini().c_str()) & FILE_ATTRIBUTE_READONLY) != 0,
                  name + ": HeadTracking.ini keeps its read-only attribute");
        }

        // Over a Defaults.ini that differs everywhere: a row the player never
        // changed is `default` and takes Defaults.ini's value, and a changed row
        // keeps the player's. With no legacy file every row is Defaults.ini's,
        // which config_tests covers.
        if (input.present) {
            Scratch skewed;
            skewed.Write(input.bytes);
            skewed.WriteDefaults(kSkewedDefaults);
            const std::optional<finch_ht::Config> c =
                Migrate(input, skewed, name + " (skewed Defaults.ini)", migrated_files);
            const std::vector<std::string> diff =
                c ? Differences(OverDefaults(want, follows, skewedObserved), ObserveCanonical(*c))
                  : std::vector<std::string>{"the load"};
            for (const std::string& d : diff) std::printf("  comparison 2, %s (skewed Defaults.ini): %s\n", name.c_str(), d.c_str());
            Check(diff.empty(), "comparison 2: over a Defaults.ini that differs everywhere, the untouched rows take its "
                                "values and the changed rows keep the import's: " + name);
            if (c) {
                const std::string bytes = ReadFileBytes(skewed.canonical().string());
                for (const Concept row : follows) {
                    const std::string key = cfg::schema::kConcepts[static_cast<std::size_t>(row)].key;
                    Check(bytes.find("\r\n" + key + "=default\r\n") != std::string::npos,
                          name + " (skewed Defaults.ini): " + key + " is written default");
                }
            }
        }
        ++compared;
    }
    std::printf("comparison 2: %d inputs\n", compared);
    std::printf("%d inputs changed a row from v1.1.1's default, %d of them the tracking mode\n", touched, modeTouched);
    Check(touched > 0 && modeTouched > 0,
          "the inputs change rows, the tracking mode among them, which then do not follow Defaults.ini");

    // Core's canonical config lint runs over these next (lint-migrated.mjs).
    const fs::path lint = MigratedFolder();
    fs::remove_all(lint);
    fs::create_directories(lint);
    std::size_t n = 0;
    for (const std::string& file : migrated_files) {
        std::ofstream out(lint / (std::to_string(n++) + ".ini"), std::ios::binary | std::ios::trunc);
        out.write(file.data(), static_cast<std::streamsize>(file.size()));
        if (!out) throw std::runtime_error("cannot write a migrated file under " + lint.string());
    }
    std::printf("%zu distinct migrated files written to %s\n", migrated_files.size(), lint.string().c_str());
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    ImportAgainstMigration(inputs);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
