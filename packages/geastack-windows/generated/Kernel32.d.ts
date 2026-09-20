export declare function GetTickCount64(): number
export declare function Sleep(milliseconds: number): void
export declare function GetComputerNameW(): string
export declare function GetUserNameW(): string
export declare function GetEnvironmentVariableW(name: string): string
export declare function SetEnvironmentVariableW(name: string, value: string): boolean
export declare function GetCurrentProcessId(): number
export declare function GetCurrentThreadId(): number
export declare function GetLastError(): number
export declare function GetSystemDirectoryW(): string
export declare function GetWindowsDirectoryW(): string
export declare function GetTempPathW(): string
export declare function GetCommandLineW(): string
export declare function GetModuleFileNameW(): string
export declare function OutputDebugStringW(text: string): void
export declare function QueryPerformanceCounter(): number
export declare function QueryPerformanceFrequency(): number
export declare function GetLogicalProcessorCount(): number
export declare function GetPhysicalMemoryBytes(): number
export declare function GetAvailableMemoryBytes(): number
export declare function GetOsVersionString(): string
export declare function FileExistsW(path: string): boolean
export declare function ReadTextFileW(path: string): string
export declare function WriteTextFileW(path: string, contents: string): boolean
export declare function CreateDirectoryW(path: string): boolean
export declare function DeleteFileW(path: string): boolean
