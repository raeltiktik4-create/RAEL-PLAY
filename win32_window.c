#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "win32_window.h"
#include "video_renderer.h"

#define UXPLAY_WINDOW_CLASS "UxPlayPresentationWindow"

static HWND g_hwnd = NULL;
static HANDLE g_thread = NULL;
static HANDLE g_ready_event = NULL;
static SRWLOCK g_window_lock = SRWLOCK_INIT;
static SRWLOCK g_lifecycle_lock = SRWLOCK_INIT;
static volatile LONG g_fullscreen = 0;
static char g_title[256] = "UxPlay";

static void apply_fullscreen(HWND hwnd, bool on) {
    LONG style = GetWindowLongA(hwnd, GWL_STYLE);
    if (on) {
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        if (GetMonitorInfoA(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) {
            SetWindowLongA(hwnd, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                         mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    } else {
        SetWindowLongA(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, NULL, 0, 0, 1280, 720, SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
        ShowWindow(hwnd, SW_SHOW);
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        AcquireSRWLockExclusive(&g_window_lock);
        if (g_hwnd == hwnd) {
            g_hwnd = NULL;
        }
        ReleaseSRWLockExclusive(&g_window_lock);
        video_renderer_set_window_handle(0);
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_F11) {
            LONG fullscreen = !InterlockedCompareExchange(&g_fullscreen, 0, 0);
            InterlockedExchange(&g_fullscreen, fullscreen);
            apply_fullscreen(hwnd, fullscreen != 0);
            return 0;
        }
        if (wp == VK_ESCAPE && InterlockedCompareExchange(&g_fullscreen, 0, 0)) {
            InterlockedExchange(&g_fullscreen, 0);
            apply_fullscreen(hwnd, false);
            return 0;
        }
        return 0;
    case WM_ERASEBKGND: return 1;
    default: return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static DWORD WINAPI window_thread(LPVOID unused) {
    (void) unused;
    SetProcessDPIAware();
    WNDCLASSEXA wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc); wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = UXPLAY_WINDOW_CLASS;
    if (!RegisterClassExA(&wc)) {
        SetEvent(g_ready_event);
        return 1;
    }
    RECT r = { 0, 0, 1280, 720 };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, g_title, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        UnregisterClassA(UXPLAY_WINDOW_CLASS, GetModuleHandleA(NULL));
        SetEvent(g_ready_event);
        return 1;
    }
    AcquireSRWLockExclusive(&g_window_lock);
    g_hwnd = hwnd;
    ReleaseSRWLockExclusive(&g_window_lock);
    video_renderer_set_window_handle((uintptr_t) hwnd);
    SetEvent(g_ready_event);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    if (InterlockedCompareExchange(&g_fullscreen, 0, 0)) {
        apply_fullscreen(hwnd, true);
    }
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    UnregisterClassA(UXPLAY_WINDOW_CLASS, GetModuleHandleA(NULL));
    return 0;
}

bool win32_window_create(const char *title, bool fullscreen) {
    AcquireSRWLockExclusive(&g_lifecycle_lock);
    AcquireSRWLockShared(&g_window_lock);
    bool already_alive = g_hwnd != NULL;
    HANDLE previous_thread = g_thread;
    ReleaseSRWLockShared(&g_window_lock);
    if (already_alive) {
        ReleaseSRWLockExclusive(&g_lifecycle_lock);
        return true;
    }
    if (previous_thread) {
        if (WaitForSingleObject(previous_thread, 0) != WAIT_OBJECT_0) {
            ReleaseSRWLockExclusive(&g_lifecycle_lock);
            return false;
        }
        AcquireSRWLockExclusive(&g_window_lock);
        g_thread = NULL;
        ReleaseSRWLockExclusive(&g_window_lock);
        CloseHandle(previous_thread);
    }
    InterlockedExchange(&g_fullscreen, fullscreen ? 1 : 0);
    if (title && *title) snprintf(g_title, sizeof(g_title), "%s", title);
    g_ready_event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!g_ready_event) {
        ReleaseSRWLockExclusive(&g_lifecycle_lock);
        return false;
    }
    HANDLE thread = CreateThread(NULL, 0, window_thread, NULL, 0, NULL);
    if (!thread) {
        CloseHandle(g_ready_event);
        g_ready_event = NULL;
        ReleaseSRWLockExclusive(&g_lifecycle_lock);
        return false;
    }
    AcquireSRWLockExclusive(&g_window_lock);
    g_thread = thread;
    ReleaseSRWLockExclusive(&g_window_lock);
    WaitForSingleObject(g_ready_event, INFINITE);
    CloseHandle(g_ready_event);
    g_ready_event = NULL;
    AcquireSRWLockShared(&g_window_lock);
    bool created = g_hwnd != NULL;
    ReleaseSRWLockShared(&g_window_lock);
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
    return created;
}

void win32_window_destroy(void) {
    AcquireSRWLockExclusive(&g_lifecycle_lock);
    AcquireSRWLockShared(&g_window_lock);
    HWND hwnd = g_hwnd;
    HANDLE thread = g_thread;
    ReleaseSRWLockShared(&g_window_lock);
    if (thread) {
        if (hwnd) {
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
        } else {
            PostThreadMessageA(GetThreadId(thread), WM_QUIT, 0, 0);
        }
        WaitForSingleObject(thread, INFINITE);
        AcquireSRWLockExclusive(&g_window_lock);
        if (g_thread == thread) {
            g_thread = NULL;
        }
        g_hwnd = NULL;
        ReleaseSRWLockExclusive(&g_window_lock);
        CloseHandle(thread);
    }
    ReleaseSRWLockExclusive(&g_lifecycle_lock);
}

bool win32_window_alive(void) {
    AcquireSRWLockShared(&g_window_lock);
    bool alive = g_hwnd != NULL;
    ReleaseSRWLockShared(&g_window_lock);
    return alive;
}

uintptr_t win32_window_handle(void) {
    AcquireSRWLockShared(&g_window_lock);
    uintptr_t handle = (uintptr_t) g_hwnd;
    ReleaseSRWLockShared(&g_window_lock);
    return handle;
}
#endif