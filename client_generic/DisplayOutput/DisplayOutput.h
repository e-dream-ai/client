#ifndef _DISPLAY_OUTPUT_H
#define _DISPLAY_OUTPUT_H

#include "SmartPtr.h"
#include "base.h"
#include "linkpool.h"
#include <queue>

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
        KEY_F = 0x46,
        KEY_s = 0x53,
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
        KEY_C = 0x5,
        KEY_A = 0x41,
        KEY_D = 0x44
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
    virtual bool Initialize(HWND _hWnd, bool _bPreview) = PureVirtual;
    virtual HWND Initialize(const uint32_t _width, const uint32_t _height,
                            const bool _bFullscreen) = PureVirtual;
    virtual HWND WindowHandle(void) = PureVirtual;
    virtual DWORD GetNumMonitors() { return 1; }
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
