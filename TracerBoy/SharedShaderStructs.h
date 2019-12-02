#ifndef HLSL
struct float2 { float x; float y; };
struct float3 { float x; float y; float z; };
typedef UINT uint;
#endif

struct RayPayload
{
	float2 barycentrics;
	uint objectIndex;
	float hitT;
	float3 normal;
};