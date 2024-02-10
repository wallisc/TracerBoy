#include "SharedShaderStructs.h"

#define DIRECTML_THREAD_GROUP_WIDTH 8
#define DIRECTML_THREAD_GROUP_HEIGHT 8

struct DirectMLConstants
{
	uint2 Resolution;
	uint UseNHWC;
	uint Padding;
};

