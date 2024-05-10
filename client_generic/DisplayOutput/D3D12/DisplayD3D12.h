#pragma once
#include "DisplayOutput.h"
#include <d3d12.h>
#include <windows.h>

namespace DisplayOutput
{

class CDisplayD3D12 : public CDisplayOutput
{
    HWND m_WindowHandle;

	// Creates a window, used mostly for testing. We are provided a HWND when screensavering
	HWND CreateDisplayWindow(uint32_t _w, uint32_t _h, const bool _bFullscreen);

	// Window event handler
	static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam);

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
