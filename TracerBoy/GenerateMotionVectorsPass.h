#pragma once
#pragma once

struct PassResource;
struct Camera;

class GenerateMotionVectorsPass
{
public:
	GenerateMotionVectorsPass(ID3D12Device& device);
	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource OutputBuffer,
		D3D12_GPU_DESCRIPTOR_HANDLE AOVWorldPosition,
		D3D12_GPU_DESCRIPTOR_HANDLE PreviousFrameWorldPosition,
		Camera& CurrentFrameCamera,
		Camera& PreviousFrameCamera,
		bool bIgnoreHistory,
		UINT width,
		UINT height);

private:
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pPSO;

	enum GenerateMotionVectorsRootSignatureParameters
	{
		ConstantsParam = 0,
		WorldPositionTexture,
		PreviousFrameWorldPositionTexture,
		OutputUAV,
		NumRootSignatureParameters
	};
};