#define HLSL
#include "DirectMLSharedShaderStructs.h"

cbuffer DirctMLCB
{
    DirectMLConstants Constants;
}

Buffer<half> Tensor : register(t0);
RWTexture2D<float4> OutputTexture : register(u0);

[numthreads(DIRECTML_THREAD_GROUP_WIDTH, DIRECTML_THREAD_GROUP_HEIGHT, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
    if (DTid.x >= Constants.Resolution.x || DTid.y >= Constants.Resolution.y) return;
    
    float4 color;
    uint index = DTid.y * Constants.Resolution.x + DTid.x;

    if (Constants.UseNHWC)
    {
        color.b = Tensor[index * 3];
        color.g = Tensor[index * 3 + 1];
        color.r = Tensor[index * 3 + 2];
        color.a = 1.0f;
    }
    else
    {
        uint blockSize = Constants.Resolution.x * Constants.Resolution.y;

        color.r = Tensor[index];
        color.g = Tensor[index + blockSize];
        color.b = Tensor[index + 2 * blockSize];
        color.a = 1.0f;
    }
    
    OutputTexture[DTid.xy] = color;
}