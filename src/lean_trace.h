#pragma once

#include <cstdint>

#include <cameraunlock/camera/lean_clamp.h>

// The engine half of the lean collision clamp: ask UE4 how far the eye may
// travel from where the game put it before it meets something solid.
//
// Core owns what to do with the answer (cameraunlock/camera/lean_clamp.h); this
// owns getting one. It calls the engine's own Blueprint sweep,
// UKismetSystemLibrary::SphereTraceSingle, so there is no second physics query
// in this mod to disagree with the game's and no FCollisionQueryParams ABI to
// reconstruct - every argument is a scalar, a pointer, or a TArray we build.
//
// A SPHERE sweep rather than a line: the radius is the standoff, so the hit
// location comes back already backed off the surface, and a sphere cannot slip
// through a gap the camera's near plane would have seen through.
namespace finch_ht::lean_trace {

// Radius of the swept sphere, in UE units (cm). This IS the standoff from the
// surface, so it must exceed the camera's near clip distance or geometry is
// culled before the eye reaches it and the wall goes transparent anyway.
void SetRadius(float centimetres);

// Which ETraceTypeQuery the sweep runs on. Configurable because the channel a
// level's geometry blocks is a project setting, not an engine constant, and the
// only way to know is to run it and read the log.
void SetChannel(int traceTypeQuery);

// Resolves the sweep against the active build profile. False, having logged
// which part was missing, when the profile does not carry it; the caller then
// runs with no clamp rather than with a broken one. Safe to call repeatedly.
bool Ready();

// The query, in the shape core's clamp takes. `context` is the
// APlayerController the hook was called on: it is a UObject in the level, which
// is all WorldContextObject has to be, so reaching the world costs no extra
// pointer chain. Must be called on the game thread.
//
// Nothing is excluded from the sweep yet. The camera sits inside the player's
// own capsule, so if that capsule blocks the configured channel every sweep
// starts penetrating and reports nowhere to go. That is a loud, unmistakable
// symptom, so it gets measured before the Pawn offset is worth hunting for.
cameraunlock::camera::LeanObstruction Query(void* context,
                                            const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction,
                                            float maxDistance);

}  // namespace finch_ht::lean_trace
