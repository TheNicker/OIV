#include "Win32Window.h"
#include "MonitorInfo.h"
#include <tchar.h>

namespace OIV
{
    namespace Win32
    {
        Win32WIndow::Win32WIndow() :
              fHandleWindow(nullptr)
            , fHandleClient(nullptr)
            , fHandleStatusBar(nullptr)
            , fStatusWindowParts(4)
            , fFullSceenState(FSS_Windowed)
            , fLastWindowPlacement({ 0 })
            , fDragAndDrop(nullptr)
            , fWindowStyles(0)
            , fWindowStylesClient(0)
        {

        }


        void Win32WIndow::UpdateWindowStyles()
        {
            HWND hwnd = fHandleWindow;
            SetWindowLong(hwnd, GWL_STYLE,fWindowStyles);
            SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }

        void Win32WIndow::SetWindowed()
        {
            fWindowStyles |= WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
            UpdateWindowStyles();
            RestorePlacement();
            fFullSceenState = FSS_Windowed;
        }



        void Win32WIndow::SavePlacement()
        {
            GetWindowPlacement(fHandleWindow, &fLastWindowPlacement);
        }


        void Win32WIndow::RestorePlacement()
        {
            SetWindowPlacement(fHandleWindow, &fLastWindowPlacement);
        }

        void Win32WIndow::RefreshWindow()
        {
            HandleResize();
        }

        void Win32WIndow::SetFullScreen(bool multiMonitor)
        {
            HWND hwnd = fHandleWindow;
            fWindowStyles = fWindowStyles & ~WS_OVERLAPPEDWINDOW & ~WS_CLIPCHILDREN;
            UpdateWindowStyles();
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(MonitorFromWindow(fHandleWindow, MONITOR_DEFAULTTOPRIMARY), &mi);
            RECT rect = mi.rcMonitor;

            if (multiMonitor)
            {
                rect = OIV::MonitorInfo::GetSingleton().getBoundingMonitorArea();
                fFullSceenState = FSS_MultiScreen;
            }
            else
                fFullSceenState = FSS_SingleScreen;

            SavePlacement();
            
            SetWindowPos(hwnd, HWND_TOP,
                rect.left, rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED);

            fShowStatusBar = fFullSceenState == FSS_Windowed;
            RefreshWindow();
        }

        void Win32WIndow::ToggleFullScreen(bool multiMonitor)
        {
            HWND hwnd = fHandleWindow;
            DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);


            switch (fFullSceenState)
            {
            case FSS_Windowed:
                SetFullScreen(multiMonitor);
                break;
            case FSS_SingleScreen:
                if (multiMonitor == true)
                    SetFullScreen(multiMonitor);
                else
                    SetWindowed();
                break;
            case FSS_MultiScreen:
                SetWindowed();
                break;
            }
            
            ShowWindow(fHandleStatusBar, fFullSceenState == FSS_Windowed ? SW_SHOW : SW_HIDE);
        }

        HWND Win32WIndow::GetHandle() const
        {
            return fHandleWindow;
        }

        HWND Win32WIndow::GetHandleClient() const
        {   
            return fHandleClient;
        }

        void Win32WIndow::AddEventListener(EventCallback callback)
        {
            fListeners.push_back(callback);
        }

        void Win32WIndow::DestroyResources()
        {
            fDragAndDrop->Detach();
            SAFE_RELEASE(fDragAndDrop);
        }

        LRESULT Win32WIndow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            Win32WIndow* window = reinterpret_cast<Win32WIndow*>(GetProp(hWnd, _T("windowClass")));

            bool defaultProc = false;
            switch (message)
            {
            case WM_CREATE:
            {
                CREATESTRUCT* s = (CREATESTRUCT*)lParam;
                if (SetProp(hWnd, _T("windowClass"), s->lpCreateParams)  == 0)
                    std::exception("Unable to set window property");
            }
            case WM_ERASEBKGND:
                break;
            case WM_SIZE:
                if (window->GetHandle() == hWnd)
                    window->HandleResize();
                else
                    defaultProc = true;
                break;
            case WM_DESTROY:
                if (window->GetHandle() == hWnd)
                {
                    window->DestroyResources();
                    PostQuitMessage(0);
                }
                else
                    defaultProc = true;
                break;
            default:
                defaultProc = true;
                break;
            }

            if (window != nullptr)
            {
                EventWinMessage winEvent;
                winEvent.window = window;
                winEvent.message.hwnd = hWnd;
                winEvent.message.lParam = lParam;
                winEvent.message.wParam = wParam;
                winEvent.message.message = message;
                window->RaiseEvent(winEvent);
            
            }

            if (defaultProc)
                return DefWindowProc(hWnd, message, wParam, lParam);
            else
                return 0;
        }

        HWND Win32WIndow::DoCreateStatusBar(HWND hwndParent, int idStatus, HINSTANCE hinst, int cParts)
        {
            HWND hwndStatus;
            // Ensure that the common control DLL is loaded.
            InitCommonControls();

            // Create the status bar.
            hwndStatus = CreateWindowEx(
                0, // no extended styles
                STATUSCLASSNAME, // name of status bar class
                (PCTSTR)NULL, // no text when first created
                SBARS_SIZEGRIP | // includes a sizing grip
                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, // creates a visible child window
                0, 0, 0, 0, // ignores size and position
                hwndParent, // handle to parent window
                (HMENU)idStatus, // child window identifier
                hinst, // handle to application instance
                NULL); // no window creation data

                       // Get the coordinates of the parent window's client area.


            return hwndStatus;
        }

        POINT Win32WIndow::GetMousePosition() const
        {
            POINT clientMousePos;
            GetCursorPos(&clientMousePos);
            ScreenToClient(fHandleClient, &clientMousePos);
            return clientMousePos;
        }

        void Win32WIndow::SetStatusBarText(std::wstring message, int part, int type)
        {
            if (fHandleStatusBar != NULL)
                SendMessage(fHandleStatusBar, SB_SETTEXT, MAKEWORD(part, type), reinterpret_cast<LPARAM>(message.c_str()));
        }

        int Win32WIndow::Create(HINSTANCE hInstance, int nCmdShow)
        {
            // The main window class name.
            static TCHAR szWindowClass[] = _T("OIV_WINDOW_CLASS");

            // The string that appears in the application's title bar.
            static TCHAR szTitle[] = _T("Open image viewer");
            WNDCLASSEX wcex;

            wcex.cbSize = sizeof(WNDCLASSEX);
            wcex.style = CS_HREDRAW | CS_VREDRAW;
            wcex.lpfnWndProc = WndProc;
            wcex.cbClsExtra = 0;
            wcex.cbWndExtra = 0;
            wcex.hInstance = hInstance;
            wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPLICATION));
            wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
            wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
            wcex.lpszMenuName = NULL;
            wcex.lpszClassName = szWindowClass;
            wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_APPLICATION));

            if (!RegisterClassEx(&wcex))
            {
                MessageBox(NULL,
                    _T("Call to RegisterClassEx failed!"),
                    _T("Win32 Guided Tour"),
                    NULL);

                return 1;
            }
            fWindowStyles = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;


            fHandleWindow = CreateWindow(
                szWindowClass,
                szTitle,
                fWindowStyles,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                1200,
                800,
                NULL,
                NULL,
                hInstance,
                this
            );


            fWindowStyles |= WS_VISIBLE;

            if (!fHandleWindow)
            {
                MessageBox(NULL,
                    _T("Call to CreateWindow failed!"),
                    _T("Win32 Guided Tour"),
                    NULL);

                return 1;
            }
         
            
            fHandleStatusBar = DoCreateStatusBar(fHandleWindow,  12, hInstance, 3);

            ResizeStatusBar();

            fWindowStylesClient = WS_CHILD;

            fHandleClient = CreateWindow(
                szWindowClass,
                szTitle,
                fWindowStylesClient,
                0,
                0,
                1200,
                800,
                fHandleWindow,
                NULL,
                hInstance,
                this
            );

       

            ShowWindow(fHandleClient, SW_SHOW);

            ShowWindow(fHandleWindow,
                nCmdShow);
            UpdateWindow(fHandleWindow);

            fDragAndDrop = new DragAndDropTarget(*this);
            
            SetStatusBarText(_T("pixel: "), 0, SBT_NOBORDERS);
            SetStatusBarText(_T("File: "), 1, 0);

            return 0;
        }

        void Win32WIndow::ResizeStatusBar()
        {
            if (fHandleStatusBar == NULL)
                return;
            RECT rcClient;
            HLOCAL hloc;
            PINT paParts;
            int i, nWidth;
            if (GetClientRect(fHandleWindow, &rcClient) == 0)
                return;


            SetWindowPos(fHandleStatusBar, NULL, 0, 0, -1, -1, 0);
            // Allocate an array for holding the right edge coordinates.
            hloc = LocalAlloc(LHND, sizeof(int) * fStatusWindowParts);
            paParts = (PINT)LocalLock(hloc);

            // Calculate the right edge coordinate for each part, and
            // copy the coordinates to the array.
            nWidth = rcClient.right / fStatusWindowParts;
            int rightEdge = nWidth;
            for (i = 0; i < fStatusWindowParts; i++)
            {
                paParts[i] = rightEdge;
                rightEdge += nWidth;
            }

            // Tell the status bar to create the window parts.
            SendMessage(fHandleStatusBar, SB_SETPARTS, (WPARAM)fStatusWindowParts, (LPARAM)
                paParts);

            // Free the array, and return.
            LocalUnlock(hloc);
            LocalFree(hloc);
        }

        void Win32WIndow::RaiseEvent(const Event& evnt)
        {
            for (auto callback : fListeners)
                bool result = callback(&evnt);
        }

        SIZE Win32WIndow::GetClientSize() const
        {
            RECT rect;
            GetClientRect(fHandleWindow, &rect);
            SIZE clientSize;
            clientSize.cx = rect.right - rect.left;
            clientSize.cy = rect.bottom - rect.top;
            if (IsWindowVisible(fHandleStatusBar))
            {
                GetWindowRect(fHandleStatusBar, &rect);
                clientSize.cy -= rect.bottom - rect.top;
            }
            return clientSize;
        }

        void Win32WIndow::HandleResize()
        {
            UpdateWindowStyles();

            RECT rect;
            GetClientRect(fHandleWindow, &rect);
            SIZE clientSize;
            clientSize.cx = rect.right - rect.left;
            clientSize.cy = rect.bottom - rect.top;


            if (fShowStatusBar)
            {
                RECT statusBarRect;
                ShowWindow(fHandleStatusBar,  SW_SHOW);
                GetWindowRect(fHandleStatusBar, &statusBarRect);
                clientSize.cy -= statusBarRect.bottom - statusBarRect.top;
                ResizeStatusBar();
            }
            else
            {
                ShowWindow(fHandleStatusBar, SW_HIDE);
            }

            SetWindowPos(fHandleClient, NULL, 0, 0, clientSize.cx, clientSize.cy,0);
        }

        void Win32WIndow::ShowStatusBar(bool show)
        {
            fShowStatusBar = show;
            HandleResize();
        }

        void Win32WIndow::ShowBorders(bool show_borders)
        {
            HWND hwnd = fHandleWindow;
            fShowBorders = show_borders;

            DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);
            //if (dwStyle & WS_OVERLAPPEDWINDOW)
            if (show_borders == false)
            {
                fWindowStyles = fWindowStyles & ~WS_OVERLAPPEDWINDOW;
            }
            else
            {
                fWindowStyles = fWindowStyles | WS_OVERLAPPEDWINDOW;
            }

            fShowStatusBar = show_borders;
            RefreshWindow();
        }
    }
}