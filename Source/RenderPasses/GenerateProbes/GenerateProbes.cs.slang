#include "Utils/HostDeviceShared.slangh"
#include "Utils/Math/MathConstants.slangh"

import Scene.Scene;
import Utils.Math.Ray;

// ------------------------------------------------------------------
// Resources
// ------------------------------------------------------------------

cbuffer CB
{
    uint2 gFrameDim;
    uint  gProbeCount;
    float gProbeRadius;
};

Texture2D<float4>        gColor;     // Tonemapped input image.
Texture2D<PackedHitInfo> gVBuffer;   // Visibility buffer (packed hit info).
RWTexture2D<float4>      gOutput;    // Output image.
StructuredBuffer<float4> gProbes;    // Probe positions (xyz = world pos, w = unused).

// ------------------------------------------------------------------
// Helper: get world position from a vbuffer hit
// ------------------------------------------------------------------

/** Reconstruct world-space position from the hit stored in the vbuffer.
    Returns the camera origin if the hit type is unknown.
*/
float3 getWorldPosition(float3 rayOrigin, float3 rayDir, HitInfo hit)
{
    switch (hit.getType())
    {
    case HitType::Triangle:
        return gScene.getVertexData(hit.getTriangleHit()).posW;
    case HitType::DisplacedTriangle:
        return gScene.getVertexData(hit.getDisplacedTriangleHit(), -rayDir).posW;
    case HitType::Curve:
        return gScene.getVertexDataFromCurve(hit.getCurveHit()).posW;
    case HitType::SDFGrid:
        return gScene.getVertexDataFromSDFGrid(hit.getSDFGridHit(), rayOrigin, rayDir).posW;
    default:
        return rayOrigin;
    }
}

// ------------------------------------------------------------------
// Main compute shader
// ------------------------------------------------------------------

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (any(pixel >= gFrameDim))
        return;

    // Read input color.
    float4 inputColor = gColor[pixel];

    // Compute camera ray for this pixel.
    Ray ray = gScene.camera.computeRayPinhole(pixel, gFrameDim);

    // ----- Determine scene depth from vbuffer -----
    HitInfo hit = HitInfo(gVBuffer[pixel]);
    float sceneDepth = 1e30f;
    if (hit.isValid())
    {
        float3 posW = getWorldPosition(ray.origin, ray.dir, hit);
        // Depth along the ray direction (consistent with ray-sphere t values).
        sceneDepth = dot(posW - ray.origin, ray.dir);
    }

    // ----- Ray-march through probes to find closest visible sphere -----
    float closestT    = sceneDepth;  // Only accept hits closer than scene geometry.
    float3 closestN   = float3(0, 1, 0);
    bool   hitProbe   = false;

    for (uint i = 0; i < gProbeCount; i++)
    {
        float3 center = gProbes[i].xyz;
        float  r      = gProbeRadius;

        // Early cull: signed distance of probe center along the ray.
        float3 toCenter = center - ray.origin;
        float  projDist = dot(toCenter, ray.dir);

        // Skip probes entirely behind the camera or beyond the closest hit so far.
        if (projDist + r <= 0.0 || projDist - r >= closestT)
            continue;

        // Ray-sphere intersection.
        float3 oc   = ray.origin - center;
        float  b    = dot(oc, ray.dir);
        float  c    = dot(oc, oc) - r * r;
        float  disc = b * b - c;

        if (disc < 0.0)
            continue;

        float sqrtDisc = sqrt(disc);
        float t = -b - sqrtDisc; // Front intersection.

        if (t <= 0.0)
            t = -b + sqrtDisc;   // Try back intersection (camera inside sphere).

        if (t > 0.0 && t < closestT)
        {
            closestT  = t;
            float3 hitPos = ray.origin + t * ray.dir;
            closestN  = normalize(hitPos - center);
            hitProbe  = true;
        }
    }

    // ----- Shading -----
    float4 outputColor = inputColor;
    if (hitProbe)
    {
        // Simple shading: directional light + ambient + rim.
        float3 probeColor = float3(0.15, 0.75, 0.35); // Bright green.
        float3 lightDir   = normalize(float3(0.5, 1.0, 0.3));
        float  ndotl      = saturate(dot(closestN, lightDir));

        // View-dependent rim highlight.
        float3 viewDir = -ray.dir;
        float  ndotv   = saturate(dot(closestN, viewDir));
        float  rim     = pow(1.0 - ndotv, 3.0);

        float3 shaded = probeColor * (0.25 + 0.75 * ndotl) + float3(1.0) * rim * 0.25;
        outputColor = float4(shaded, 1.0);
    }

    gOutput[pixel] = outputColor;
}
