#define IS_SHADER_TOY 0
cbuffer Constants : register(b0)
{
	float2 Mouse;
	float2 Resolution;
	float Time;
}

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

#include "GLSLCompat.h"
#include "kernel.glsl"
#include "FullScreenPlaneVS.hlsl"

float4 main(FULLSCREEN_PLANE_VS_OUTPUT input) : SV_Target
{
	vec2 uv = vec2(0, 1) + input.UV * vec2(1, -1);
	return PathTrace(uv *  GetResoultion());
}