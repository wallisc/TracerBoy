#pragma once

struct VisualizerRay
{
	float3 Origin;
	float3 Direction;
	float HitT;
	float BounceCount;
};

#define MAX_VISUALIZER_RAYS 1024
#define VISUALIZER_RAY_COUNTER_SIZE_IN_BYTES 4
#define VISUALIZER_RAY_SIZE_IN_BYTES 32

static inline int GetOffsetToVisualizationRay(int RayIndex)
{
	return VISUALIZER_RAY_SIZE_IN_BYTES * RayIndex + VISUALIZER_RAY_COUNTER_SIZE_IN_BYTES;
}