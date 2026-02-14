/**
 * Compute shader for generating probe distance images.
 *
 * For each pixel, computes an octahedral direction, traces a ray from the
 * probe position, and stores the distance to the nearest occluder.
 * Stores a negative value (-1) if no geometry is hit.
 */

#include "Utils/HostDeviceShared.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.RaytracingInline;
import Scene.Scene;
import Utils.Math.Ray;

// ------------------------------------------------------------------
// Resources
// ------------------------------------------------------------------

cbuffer CB
{
    float3 gProbePosition;
    uint   gImageResolution;
};

RWTexture2D<float> gOutput; // Distance to nearest occluder (negative if no hit).

// ------------------------------------------------------------------
// Octahedral mapping
// ------------------------------------------------------------------

/** Decode a [0,1]^2 UV coordinate to a unit-sphere direction
    using the standard octahedral mapping.
*/
float3 octDecode(float2 uv)
{
    // Map [0,1] to [-1,1].
    float2 f = uv * 2.0 - 1.0;

    // Reconstruct z from the L1 norm.
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));

    // Reflect the folds of the lower hemisphere.
    if (n.z < 0.0)
    {
        float2 sign_xy = float2(n.x >= 0.0 ? 1.0 : -1.0,
                                n.y >= 0.0 ? 1.0 : -1.0);
        n.xy = (1.0 - abs(n.yx)) * sign_xy;
    }

    return normalize(n);
}

// ------------------------------------------------------------------
// Main
// ------------------------------------------------------------------

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= uint2(gImageResolution, gImageResolution)))
        return;

    // Pixel center in [0,1]^2.
    float2 uv = (float2(pixel) + 0.5) / float(gImageResolution);

    // Convert to world-space direction via octahedral decoding.
    float3 dir = octDecode(uv);

    // Trace ray from the probe position.
    Ray ray = Ray(gProbePosition, dir, 0.0, 1e30);
    float hitT = 1e30;

    SceneRayQuery<1> sceneRayQuery; // 1 = use alpha test.
    HitInfo hit = sceneRayQuery.traceRay(ray, hitT);

    // Write distance (negative means no occluder).
    gOutput[pixel] = hit.isValid() ? hitT : -1.0;
}
