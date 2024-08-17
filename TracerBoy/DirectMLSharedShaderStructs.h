#include "SharedShaderStructs.h"

#define DIRECTML_THREAD_GROUP_WIDTH 8
#define DIRECTML_THREAD_GROUP_HEIGHT 8

struct DirectMLConstants
{
	uint2 InputResolution;
	uint2 OutputResolution;
	uint UseNHWC;
	uint SliceToDebug;
	uint InputChannelDepth;
	uint UseNormalsAndAlbedo;
};

