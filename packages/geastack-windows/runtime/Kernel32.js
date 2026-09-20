function geaWindowsKernel32NativeOnly(name) {
  throw new Error(`@geastack/windows/Kernel32 ${name} is native-only and must be lowered by @geastack/geatsc-plugin-windows-native.`)
}

export function GetTickCount64() {
  return geaWindowsKernel32NativeOnly("GetTickCount64")
}

export function Sleep() {
  return geaWindowsKernel32NativeOnly("Sleep")
}

export function GetComputerNameW() {
  return geaWindowsKernel32NativeOnly("GetComputerNameW")
}

export function GetUserNameW() {
  return geaWindowsKernel32NativeOnly("GetUserNameW")
}

export function GetEnvironmentVariableW() {
  return geaWindowsKernel32NativeOnly("GetEnvironmentVariableW")
}

export function SetEnvironmentVariableW() {
  return geaWindowsKernel32NativeOnly("SetEnvironmentVariableW")
}

export function GetCurrentProcessId() {
  return geaWindowsKernel32NativeOnly("GetCurrentProcessId")
}

export function GetCurrentThreadId() {
  return geaWindowsKernel32NativeOnly("GetCurrentThreadId")
}

export function GetLastError() {
  return geaWindowsKernel32NativeOnly("GetLastError")
}

export function GetSystemDirectoryW() {
  return geaWindowsKernel32NativeOnly("GetSystemDirectoryW")
}

export function GetWindowsDirectoryW() {
  return geaWindowsKernel32NativeOnly("GetWindowsDirectoryW")
}

export function GetTempPathW() {
  return geaWindowsKernel32NativeOnly("GetTempPathW")
}

export function GetCommandLineW() {
  return geaWindowsKernel32NativeOnly("GetCommandLineW")
}

export function GetModuleFileNameW() {
  return geaWindowsKernel32NativeOnly("GetModuleFileNameW")
}

export function OutputDebugStringW() {
  return geaWindowsKernel32NativeOnly("OutputDebugStringW")
}

export function QueryPerformanceCounter() {
  return geaWindowsKernel32NativeOnly("QueryPerformanceCounter")
}

export function QueryPerformanceFrequency() {
  return geaWindowsKernel32NativeOnly("QueryPerformanceFrequency")
}

export function GetLogicalProcessorCount() {
  return geaWindowsKernel32NativeOnly("GetLogicalProcessorCount")
}

export function GetPhysicalMemoryBytes() {
  return geaWindowsKernel32NativeOnly("GetPhysicalMemoryBytes")
}

export function GetAvailableMemoryBytes() {
  return geaWindowsKernel32NativeOnly("GetAvailableMemoryBytes")
}

export function GetOsVersionString() {
  return geaWindowsKernel32NativeOnly("GetOsVersionString")
}

export function FileExistsW() {
  return geaWindowsKernel32NativeOnly("FileExistsW")
}

export function ReadTextFileW() {
  return geaWindowsKernel32NativeOnly("ReadTextFileW")
}

export function WriteTextFileW() {
  return geaWindowsKernel32NativeOnly("WriteTextFileW")
}

export function CreateDirectoryW() {
  return geaWindowsKernel32NativeOnly("CreateDirectoryW")
}

export function DeleteFileW() {
  return geaWindowsKernel32NativeOnly("DeleteFileW")
}
