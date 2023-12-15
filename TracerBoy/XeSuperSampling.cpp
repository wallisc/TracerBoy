#include "pch.h"

#if USE_XESS
#include "XeSuperSampling.h"

void xessCallback(const char* message, xess_logging_level_t loggingLevel)
{
	OutputDebugString(message);
}

XeSuperSamplingPass::XeSuperSamplingPass(ID3D12Device& device)
{
	auto status = xessD3D12CreateContext(&device, &m_xessContext);
	if (status != XESS_RESULT_SUCCESS)
	{
		HANDLE_FAILURE();
	}
}

void XeSuperSamplingPass::OnResize(
	ID3D12GraphicsCommandList& commandList,
	UINT Width,
	UINT Height,
	float &OutDownscaleFactor)
{
	xess_d3d12_init_params_t initParams = {};
	initParams.outputResolution.x = Width;
	initParams.outputResolution.y = Height;
	initParams.qualitySetting = XESS_QUALITY_SETTING_ULTRA_QUALITY;
	initParams.initFlags = XESS_INIT_FLAG_NONE;
	xessD3D12Init(m_xessContext, &initParams);

	_xess_2d_t outputResolution = { Width, Height };
	_xess_2d_t renderResolution;
	xessGetInputResolution(m_xessContext, &outputResolution, initParams.qualitySetting, &renderResolution);

	OutDownscaleFactor = (float)renderResolution.x / (float)outputResolution.x;
}

void XeSuperSamplingPass::Run(
	ID3D12GraphicsCommandList& commandList,
	PassResource OutputBuffer,
	ID3D12Resource* InputBuffer,
	ID3D12Resource* MotionVectors,
	ID3D12Resource* DepthBuffer,
	float PixelOffsetX,
	float PixelOffsetY,
	UINT inputWidth,
	UINT inputHeight)
{
	PIXScopedEvent(&commandList, PIX_COLOR_DEFAULT, L"XeSS");

	ScopedResourceBarrier colorTextureBarrier(commandList, InputBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	ScopedResourceBarrier outputBarrier(commandList, OutputBuffer.m_pResource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	auto inputDesc = InputBuffer->GetDesc();

	xess_d3d12_execute_params_t exec_params{};
	exec_params.inputWidth = inputDesc.Width;
	exec_params.inputHeight = inputDesc.Height;
	exec_params.jitterOffsetX = 0.0f;// constants.FixedPixelOffset.x - 0.5f;
	exec_params.jitterOffsetY = 0.0f;// constants.FixedPixelOffset.y - 0.5f;
	exec_params.exposureScale = 1.0f;
	exec_params.pColorTexture = InputBuffer;
	exec_params.pVelocityTexture = MotionVectors;
	exec_params.pOutputTexture = OutputBuffer.m_pResource.Get();
	exec_params.pDepthTexture = DepthBuffer;
	exec_params.pExposureScaleTexture = 0;
	auto status = xessD3D12Execute(m_xessContext, &commandList, &exec_params);
	if (status != XESS_RESULT_SUCCESS)
	{
		HANDLE_FAILURE();
	}
}

#endif