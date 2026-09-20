export declare const DWMWA_USE_IMMERSIVE_DARK_MODE: number
export declare const DWMWA_WINDOW_CORNER_PREFERENCE: number
export declare const DWMWA_BORDER_COLOR: number
export declare const DWMWA_CAPTION_COLOR: number
export declare const DWMWA_TEXT_COLOR: number
export declare const DWMWA_SYSTEMBACKDROP_TYPE: number
export declare const DWMSBT_AUTO: number
export declare const DWMSBT_NONE: number
export declare const DWMSBT_MAINWINDOW: number
export declare const DWMSBT_TRANSIENTWINDOW: number
export declare const DWMSBT_TABBEDWINDOW: number
export declare const DWMWCP_DEFAULT: number
export declare const DWMWCP_DONOTROUND: number
export declare const DWMWCP_ROUND: number
export declare const DWMWCP_ROUNDSMALL: number

export declare function DwmSetWindowAttribute(hwnd: number, attribute: number, value: number): number
export declare function DwmGetColorizationColor(): number
export declare function DwmIsCompositionEnabled(): boolean
export declare function DwmFlush(): number
