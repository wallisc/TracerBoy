
bool ShouldSkipRay(
	RWTexture2D<float4> InputColor, 
	RWTexture2D<float4> InputJitteredColor, 
	uint2 Index, 
	float MinConvergence,
	uint FrameCount)
{
	bool bSkipRay = false;
	if(FrameCount > 16)
	{
		float4 jitteredOutput = InputJitteredColor[Index];
		float4 output = InputColor[Index];
		float3 jitteredColor = jitteredOutput.rgb / jitteredOutput.a;
		float3 color = output.rgb / output.a;
		
		float error = (abs(jitteredColor.r - color.r) + abs(jitteredColor.g - color.g) + abs(jitteredColor.b - color.b)) / sqrt(color.r + color.g + color.b);
		
		bSkipRay = error < MinConvergence;
	}
	return bSkipRay;
}