function geaWindowsWin32NativeOnly(name) {
  throw new Error(`@geastack/windows/Win32 ${name} is native-only and must be lowered by @geastack/geatsc-plugin-windows-native.`)
}

export function MakePoint() {
  return geaWindowsWin32NativeOnly("MakePoint")
}

export function MakeSize() {
  return geaWindowsWin32NativeOnly("MakeSize")
}

export function MakeRect() {
  return geaWindowsWin32NativeOnly("MakeRect")
}

export function RectWidth() {
  return geaWindowsWin32NativeOnly("RectWidth")
}

export function RectHeight() {
  return geaWindowsWin32NativeOnly("RectHeight")
}
