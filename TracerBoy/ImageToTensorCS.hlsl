#define HLSL
#include "DirectMLSharedShaderStructs.h"

cbuffer DirctMLCB
{
    DirectMLConstants Constants;
}

Texture2D<float4> inputImage : register(t0);
RWBuffer<half> opTensor : register(u0);

[numthreads(DIRECTML_THREAD_GROUP_WIDTH, DIRECTML_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    if (DTid.x >= Constants.OutputResolution.x || DTid.y >= Constants.OutputResolution.y)
        return;
    
    uint index = DTid.y * Constants.OutputResolution.x + DTid.x;

    float3 val = inputImage[DTid.xy].xyz;

    if (Constants.UseNHWC)
    {
        opTensor[index * 3] = val.x;
        opTensor[index * 3 + 1] = val.y;
        opTensor[index * 3 + 2] = val.z;
    }
    else
    {
        uint planeSize = Constants.OutputResolution.x * Constants.OutputResolution.y;

        // RGB plane order since model was trained on this
        opTensor[index] = val.x;
        opTensor[index + planeSize] = val.y;
        opTensor[index + planeSize * 2] = val.z;
    }

}