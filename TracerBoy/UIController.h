#pragma once

class UIController
{
public:
	UIController(HWND hwnd, ID3D12Device &device, ComPtr<IDXGISwapChain3> pSwapchain);
	void Render(ID3D12GraphicsCommandList& commandList);

	const TracerBoy::OutputSettings& GetOutputSettings() { return m_outputSettings;  }
	float GetCameraSpeed() { return m_cameraSpeed; }
private:
	void SetDefaultSettings();
	TracerBoy::OutputSettings m_outputSettings;

	float m_cameraSpeed;

	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12DescriptorHeap> m_pImguiSRVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> m_pImguiRTVDescriptorHeap;

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_RTVs;
};