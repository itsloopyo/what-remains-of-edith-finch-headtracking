// Consistency checks on the shipped build profile. The RVAs themselves can
// only be confirmed against the game, but the relationships between them are
// checkable here, and each one has a failure mode that reaches a user: a
// default inject mode pointing at an empty slot silently disables tracking, and
// an FOV write aimed at a caller that is not the FMinimalViewInfo builder
// corrupts that caller's stack frame.

#include "builds/build_profile.h"
#include "view_injection.h"

#include "test_support.h"

namespace finch_ht::builds {
    extern const BuildProfile kSteamProfile_20170621;
}

namespace {

using finch_tests::Check;

}  // namespace

int RunBuildProfileTests()
{
    int failures = 0;
    std::cout << "Build profile tests\n";

    const finch_ht::BuildProfile& profile = finch_ht::builds::kSteamProfile_20170621;
    const finch_ht::OffsetTable& offsets = profile.Offsets;

    // Confirmed in-process against the running FinchGame.exe (see .lab/NOTES.md).
    Check(failures, profile.Fingerprint.TimeDateStamp == 0x5949BA9Du
                 && profile.Fingerprint.SizeOfImage == 0x02DBA000u
                 && profile.Fingerprint.CheckSum == 0x02C3913Fu,
          "the Steam 2017-06-21 fingerprint is unchanged");

    Check(failures, offsets.kGetPlayerViewPointRva != 0,
          "the profile is complete, so the mod activates rather than staying dormant");

    Check(failures, offsets.kGetPlayerViewPointPrologue[0] != 0,
          "the decrypted prologue is pinned, so the SteamStub wait has something to match");

    Check(failures, offsets.kDefaultInjectMode >= finch_ht::kInjectModeFirstCaller
                 && offsets.kDefaultInjectMode <= finch_ht::kInjectModeLastCaller,
          "the default inject mode selects a single caller, not all or none");

    Check(failures, finch_ht::CallerRvaForMode(offsets.kDefaultInjectMode,
                                               offsets.kKnownCallerRvas) != 0,
          "the default inject mode points at a pinned caller slot");

    Check(failures, offsets.kViewInfoCallerRva == offsets.kKnownCallerRvas[0],
          "the FOV write targets the same render-path caller the head pose does");

    Check(failures, offsets.kViewInfoRotationOffset == 0x0C && offsets.kViewInfoFovOffset == 0x18,
          "the FMinimalViewInfo field offsets are unchanged");

    // Characterizes what ships today rather than what is wanted:
    // bShowMouseCursor has never been derived for this build, so InGameplay()
    // returns true unconditionally and tracking keeps running in menus and
    // cutscenes. Derive the offset and this check is the reminder to revisit
    // the gate.
    Check(failures, offsets.kShowMouseCursorOffset == 0,
          "the menu/cutscene gate is still disabled on this profile (offset not derived)");

    return finch_tests::Report("Build profile tests", failures);
}
