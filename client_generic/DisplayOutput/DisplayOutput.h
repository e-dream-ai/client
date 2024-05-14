#ifndef _DISPLAY_OUTPUT_H
#define _DISPLAY_OUTPUT_H

#include "SmartPtr.h"
#include "base.h"
#include "linkpool.h"
#include <queue>

#ifdef WIN32

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
#endif

#ifdef __OBJC__
typedef void* CGraphicsContext;
#else
class MTKView;
typedef MTKView* CGraphicsContext;
#endif

namespace DisplayOutput
{

/*
        CEvent.
        Event interface.
*/
class CEvent
{
  public:
    enum eEventType
    {
        Event_KEY,
        Event_Mouse,
        Event_Power,
        Event_NONE
    };

    virtual ~CEvent() {}

    virtual eEventType Type() { return (Event_NONE); };

    POOLED(CEvent, Memory::CLinkPool);
};

MakeSmartPointers(CEvent);

/*
        CKeyEvent.
        Keyboard event.
*/
class CKeyEvent : public CEvent
{
  public:
    enum eKeyCode
    {
        KEY_TAB = 0x09,
        KEY_LALT = 0xA4, // Left Alt
        KEY_MENU = 0x5D, // Alt Gr / Right Alt
        KEY_CTRL = 0x11, // Ctrl
        KEY_SPACE = 0x20,
        KEY_LEFT = 0x25,
        KEY_RIGHT = 0x27,
        KEY_UP = 0x26,
        KEY_DOWN = 0x28,
        KEY_F1 = 0x70,
        KEY_F2 = 0x71,
        KEY_F3 = 0x72,
        KEY_F4 = 0x73,
        KEY_F5 = 0x74,
        KEY_F6 = 0x75,
        KEY_F7 = 0x76,
        KEY_F8 = 0x77,
        KEY_F9 = 0x78,
        KEY_F10 = 0x79,
        KEY_F11 = 0x7A,
        KEY_F12 = 0x7B,
        KEY_Esc = 0x1B,
        KEY_NONE = 0x00, // No key pressed
        KEY_Comma = 0xBC,
        KEY_Period = 0xBE,
        KEY_A = 0x41,
        KEY_B = 0x42,
        KEY_C = 0x43,
        KEY_D = 0x44,
        KEY_E = 0x45,
        KEY_F = 0x46,
        KEY_G = 0x47,
        KEY_H = 0x48,
        KEY_I = 0x49,
        KEY_J = 0x4A,
        KEY_K = 0x4B,
        KEY_L = 0x4C,
        KEY_M = 0x4D,
        KEY_N = 0x4E,
        KEY_O = 0x4F,
        KEY_P = 0x50,
        KEY_Q = 0x51,
        KEY_R = 0x52,
        KEY_S = 0x53,
        KEY_T = 0x54,
        KEY_U = 0x55,
        KEY_V = 0x56,
        KEY_W = 0x57,
        KEY_X = 0x58,
        KEY_Y = 0x59,
        KEY_Z = 0x5A,
        KEY_0 = 0x30,
        KEY_1 = 0x31,
        KEY_2 = 0x32,
        KEY_3 = 0x33,
        KEY_4 = 0x34,
        KEY_5 = 0x35,
        KEY_6 = 0x36,
        KEY_7 = 0x37,
        KEY_8 = 0x38,
        KEY_9 = 0x39,
        KEY_BACKSPACE = 0x08,
        KEY_ENTER = 0x0D,
        KEY_SHIFT = 0x10, // Both left and right shift keys
        KEY_CAPSLOCK = 0x14,
        KEY_DELETE = 0x2E,
        KEY_END = 0x23,
        KEY_HOME = 0x24,
        KEY_INSERT = 0x2D,
        KEY_PAGEUP = 0x21,
        KEY_PAGEDOWN = 0x22
    };

    CKeyEvent() : m_bPressed(true), m_Code(KEY_NONE) {}

    virtual ~CKeyEvent() {}

    virtual eEventType Type() { return (CEvent::Event_KEY); };
    bool m_bPressed;
    eKeyCode m_Code;

    POOLED(CKeyEvent, Memory::CLinkPool);
};

MakeSmartPointers(CKeyEvent);

/*
        CMouseEvent.
        Mouse event.
*/
class CMouseEvent : public CEvent
{
  public:
    enum eMouseCode
    {
        Mouse_LEFT,
        Mouse_RIGHT,
        Mouse_MOVE,
        Mouse_NONE
    };

    CMouseEvent() : m_Code(Mouse_NONE) {}

    virtual ~CMouseEvent() {}

    virtual eEventType Type() { return (CEvent::Event_Mouse); };
    eMouseCode m_Code;

    int32_t m_X;
    int32_t m_Y;

    POOLED(CMouseEvent, Memory::CLinkPool);
};

MakeSmartPointers(CMouseEvent);

/*
        CPowerEvent.
        Power broadcast event.
*/
class CPowerEvent : public CEvent
{
  public:
    CPowerEvent() {}

    virtual ~CPowerEvent() {}

    virtual eEventType Type() { return (CEvent::Event_Power); };
};

MakeSmartPointers(CPowerEvent);

/*
        CDisplayOutput.
        Base class.

        Note, it's up to constructor(or Initialize()), to set width/height.
*/
class CDisplayOutput
{
  protected:
    int32_t m_XPosition;
    int32_t m_YPosition;
    uint32_t m_Width;
    uint32_t m_Height;
    bool m_bFullScreen;
    bool m_bVSync;
    bool m_bClosed;

    static std::queue<spCEvent> m_EventQueue;

  public:
    CDisplayOutput();
    virtual ~CDisplayOutput();

#ifdef WIN32
    //virtual bool Initialize(HWND _hWnd, bool _bPreview) = PureVirtual;
    virtual HWND Initialize(const uint32_t _width, const uint32_t _height,
                            const bool _bFullscreen) = PureVirtual;

    virtual ComPtr<ID3D12Device> GetDevice() = PureVirtual;

    //virtual HWND WindowHandle(void) = PureVirtual;
    //virtual D3DPRESENT_PARAMETERS PresentParameters() = PureVirtual;
    //virtual void SetScreen(const uint32_t _screen) = PureVirtual;
    //virtual DWORD GetNumMonitors() { return 1; };

#else
#ifdef MAC
    virtual bool Initialize(CGraphicsContext _graphicsContext,
                            bool _bPreview) = PureVirtual;
    virtual void SetContext(CGraphicsContext _graphicsContext) = PureVirtual;
    virtual CGraphicsContext GetContext(void) = PureVirtual;
    virtual void ForceWidthAndHeight(uint32_t _width,
                                     uint32_t _height) = PureVirtual;
#else
    virtual bool Initialize(const uint32_t _width, const uint32_t _height,
                            const bool _bFullscreen) = PureVirtual;
#endif
#endif

    //
    virtual void Title(const std::string& _title) = PureVirtual;
    virtual void Update() = PureVirtual;
    virtual void SwapBuffers() = PureVirtual;

    bool GetEvent(spCEvent& _event);
    void AppendEvent(spCEvent _event);
    void ClearEvents();

    virtual bool HasShaders() { return false; };
    uint32_t Width() { return (m_Width); };
    uint32_t Height() { return (m_Height); };
    float Aspect() { return ((float)m_Height / (float)m_Width); };
    bool Closed() { return (m_bClosed); };
    void Close() { m_bClosed = true; };
};

MakeSmartPointers(CDisplayOutput);

}; // namespace DisplayOutput

#endif
