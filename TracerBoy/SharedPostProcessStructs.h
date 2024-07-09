#include "SharedShaderStructs.h"

struct PostProcessConstants
{
	uint2 Resolution;
	uint FramesRendered;
	float ExposureMultiplier;
	uint TonemapType;
	uint UseGammaCorrection;
	uint OutputType;
	float VarianceMultiplier;
};