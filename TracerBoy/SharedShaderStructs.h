#ifndef HLSL
struct float2 { float x; float y; };
typedef UINT uint;
#endif

struct RayPayload
{
	float2 barycentrics;
	uint objectIndex;
};