/** Structured entry for ray logging.
    Each entry captures the origin, direction, hit point, and hit status of a single ray.
    The struct is padded to 48 bytes for proper GPU alignment.
*/
struct RayLogEntry
{
    float3 origin;      ///< Ray origin in world space.
    float  _pad0;       ///< Padding for 16-byte alignment.
    float3 direction;   ///< Ray direction (normalized).
    float  _pad1;       ///< Padding for 16-byte alignment.
    float3 hitPoint;    ///< Hit point in world space (zero if miss).
    uint   isHit;       ///< 1 if the ray hit geometry, 0 if miss.
};
