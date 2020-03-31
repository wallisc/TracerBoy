#pragma once

class DenoiserPass
{
public:
	DenoiserPass(ID3D12Device& device);
	void Run(ID3D12GraphicsCommandList& commandList,
		D3D12_GPU_DESCRIPTOR_HANDLE outputUAV, 
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