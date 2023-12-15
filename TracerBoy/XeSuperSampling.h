#pragma once

#if USE_XESS

#include "xess/xess_d3d12.h"
#include "xess/xess_debug.h"

class XeSuperSamplingPass
{
public:
	XeSuperSamplingPass(ID3D12Device& device);

	void Run(
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
		float& OutDownscaleFactor);

private:
	xess_context_handle_t m_xessContext = nullptr;
};
#endif