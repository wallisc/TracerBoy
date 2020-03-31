#pragma once

struct PassResource;

class DenoiserPass
{
public:
	DenoiserPass(ID3D12Device& device);
	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource DenoiserBuffers[2],
		D3D12_GPU_DESCRIPTOR_HANDLE inputSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE normalsSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE intersectPositionSRV,
		UINT width,
		UINT height);

private:
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pPSO;

	enum DenoiserRootSignatureParameters
	{
		ConstantsParam = 0,
		InputSRV,
		AOVNormal,
		AOVIntersectPosition,
		OutputUAV,
		NumRootSignatureParameters
	};
};