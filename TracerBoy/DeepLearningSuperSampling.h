#pragma once

#if USE_DLSS
#include "dlss/nvsdk_ngx_helpers.h"

class DeepLearningSuperSamplingPass
{
public:

	DeepLearningSuperSamplingPass(ID3D12Device& device);

	void Enable(ID3D12Device& device);
	void Disable(ID3D12Device& device);

	bool IsSupported() { return m_bSupportsDLSS != 0; }
	bool IsEnabled() { return m_bEnabled; }

	D3D12_GPU_DESCRIPTOR_HANDLE Run(
		ID3D12GraphicsCommandList& commandList,
		PassResource OutputBuffer,
		ID3D12Resource* InputBuffer,
		ID3D12Resource* MotionVectors,
		ID3D12Resource* DepthBuffer,
		float PixelOffsetX,
		float PixelOffsetY,
		UINT inputWidth,
		UINT inputHeight);

	void OnResize(
		ID3D12GraphicsCommandList& commandList,
		UINT Width, 
		UINT Height, 
		UINT UpscaledWidth, 
		UINT UpscaledHeight);

private:
	bool m_bEnabled = false;
	int m_bSupportsDLSS = false;
	NVSDK_NGX_Parameter* m_pNGXParameters = nullptr;
	NVSDK_NGX_Handle* m_pDLSSFeature = nullptr;
};
#endif