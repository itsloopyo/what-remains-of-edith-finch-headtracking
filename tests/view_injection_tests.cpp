// Characterization tests for the decisions and maths the GetPlayerViewPoint
// hook makes. These lock behaviour that is otherwise only observable by playing
// the game with a tracker attached: which caller gets the head pose (the
// look/aim decoupling), how the two yaw modes compose, which way a 6DOF lean
// moves the camera in world space, and which views are held off.

#include "view_injection.h"

#include "test_support.h"

namespace {

using namespace finch_ht;
using finch_tests::Check;
using finch_tests::NearEqual;

CallerRvaTable MakeCallers(std::initializer_list<std::uintptr_t> rvas)
{
    CallerRvaTable table{};
    std::size_t i = 0;
    for (auto rva : rvas) table[i++] = rva;
    return table;
}

void InjectGateTests(int& failures)
{
    const CallerRvaTable callers = MakeCallers({0x010cfeef, 0x0104396a, 0x0110d073});

    Check(failures, ShouldInjectForCaller(0xdead, kInjectModeAllCallers, callers),
          "mode 0 injects for every caller (diagnostic)");
    Check(failures, !ShouldInjectForCaller(0x010cfeef, kInjectModeNone, callers),
          "the none-mode injects for nothing");

    Check(failures, ShouldInjectForCaller(0x010cfeef, 1, callers),
          "mode 1 injects for the render-path caller");
    Check(failures, !ShouldInjectForCaller(0x0104396a, 1, callers),
          "mode 1 rejects every other caller - this IS the aim decoupling");
    Check(failures, ShouldInjectForCaller(0x0110d073, 3, callers),
          "mode N selects the Nth pinned caller");

    // An empty slot must never match, or a profile that pins fewer callers than
    // the table holds would inject on a return address of 0.
    Check(failures, !ShouldInjectForCaller(0, 5, callers),
          "an unpinned (zero) slot never matches");
    Check(failures, !ShouldInjectForCaller(0x010cfeef, kInjectModeCount, callers),
          "a mode past the end injects for nothing");
    Check(failures, !ShouldInjectForCaller(0x010cfeef, -1, callers),
          "a negative mode injects for nothing");

    Check(failures, CallerRvaForMode(1, callers) == 0x010cfeef,
          "CallerRvaForMode reports the pinned RVA");
    Check(failures, CallerRvaForMode(kInjectModeAllCallers, callers) == 0
                 && CallerRvaForMode(kInjectModeNone, callers) == 0,
          "CallerRvaForMode reports 0 for the modes that pin no caller");

    Check(failures, CycleInjectMode(1, +1) == 2, "cycling forward steps one mode");
    Check(failures, CycleInjectMode(kInjectModeNone, +1) == kInjectModeAllCallers,
          "cycling forward past the last mode wraps to 0");
    Check(failures, CycleInjectMode(kInjectModeAllCallers, -1) == kInjectModeNone,
          "cycling back from 0 wraps to the last mode");
    Check(failures, kInjectModeNone == static_cast<int>(kMaxKnownCallers) + 1,
          "the none-mode sits one past the last caller slot");
}

void PinnedViewTests(int& failures)
{
    // Barbara's comic camera (-89.50) and Lewis' daydream map (-90.00) are the
    // two the rule exists for; the player's own look tops out near -78.
    Check(failures, IsPinnedDownView(-89.50f), "Barbara's comic camera is suppressed");
    Check(failures, IsPinnedDownView(-90.00f), "Lewis' daydream map camera is suppressed");
    Check(failures, IsPinnedDownView(89.50f), "a camera pinned straight up is suppressed too");
    Check(failures, !IsPinnedDownView(-78.0f), "the steepest player look is not suppressed");
    Check(failures, !IsPinnedDownView(0.0f), "an ordinary horizon view is not suppressed");
    Check(failures, IsPinnedDownView(-89.0f) && !IsPinnedDownView(-88.99f),
          "the threshold sits at exactly 89 degrees");
}

void FovClampTests(int& failures)
{
    Check(failures, NearEqual(ClampFov(80.0f), 80.0f), "an in-range FOV passes through");
    Check(failures, NearEqual(ClampFov(105.07f), 105.07f, 1e-3), "the measured +25 offset passes through");
    Check(failures, NearEqual(ClampFov(400.0f), 170.0f), "a degenerate wide FOV clamps to 170");
    Check(failures, NearEqual(ClampFov(-5.0f), 10.0f), "a degenerate narrow FOV clamps to 10");
}

void RotationCompositionTests(int& failures)
{
    const FRotator4f clean{10.0f, 20.0f, 5.0f};
    const ue::FQuat4d baseQuat = ViewQuat(clean);

    // World-space yaw is plain FRotator addition, and roll is SUBTRACTED - the
    // engine's roll runs opposite the tracker's.
    const FRotator4f world = ComposeTrackedRotation(clean, baseQuat, 3.0f, 4.0f, 2.0f, true);
    Check(failures, NearEqual(world.Yaw, 23.0f) && NearEqual(world.Pitch, 14.0f)
                 && NearEqual(world.Roll, 3.0f),
          "world yaw adds yaw/pitch and subtracts roll");

    // A zero head pose must leave the game's own rotation untouched in both
    // modes, or enabling tracking would nudge the view before the user moves.
    const FRotator4f worldIdle = ComposeTrackedRotation(clean, baseQuat, 0, 0, 0, true);
    const FRotator4f localIdle = ComposeTrackedRotation(clean, baseQuat, 0, 0, 0, false);
    Check(failures, NearEqual(worldIdle.Yaw, clean.Yaw) && NearEqual(worldIdle.Pitch, clean.Pitch)
                 && NearEqual(worldIdle.Roll, clean.Roll),
          "world yaw with no head pose is the identity");
    Check(failures, NearEqual(localIdle.Yaw, clean.Yaw, 1e-3) && NearEqual(localIdle.Pitch, clean.Pitch, 1e-3)
                 && NearEqual(localIdle.Roll, clean.Roll, 1e-3),
          "local yaw with no head pose round-trips through the quaternion unchanged");

    // From a level view the two modes agree; the point of the local mode is
    // that it leans once the game camera is pitched.
    const FRotator4f level{0.0f, 0.0f, 0.0f};
    const ue::FQuat4d levelQuat = ViewQuat(level);
    const FRotator4f levelLocal = ComposeTrackedRotation(level, levelQuat, 30.0f, 0, 0, false);
    Check(failures, NearEqual(levelLocal.Yaw, 30.0f, 1e-3) && NearEqual(levelLocal.Pitch, 0.0f, 1e-3)
                 && NearEqual(levelLocal.Roll, 0.0f, 1e-3),
          "local yaw from a level view is a plain yaw");

    const FRotator4f pitched{45.0f, 0.0f, 0.0f};
    const ue::FQuat4d pitchedQuat = ViewQuat(pitched);
    const FRotator4f pitchedLocal = ComposeTrackedRotation(pitched, pitchedQuat, 30.0f, 0, 0, false);
    const FRotator4f pitchedWorld = ComposeTrackedRotation(pitched, pitchedQuat, 30.0f, 0, 0, true);
    Check(failures, NearEqual(pitchedWorld.Roll, 0.0f) && NearEqual(pitchedWorld.Yaw, 30.0f),
          "world yaw stays horizon-locked on a pitched camera");
    Check(failures, std::fabs(pitchedLocal.Roll) > 1.0f,
          "local yaw leans on a pitched camera (the reason world yaw is the default)");
}

void PositionOffsetTests(int& failures)
{
    // Identity view: UE camera-forward is +X, right +Y, up +Z.
    const ue::FQuat4d identity = ViewQuat(FRotator4f{0.0f, 0.0f, 0.0f});

    // The processor's forward lean is NEGATIVE z, and the offset is metres
    // while UE works in centimetres. Leaning in must move the camera FORWARD
    // by the full amount - a mirrored sign here is the "leaning in barely
    // moves" bug AGENTS.md calls out.
    const ue::FVector lean = PositionOffsetUE(identity, 0.0f, 0.0f, -0.40f);
    Check(failures, NearEqual(lean.X, 40.0) && NearEqual(lean.Y, 0.0) && NearEqual(lean.Z, 0.0),
          "a full forward lean moves 40cm along camera-forward");

    const ue::FVector back = PositionOffsetUE(identity, 0.0f, 0.0f, 0.10f);
    Check(failures, NearEqual(back.X, -10.0), "a backward lean moves 10cm backwards");

    const ue::FVector sway = PositionOffsetUE(identity, 0.30f, 0.0f, 0.0f);
    Check(failures, NearEqual(sway.Y, -30.0) && NearEqual(sway.X, 0.0),
          "sway runs opposite UE camera-right");

    const ue::FVector heave = PositionOffsetUE(identity, 0.0f, 0.20f, 0.0f);
    Check(failures, NearEqual(heave.Z, 20.0) && NearEqual(heave.X, 0.0),
          "heave runs along camera-up");

    // The offset is built in the CLEAN camera basis, so it follows where the
    // body faces: yawed 90 degrees, forward is world +Y.
    const ue::FQuat4d yawed = ViewQuat(FRotator4f{0.0f, 90.0f, 0.0f});
    const ue::FVector leanYawed = PositionOffsetUE(yawed, 0.0f, 0.0f, -0.40f);
    Check(failures, NearEqual(leanYawed.X, 0.0, 1e-3) && NearEqual(leanYawed.Y, 40.0, 1e-3),
          "the offset follows the camera basis, not world axes");
}

}  // namespace

int RunViewInjectionTests()
{
    int failures = 0;
    std::cout << "View injection tests\n";
    InjectGateTests(failures);
    PinnedViewTests(failures);
    FovClampTests(failures);
    RotationCompositionTests(failures);
    PositionOffsetTests(failures);
    return finch_tests::Report("View injection tests", failures);
}
