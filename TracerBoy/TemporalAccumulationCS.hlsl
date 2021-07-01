#define HLSL
#include "TemporalAccumulationSharedShaderStructs.h"

Texture2D TemporalHistory : register(t0);
Texture2D CurrentFrame : register(t1);
Texture2D WorldPositionTexture : register(t2);
SamplerState BilinearSampler : register(s0);

RWTexture2D<float4> OutputTexture;

cbuffer TemporalAccumulationCB
{
	TemporalAccumulationConstants Constants;
}

float PlaneIntersection(float3 RayOrigin, float3 RayDirection, float3 PlaneOrigin, float3 PlaneNormal)
{
      float denom = dot(PlaneNormal, RayDirection);
      if(abs(denom) > 0.0)
      {
          float t =  dot(PlaneOrigin - RayOrigin, PlaneNormal) / denom;
		return t;
      }
      return -1.0;
}

[numthreads(TEMPORAL_ACCUMULATION_THREAD_GROUP_WIDTH, TEMPORAL_ACCUMULATION_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;

	float3 WorldPosition = WorldPositionTexture[DTid.xy];
	float aspectRatio = float(Constants.Resolution.x) / float(Constants.Resolution.y);
	float lensHeight = Constants.CameraLensHeight;
	float lensWidth = lensHeight * aspectRatio;

	float3 PrevFrameCameraDir = normalize(Constants.PrevFrameCameraLookAt - Constants.PrevFrameCameraPosition);
	float3 PrevFrameFocalPoint = Constants.PrevFrameCameraPosition - Constants.CameraFocalDistance * PrevFrameCameraDir;
	float3 PrevFrameRayDirection = normalize(WorldPosition - PrevFrameFocalPoint);

	float3 OutputColor = CurrentFrame[DTid.xy].rgb;
	float3 PrevFrameColor = OutputColor;
	float t = PlaneIntersection(PrevFrameFocalPoint, PrevFrameRayDirection, Constants.PrevFrameCameraPosition, PrevFrameCameraDir);
	if (!Constants.IgnoreHistory && t >= 0)
	{
		PrevFrameColor = TemporalHistory[DTid.xy].rgb;
#if 1
		float3 LensPosition = PrevFrameFocalPoint + PrevFrameRayDirection * t;

		float3 OffsetFromCenter = LensPosition  - Constants.PrevFrameCameraPosition;
		float2 UV = float2(
			dot(OffsetFromCenter, Constants.PrevFrameCameraRight) / (lensWidth / 2.0),
			dot(OffsetFromCenter, Constants.PrevFrameCameraUp)    / (lensHeight / 2.0));
		
		// Convert from (-1, 1) -> (0,1)
		UV = (UV + 1.0) / 2.0;
		UV.y = 1.0 - UV.y;
	
		if (all(UV >= 0.0 && UV <= 1.0))
		{
	#if 0
			float distanceToNeighbor;
			float3 PreviousFrameWorldPosition = GetPreviousFrameWorldPosition(UV, distanceToNeighbor);
			distanceToNeighbor = length(MaxWorldPosition - MinWorldPosition) * 0.035;
			if (length(PreviousFrameWorldPosition - WorldPosition) < distanceToNeighbor)
			{
				previousFrameColor = PreviousFrameOutput.SampleLevel(BilinearSampler, UV, 0);
				bValidHistory = true;
			}
	#endif
			PrevFrameColor = TemporalHistory.SampleLevel(BilinearSampler, UV, 0).rgb;
		
		}
#endif
	}

	OutputColor = lerp(OutputColor, PrevFrameColor, 0.9);
	OutputTexture[DTid.xy] = float4(OutputColor, 1);
}