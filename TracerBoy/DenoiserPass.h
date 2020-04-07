#pragma once

struct PassResource;

struct DenoiserSettings
{
	float m_fireflyClampValue;
	bool m_bEnabled;
	float m_normalWeightingExponential;
	float m_intersectPositionWeightingMultiplier;
	float m_luminanceWeightingMultiplier;
	int m_waveletIterations;
};

class DenoiserPass
{
public:
	DenoiserPass(ID3D12Device& device);
	D3D12_GPU_DESCRIPTOR_HANDLE Run(ID3D12GraphicsCommandList& commandList,
		PassResource DenoiserBuffers[2],
		const DenoiserSettings& denoiserSettings,
		D3D12_GPU_DESCRIPTOR_HANDLE inputSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE normalsSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE intersectPositionSRV,
		D3D12_GPU_DESCRIPTOR_HANDLE sdrHistogramSRV,
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
		AOVSDRHistogram,
		UndenoisedInputSRV,
		OutputUAV,
		NumRootSignatureParameters
	};
};