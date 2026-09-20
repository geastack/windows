import type { POINT, RECT } from '@geastack/windows/Win32'

export declare const MB_OK: number
export declare const MB_OKCANCEL: number
export declare const MB_YESNOCANCEL: number
export declare const MB_YESNO: number
export declare const MB_ICONERROR: number
export declare const MB_ICONQUESTION: number
export declare const MB_ICONWARNING: number
export declare const MB_ICONINFORMATION: number
export declare const IDOK: number
export declare const IDCANCEL: number
export declare const IDYES: number
export declare const IDNO: number
export declare const SM_CXSCREEN: number
export declare const SM_CYSCREEN: number
export declare const SM_CMONITORS: number
export declare const SW_HIDE: number
export declare const SW_SHOWNORMAL: number
export declare const SW_SHOWMINIMIZED: number
export declare const SW_SHOWMAXIMIZED: number
export declare const SW_SHOW: number
export declare const SW_MINIMIZE: number
export declare const SW_RESTORE: number
export declare const SWP_NOSIZE: number
export declare const SWP_NOMOVE: number
export declare const SWP_NOZORDER: number
export declare const SWP_NOACTIVATE: number
export declare const WM_CLOSE: number
export declare const WM_SETTEXT: number
export declare const WM_COMMAND: number
export declare const VK_SHIFT: number
export declare const VK_CONTROL: number
export declare const VK_MENU: number
export declare const VK_ESCAPE: number
export declare const VK_RETURN: number
export declare const VK_SPACE: number
export declare const MB_ICONASTERISK: number

export declare function MessageBoxW(hwnd: number, text: string, caption: string, type: number): number
export declare function MessageBeep(type: number): boolean
export declare function GetSystemMetrics(index: number): number
export declare function GetSystemMetricsForDpi(index: number, dpi: number): number
export declare function GetForegroundWindow(): number
export declare function GetDesktopWindow(): number
export declare function FindWindowW(className: string, windowName: string): number
export declare function IsWindow(hwnd: number): boolean
export declare function IsWindowVisible(hwnd: number): boolean
export declare function SetWindowTextW(hwnd: number, text: string): boolean
export declare function GetWindowTextW(hwnd: number): string
export declare function ShowWindow(hwnd: number, command: number): boolean
export declare function SetForegroundWindow(hwnd: number): boolean
export declare function FlashWindow(hwnd: number, invert: boolean): boolean
export declare function GetDpiForWindow(hwnd: number): number
export declare function GetDpiForSystem(): number
export declare function GetCursorPos(): POINT
export declare function SetCursorPos(x: number, y: number): boolean
export declare function GetWindowRect(hwnd: number): RECT
export declare function GetClientRect(hwnd: number): RECT
export declare function ClientToScreen(hwnd: number, point: POINT): POINT
export declare function ScreenToClient(hwnd: number, point: POINT): POINT
export declare function MoveWindow(hwnd: number, x: number, y: number, width: number, height: number, repaint: boolean): boolean
export declare function SetWindowPos(hwnd: number, insertAfter: number, x: number, y: number, width: number, height: number, flags: number): boolean
export declare function PostMessageW(hwnd: number, message: number, wParam: number, lParam: number): boolean
export declare function SendMessageW(hwnd: number, message: number, wParam: number, lParam: number): number
export declare function GetKeyState(virtualKey: number): number
export declare function GetAsyncKeyState(virtualKey: number): number
export declare function GetDoubleClickTime(): number
export declare function GetSysColor(index: number): number
export declare function GetWindowThreadProcessId(hwnd: number): number
export declare function SetClipboardText(text: string): boolean
export declare function GetClipboardText(): string
