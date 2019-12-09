#ifndef HLSL
struct float2 { float x; float y; };
struct float3 { float x; float y; float z; };
typedef UINT uint;
#endif

struct RayPayload
{
	float2 barycentrics;
	uint materialIndex;
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

struct ConfigConstants
{
	float3 CameraRight;
	float FocalDistance;
	float3 CameraUp;
	float CameraLensHeight;
};

struct Vertex
{
	float3 Position;
	float3 Normal;
};

#define DEFAULT_MATERIAL_FLAG 0x0
#define METALLIC_MATERIAL_FLAG 0x1
#define SUBSURFACE_SCATTER_MATERIAL_FLAG 0x2
#define NO_SPECULAR_MATERIAL_FLAG 0x4
#define MIX_MATERIAL_FLAG 0x8

struct Material
{
	float3 albedo;
	float IOR;

	float roughness;
	float3 emissive;

	float absorption;
	float scattering;
	int Flags;
};