// The differential test for the conversion from HeadTracking.ini to the
// canonical config format.
//
// Two readings of every input, and what may differ between them:
//
//   Oracle     the reader of the newest published build (v1.1.1, e6a9d7d, core
//              3038291), compiled from its own sources (oracle_api.h)
//   Import     the frozen reader in src/legacy_config/
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since the published build that change how
// the file is read. There are none. src/ was unchanged from v1.1.1 to the commit
// the reader was frozen at, and every core source either reader compiles holds
// the same bytes at 3038291 and at the pin, which SourcesAreThePinnedOnes checks.
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
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "config.h"
#include "legacy_config/legacy_config.h"
#include "oracle_api.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace {

using cameraunlock::TrackingMode;
namespace cfg = cameraunlock::config;
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
// game exe.

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
    std::string ini() const { return dir() + "\\HeadTracking.ini"; }

    void Write(const std::string& bytes) const {
        std::ofstream out(ini(), std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) throw std::runtime_error("cannot write " + ini());
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

std::vector<cfg::LegacyKey> CorpusReads() {
    std::vector<cfg::LegacyKey> reads;
    for (const testing::MutationKey& k : CorpusKeys()) reads.push_back({k.section, k.key});
    return reads;
}

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

}  // namespace

int main() {
    // Unbuffered, so the lines before an uncaught exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SourcesAreThePinnedOnes();
    FirstRunFileIsThePublishedBuilds();
    const std::vector<Input> inputs = Inputs();
    OracleAgainstImport(inputs);
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
