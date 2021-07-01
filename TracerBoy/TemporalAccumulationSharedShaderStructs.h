#pragma once

#define TEMPORAL_ACCUMULATION_THREAD_GROUP_WIDTH 8
#define TEMPORAL_ACCUMULATION_THREAD_GROUP_HEIGHT 8

struct TemporalAccumulationConstants
{
	uint2 Resolution;
	float CameraFocalDistance;
	uint IgnoreHistory;

	float3 CameraPosition;
	float CameraLensHeight;

	float3 CameraLookAt;
	uint padding1;

	float3 CameraUp;
	uint padding2;
	
	float3 CameraRight;
	uint padding3;

	float3 PrevFrameCameraPosition;
	uint padding4;
	
	float3 PrevFrameCameraUp;
	uint padding5;
	
	float3 PrevFrameCameraRight;
	uint padding6;

	float3 PrevFrameCameraLookAt;
	uint padding7;
};