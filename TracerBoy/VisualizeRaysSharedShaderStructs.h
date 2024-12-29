#include "SharedShaderStructs.h"

#define VISUALIZE_RAYS_THREAD_GROUP_WIDTH 8
#define VISUALIZE_RAYS_THREAD_GROUP_HEIGHT 8

struct VisualizationRaysConstants
{
	uint2 Resolution;
	float CylinderRadius;
	float FocalLength;

	float3 CameraPosition;
	float LensHeight;
	
	float3 CameraLookAt;
	float RayDepth;

	float3 CameraRight;
	float Padding2;

	float3 CameraUp;
	int RayCount;
};