#define HLSL
#include "SharedShaderStructs.h"

#define IS_SHADER_TOY 0
cbuffer PerFrameConstants : register(b0)
{
	float3 CameraPosition;
	float Time;
	float2 Mouse;
}

cbuffer ConfigConstants : register(b1)
{
	float2 Resolution;
	float FocalDistance;
	float CameraLensHeight;
	float3 CameraLookAt;
	float3 CameraRight;
	float3 CameraUp;
}

float3 GetCameraPosition() { return CameraPosition; }
float3 GetCameraLookAt() { return CameraLookAt; }
float3 GetCameraUp() { return CameraUp; }
float3 GetCameraRight() { return CameraRight; }
float GetCameraLensHeight() { return CameraLensHeight; }
float GetCameraFocalDistance() { return FocalDistance; }

RWTexture2D<float4> OutputTexture : register(u0);
Texture2D LastFrameTexture : register(t0);
RaytracingAccelerationStructure AS : register(t1);
SamplerState PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

#define GLOBAL static
float GetTime() { return Time; }
float3 GetResoultion() { return float3(Resolution.xy, 1.0); }
float4 GetMouse() { return float4(Mouse.x, GetResoultion().y - Mouse.y, 0.0, 0.0); }
float4 GetLastFrameData() {
	return LastFrameTexture.SampleLevel(PointSampler, float2(0, 1), 0);
}
float4 GetAccumulatedColor(float2 uv) {
	uv.y = 1.0 - uv.y;
	return LastFrameTexture.SampleLevel(PointSampler, uv, 0);
}
bool NeedsToSaveLastFrameData() { return false; } // Handled by the CPU

struct Ray
{
	float3 origin;
	float3 direction;
};
float2 Intersect(Ray ray, out float3 normal, out uint PrimitiveID)
{
	RayDesc dxrRay;
	dxrRay.Origin = ray.origin;
	dxrRay.Direction = ray.direction;
	dxrRay.TMin = 0.001;
	dxrRay.TMax = 10000.0;

	float bigNumber = 9999.0f;
	RayPayload payload = { float2(0, 0), uint(-1), bigNumber, float3(0, 0, 0) };
	TraceRay(AS, RAY_FLAG_NONE, ~0, 0, 1, 0, dxrRay, payload);

	float2 result;
	result.x = payload.hitT;
	bool hitFound = result.x < bigNumber;
	if (hitFound)
	{
		if (payload.objectIndex > 0)
		{
			result.y = 9;
		}
		else
		{
			result.y = 32;
		}
	}
	else
	{
		result.y = -1;
	}
	PrimitiveID = 0;
	normal = payload.normal;
	return result;
}

#include "GLSLCompat.h"
#include "kernel.glsl"
#include "FullScreenPlaneVS.hlsl"

[shader("raygeneration")]
void RayGen()
{
	float2 dispatchUV = float2(DispatchRaysIndex().xy + 0.5) / float2(DispatchRaysDimensions().xy);
	float2 uv = vec2(0, 1) + dispatchUV * vec2(1, -1);
	
	OutputTexture[DispatchRaysIndex().xy] = PathTrace(uv * GetResoultion());
}
