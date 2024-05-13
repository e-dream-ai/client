#pragma once

#include <windows.h>
#include <wrl.h>
#include <shellapi.h>


#include "DisplayOutput.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <d3dx12.h>


using Microsoft::WRL::ComPtr;


// Misc helpers

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        //throw HrException(hr);
    }
}


namespace DisplayOutput
{

class CDisplayD3D12 : public CDisplayOutput
{
    HWND m_WindowHandle;

    // DirectX 12 objects.
    bool m_useWarpDevice;	// This is used to determine if we are using the WARP device (software rasterizer)

    // From DXSample. Is that double buffering ?
    static const UINT FrameCount = 2;

	// Pipeline objects.
    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;
    ComPtr<IDXGISwapChain3> m_swapChain;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
    ComPtr<ID3D12CommandAllocator> m_commandAllocator;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    ComPtr<ID3D12PipelineState> m_pipelineState;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    UINT m_rtvDescriptorSize;

    // App resources.
    ComPtr<ID3D12Resource> m_vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    // Synchronization objects.
    UINT m_frameIndex;
    HANDLE m_fenceEvent;
    ComPtr<ID3D12Fence> m_fence;
    UINT64 m_fenceValue;


	// Creates a window, used mostly for testing. We are provided a HWND when screensavering
	HWND CreateDisplayWindow(uint32_t _w, uint32_t _h, const bool _bFullscreen);

	// Window event handler
	static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam);
	bool InitD3D12();

    void GetHardwareAdapter(_In_ IDXGIFactory1* pFactory,
                           _Outptr_result_maybenull_ IDXGIAdapter1** ppAdapter,
                           bool requestHighPerformanceAdapter = false);


	public:
	CDisplayD3D12();
	virtual ~CDisplayD3D12();

	static const char* Description()
	{
		return "Direct3D 12 display";
	}

	virtual bool Initialize();

    virtual HWND Initialize(const uint32_t _width, const uint32_t _height,
                                const bool _bFullscreen);


    virtual void Title(const std::string& _title);
    virtual void Update();

    virtual bool HasShaders() { return true; };
    void SwapBuffers();
};




}
