#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <uxtheme.h>
#include <vssym32.h>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "uxtheme.lib")

#include <stdbool.h>

// These are defined in <windowsx.h> but we don't want to pull in whole header
// And we need them because coordinates are signed when you have multi-monitor setup.
#ifndef GET_X_PARAM
#define GET_X_PARAM(lp) ((int)(short)LOWORD(lp))
#endif

#ifndef GET_Y_PARAM
#define GET_Y_PARAM(lp) ((int)(short)HIWORD(lp))
#endif

static LRESULT CALLBACK win32_custom_title_bar_example_window_callback(HWND, UINT, WPARAM, LPARAM); // Implementation is at the end of the file

// Adding SAL annotations for WinMain
int APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR pCmdLine, _In_ int nCmdShow) {
    // Support high-dpi screens
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        OutputDebugStringA("WARNING: could not set DPI awareness");
    }

    // Register a "Window Class"
    static const wchar_t* window_class_name = L"FIRST_GUI";
    WNDCLASSEXW window_class = { 0 };
    window_class.cbSize = sizeof(window_class);
    window_class.lpszClassName = window_class_name;
    window_class.lpfnWndProc = win32_custom_title_bar_example_window_callback;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&window_class);

    int window_style = WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_VISIBLE;// | WS_POPUP; // Added minimize and maximize buttons
    CreateWindowExW(
        WS_EX_APPWINDOW,
        window_class_name,
        L"First GUI",
        window_style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        800,
        600,
        0,
        0,
        0,
        0
    );

    // Run message loop
    for (MSG message = { 0 };;) {
        BOOL result = GetMessageW(&message, 0, 0, 0);
        if (result > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        else {
            break;
        }
    }

    return 0;
}

static int win32_dpi_scale(int value, UINT dpi) {
    return (int)((float)value * dpi / 96);
}

static RECT win32_titlebar_rect(HWND handle) {
    SIZE title_bar_size = { 0 };
    const int top_and_bottom_borders = 2;
    HTHEME theme = OpenThemeData(handle, L"WINDOW");
    UINT dpi = GetDpiForWindow(handle);
    GetThemePartSize(theme, NULL, WP_CAPTION, CS_ACTIVE, NULL, TS_TRUE, &title_bar_size);
    CloseThemeData(theme);

    int height = win32_dpi_scale(title_bar_size.cy, dpi) + top_and_bottom_borders;

    RECT rect;
    GetClientRect(handle, &rect);
    rect.bottom = rect.top + height;
    return rect;
}

// Set this to 0 to remove the fake shadow painting
#define WIN32_FAKE_SHADOW_HEIGHT 1
// The offset of the 2 rectangles of the maximized window button
#define WIN32_MAXIMIZED_RECTANGLE_OFFSET 2

static RECT win32_fake_shadow_rect(HWND handle) {
    RECT rect;
    GetClientRect(handle, &rect);
    rect.bottom = rect.top;
    return rect;
}

typedef struct {
    RECT close;
    RECT minimize;
    RECT maximize;
    RECT help;
} CustomTitleBarButtonRects;

typedef enum {
    CustomTitleBarHoveredButton_None,
    CustomTitleBarHoveredButton_Help,
    CustomTitleBarHoveredButton_Close,
    CustomTitleBarHoveredButton_Minimize,
    CustomTitleBarHoveredButton_Maximize,
} CustomTitleBarHoveredButton;

static CustomTitleBarButtonRects win32_get_title_bar_button_rects(HWND handle, const RECT* title_bar_rect) {
    UINT dpi = GetDpiForWindow(handle);
    CustomTitleBarButtonRects button_rects;
    int button_width = win32_dpi_scale(47, dpi);

    button_rects.close = *title_bar_rect;
    button_rects.close.top += WIN32_FAKE_SHADOW_HEIGHT;
    button_rects.close.left = button_rects.close.right - button_width;

    button_rects.maximize = button_rects.close;
    button_rects.maximize.left -= button_width;
    button_rects.maximize.right -= button_width;

    button_rects.minimize = button_rects.maximize;
    button_rects.minimize.left -= button_width;
    button_rects.minimize.right -= button_width;

    button_rects.help = button_rects.minimize;
    button_rects.help.left -= button_width;
    button_rects.help.right -= button_width;

    return button_rects;
}

static bool win32_window_is_maximized(HWND handle) {
    WINDOWPLACEMENT placement = { 0 };
    placement.length = sizeof(WINDOWPLACEMENT);
    if (GetWindowPlacement(handle, &placement)) {
        return placement.showCmd == SW_SHOWMAXIMIZED;
    }
    return false;
}

static void win32_center_rect_in_rect(RECT* to_center, const RECT* outer_rect) {
    int to_width = to_center->right - to_center->left;
    int to_height = to_center->bottom - to_center->top;
    int outer_width = outer_rect->right - outer_rect->left;
    int outer_height = outer_rect->bottom - outer_rect->top;

    int padding_x = (outer_width - to_width) / 2;
    int padding_y = (outer_height - to_height) / 2;

    to_center->left = outer_rect->left + padding_x;
    to_center->top = outer_rect->top + padding_y;
    to_center->right = to_center->left + to_width;
    to_center->bottom = to_center->top + to_height;
}

static LRESULT CALLBACK win32_custom_title_bar_example_window_callback(HWND handle, UINT message, WPARAM w_param, LPARAM l_param) {
    CustomTitleBarHoveredButton title_bar_hovered_button = (CustomTitleBarHoveredButton)GetWindowLongPtrW(handle, GWLP_USERDATA);

    switch (message) {

        //   To remove the standard window frame, we must handle the WM_NCCALCSIZE message,
        //   specifically when its wParam value is TRUE and the return value is 0.
        //   By doing so, the application uses the entire window region as the client area,
        //   removing the standard frame.
    case WM_NCCALCSIZE: {
        if (!w_param) return DefWindowProc(handle, message, w_param, l_param);
        UINT dpi = GetDpiForWindow(handle);

        int frame_x = GetSystemMetricsForDpi(SM_CXFRAME, dpi);
        int frame_y = GetSystemMetricsForDpi(SM_CYFRAME, dpi);
        int padding = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);

        NCCALCSIZE_PARAMS* params = (NCCALCSIZE_PARAMS*)l_param;
        RECT* requested_client_rect = params->rgrc;

        requested_client_rect->right -= frame_x + padding;
        requested_client_rect->left += frame_x + padding;
        requested_client_rect->bottom -= frame_y + padding;

        if (win32_window_is_maximized(handle)) {
            requested_client_rect->top += padding;
        }

        return 0;
    }
    case WM_CREATE: {
        RECT size_rect;
        GetWindowRect(handle, &size_rect);

        // Inform the application of the frame change to force redrawing with the new
        // client area that is extended into the title bar
        SetWindowPos(
            handle, NULL,
            size_rect.left, size_rect.top,
            size_rect.right - size_rect.left, size_rect.bottom - size_rect.top,
            SWP_FRAMECHANGED
        );
        // Necessary for custom drawing the titlebar buttons
        SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)CustomTitleBarHoveredButton_None);
        return 0;
    }
    case WM_CLOSE: {
        PostQuitMessage(0);
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT result = DefWindowProc(handle, message, w_param, l_param);
        if (result == HTCLIENT) {
            RECT title_bar_rect = win32_titlebar_rect(handle);
            POINT mouse_position = { GET_X_PARAM(l_param), GET_Y_PARAM(l_param) };
            ScreenToClient(handle, &mouse_position);

            if (PtInRect(&title_bar_rect, mouse_position)) {
                RECT close_button_rect;
                RECT minimize_button_rect;
                RECT maximize_button_rect;
                RECT help_button_rect;
                CustomTitleBarButtonRects button_rects =
                    win32_get_title_bar_button_rects(handle, &title_bar_rect);
                close_button_rect = button_rects.close;
                minimize_button_rect = button_rects.minimize;
                maximize_button_rect = button_rects.maximize;
                help_button_rect = button_rects.help;
                if (PtInRect(&close_button_rect, mouse_position)) {
                    result = HTCLOSE;
                }
                else if (PtInRect(&minimize_button_rect, mouse_position)) {
                    result = HTMINBUTTON;
                }
                else if (PtInRect(&maximize_button_rect, mouse_position)) {
                    result = HTMAXBUTTON;
                }
                else if (PtInRect(&help_button_rect, mouse_position)) {
                    result = HTHELP;
                }
                else {
                    result = HTCAPTION;
                }
            }
        }
        return result;
    }

                     //To draw the custom title bar
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC device_context = BeginPaint(handle, &paint);


        // Get the client area rectangle that needs repainting
        RECT clientRect = paint.rcPaint;

        // Fill the client area with white
        FillRect(device_context, &clientRect, (HBRUSH)(COLOR_WINDOW + 1));

        // Draw titlebar background
        RECT title_bar_rect = win32_titlebar_rect(handle);
        FillRect(device_context, &title_bar_rect, (HBRUSH)GetStockObject(BLACK_BRUSH));


        // Draw titlebar buttons
        HTHEME theme = OpenThemeData(handle, L"WINDOW");
        UINT dpi = GetDpiForWindow(handle);
        CustomTitleBarButtonRects button_rects =
            win32_get_title_bar_button_rects(handle, &title_bar_rect);



        // Offset rectangles for the maximized window state
        bool is_maximized = win32_window_is_maximized(handle);
        if (is_maximized) {
            OffsetRect(
                &button_rects.close,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET
            );
            OffsetRect(
                &button_rects.minimize,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET
            );
            OffsetRect(
                &button_rects.maximize,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET
            );
            OffsetRect(
                &button_rects.help,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET,
                WIN32_MAXIMIZED_RECTANGLE_OFFSET
            );
        }

        // Draw close button
        DrawThemeBackground(
            theme,
            device_context,
            WP_CLOSEBUTTON,
            title_bar_hovered_button == CustomTitleBarHoveredButton_Close
            ? CBS_HOT
            : CBS_NORMAL,
            &button_rects.close,
            NULL
        );

        // Draw minimize button
        DrawThemeBackground(
            theme,
            device_context,
            WP_MINBUTTON,
            title_bar_hovered_button == CustomTitleBarHoveredButton_Minimize
            ? CBS_HOT
            : CBS_NORMAL,
            &button_rects.minimize,
            NULL
        );

        // Draw maximize button
        DrawThemeBackground(
            theme,
            device_context,
            WP_MAXBUTTON,
            title_bar_hovered_button == CustomTitleBarHoveredButton_Maximize
            ? CBS_HOT
            : CBS_NORMAL,
            &button_rects.maximize,
            NULL
        );

        // Draw help button
        DrawThemeBackground(
            theme,
            device_context,
            WP_HELPBUTTON,
            title_bar_hovered_button == CustomTitleBarHoveredButton_Help
            ? CBS_HOT
            : CBS_NORMAL,
            &button_rects.help,
            NULL
        );

        CloseThemeData(theme);
        EndPaint(handle, &paint);
        return 0;
    }

    case WM_NCMOUSEMOVE: {
        CustomTitleBarHoveredButton new_hovered_button = CustomTitleBarHoveredButton_None;
        switch (w_param) {
        case HTCLOSE:
            new_hovered_button = CustomTitleBarHoveredButton_Close;
            break;
        case HTMINBUTTON:
            new_hovered_button = CustomTitleBarHoveredButton_Minimize;
            break;
        case HTMAXBUTTON:
            new_hovered_button = CustomTitleBarHoveredButton_Maximize;
            break;
        case HTHELP:
            new_hovered_button = CustomTitleBarHoveredButton_Help;
            break;
        }
        if (new_hovered_button != title_bar_hovered_button) {
            SetWindowLongPtrW(handle, GWLP_USERDATA, (LONG_PTR)new_hovered_button);
            RECT title_bar_rect = win32_titlebar_rect(handle);
            CustomTitleBarButtonRects button_rects = win32_get_title_bar_button_rects(handle, &title_bar_rect);
            InvalidateRect(handle, &button_rects.close, FALSE);
            InvalidateRect(handle, &button_rects.minimize, FALSE);
            InvalidateRect(handle, &button_rects.maximize, FALSE);
            InvalidateRect(handle, &button_rects.help, FALSE);
        }
        break;
    }



    case WM_NCLBUTTONDOWN: {
        if (w_param == HTHELP) {
            MessageBox(handle, L"Help Button Clicked!", L"Help", MB_OK);
            return 0;
        }
        else if (w_param == HTCLOSE) {
            SendMessage(handle, WM_CLOSE, 0, 0);
            return 0;
        }
        else if (w_param == HTMINBUTTON) {
            SendMessage(handle, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            return 0;
        }
        else if (w_param == HTMAXBUTTON) {
            if (win32_window_is_maximized(handle)) {
                SendMessage(handle, WM_SYSCOMMAND, SC_RESTORE, 0);
            }
            else {
                SendMessage(handle, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
            }
            return 0;
        }
        return DefWindowProc(handle, message, w_param, l_param);
    }

   
     

    case WM_NCRBUTTONDOWN: {
        if (w_param == HTCAPTION) { // Right-click on the title bar
            POINT pt;
            pt.x = GET_X_PARAM(l_param);
            pt.y = GET_Y_PARAM(l_param);
           

            HMENU hMenu = GetSystemMenu(handle, FALSE);
            if (hMenu) {
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, handle, NULL);
                if (cmd) {
                    SendMessage(handle, WM_SYSCOMMAND, cmd, 0);
                }
            }
            return 0;
        }
        break;
    }

    case WM_SYSCOMMAND: {
        switch (w_param & 0xFFF0) {
        case SC_CLOSE:
            PostQuitMessage(0);
            break;
        case SC_MINIMIZE:
            ShowWindow(handle, SW_MINIMIZE);
            break;
        case SC_MAXIMIZE:
            ShowWindow(handle, SW_MAXIMIZE);
            break;
        case SC_RESTORE:
            ShowWindow(handle, SW_RESTORE);
            break;
        default:
            return DefWindowProc(handle, message, w_param, l_param);
        }

        // Update the system menu after handling the command
        HMENU hMenu = GetSystemMenu(handle, FALSE);
        if (hMenu) {
            bool isMaximized = IsZoomed(handle);
            EnableMenuItem(hMenu, SC_MAXIMIZE, MF_BYCOMMAND | (isMaximized ? MF_GRAYED : MF_ENABLED));
            EnableMenuItem(hMenu, SC_RESTORE, MF_BYCOMMAND | (isMaximized ? MF_ENABLED : MF_GRAYED));
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)l_param;
        mmi->ptMaxSize.x = GetSystemMetrics(SM_CXMAXIMIZED);
        mmi->ptMaxSize.y = GetSystemMetrics(SM_CYMAXIMIZED);
        mmi->ptMaxPosition.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        mmi->ptMaxPosition.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        return 0;
    }




    default:
        return DefWindowProc(handle, message, w_param, l_param);
    }

    return 0;
}
