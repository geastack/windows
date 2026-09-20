function geaWindowsShell32NativeOnly(name) {
  throw new Error(`@geastack/windows/Shell32 ${name} is native-only and must be lowered by @geastack/geatsc-plugin-windows-native.`)
}

export const SW_SHOWNORMAL = 1
export const SW_SHOW = 5

export function ShellExecuteW() {
  return geaWindowsShell32NativeOnly("ShellExecuteW")
}

export function OpenUrl() {
  return geaWindowsShell32NativeOnly("OpenUrl")
}

export function SHGetKnownFolderPath() {
  return geaWindowsShell32NativeOnly("SHGetKnownFolderPath")
}

export function RevealInExplorer() {
  return geaWindowsShell32NativeOnly("RevealInExplorer")
}

export function ShowNotification() {
  return geaWindowsShell32NativeOnly("ShowNotification")
}
