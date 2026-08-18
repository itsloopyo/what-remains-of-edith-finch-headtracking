#pragma once

namespace finch_ht
{
    // UE4 (not UE5) ABI: Large World Coordinates do not exist before UE5, so the
    // engine's FVector / FRotator are 3-FLOAT POD (12 bytes each). These are the
    // exact types APlayerController::GetPlayerViewPoint(self, &OutLocation,
    // &OutRotation) writes through. Declaring them as doubles (24B) would
    // overflow the engine's stack out-params - the UE5 LWC trap in reverse.
    // Quaternion math stays in core's double types; we convert only at this ABI
    // boundary.
    struct FVector4f  { float X, Y, Z; };
    struct FRotator4f { float Pitch, Yaw, Roll; };

    using GetPlayerViewPoint_t =
        void(__fastcall*)(void* self, FVector4f* outLocation, FRotator4f* outRotation);
}
