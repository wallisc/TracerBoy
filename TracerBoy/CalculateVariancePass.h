#pragma once

class CalculateVariancePass
{
public:
	CalculateVariancePass(ID3D12Device& device);

	void Run(ID3D12GraphicsCommandList& commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE cachedLuminanceSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE pathTracedOutputSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE summedVarianceUAV,
		UINT width,
		UINT height);

private:
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pPSO;

	enum CalculateVarianceRootSignatureParameters
	{
		RootConstants,
		CachedLuminanceSRV,
		PathTracedOutputSRV,
		SummedVarianceUAV,
		NumRootSignatureParameters
	};
};