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

Texture2D LastFrameTexture : register(t0);
//RaytracingAccelerationStructure AS : register(t1);
SamplerState PointSampler;
SamplerState BilinearSampler;

#define GLOBAL static
float GetTime() { return Time; }
float3 GetResoultion() { return float3(Resolution.xy, 1.0); }
float4 GetMouse() { return float4(Mouse.x, GetResoultion().y - Mouse.y, 0.0, 0.0); }
float4 GetLastFrameData() { 
	return LastFrameTexture.Sample(PointSampler, float2(0, 1));
}
float4 GetAccumulatedColor(float2 uv) { 
	uv.y = 1.0 - uv.y;
	return LastFrameTexture.Sample(PointSampler, uv); 
}
bool NeedsToSaveLastFrameData() { return false; } // HAndled by the CPU


struct Ray
{
	float3 origin;
	float3 direction;
};

float2 Intersect(Ray ray, out float3 normal, out uint PrimitiveID)
{
	normal = float3(0, 0, 0);
	PrimitiveID = 0;
	return float2(0, 0);
}

#include "GLSLCompat.h"
#include "kernel.glsl"
#include "FullScreenPlaneVS.hlsl"

float4 main(FULLSCREEN_PLANE_VS_OUTPUT input) : SV_Target
{
	vec2 uv = vec2(0, 1) + input.UV * vec2(1, -1);
	return PathTrace(uv *  GetResoultion());
}