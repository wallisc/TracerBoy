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
    if (DTid.x >= Constants.OutputResolution.x || DTid.y >= Constants.OutputResolution.y)
        return;
    
    float4 color;
    if (DTid.x < Constants.InputResolution.x && DTid.y < Constants.InputResolution.y)
    {
        uint index = DTid.y * Constants.InputResolution.x + DTid.x;
        if (Constants.UseNHWC)
        {
            color.b = Tensor[index * 3];
            color.g = Tensor[index * 3 + 1];
            color.r = Tensor[index * 3 + 2];
            color.a = 1.0f;
        }
        else
        {
            uint blockSize = Constants.InputResolution.x * Constants.InputResolution.y;

            color.r = Tensor[index];
            color.g = Tensor[index + blockSize];
            color.b = Tensor[index + 2 * blockSize];
            color.a = 1.0f;
        }
    }
    else
    {
        color = float4(0, 0, 0, 0);
    }

   
    OutputTexture[DTid.xy] = color;
}