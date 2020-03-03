#pragma once

class UIController
{
public:
	UIController(HWND hwnd, ID3D12Device &device, ComPtr<IDXGISwapChain3> pSwapchain);
	void Render(ID3D12GraphicsCommandList& commandList);

private:
	ComPtr<IDXGISwapChain3> m_pSwapchain;
	ComPtr<ID3D12DescriptorHeap> m_pImguiSRVDescriptorHeap;
	ComPtr<ID3D12DescriptorHeap> m_pImguiRTVDescriptorHeap;

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_RTVs;
};