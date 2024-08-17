#define HLSL
#include "DirectMLSharedShaderStructs.h"

cbuffer DirctMLCB
{
    DirectMLConstants Constants;
}

Texture2D<float4> inputImage0 : register(t0);
Texture2D<float4> inputImage1 : register(t1);
Texture2D<float4> inputImage2 : register(t2);
RWBuffer<half> opTensor : register(u0);

[numthreads(DIRECTML_THREAD_GROUP_WIDTH, DIRECTML_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    if (DTid.x >= Constants.OutputResolution.x || DTid.y >= Constants.OutputResolution.y)
        return;
    
    uint index = DTid.y * Constants.OutputResolution.x + DTid.x;

    float3 val0 = inputImage0[DTid.xy].xyz;
    float3 val1 = inputImage1[DTid.xy].xyz;
    float3 val2 = inputImage2[DTid.xy].xyz;

    uint stride = Constants.UseNormalsAndAlbedo ? 9 : 3;

    if (Constants.UseNHWC)
    {
        opTensor[index * stride] = val0.x;
        opTensor[index * stride + 1] = val0.y;
        opTensor[index * stride + 2] = val0.z;

        if(Constants.UseNormalsAndAlbedo)
        {
            opTensor[index * stride + 3] = val1.x;
            opTensor[index * stride + 4] = val1.y;
            opTensor[index * stride + 5] = val1.z;

            opTensor[index * stride + 6] = val2.x;
            opTensor[index * stride + 7] = val2.y;
            opTensor[index * stride + 8] = val2.z;
        }

    }
    else
    {
        uint planeSize = Constants.OutputResolution.x * Constants.OutputResolution.y;

        // RGB plane order since model was trained on this
        opTensor[index] = val0.x;
        opTensor[index + planeSize] = val0.y;
        opTensor[index + planeSize * 2] = val0.z;
    }

}