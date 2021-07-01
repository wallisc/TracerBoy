#include "SharedShaderStructs.h"

#define COMPOSITE_ALBEDO_THREAD_GROUP_WIDTH 8
#define COMPOSITE_ALBEDO_THREAD_GROUP_HEIGHT 8

struct CalculateVarianceConstants
{
	uint2 Resolution;
	uint GlobalFrameCount;
};

