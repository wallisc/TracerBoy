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
    
    float InputToOutputScaler = Constants.OutputResolution.x / Constants.InputResolution.x;
    uint2 InputIndex = uint2(DTid.xy / InputToOutputScaler);
    
    float4 color;
    if (InputIndex.x < Constants.InputResolution.x && InputIndex.y < Constants.InputResolution.y)
    {
        uint index = InputIndex.y * Constants.InputResolution.x + InputIndex.x;
        uint channelOffset = Constants.SliceToDebug * 3;
        if (Constants.UseNHWC)
        {
            color.r = Tensor[index * Constants.InputChannelDepth + channelOffset + 0];
            color.g = Tensor[index * Constants.InputChannelDepth + channelOffset + 1];
            color.b = Tensor[index * Constants.InputChannelDepth + channelOffset + 2];
            color.a = 1.0f;
        }
        else
        {
            uint blockSize = Constants.InputResolution.x * Constants.InputResolution.y;
            uint sliceOffset = Constants.SliceToDebug * 3 * blockSize;

            color.r = Tensor[sliceOffset + index + 0 * blockSize];
            color.g = Tensor[sliceOffset + index + 1 * blockSize];
            color.b = Tensor[sliceOffset + index + 2 * blockSize];
            color.a = 1.0f;
        }
    }
    else
    {
        color = float4(0, 0, 0, 0);
    }

   
    OutputTexture[DTid.xy] = abs(color);
}