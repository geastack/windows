function geaWindowsControlsNativeOnly(name) {
  throw new Error(`@geastack/windows/Controls ${name} is native-only and must be lowered by @geastack/geatsc-plugin-windows-native.`)
}

export const OrientationHorizontal = 0
export const OrientationVertical = 1
export const AlignmentLeading = 0
export const AlignmentCenter = 1
export const AlignmentTrailing = 2
export const AlignmentFill = 3
export const DistributionFill = 0
export const DistributionFillEqually = 1
export const DistributionEqualSpacing = 2
export const DistributionGravityAreas = 3
export const TextAlignmentLeft = 0
export const TextAlignmentCenter = 1
export const TextAlignmentRight = 2
export const LineBreakWordWrap = 0
export const LineBreakTruncateTail = 1
export const LineBreakClip = 2
export const FontWeightLight = 300
export const FontWeightRegular = 400
export const FontWeightMedium = 500
export const FontWeightSemibold = 600
export const FontWeightBold = 700
export const PaneRoleSidebar = 0
export const PaneRoleList = 1
export const PaneRoleDetail = 2
export const ContentModeScaleToFill = 0
export const ContentModeAspectFit = 1
export const ContentModeAspectFill = 2
export const ContentModeCenter = 3

export function installRootView() {
  return geaWindowsControlsNativeOnly("installRootView")
}

export function installToolbar() {
  return geaWindowsControlsNativeOnly("installToolbar")
}

export function setWindowTitle() {
  return geaWindowsControlsNativeOnly("setWindowTitle")
}

export function setWindowAppearance() {
  return geaWindowsControlsNativeOnly("setWindowAppearance")
}

export function setWindowBackgroundColor() {
  return geaWindowsControlsNativeOnly("setWindowBackgroundColor")
}

export function setWindowSize() {
  return geaWindowsControlsNativeOnly("setWindowSize")
}

export function mainWindowHandle() {
  return geaWindowsControlsNativeOnly("mainWindowHandle")
}

export function runDeviceCommand() {
  return geaWindowsControlsNativeOnly("runDeviceCommand")
}

export function requestFrame() {
  return geaWindowsControlsNativeOnly("requestFrame")
}

export function quit() {
  return geaWindowsControlsNativeOnly("quit")
}

export class WinCallback {
  static create() {
    return geaWindowsControlsNativeOnly("WinCallback.create")
  }
  invoke() {
    return geaWindowsControlsNativeOnly("WinCallback.invoke")
  }
}

export class WinColor {
  static rgb() {
    return geaWindowsControlsNativeOnly("WinColor.rgb")
  }
  static rgba() {
    return geaWindowsControlsNativeOnly("WinColor.rgba")
  }
  static fromHex() {
    return geaWindowsControlsNativeOnly("WinColor.fromHex")
  }
  static clear() {
    return geaWindowsControlsNativeOnly("WinColor.clear")
  }
  static windowBackground() {
    return geaWindowsControlsNativeOnly("WinColor.windowBackground")
  }
  static controlBackground() {
    return geaWindowsControlsNativeOnly("WinColor.controlBackground")
  }
  static label() {
    return geaWindowsControlsNativeOnly("WinColor.label")
  }
  static secondaryLabel() {
    return geaWindowsControlsNativeOnly("WinColor.secondaryLabel")
  }
  static tertiaryLabel() {
    return geaWindowsControlsNativeOnly("WinColor.tertiaryLabel")
  }
  static separator() {
    return geaWindowsControlsNativeOnly("WinColor.separator")
  }
  static accent() {
    return geaWindowsControlsNativeOnly("WinColor.accent")
  }
  static systemBlue() {
    return geaWindowsControlsNativeOnly("WinColor.systemBlue")
  }
  static systemYellow() {
    return geaWindowsControlsNativeOnly("WinColor.systemYellow")
  }
  static systemRed() {
    return geaWindowsControlsNativeOnly("WinColor.systemRed")
  }
  static systemGreen() {
    return geaWindowsControlsNativeOnly("WinColor.systemGreen")
  }
}

export class WinFont {
  static system() {
    return geaWindowsControlsNativeOnly("WinFont.system")
  }
  static systemWeight() {
    return geaWindowsControlsNativeOnly("WinFont.systemWeight")
  }
  static named() {
    return geaWindowsControlsNativeOnly("WinFont.named")
  }
  static monospace() {
    return geaWindowsControlsNativeOnly("WinFont.monospace")
  }
}

export class WinImage {
  static symbol() {
    return geaWindowsControlsNativeOnly("WinImage.symbol")
  }
  static fromFile() {
    return geaWindowsControlsNativeOnly("WinImage.fromFile")
  }
}

export class WinView {
  constructor() {
    geaWindowsControlsNativeOnly("new WinView")
  }
  addSubview() {
    return geaWindowsControlsNativeOnly("WinView.addSubview")
  }
  removeFromSuperview() {
    return geaWindowsControlsNativeOnly("WinView.removeFromSuperview")
  }
  setFrame() {
    return geaWindowsControlsNativeOnly("WinView.setFrame")
  }
  setSize() {
    return geaWindowsControlsNativeOnly("WinView.setSize")
  }
  setMinimumSize() {
    return geaWindowsControlsNativeOnly("WinView.setMinimumSize")
  }
  setIntrinsicSize() {
    return geaWindowsControlsNativeOnly("WinView.setIntrinsicSize")
  }
  anchorFill() {
    return geaWindowsControlsNativeOnly("WinView.anchorFill")
  }
  anchorEdges() {
    return geaWindowsControlsNativeOnly("WinView.anchorEdges")
  }
  onClick() {
    return geaWindowsControlsNativeOnly("WinView.onClick")
  }
  setNeedsLayout() {
    return geaWindowsControlsNativeOnly("WinView.setNeedsLayout")
  }
  setNeedsDisplay() {
    return geaWindowsControlsNativeOnly("WinView.setNeedsDisplay")
  }
}

export class WinStackView {
  constructor() {
    geaWindowsControlsNativeOnly("new WinStackView")
  }
  addArrangedSubview() {
    return geaWindowsControlsNativeOnly("WinStackView.addArrangedSubview")
  }
  insertArrangedSubviewAtIndex() {
    return geaWindowsControlsNativeOnly("WinStackView.insertArrangedSubviewAtIndex")
  }
  removeArrangedSubview() {
    return geaWindowsControlsNativeOnly("WinStackView.removeArrangedSubview")
  }
  setPadding() {
    return geaWindowsControlsNativeOnly("WinStackView.setPadding")
  }
}

export class WinLabel {
  constructor() {
    geaWindowsControlsNativeOnly("new WinLabel")
  }
}

export class WinTextField {
  constructor() {
    geaWindowsControlsNativeOnly("new WinTextField")
  }
  setOnChange() {
    return geaWindowsControlsNativeOnly("WinTextField.setOnChange")
  }
  setOnSubmit() {
    return geaWindowsControlsNativeOnly("WinTextField.setOnSubmit")
  }
  focus() {
    return geaWindowsControlsNativeOnly("WinTextField.focus")
  }
  selectAll() {
    return geaWindowsControlsNativeOnly("WinTextField.selectAll")
  }
}

export class WinTextView {
  constructor() {
    geaWindowsControlsNativeOnly("new WinTextView")
  }
  setOnChange() {
    return geaWindowsControlsNativeOnly("WinTextView.setOnChange")
  }
  focus() {
    return geaWindowsControlsNativeOnly("WinTextView.focus")
  }
}

export class WinButton {
  constructor() {
    geaWindowsControlsNativeOnly("new WinButton")
  }
}

export class WinCheckBox {
  constructor() {
    geaWindowsControlsNativeOnly("new WinCheckBox")
  }
  setOnChange() {
    return geaWindowsControlsNativeOnly("WinCheckBox.setOnChange")
  }
}

export class WinSlider {
  constructor() {
    geaWindowsControlsNativeOnly("new WinSlider")
  }
  setOnChange() {
    return geaWindowsControlsNativeOnly("WinSlider.setOnChange")
  }
}

export class WinProgressBar {
  constructor() {
    geaWindowsControlsNativeOnly("new WinProgressBar")
  }
}

export class WinImageView {
  constructor() {
    geaWindowsControlsNativeOnly("new WinImageView")
  }
}

export class WinScrollView {
  constructor() {
    geaWindowsControlsNativeOnly("new WinScrollView")
  }
  scrollToTop() {
    return geaWindowsControlsNativeOnly("WinScrollView.scrollToTop")
  }
  scrollTo() {
    return geaWindowsControlsNativeOnly("WinScrollView.scrollTo")
  }
}

export class WinBox {
  constructor() {
    geaWindowsControlsNativeOnly("new WinBox")
  }
  setContentInsets() {
    return geaWindowsControlsNativeOnly("WinBox.setContentInsets")
  }
}

export class WinSplitView {
  constructor() {
    geaWindowsControlsNativeOnly("new WinSplitView")
  }
  addPane() {
    return geaWindowsControlsNativeOnly("WinSplitView.addPane")
  }
  toggleSidebar() {
    return geaWindowsControlsNativeOnly("WinSplitView.toggleSidebar")
  }
  setPaneThickness() {
    return geaWindowsControlsNativeOnly("WinSplitView.setPaneThickness")
  }
}

export class WinToolbar {
  constructor() {
    geaWindowsControlsNativeOnly("new WinToolbar")
  }
  addItem() {
    return geaWindowsControlsNativeOnly("WinToolbar.addItem")
  }
  addSidebarToggle() {
    return geaWindowsControlsNativeOnly("WinToolbar.addSidebarToggle")
  }
  addSpace() {
    return geaWindowsControlsNativeOnly("WinToolbar.addSpace")
  }
  addSearchField() {
    return geaWindowsControlsNativeOnly("WinToolbar.addSearchField")
  }
  searchText() {
    return geaWindowsControlsNativeOnly("WinToolbar.searchText")
  }
}
