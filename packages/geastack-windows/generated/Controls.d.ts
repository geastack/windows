export declare const OrientationHorizontal: number
export declare const OrientationVertical: number
export declare const AlignmentLeading: number
export declare const AlignmentCenter: number
export declare const AlignmentTrailing: number
export declare const AlignmentFill: number
export declare const DistributionFill: number
export declare const DistributionFillEqually: number
export declare const DistributionEqualSpacing: number
export declare const DistributionGravityAreas: number
export declare const TextAlignmentLeft: number
export declare const TextAlignmentCenter: number
export declare const TextAlignmentRight: number
export declare const LineBreakWordWrap: number
export declare const LineBreakTruncateTail: number
export declare const LineBreakClip: number
export declare const FontWeightLight: number
export declare const FontWeightRegular: number
export declare const FontWeightMedium: number
export declare const FontWeightSemibold: number
export declare const FontWeightBold: number
export declare const PaneRoleSidebar: number
export declare const PaneRoleList: number
export declare const PaneRoleDetail: number
export declare const ContentModeScaleToFill: number
export declare const ContentModeAspectFit: number
export declare const ContentModeAspectFill: number
export declare const ContentModeCenter: number

export declare function installRootView(view: WinView): void
export declare function installToolbar(toolbar: WinToolbar): void
export declare function setWindowTitle(title: string): void
export declare function setWindowAppearance(appearance: string): void
export declare function setWindowBackgroundColor(color: WinColor): void
export declare function setWindowSize(width: number, height: number): void
export declare function mainWindowHandle(): number
export declare function runDeviceCommand(command: string): string
export declare function requestFrame(): void
export declare function quit(): void

export declare class WinCallback {
  private constructor()
  static create(handler: () => void): WinCallback
  invoke(): void
}

export declare class WinColor {
  private constructor()
  readonly red: number
  readonly green: number
  readonly blue: number
  readonly alpha: number
  static rgb(red: number, green: number, blue: number): WinColor
  static rgba(red: number, green: number, blue: number, alpha: number): WinColor
  static fromHex(hex: string): WinColor
  static clear(): WinColor
  static windowBackground(): WinColor
  static controlBackground(): WinColor
  static label(): WinColor
  static secondaryLabel(): WinColor
  static tertiaryLabel(): WinColor
  static separator(): WinColor
  static accent(): WinColor
  static systemBlue(): WinColor
  static systemYellow(): WinColor
  static systemRed(): WinColor
  static systemGreen(): WinColor
}

export declare class WinFont {
  private constructor()
  readonly size: number
  readonly family: string
  readonly weight: number
  static system(size: number): WinFont
  static systemWeight(size: number, weight: number): WinFont
  static named(family: string, size: number, weight: number): WinFont
  static monospace(size: number): WinFont
}

export declare class WinImage {
  private constructor()
  readonly width: number
  readonly height: number
  static symbol(name: string): WinImage
  static fromFile(path: string): WinImage
}

export declare class WinView {
  constructor()
  readonly props: Partial<this>
  hidden: boolean
  backgroundColor: WinColor
  cornerRadius: number
  borderWidth: number
  borderColor: WinColor
  tag: string
  tooltip: string
  readonly x: number
  readonly y: number
  readonly width: number
  readonly height: number
  readonly handle: number
  readonly superview: WinView
  addSubview(child: WinView): void
  removeFromSuperview(): void
  setFrame(x: number, y: number, width: number, height: number): void
  setSize(width: number, height: number): void
  setMinimumSize(width: number, height: number): void
  setIntrinsicSize(width: number, height: number): void
  anchorFill(inset: number): void
  anchorEdges(leading: number, top: number, trailing: number, bottom: number): void
  onClick(callback: WinCallback): void
  setNeedsLayout(): void
  setNeedsDisplay(): void
}

export declare class WinStackView extends WinView {
  constructor()
  readonly props: Partial<this>
  orientation: number
  spacing: number
  alignment: number
  distribution: number
  detachesHiddenViews: boolean
  readonly arrangedSubviewCount: number
  addArrangedSubview(view: WinView): void
  insertArrangedSubviewAtIndex(view: WinView, index: number): void
  removeArrangedSubview(view: WinView): void
  setPadding(top: number, leading: number, bottom: number, trailing: number): void
}

export declare class WinLabel extends WinView {
  constructor()
  readonly props: Partial<this>
  text: string
  font: WinFont
  textColor: WinColor
  alignment: number
  maximumNumberOfLines: number
  lineBreakMode: number
  selectable: boolean
}

export declare class WinTextField extends WinView {
  constructor()
  readonly props: Partial<this>
  text: string
  placeholder: string
  font: WinFont
  textColor: WinColor
  alignment: number
  editable: boolean
  bordered: boolean
  setOnChange(callback: WinCallback): void
  setOnSubmit(callback: WinCallback): void
  focus(): void
  selectAll(): void
}

export declare class WinTextView extends WinView {
  constructor()
  readonly props: Partial<this>
  text: string
  font: WinFont
  textColor: WinColor
  editable: boolean
  drawsBackground: boolean
  setOnChange(callback: WinCallback): void
  focus(): void
}

export declare class WinButton extends WinView {
  constructor()
  readonly props: Partial<this>
  title: string
  font: WinFont
  enabled: boolean
  image: WinImage
}

export declare class WinCheckBox extends WinView {
  constructor()
  readonly props: Partial<this>
  title: string
  checked: boolean
  setOnChange(callback: WinCallback): void
}

export declare class WinSlider extends WinView {
  constructor()
  readonly props: Partial<this>
  minimum: number
  maximum: number
  value: number
  setOnChange(callback: WinCallback): void
}

export declare class WinProgressBar extends WinView {
  constructor()
  readonly props: Partial<this>
  minimum: number
  maximum: number
  value: number
  indeterminate: boolean
}

export declare class WinImageView extends WinView {
  constructor()
  readonly props: Partial<this>
  image: WinImage
  tintColor: WinColor
  contentMode: number
}

export declare class WinScrollView extends WinView {
  constructor()
  readonly props: Partial<this>
  documentView: WinView
  drawsBackground: boolean
  hasVerticalScroller: boolean
  hasHorizontalScroller: boolean
  readonly scrollTop: number
  scrollToTop(): void
  scrollTo(y: number): void
}

export declare class WinBox extends WinView {
  constructor()
  readonly props: Partial<this>
  fillColor: WinColor
  contentView: WinView
  setContentInsets(top: number, leading: number, bottom: number, trailing: number): void
}

export declare class WinSplitView extends WinView {
  constructor()
  readonly props: Partial<this>
  dividerColor: WinColor
  dividerWidth: number
  addPane(view: WinView, role: number, minimumThickness: number, maximumThickness: number, canCollapse: boolean): void
  toggleSidebar(): void
  setPaneThickness(index: number, thickness: number): void
}

export declare class WinToolbar {
  constructor()
  readonly props: Partial<this>
  height: number
  backgroundColor: WinColor
  addItem(symbol: string, label: string, action: WinCallback): void
  addSidebarToggle(splitView: WinSplitView): void
  addSpace(): void
  addSearchField(placeholder: string, onChange: WinCallback): void
  searchText(): string
}
