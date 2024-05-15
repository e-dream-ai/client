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
#include "D3D12Helpers.h"
#include "DeviceResources.h"

using Microsoft::WRL::ComPtr;


namespace DisplayOutput
{

class CDisplayD3D12 : public CDisplayOutput
{
    HWND m_WindowHandle;

	// Creates a window, used mostly for testing. We are provided a HWND when screensavering
	HWND CreateDisplayWindow(uint32_t _w, uint32_t _h, const bool _bFullscreen);

	// Window event handler
	static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

    virtual HWND GetWindowHandle(void) { return m_WindowHandle; };


    virtual void Title(const std::string& _title);
    virtual void Update();

    virtual bool HasShaders() { return true; };
    void SwapBuffers();
};




}
