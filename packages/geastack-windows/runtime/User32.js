function geaWindowsUser32NativeOnly(name) {
  throw new Error(`@geastack/windows/User32 ${name} is native-only and must be lowered by @geastack/geatsc-plugin-windows-native.`)
}

export const MB_OK = 0
export const MB_OKCANCEL = 1
export const MB_YESNOCANCEL = 3
export const MB_YESNO = 4
export const MB_ICONERROR = 16
export const MB_ICONQUESTION = 32
export const MB_ICONWARNING = 48
export const MB_ICONINFORMATION = 64
export const IDOK = 1
export const IDCANCEL = 2
export const IDYES = 6
export const IDNO = 7
export const SM_CXSCREEN = 0
export const SM_CYSCREEN = 1
export const SM_CMONITORS = 80
export const SW_HIDE = 0
export const SW_SHOWNORMAL = 1
export const SW_SHOWMINIMIZED = 2
export const SW_SHOWMAXIMIZED = 3
export const SW_SHOW = 5
export const SW_MINIMIZE = 6
export const SW_RESTORE = 9
export const SWP_NOSIZE = 1
export const SWP_NOMOVE = 2
export const SWP_NOZORDER = 4
export const SWP_NOACTIVATE = 16
export const WM_CLOSE = 16
export const WM_SETTEXT = 12
export const WM_COMMAND = 273
export const VK_SHIFT = 16
export const VK_CONTROL = 17
export const VK_MENU = 18
export const VK_ESCAPE = 27
export const VK_RETURN = 13
export const VK_SPACE = 32
export const MB_ICONASTERISK = 64

export function MessageBoxW() {
  return geaWindowsUser32NativeOnly("MessageBoxW")
}

export function MessageBeep() {
  return geaWindowsUser32NativeOnly("MessageBeep")
}

export function GetSystemMetrics() {
  return geaWindowsUser32NativeOnly("GetSystemMetrics")
}

export function GetSystemMetricsForDpi() {
  return geaWindowsUser32NativeOnly("GetSystemMetricsForDpi")
}

export function GetForegroundWindow() {
  return geaWindowsUser32NativeOnly("GetForegroundWindow")
}

export function GetDesktopWindow() {
  return geaWindowsUser32NativeOnly("GetDesktopWindow")
}

export function FindWindowW() {
  return geaWindowsUser32NativeOnly("FindWindowW")
}

export function IsWindow() {
  return geaWindowsUser32NativeOnly("IsWindow")
}

export function IsWindowVisible() {
  return geaWindowsUser32NativeOnly("IsWindowVisible")
}

export function SetWindowTextW() {
  return geaWindowsUser32NativeOnly("SetWindowTextW")
}

export function GetWindowTextW() {
  return geaWindowsUser32NativeOnly("GetWindowTextW")
}

export function ShowWindow() {
  return geaWindowsUser32NativeOnly("ShowWindow")
}

export function SetForegroundWindow() {
  return geaWindowsUser32NativeOnly("SetForegroundWindow")
}

export function FlashWindow() {
  return geaWindowsUser32NativeOnly("FlashWindow")
}

export function GetDpiForWindow() {
  return geaWindowsUser32NativeOnly("GetDpiForWindow")
}

export function GetDpiForSystem() {
  return geaWindowsUser32NativeOnly("GetDpiForSystem")
}

export function GetCursorPos() {
  return geaWindowsUser32NativeOnly("GetCursorPos")
}

export function SetCursorPos() {
  return geaWindowsUser32NativeOnly("SetCursorPos")
}

export function GetWindowRect() {
  return geaWindowsUser32NativeOnly("GetWindowRect")
}

export function GetClientRect() {
  return geaWindowsUser32NativeOnly("GetClientRect")
}

export function ClientToScreen() {
  return geaWindowsUser32NativeOnly("ClientToScreen")
}

export function ScreenToClient() {
  return geaWindowsUser32NativeOnly("ScreenToClient")
}

export function MoveWindow() {
  return geaWindowsUser32NativeOnly("MoveWindow")
}

export function SetWindowPos() {
  return geaWindowsUser32NativeOnly("SetWindowPos")
}

export function PostMessageW() {
  return geaWindowsUser32NativeOnly("PostMessageW")
}

export function SendMessageW() {
  return geaWindowsUser32NativeOnly("SendMessageW")
}

export function GetKeyState() {
  return geaWindowsUser32NativeOnly("GetKeyState")
}

export function GetAsyncKeyState() {
  return geaWindowsUser32NativeOnly("GetAsyncKeyState")
}

export function GetDoubleClickTime() {
  return geaWindowsUser32NativeOnly("GetDoubleClickTime")
}

export function GetSysColor() {
  return geaWindowsUser32NativeOnly("GetSysColor")
}

export function GetWindowThreadProcessId() {
  return geaWindowsUser32NativeOnly("GetWindowThreadProcessId")
}

export function SetClipboardText() {
  return geaWindowsUser32NativeOnly("SetClipboardText")
}

export function GetClipboardText() {
  return geaWindowsUser32NativeOnly("GetClipboardText")
}
