#pragma once

#include <cstdint>

#include "ue4_types.h"

// The render-path caller (ULocalPlayer::GetViewPoint) hands GetPlayerViewPoint
// the Location and Rotation fields of a single FMinimalViewInfo, and fills that
// struct's FOV from the camera manager first. So outLocation doubles as the
// address of the view info, and the FOV the projection matrix is about to be
// built from sits a fixed distance past it - readable, and writable in time to
// change the frame.
//
// Nothing in the injection maths needs it: head rotation and the 6DOF offset are
// angles and world-space centimetres, and this game draws no crosshair, so there
// is no screen-space projection to keep in step with the FOV. It is read so the
// log reports what the game is actually rendering with, and written so the user
// can widen a game that ships no FOV setting at all.

namespace finch_ht
{
    // Publishes the FOV the game just wrote (for the log) and applies the user's
    // offset in place. A no-op unless this return address is the view-info
    // builder pinned by the active profile.
    void ApplyFovOverride(std::uintptr_t retRva, FVector4f* outLocation,
                          const FRotator4f* outRotation);
}
