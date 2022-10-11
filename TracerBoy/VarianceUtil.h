
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
		
		bool isBlack = all(color <= 0.0);
	
		// Pure black pixel will cause divide by 0. If the pixel is still pure black after 16 samples
		// just consider it converged to black. This happens when a primary ray misses and there's 
		// no environment map
		bSkipRay = isBlack;
		if (!bSkipRay)
		{
			float error = (abs(jitteredColor.r - color.r) + abs(jitteredColor.g - color.g) + abs(jitteredColor.b - color.b)) / sqrt(color.r + color.g + color.b);

			bSkipRay = error < MinConvergence;
		}
	}
	return bSkipRay;
}