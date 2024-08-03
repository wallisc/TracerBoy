#pragma once

#include "HistogramSharedShaderStructs.h"

#define CALCULATE_AVERAGED_LUMINANCE_THREAD_GROUP_WIDTH 16
#define CALCULATE_AVERAGED_LUMINANCE_THREAD_GROUP_HEIGHT 16

struct CalculateAveragedLuminanceConstants
{
	uint PixelCount;
	float LogLuminanceRange;
	float MinLogLuminance;
};