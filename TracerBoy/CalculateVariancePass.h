#pragma once

class CalculateVariancePass
{
public:
	CalculateVariancePass(ID3D12Device& device);

	void Run(ID3D12GraphicsCommandList& commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE summedLumaSquaredSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE pathTracedOutputSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE lumianceVarianceUAV,
		UINT width,
		UINT height,
		UINT globalFrameCount);

private:
	ComPtr<ID3D12RootSignature> m_pRootSignature;
	ComPtr<ID3D12PipelineState> m_pPSO;

	enum CalculateVarianceRootSignatureParameters
	{
		RootConstants,
		SummedLumaSquaredSRV,
		PathTracedOutputSRV,
		LuminanceVarianceUAV,
		NumRootSignatureParameters
	};
};