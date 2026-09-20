export declare const SW_SHOWNORMAL: number
export declare const SW_SHOW: number

export declare function ShellExecuteW(operation: string, file: string, parameters: string, directory: string, showCommand: number): number
export declare function OpenUrl(url: string): boolean
export declare function SHGetKnownFolderPath(folder: string): string
export declare function RevealInExplorer(path: string): boolean
export declare function ShowNotification(title: string, text: string): boolean
