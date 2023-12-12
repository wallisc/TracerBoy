#define HLSL
#include "GenerateMotionVectorsSharedShaderStructs.h"

Texture2D WorldPositionTexture : register(t0);
Texture2D PreviousFrameWorldPositionTexture : register(t1);

RWTexture2D<float2> OutputMotionVectors : register(u0);

cbuffer GenerateMotionVectorsCB
{
	GenerateMotionVectorsConstants Constants;
}

float PlaneIntersection(float3 RayOrigin, float3 RayDirection, float3 PlaneOrigin, float3 PlaneNormal)
{
	float denom = dot(PlaneNormal, RayDirection);
	if (abs(denom) > 0.0)
	{
		float t = dot(PlaneOrigin - RayOrigin, PlaneNormal) / denom;
		return t;
	}
	return -1.0;
}

[numthreads(GENERATE_MOTION_VECTORS_THREAD_GROUP_WIDTH, GENERATE_MOTION_VECTORS_THREAD_GROUP_HEIGHT, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	float3 WorldPosition = WorldPositionTexture[DTid.xy];
	float aspectRatio = float(Constants.Resolution.x) / float(Constants.Resolution.y);
	float lensHeight = Constants.CameraLensHeight;
	float lensWidth = lensHeight * aspectRatio;

	float3 PrevFrameCameraDir = normalize(Constants.PrevFrameCameraLookAt - Constants.PrevFrameCameraPosition);
	float3 PrevFrameFocalPoint = Constants.PrevFrameCameraPosition - Constants.CameraFocalDistance * PrevFrameCameraDir;
	float3 PrevFrameRayDirection = normalize(WorldPosition - PrevFrameFocalPoint);

	float t = PlaneIntersection(PrevFrameFocalPoint, PrevFrameRayDirection, Constants.PrevFrameCameraPosition, PrevFrameCameraDir);
	bool bValidHistory = false;
	float3 LensPosition = PrevFrameFocalPoint + PrevFrameRayDirection * t;

	float2 CurrentUV = (float2(DTid.xy) + float2(0.5, 0.5)) / float2(Constants.Resolution.xy);
	float3 OffsetFromCenter = LensPosition - Constants.PrevFrameCameraPosition;
	float2 UV = float2(
		dot(OffsetFromCenter, Constants.PrevFrameCameraRight) / (lensWidth / 2.0),
		dot(OffsetFromCenter, Constants.PrevFrameCameraUp) / (lensHeight / 2.0));

	// Convert from (-1, 1) -> (0,1)
	UV = (UV + 1.0) / 2.0;
	UV.y = 1.0 - UV.y;
	
	float2 MotionVectors = (UV - CurrentUV) * Constants.Resolution;
	OutputMotionVectors[DTid.xy] = MotionVectors;
}