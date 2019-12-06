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

struct PerFrameConstants
{
	float3 CameraPosition;
	float Time;
	float3 CameraLookAt;
	uint InvalidateHistory;
	float2 Mouse;
};

struct Vertex
{
	float3 Position;
	float3 Normal;
};