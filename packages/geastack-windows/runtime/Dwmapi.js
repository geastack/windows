function geaWindowsDwmapiNativeOnly(name) {
  throw new Error(`@geastack/windows/Dwmapi ${name} is native-only and must be lowered by @geastack/geatsc-plugin-windows-native.`)
}

export const DWMWA_USE_IMMERSIVE_DARK_MODE = 20
export const DWMWA_WINDOW_CORNER_PREFERENCE = 33
export const DWMWA_BORDER_COLOR = 34
export const DWMWA_CAPTION_COLOR = 35
export const DWMWA_TEXT_COLOR = 36
export const DWMWA_SYSTEMBACKDROP_TYPE = 38
export const DWMSBT_AUTO = 0
export const DWMSBT_NONE = 1
export const DWMSBT_MAINWINDOW = 2
export const DWMSBT_TRANSIENTWINDOW = 3
export const DWMSBT_TABBEDWINDOW = 4
export const DWMWCP_DEFAULT = 0
export const DWMWCP_DONOTROUND = 1
export const DWMWCP_ROUND = 2
export const DWMWCP_ROUNDSMALL = 3

export function DwmSetWindowAttribute() {
  return geaWindowsDwmapiNativeOnly("DwmSetWindowAttribute")
}

export function DwmGetColorizationColor() {
  return geaWindowsDwmapiNativeOnly("DwmGetColorizationColor")
}

export function DwmIsCompositionEnabled() {
  return geaWindowsDwmapiNativeOnly("DwmIsCompositionEnabled")
}

export function DwmFlush() {
  return geaWindowsDwmapiNativeOnly("DwmFlush")
}
