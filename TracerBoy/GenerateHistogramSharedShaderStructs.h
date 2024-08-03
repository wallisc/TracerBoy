#pragma once

#include "HistogramSharedShaderStructs.h"

#define GENERATE_HISTOGRAM_THREAD_GROUP_WIDTH 16
#define GENERATE_HISTOGRAM_THREAD_GROUP_HEIGHT 16


struct GenerateHistogramConstants
{
	uint2 Resolution;
	float minLogLuminance;
	float oneOverLogLuminanceRange;
};
