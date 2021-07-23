#pragma once

struct PassResource;
struct Camera;

class TemporalAccumulationPass
{
public:
	struct MomentResources
	{
		ID3D12Resource &MomentBuffer;
		D3D12_GPU_DESCRIPTOR_HANDLE MomentBufferUAV;
		D3D12_GPU_DESCRIPTOR_HANDLE MomentHistory;
	};

	TemporalAccumulationPass(ID3D12Device& device);
	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource OutputBuffer,
		D3D12_GPU_DESCRIPTOR_HANDLE TemporalHistory,
		D3D12_GPU_DESCRIPTOR_HANDLE CurrentFrame,
		D3D12_GPU_DESCRIPTOR_HANDLE AOVWorldPosition,
		D3D12_GPU_DESCRIPTOR_HANDLE PreviousFrameWorldPosition,
		D3D12_GPU_DESCRIPTOR_HANDLE AOVNormal,
		Camera& CurrentFrameCamera,
		Camera& PreviousFrameCamera,
		float HistoryWeight,
		bool bIgnoreHistory,
		UINT width,
		UINT height,
		MomentResources *pMomentResources = nullptr);

private:
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pPSO;

	enum TemporalAccumulationRootSignatureParameters
	{
		ConstantsParam = 0,
		TemporalHistory,
		CurrentFrameTexture,
		WorldPositionTexture,
		PreviousFrameWorldPositionTexture,
		WorldNormalTexture,
		MomentHistory,
		OutputUAV,
		OutputMomentUAV,
		NumRootSignatureParameters
	};
};