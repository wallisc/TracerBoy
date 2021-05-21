#pragma once

class UIController
{
public:
	UIController(HWND hwnd, ID3D12Device &device, ComPtr<IDXGISwapChain3> pSwapchain);

	struct PerFrameStats
	{
		float ElapsedTimeSinceLastInvalidate;
		UINT32 WavesWithLivePixels;
		UINT32 NumberOfWavesExecuted;
	};

	void Render(ID3D12GraphicsCommandList& commandList, const PerFrameStats &stats);

	const TracerBoy::OutputSettings& GetOutputSettings() { return m_outputSettings;  }
	float GetCameraSpeed() { return m_cameraSpeed; }
	float GetCaptureLengthInSeconds() { return m_captureLengthInSeconds; }
	float GetCaptureFramesPerSecond() { return m_captureFramesPerSecond; }
	float GetCaptureSamplesPerFrame() { return m_captureSamplesPerFrame; }
private:
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