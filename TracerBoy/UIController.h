#pragma once

#if ENABLE_UI

class UIController
{
public:
	UIController(HWND hwnd, ID3D12Device &device, IDXGISwapChain3 &swapchain);

	struct PerFrameStats
	{
		float ElapsedTimeSinceLastInvalidate;
		UINT32 NumberOfWavesExecuted;
		UINT32 NumberOfPixelsActive;
		UINT32 NumberOfTotalPixels;
	};

	void RenderLoadingScreen(ID3D12GraphicsCommandList& commandList, const SceneLoadStatus &loadSceneStatus);
	void Render(ID3D12GraphicsCommandList& commandList, const PerFrameStats &stats);

	const TracerBoy::OutputSettings& GetOutputSettings() { return m_outputSettings;  }
	float GetCameraSpeed() { return m_cameraSpeed; }
	float GetCaptureLengthInSeconds() { return m_captureLengthInSeconds; }
	float GetCaptureFramesPerSecond() { return m_captureFramesPerSecond; }
	float GetCaptureSamplesPerFrame() { return m_captureSamplesPerFrame; }
private:
	void SubmitDrawData(ID3D12GraphicsCommandList& commandList, bool bClearRenderTarget);
	void SetDefaultSettings();
	TracerBoy::OutputSettings m_outputSettings;

	float m_cameraSpeed;
	float m_captureLengthInSeconds;
	int m_captureFramesPerSecond;
	int m_captureSamplesPerFrame;

	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12DescriptorHeap> m_pImguiSRVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> m_pImguiRTVDescriptorHeap;

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_RTVs;
};

#endif