#pragma once

#if ENABLE_UI

class UIController
{
public:
	UIController(HWND hwnd, ID3D12Device &device, IDXGISwapChain3 &swapchain);

	struct PixelSelection
	{
		float DistanceFromCamera;
		Material Material;
		const std::string* MaterialName;
	};

	struct PerFrameStats
	{
		float ElapsedTimeSinceLastInvalidate;
		UINT32 NumberOfWavesExecuted;
		UINT32 NumberOfPixelsActive;
		UINT32 NumberOfTotalPixels;
	};

	void RenderLoadingScreen(ID3D12GraphicsCommandList& commandList, const SceneLoadStatus &loadSceneStatus);
	void Render(ID3D12GraphicsCommandList& commandList, const PerFrameStats &stats, const PixelSelection *pPixelSelection = nullptr);

	const TracerBoy::OutputSettings& GetOutputSettings() { return m_outputSettings;  }
	float GetCameraSpeed() { return m_cameraSpeed; }
	float GetCaptureLengthInSeconds() { return m_captureLengthInSeconds; }
	float GetCaptureFramesPerSecond() { return (float)m_captureFramesPerSecond; }
	float GetCaptureSamplesPerFrame() { return (float)m_captureSamplesPerFrame; }

	bool HasSceneChangeRequest() { return m_bHasSceneChangeRequest; }
	
	// Only call if HasSceneChangeRequest() == true
	// After this has been called, HasSceneChangeRequest() will return back to false
	const std::string& GetRequestedSceneName();

	bool HasRecompileRequest() const { return m_bHasRecompileRequest; }
	void NotifyRecompileComplete() { m_bHasRecompileRequest = false; }

private:
	void SubmitDrawData(ID3D12GraphicsCommandList& commandList, bool bClearRenderTarget);
	void SetDefaultSettings();
	TracerBoy::OutputSettings m_outputSettings;

	float m_cameraSpeed;
	float m_captureLengthInSeconds;
	int m_captureFramesPerSecond;
	int m_captureSamplesPerFrame;

	std::mutex m_sceneNameMutex;
	bool m_bHasSceneChangeRequest = false;
	bool m_bHasRecompileRequest = false;
	std::string m_sceneName;

	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12DescriptorHeap> m_pImguiSRVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> m_pImguiRTVDescriptorHeap;

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_RTVs;
};

#endif