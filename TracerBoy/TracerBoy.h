#pragma once

class TracerBoy
{
public:
	TracerBoy(ID3D12CommandQueue *pQueue, const std::string &sceneFileName);
	~TracerBoy() { WaitForGPUIdle(); }
	void Render(ID3D12Resource *pBackBuffer);

	void UpdateMouseInput(int x, int y)
	{
		m_mouseX = x;
		m_mouseY = y;
	}

private:
	UINT64 SignalFence();
	void ResizeBuffersIfNeeded(ID3D12Resource *pBackBuffer);
	void WaitForGPUIdle();
	void UpdateConfigConstants(UINT backBufferWidth, UINT backBufferHeight);

	typedef std::pair<CComPtr<ID3D12GraphicsCommandList>, CComPtr<ID3D12CommandAllocator>> CommandListAllocatorPair;
	void AcquireCommandListAllocatorPair(CommandListAllocatorPair &pair);
	void ExecuteAndFreeCommandListAllocatorPair(CommandListAllocatorPair &pair);

	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap *pDescriptorHeap, UINT slot);

	void AllocateUploadBuffer(UINT bufferSize, CComPtr<ID3D12Resource> &pBuffer);

	CComPtr<ID3D12Device5> m_pDevice;
	CComPtr<ID3D12CommandQueue> m_pCommandQueue;
	CComPtr<ID3D12DescriptorHeap> m_pViewDescriptorHeap;
	CComPtr<ID3D12DescriptorHeap> m_pRTVDescriptorHeap;

	CComPtr<ID3D12Resource> m_pBottomLevelAS;
	CComPtr<ID3D12Resource> m_pTopLevelAS;
	CComPtr<ID3D12Resource> m_pConfigConstants;

	const DXGI_FORMAT RayTracingOutputFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	enum RenderTargets
	{
		RTV0 = 0,
		RTV1,
		NumRTVs
	};
	CComPtr<ID3D12Resource> m_pAccumulatedPathTracerOutput[RenderTargets::NumRTVs];
	UINT8 m_ActivePathTraceOutputIndex;

	enum RayTracingRootSignatureParameters
	{
		PerFrameConstants = 0,
		ConfigConstants,
		LastFrameSRV,
		AccelerationStructureRootSRV,
		NumRayTracingParameters
	};
	CComPtr<ID3D12RootSignature> m_pRayTracingRootSignature;
	CComPtr<ID3D12PipelineState> m_pRayTracingPSO;

	enum PostProcessRootSignatureParameters
	{
		InputTexture = 0,
		OutputTexture,
		Constants,
		NumParameters
	};
	CComPtr<ID3D12RootSignature> m_pPostProcessRootSignature;
	CComPtr<ID3D12PipelineState> m_pPostProcessPSO;

	std::deque<std::pair<CommandListAllocatorPair, UINT64>> FreedCommandListAllocatorPairs;

	UINT32 m_mouseX, m_mouseY;
	UINT m_FramesRendered;

	CComPtr<ID3D12Fence> m_pFence;
	UINT64 m_SignalValue;

	CComPtr<ID3D12Resource> m_pPostProcessOutput;

	enum ViewDescriptorHeapSlots
	{
		PostProcessOutputUAV = 0,
		PathTracerOutputSRVBaseSlot,
		PathTracerOutputSRVLastSlot = PathTracerOutputSRVBaseSlot + RenderTargets::NumRTVs - 1,
		NumReservedViewSlots,
		NumTotalViews = 1024
	};
};