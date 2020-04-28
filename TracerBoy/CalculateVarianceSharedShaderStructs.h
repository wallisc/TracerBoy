#include "SharedShaderStructs.h"

#define CALCULATE_VARIANCE_THREAD_GROUP_WIDTH 1
#define CALCULATE_VARIANCE_THREAD_GROUP_HEIGHT 1

struct CalculateVarianceConstants
{
	uint2 Resolution;
	uint GlobalFrameCount;
};

