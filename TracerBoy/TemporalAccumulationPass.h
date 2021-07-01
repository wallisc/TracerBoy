#pragma once

struct PassResource;
struct Camera;

class TemporalAccumulationPass
{
public:
	TemporalAccumulationPass(ID3D12Device& device);
	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource OutputBuffer,
		D3D12_GPU_DESCRIPTOR_HANDLE TemporalHistory,
		D3D12_GPU_DESCRIPTOR_HANDLE CurrentFrame,
		D3D12_GPU_DESCRIPTOR_HANDLE AOVWorldPosition,
		Camera& CurrentFrameCamera,
		Camera& PreviousFrameCamera,
		bool bIgnoreHistory,
		UINT width,
		UINT height);

private:
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pPSO;

	enum TemporalAccumulationRootSignatureParameters
	{
		ConstantsParam = 0,
		TemporalHistory,
		CurrentFrameTexture,
		WorldPositionTexture,
		OutputUAV,
		NumRootSignatureParameters
	};
};