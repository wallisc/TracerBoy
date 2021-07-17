#pragma once

struct PassResource;
struct Camera;

class FidelityFXSuperResolutionPass
{
public:

	FidelityFXSuperResolutionPass(ID3D12Device& device);
	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource OutputBuffer,
		PassResource IntermediateBuffer,
		D3D12_GPU_DESCRIPTOR_HANDLE InputTexture,
		UINT inputWidth,
		UINT inputHeight);

private:
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pUpscalePSO;
	ComPtr<ID3D12PipelineState> m_pSharpenPSO;

	enum FidelityFXSuperResolutionRootSignatureParameters
	{
		ConstantsParam = 0,
		InputTexture,
		OutputTexture,
		NumRootSignatureParameters
	};
};