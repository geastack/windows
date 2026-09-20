// @geastack/windows — the Windows SDK as data.
//
// This module describes the Win32 surface a Gea program may reach from
// TypeScript: raw API groupings (User32, Kernel32, Shell32, Dwmapi, Win32
// structs) and the Controls object model the Win32 target implements over
// HWNDs. Everything a consumer needs is generated from `windowsSdkFixture`:
//
//   - `generated/<Library>.d.ts`   what the checker types a program against
//   - `runtime/<Library>.js`       native-only stubs Vite bundles in place of
//                                  code (every export throws: nothing here runs
//                                  as JavaScript, the compiler lowers each call)
//   - bridge metadata              what `@geastack/geatsc-plugin-windows-native`
//                                  turns into compiler host tables
//   - `gea/windows/native_bridge.h` the C++ contract the target implements:
//                                  handle-carrying wrapper structs plus one
//                                  free-function thunk per function, method,
//                                  property and constructor
//
// Objects live in a handle table on the native side; a wrapper struct carries
// the handle as a double so a generated program can store, pass and box it
// like any other value. Unlike the Apple bindings there is no message send to
// generate against: each thunk is hand-implemented in the target
// (`targets/win32/main/native/*.cpp`), so the header generated here is the
// list of what those files must define.

export type WindowsPrimitiveType = 'boolean' | 'number' | 'string' | 'void'

export type WindowsTypeReference =
  | { kind: 'primitive'; name: WindowsPrimitiveType; nullable?: boolean }
  | { kind: 'class' | 'struct'; name: string; nullable?: boolean }
  | { kind: 'array'; element: WindowsTypeReference; nullable?: boolean }
  | { kind: 'function'; returns: WindowsTypeReference; parameters?: WindowsParameterDefinition[]; nullable?: boolean }

export interface WindowsParameterDefinition {
  name: string
  type: WindowsTypeReference
}

export interface WindowsMethodDefinition {
  name: string
  returns: WindowsTypeReference
  parameters?: WindowsParameterDefinition[]
  static?: boolean
}

export interface WindowsPropertyDefinition {
  name: string
  type: WindowsTypeReference
  readonly?: boolean
}

export interface WindowsFunctionDefinition {
  name: string
  returns: WindowsTypeReference
  parameters?: WindowsParameterDefinition[]
  /** C++ body emitted inline in the generated header instead of a thunk the target defines. */
  inline?: string
}

export interface WindowsConstantDefinition {
  name: string
  type: WindowsTypeReference
  value: string | number | boolean
}

export interface WindowsClassDefinition {
  name: string
  extends?: string
  /** A class with a constructor is created with `new`; one without is reached through statics only. */
  construct?: { parameters?: WindowsParameterDefinition[] }
  methods?: WindowsMethodDefinition[]
  properties?: WindowsPropertyDefinition[]
}

export interface WindowsStructDefinition {
  name: string
  fields: WindowsParameterDefinition[]
}

export interface WindowsLibraryDefinition {
  name: string
  /** Types this library's declarations import from sibling libraries. */
  imports?: Record<string, string[]>
  constants?: WindowsConstantDefinition[]
  functions?: WindowsFunctionDefinition[]
  classes?: WindowsClassDefinition[]
  structs?: WindowsStructDefinition[]
}

export interface WindowsSdkDefinition {
  libraries: WindowsLibraryDefinition[]
}

// --- bridge metadata (what the compiler plugin consumes) ---------------------

export interface WindowsBridgeMetadata {
  libraries: Array<{ name: string }>
  constants: Record<string, WindowsBridgeConstantMetadata>
  functions: Record<string, WindowsBridgeFunctionMetadata>
  classes: Record<string, WindowsBridgeClassMetadata>
  structs: Record<string, WindowsBridgeStructMetadata>
}

export interface WindowsBridgeConstantMetadata {
  library: string
  name: string
  type: WindowsTypeReference
  value: string | number | boolean
}

export interface WindowsBridgeFunctionMetadata {
  library: string
  name: string
  thunk: string
  returns: WindowsTypeReference
  parameters: WindowsParameterDefinition[]
  inline?: string
}

export interface WindowsBridgeClassMetadata {
  library: string
  name: string
  extends?: string
  wrapper: string
  constructor?: { thunk: string; parameters: WindowsParameterDefinition[] }
  methods: Record<string, WindowsBridgeMethodMetadata>
  properties: Record<string, WindowsBridgePropertyMetadata>
}

export interface WindowsBridgeMethodMetadata {
  name: string
  thunk: string
  returns: WindowsTypeReference
  parameters: WindowsParameterDefinition[]
  static?: boolean
}

export interface WindowsBridgePropertyMetadata {
  name: string
  getter: string
  setter?: string
  type: WindowsTypeReference
}

export interface WindowsBridgeStructMetadata {
  library: string
  name: string
  wrapper: string
  fields: WindowsParameterDefinition[]
}

// --- fixture helpers ---------------------------------------------------------

const numberType: WindowsTypeReference = { kind: 'primitive', name: 'number' }
const booleanType: WindowsTypeReference = { kind: 'primitive', name: 'boolean' }
const stringType: WindowsTypeReference = { kind: 'primitive', name: 'string' }
const voidType: WindowsTypeReference = { kind: 'primitive', name: 'void' }
const voidCallbackType: WindowsTypeReference = { kind: 'function', returns: voidType }
const cls = (name: string): WindowsTypeReference => ({ kind: 'class', name })
const struct = (name: string): WindowsTypeReference => ({ kind: 'struct', name })
const param = (name: string, type: WindowsTypeReference): WindowsParameterDefinition => ({ name, type })
const num = (name: string): WindowsParameterDefinition => param(name, numberType)
const str = (name: string): WindowsParameterDefinition => param(name, stringType)
const bool = (name: string): WindowsParameterDefinition => param(name, booleanType)
const constant = (name: string, value: number): WindowsConstantDefinition => ({ name, type: numberType, value })
const fn = (name: string, returns: WindowsTypeReference, parameters: WindowsParameterDefinition[] = [], inline?: string): WindowsFunctionDefinition => ({
  name,
  returns,
  parameters,
  ...(inline ? { inline } : {}),
})
const method = (name: string, returns: WindowsTypeReference, parameters: WindowsParameterDefinition[] = []): WindowsMethodDefinition => ({ name, returns, parameters })
const staticMethod = (name: string, returns: WindowsTypeReference, parameters: WindowsParameterDefinition[] = []): WindowsMethodDefinition => ({
  name,
  returns,
  parameters,
  static: true,
})
const prop = (name: string, type: WindowsTypeReference): WindowsPropertyDefinition => ({ name, type })
const readonlyProp = (name: string, type: WindowsTypeReference): WindowsPropertyDefinition => ({ name, type, readonly: true })

const Controls = (name: string): WindowsTypeReference => cls(`Controls.${name}`)

// --- the SDK -----------------------------------------------------------------

export const windowsSdkFixture: WindowsSdkDefinition = {
  libraries: [
    {
      // Plain C structs shared by the raw API groupings. Value types: a field
      // read is a member access on the C++ struct, no handle table involved.
      name: 'Win32',
      structs: [
        { name: 'POINT', fields: [num('x'), num('y')] },
        { name: 'SIZE', fields: [num('cx'), num('cy')] },
        { name: 'RECT', fields: [num('left'), num('top'), num('right'), num('bottom')] },
      ],
      functions: [
        fn('MakePoint', struct('Win32.POINT'), [num('x'), num('y')], 'return POINT{x, y};'),
        fn('MakeSize', struct('Win32.SIZE'), [num('cx'), num('cy')], 'return SIZE{cx, cy};'),
        fn('MakeRect', struct('Win32.RECT'), [num('left'), num('top'), num('right'), num('bottom')], 'return RECT{left, top, right, bottom};'),
        fn('RectWidth', numberType, [param('rect', struct('Win32.RECT'))], 'return rect.right - rect.left;'),
        fn('RectHeight', numberType, [param('rect', struct('Win32.RECT'))], 'return rect.bottom - rect.top;'),
      ],
    },
    {
      name: 'User32',
      imports: { Win32: ['POINT', 'RECT'] },
      constants: [
        constant('MB_OK', 0x0),
        constant('MB_OKCANCEL', 0x1),
        constant('MB_YESNOCANCEL', 0x3),
        constant('MB_YESNO', 0x4),
        constant('MB_ICONERROR', 0x10),
        constant('MB_ICONQUESTION', 0x20),
        constant('MB_ICONWARNING', 0x30),
        constant('MB_ICONINFORMATION', 0x40),
        constant('IDOK', 1),
        constant('IDCANCEL', 2),
        constant('IDYES', 6),
        constant('IDNO', 7),
        constant('SM_CXSCREEN', 0),
        constant('SM_CYSCREEN', 1),
        constant('SM_CMONITORS', 80),
        constant('SW_HIDE', 0),
        constant('SW_SHOWNORMAL', 1),
        constant('SW_SHOWMINIMIZED', 2),
        constant('SW_SHOWMAXIMIZED', 3),
        constant('SW_SHOW', 5),
        constant('SW_MINIMIZE', 6),
        constant('SW_RESTORE', 9),
        constant('SWP_NOSIZE', 0x1),
        constant('SWP_NOMOVE', 0x2),
        constant('SWP_NOZORDER', 0x4),
        constant('SWP_NOACTIVATE', 0x10),
        constant('WM_CLOSE', 0x10),
        constant('WM_SETTEXT', 0xc),
        constant('WM_COMMAND', 0x111),
        constant('VK_SHIFT', 0x10),
        constant('VK_CONTROL', 0x11),
        constant('VK_MENU', 0x12),
        constant('VK_ESCAPE', 0x1b),
        constant('VK_RETURN', 0xd),
        constant('VK_SPACE', 0x20),
        constant('MB_ICONASTERISK', 0x40),
      ],
      functions: [
        fn('MessageBoxW', numberType, [num('hwnd'), str('text'), str('caption'), num('type')]),
        fn('MessageBeep', booleanType, [num('type')]),
        fn('GetSystemMetrics', numberType, [num('index')]),
        fn('GetSystemMetricsForDpi', numberType, [num('index'), num('dpi')]),
        fn('GetForegroundWindow', numberType),
        fn('GetDesktopWindow', numberType),
        fn('FindWindowW', numberType, [str('className'), str('windowName')]),
        fn('IsWindow', booleanType, [num('hwnd')]),
        fn('IsWindowVisible', booleanType, [num('hwnd')]),
        fn('SetWindowTextW', booleanType, [num('hwnd'), str('text')]),
        fn('GetWindowTextW', stringType, [num('hwnd')]),
        fn('ShowWindow', booleanType, [num('hwnd'), num('command')]),
        fn('SetForegroundWindow', booleanType, [num('hwnd')]),
        fn('FlashWindow', booleanType, [num('hwnd'), bool('invert')]),
        fn('GetDpiForWindow', numberType, [num('hwnd')]),
        fn('GetDpiForSystem', numberType),
        fn('GetCursorPos', struct('Win32.POINT')),
        fn('SetCursorPos', booleanType, [num('x'), num('y')]),
        fn('GetWindowRect', struct('Win32.RECT'), [num('hwnd')]),
        fn('GetClientRect', struct('Win32.RECT'), [num('hwnd')]),
        fn('ClientToScreen', struct('Win32.POINT'), [num('hwnd'), param('point', struct('Win32.POINT'))]),
        fn('ScreenToClient', struct('Win32.POINT'), [num('hwnd'), param('point', struct('Win32.POINT'))]),
        fn('MoveWindow', booleanType, [num('hwnd'), num('x'), num('y'), num('width'), num('height'), bool('repaint')]),
        fn('SetWindowPos', booleanType, [num('hwnd'), num('insertAfter'), num('x'), num('y'), num('width'), num('height'), num('flags')]),
        fn('PostMessageW', booleanType, [num('hwnd'), num('message'), num('wParam'), num('lParam')]),
        fn('SendMessageW', numberType, [num('hwnd'), num('message'), num('wParam'), num('lParam')]),
        fn('GetKeyState', numberType, [num('virtualKey')]),
        fn('GetAsyncKeyState', numberType, [num('virtualKey')]),
        fn('GetDoubleClickTime', numberType),
        fn('GetSysColor', numberType, [num('index')]),
        fn('GetWindowThreadProcessId', numberType, [num('hwnd')]),
        fn('SetClipboardText', booleanType, [str('text')]),
        fn('GetClipboardText', stringType),
      ],
    },
    {
      name: 'Kernel32',
      functions: [
        fn('GetTickCount64', numberType),
        fn('Sleep', voidType, [num('milliseconds')]),
        fn('GetComputerNameW', stringType),
        fn('GetUserNameW', stringType),
        fn('GetEnvironmentVariableW', stringType, [str('name')]),
        fn('SetEnvironmentVariableW', booleanType, [str('name'), str('value')]),
        fn('GetCurrentProcessId', numberType),
        fn('GetCurrentThreadId', numberType),
        fn('GetLastError', numberType),
        fn('GetSystemDirectoryW', stringType),
        fn('GetWindowsDirectoryW', stringType),
        fn('GetTempPathW', stringType),
        fn('GetCommandLineW', stringType),
        fn('GetModuleFileNameW', stringType),
        fn('OutputDebugStringW', voidType, [str('text')]),
        fn('QueryPerformanceCounter', numberType),
        fn('QueryPerformanceFrequency', numberType),
        fn('GetLogicalProcessorCount', numberType),
        fn('GetPhysicalMemoryBytes', numberType),
        fn('GetAvailableMemoryBytes', numberType),
        fn('GetOsVersionString', stringType),
        fn('FileExistsW', booleanType, [str('path')]),
        fn('ReadTextFileW', stringType, [str('path')]),
        fn('WriteTextFileW', booleanType, [str('path'), str('contents')]),
        fn('CreateDirectoryW', booleanType, [str('path')]),
        fn('DeleteFileW', booleanType, [str('path')]),
      ],
    },
    {
      name: 'Shell32',
      constants: [
        constant('SW_SHOWNORMAL', 1),
        constant('SW_SHOW', 5),
      ],
      functions: [
        fn('ShellExecuteW', numberType, [str('operation'), str('file'), str('parameters'), str('directory'), num('showCommand')]),
        fn('OpenUrl', booleanType, [str('url')]),
        fn('SHGetKnownFolderPath', stringType, [str('folder')]),
        fn('RevealInExplorer', booleanType, [str('path')]),
        fn('ShowNotification', booleanType, [str('title'), str('text')]),
      ],
    },
    {
      name: 'Dwmapi',
      constants: [
        constant('DWMWA_USE_IMMERSIVE_DARK_MODE', 20),
        constant('DWMWA_WINDOW_CORNER_PREFERENCE', 33),
        constant('DWMWA_BORDER_COLOR', 34),
        constant('DWMWA_CAPTION_COLOR', 35),
        constant('DWMWA_TEXT_COLOR', 36),
        constant('DWMWA_SYSTEMBACKDROP_TYPE', 38),
        constant('DWMSBT_AUTO', 0),
        constant('DWMSBT_NONE', 1),
        constant('DWMSBT_MAINWINDOW', 2),
        constant('DWMSBT_TRANSIENTWINDOW', 3),
        constant('DWMSBT_TABBEDWINDOW', 4),
        constant('DWMWCP_DEFAULT', 0),
        constant('DWMWCP_DONOTROUND', 1),
        constant('DWMWCP_ROUND', 2),
        constant('DWMWCP_ROUNDSMALL', 3),
      ],
      functions: [
        fn('DwmSetWindowAttribute', numberType, [num('hwnd'), num('attribute'), num('value')]),
        fn('DwmGetColorizationColor', numberType),
        fn('DwmIsCompositionEnabled', booleanType),
        fn('DwmFlush', numberType),
      ],
    },
    {
      // The control object model the Win32 target implements over real child
      // windows. Names carry a `Win` prefix the way AppKit's carry `NS`, so a
      // program that mixes these with Gea's own elements never has two things
      // called `Button`.
      name: 'Controls',
      constants: [
        constant('OrientationHorizontal', 0),
        constant('OrientationVertical', 1),
        constant('AlignmentLeading', 0),
        constant('AlignmentCenter', 1),
        constant('AlignmentTrailing', 2),
        constant('AlignmentFill', 3),
        constant('DistributionFill', 0),
        constant('DistributionFillEqually', 1),
        constant('DistributionEqualSpacing', 2),
        constant('DistributionGravityAreas', 3),
        constant('TextAlignmentLeft', 0),
        constant('TextAlignmentCenter', 1),
        constant('TextAlignmentRight', 2),
        constant('LineBreakWordWrap', 0),
        constant('LineBreakTruncateTail', 1),
        constant('LineBreakClip', 2),
        constant('FontWeightLight', 300),
        constant('FontWeightRegular', 400),
        constant('FontWeightMedium', 500),
        constant('FontWeightSemibold', 600),
        constant('FontWeightBold', 700),
        constant('PaneRoleSidebar', 0),
        constant('PaneRoleList', 1),
        constant('PaneRoleDetail', 2),
        constant('ContentModeScaleToFill', 0),
        constant('ContentModeAspectFit', 1),
        constant('ContentModeAspectFill', 2),
        constant('ContentModeCenter', 3),
      ],
      functions: [
        fn('installRootView', voidType, [param('view', Controls('WinView'))]),
        fn('installToolbar', voidType, [param('toolbar', Controls('WinToolbar'))]),
        fn('setWindowTitle', voidType, [str('title')]),
        fn('setWindowAppearance', voidType, [str('appearance')]),
        fn('setWindowBackgroundColor', voidType, [param('color', Controls('WinColor'))]),
        fn('setWindowSize', voidType, [num('width'), num('height')]),
        fn('mainWindowHandle', numberType),
        fn('runDeviceCommand', stringType, [str('command')]),
        fn('requestFrame', voidType),
        fn('quit', voidType),
      ],
      classes: [
        {
          name: 'WinCallback',
          methods: [staticMethod('create', Controls('WinCallback'), [param('handler', voidCallbackType)]), method('invoke', voidType)],
        },
        {
          name: 'WinColor',
          methods: [
            staticMethod('rgb', Controls('WinColor'), [num('red'), num('green'), num('blue')]),
            staticMethod('rgba', Controls('WinColor'), [num('red'), num('green'), num('blue'), num('alpha')]),
            staticMethod('fromHex', Controls('WinColor'), [str('hex')]),
            staticMethod('clear', Controls('WinColor')),
            staticMethod('windowBackground', Controls('WinColor')),
            staticMethod('controlBackground', Controls('WinColor')),
            staticMethod('label', Controls('WinColor')),
            staticMethod('secondaryLabel', Controls('WinColor')),
            staticMethod('tertiaryLabel', Controls('WinColor')),
            staticMethod('separator', Controls('WinColor')),
            staticMethod('accent', Controls('WinColor')),
            staticMethod('systemBlue', Controls('WinColor')),
            staticMethod('systemYellow', Controls('WinColor')),
            staticMethod('systemRed', Controls('WinColor')),
            staticMethod('systemGreen', Controls('WinColor')),
          ],
          properties: [readonlyProp('red', numberType), readonlyProp('green', numberType), readonlyProp('blue', numberType), readonlyProp('alpha', numberType)],
        },
        {
          name: 'WinFont',
          methods: [
            staticMethod('system', Controls('WinFont'), [num('size')]),
            staticMethod('systemWeight', Controls('WinFont'), [num('size'), num('weight')]),
            staticMethod('named', Controls('WinFont'), [str('family'), num('size'), num('weight')]),
            staticMethod('monospace', Controls('WinFont'), [num('size')]),
          ],
          properties: [readonlyProp('size', numberType), readonlyProp('family', stringType), readonlyProp('weight', numberType)],
        },
        {
          name: 'WinImage',
          methods: [
            staticMethod('symbol', Controls('WinImage'), [str('name')]),
            staticMethod('fromFile', Controls('WinImage'), [str('path')]),
          ],
          properties: [readonlyProp('width', numberType), readonlyProp('height', numberType)],
        },
        {
          name: 'WinView',
          construct: {},
          properties: [
            prop('hidden', booleanType),
            prop('backgroundColor', Controls('WinColor')),
            prop('cornerRadius', numberType),
            prop('borderWidth', numberType),
            prop('borderColor', Controls('WinColor')),
            prop('tag', stringType),
            prop('tooltip', stringType),
            readonlyProp('x', numberType),
            readonlyProp('y', numberType),
            readonlyProp('width', numberType),
            readonlyProp('height', numberType),
            readonlyProp('handle', numberType),
            readonlyProp('superview', Controls('WinView')),
          ],
          methods: [
            method('addSubview', voidType, [param('child', Controls('WinView'))]),
            method('removeFromSuperview', voidType),
            method('setFrame', voidType, [num('x'), num('y'), num('width'), num('height')]),
            method('setSize', voidType, [num('width'), num('height')]),
            method('setMinimumSize', voidType, [num('width'), num('height')]),
            method('setIntrinsicSize', voidType, [num('width'), num('height')]),
            method('anchorFill', voidType, [num('inset')]),
            method('anchorEdges', voidType, [num('leading'), num('top'), num('trailing'), num('bottom')]),
            method('onClick', voidType, [param('callback', Controls('WinCallback'))]),
            method('setNeedsLayout', voidType),
            method('setNeedsDisplay', voidType),
          ],
        },
        {
          name: 'WinStackView',
          extends: 'Controls.WinView',
          construct: {},
          properties: [
            prop('orientation', numberType),
            prop('spacing', numberType),
            prop('alignment', numberType),
            prop('distribution', numberType),
            prop('detachesHiddenViews', booleanType),
            readonlyProp('arrangedSubviewCount', numberType),
          ],
          methods: [
            method('addArrangedSubview', voidType, [param('view', Controls('WinView'))]),
            method('insertArrangedSubviewAtIndex', voidType, [param('view', Controls('WinView')), num('index')]),
            method('removeArrangedSubview', voidType, [param('view', Controls('WinView'))]),
            method('setPadding', voidType, [num('top'), num('leading'), num('bottom'), num('trailing')]),
          ],
        },
        {
          name: 'WinLabel',
          extends: 'Controls.WinView',
          construct: {},
          properties: [
            prop('text', stringType),
            prop('font', Controls('WinFont')),
            prop('textColor', Controls('WinColor')),
            prop('alignment', numberType),
            prop('maximumNumberOfLines', numberType),
            prop('lineBreakMode', numberType),
            prop('selectable', booleanType),
          ],
        },
        {
          name: 'WinTextField',
          extends: 'Controls.WinView',
          construct: {},
          properties: [
            prop('text', stringType),
            prop('placeholder', stringType),
            prop('font', Controls('WinFont')),
            prop('textColor', Controls('WinColor')),
            prop('alignment', numberType),
            prop('editable', booleanType),
            prop('bordered', booleanType),
          ],
          methods: [
            method('setOnChange', voidType, [param('callback', Controls('WinCallback'))]),
            method('setOnSubmit', voidType, [param('callback', Controls('WinCallback'))]),
            method('focus', voidType),
            method('selectAll', voidType),
          ],
        },
        {
          name: 'WinTextView',
          extends: 'Controls.WinView',
          construct: {},
          properties: [
            prop('text', stringType),
            prop('font', Controls('WinFont')),
            prop('textColor', Controls('WinColor')),
            prop('editable', booleanType),
            prop('drawsBackground', booleanType),
          ],
          methods: [method('setOnChange', voidType, [param('callback', Controls('WinCallback'))]), method('focus', voidType)],
        },
        {
          name: 'WinButton',
          extends: 'Controls.WinView',
          construct: {},
          properties: [prop('title', stringType), prop('font', Controls('WinFont')), prop('enabled', booleanType), prop('image', Controls('WinImage'))],
        },
        {
          name: 'WinCheckBox',
          extends: 'Controls.WinView',
          construct: {},
          properties: [prop('title', stringType), prop('checked', booleanType)],
          methods: [method('setOnChange', voidType, [param('callback', Controls('WinCallback'))])],
        },
        {
          name: 'WinSlider',
          extends: 'Controls.WinView',
          construct: {},
          properties: [prop('minimum', numberType), prop('maximum', numberType), prop('value', numberType)],
          methods: [method('setOnChange', voidType, [param('callback', Controls('WinCallback'))])],
        },
        {
          name: 'WinProgressBar',
          extends: 'Controls.WinView',
          construct: {},
          properties: [prop('minimum', numberType), prop('maximum', numberType), prop('value', numberType), prop('indeterminate', booleanType)],
        },
        {
          name: 'WinImageView',
          extends: 'Controls.WinView',
          construct: {},
          properties: [prop('image', Controls('WinImage')), prop('tintColor', Controls('WinColor')), prop('contentMode', numberType)],
        },
        {
          name: 'WinScrollView',
          extends: 'Controls.WinView',
          construct: {},
          properties: [
            prop('documentView', Controls('WinView')),
            prop('drawsBackground', booleanType),
            prop('hasVerticalScroller', booleanType),
            prop('hasHorizontalScroller', booleanType),
            readonlyProp('scrollTop', numberType),
          ],
          methods: [method('scrollToTop', voidType), method('scrollTo', voidType, [num('y')])],
        },
        {
          name: 'WinBox',
          extends: 'Controls.WinView',
          construct: {},
          properties: [prop('fillColor', Controls('WinColor')), prop('contentView', Controls('WinView'))],
          methods: [method('setContentInsets', voidType, [num('top'), num('leading'), num('bottom'), num('trailing')])],
        },
        {
          name: 'WinSplitView',
          extends: 'Controls.WinView',
          construct: {},
          properties: [prop('dividerColor', Controls('WinColor')), prop('dividerWidth', numberType)],
          methods: [
            method('addPane', voidType, [param('view', Controls('WinView')), num('role'), num('minimumThickness'), num('maximumThickness'), bool('canCollapse')]),
            method('toggleSidebar', voidType),
            method('setPaneThickness', voidType, [num('index'), num('thickness')]),
          ],
        },
        {
          name: 'WinToolbar',
          construct: {},
          properties: [prop('height', numberType), prop('backgroundColor', Controls('WinColor'))],
          methods: [
            method('addItem', voidType, [str('symbol'), str('label'), param('action', Controls('WinCallback'))]),
            method('addSidebarToggle', voidType, [param('splitView', Controls('WinSplitView'))]),
            method('addSpace', voidType),
            method('addSearchField', voidType, [str('placeholder'), param('onChange', Controls('WinCallback'))]),
            method('searchText', stringType),
          ],
        },
      ],
    },
  ],
}

// --- naming ------------------------------------------------------------------

function splitQualifiedName(name: string, fallbackLibrary: string): [string, string] {
  const dot = name.indexOf('.')
  if (dot < 0) return [fallbackLibrary, name]
  return [name.slice(0, dot), name.slice(dot + 1)]
}

function shortTypeName(name: string): string {
  return splitQualifiedName(name, '')[1]
}

function qualifiedName(library: string, name: string): string {
  return `${library}.${name}`
}

export function cppTypeName(library: string, name: string): string {
  return `gea::windows::${library}::${name}`
}

function bridgeFunctionName(library: string, name: string): string {
  return `gea::windows::${library}::${name}`
}

function constructorThunk(library: string, className: string): string {
  return bridgeFunctionName(library, `${className}_create`)
}

function methodThunk(library: string, className: string, name: string): string {
  return bridgeFunctionName(library, `${className}_${name}`)
}

function propertyThunk(library: string, className: string, property: string, kind: 'get' | 'set'): string {
  return bridgeFunctionName(library, `${className}_${kind}_${property}`)
}

// --- TypeScript declarations -------------------------------------------------

export function generateWindowsDeclarations(sdk: WindowsSdkDefinition): Record<string, string> {
  const declarations: Record<string, string> = {}
  for (const library of sdk.libraries) {
    declarations[`@geastack/windows/${library.name}`] = declarationForLibrary(library)
  }
  return declarations
}

function declarationForLibrary(library: WindowsLibraryDefinition): string {
  const lines: string[] = []
  for (const [module, imports] of Object.entries(library.imports ?? {})) {
    lines.push(`import type { ${imports.join(', ')} } from '@geastack/windows/${module}'`)
  }
  if (lines.length > 0) lines.push('')
  for (const entry of library.constants ?? []) {
    lines.push(`export declare const ${entry.name}: ${typeScriptType(entry.type, library.name)}`)
  }
  if ((library.constants ?? []).length > 0) lines.push('')
  for (const entry of library.functions ?? []) {
    lines.push(`export declare function ${entry.name}(${parametersSignature(entry.parameters ?? [], library.name)}): ${typeScriptType(entry.returns, library.name)}`)
  }
  if ((library.functions ?? []).length > 0) lines.push('')
  for (const entry of library.structs ?? []) {
    lines.push(`export interface ${entry.name} {`)
    for (const field of entry.fields) lines.push(`  ${field.name}: ${typeScriptType(field.type, library.name)}`)
    lines.push('}', '')
  }
  for (const entry of library.classes ?? []) {
    const base = entry.extends ? ` extends ${shortTypeName(entry.extends)}` : ''
    lines.push(`export declare class ${entry.name}${base} {`)
    if (entry.construct) {
      lines.push(`  constructor(${parametersSignature(entry.construct.parameters ?? [], library.name)})`)
      // JSX over a control (`<WinStackView spacing={8}/>`) is checked against
      // the control's own properties: the element is the instance it builds.
      // Declaration-only; no program reads `props` and the compiler lowers
      // the element to construction before the checker sees it.
      lines.push('  readonly props: Partial<this>')
    } else {
      // No `new`: a program reaches the class through its statics only.
      lines.push('  private constructor()')
    }
    for (const property of entry.properties ?? []) {
      lines.push(`  ${property.readonly ? 'readonly ' : ''}${property.name}: ${typeScriptType(property.type, library.name)}`)
    }
    for (const entryMethod of entry.methods ?? []) {
      lines.push(
        `  ${entryMethod.static ? 'static ' : ''}${entryMethod.name}(${parametersSignature(entryMethod.parameters ?? [], library.name)}): ${typeScriptType(entryMethod.returns, library.name)}`,
      )
    }
    lines.push('}', '')
  }
  return `${lines.join('\n').trimEnd()}\n`
}

function parametersSignature(parameters: WindowsParameterDefinition[], localLibrary: string): string {
  return parameters.map((entry) => `${entry.name}: ${typeScriptType(entry.type, localLibrary)}`).join(', ')
}

function typeScriptType(type: WindowsTypeReference, localLibrary: string): string {
  const base = (() => {
    if (type.kind === 'function') {
      return `(${parametersSignature(type.parameters ?? [], localLibrary)}) => ${typeScriptType(type.returns, localLibrary)}`
    }
    if (type.kind === 'array') return `${typeScriptType(type.element, localLibrary)}[]`
    if (type.kind === 'primitive') return type.name
    return shortTypeName(splitQualifiedName(type.name, localLibrary).join('.'))
  })()
  return type.nullable && base !== 'void' ? `${base} | null` : base
}

// --- runtime stubs -----------------------------------------------------------

export function generateWindowsRuntimeModules(sdk: WindowsSdkDefinition): Record<string, string> {
  const modules: Record<string, string> = {}
  for (const library of sdk.libraries) {
    modules[`@geastack/windows/${library.name}`] = runtimeModuleForLibrary(library)
  }
  return modules
}

function runtimeModuleForLibrary(library: WindowsLibraryDefinition): string {
  if ((library.constants ?? []).length === 0 && (library.functions ?? []).length === 0 && (library.classes ?? []).length === 0) {
    return 'export {}\n'
  }
  const nativeOnly = `geaWindows${library.name}NativeOnly`
  const lines = [
    `function ${nativeOnly}(name) {`,
    `  throw new Error(\`@geastack/windows/${library.name} \${name} is native-only and must be lowered by @geastack/geatsc-plugin-windows-native.\`)`,
    '}',
    '',
  ]
  for (const entry of library.constants ?? []) lines.push(`export const ${entry.name} = ${JSON.stringify(entry.value)}`)
  if ((library.constants ?? []).length > 0) lines.push('')
  for (const entry of library.functions ?? []) {
    lines.push(`export function ${entry.name}() {`, `  return ${nativeOnly}(${JSON.stringify(entry.name)})`, '}', '')
  }
  for (const entry of library.classes ?? []) {
    lines.push(`export class ${entry.name} {`)
    if (entry.construct) {
      lines.push(`  constructor() {`, `    ${nativeOnly}(${JSON.stringify(`new ${entry.name}`)})`, '  }')
    }
    for (const entryMethod of entry.methods ?? []) {
      lines.push(`  ${entryMethod.static ? 'static ' : ''}${entryMethod.name}() {`)
      lines.push(`    return ${nativeOnly}(${JSON.stringify(`${entry.name}.${entryMethod.name}`)})`)
      lines.push('  }')
    }
    lines.push('}', '')
  }
  return `${lines.join('\n').trimEnd()}\n`
}

// --- bridge metadata ---------------------------------------------------------

export function generateWindowsBridgeMetadata(sdk: WindowsSdkDefinition): WindowsBridgeMetadata {
  const metadata: WindowsBridgeMetadata = {
    libraries: sdk.libraries.map((library) => ({ name: library.name })),
    constants: {},
    functions: {},
    classes: {},
    structs: {},
  }
  for (const library of sdk.libraries) {
    for (const entry of library.constants ?? []) {
      metadata.constants[qualifiedName(library.name, entry.name)] = { library: library.name, name: entry.name, type: entry.type, value: entry.value }
    }
    for (const entry of library.functions ?? []) {
      metadata.functions[qualifiedName(library.name, entry.name)] = {
        library: library.name,
        name: entry.name,
        thunk: bridgeFunctionName(library.name, entry.name),
        returns: entry.returns,
        parameters: entry.parameters ?? [],
        ...(entry.inline ? { inline: entry.inline } : {}),
      }
    }
    for (const entry of library.structs ?? []) {
      metadata.structs[qualifiedName(library.name, entry.name)] = {
        library: library.name,
        name: entry.name,
        wrapper: cppTypeName(library.name, entry.name),
        fields: entry.fields,
      }
    }
    for (const entry of library.classes ?? []) {
      metadata.classes[qualifiedName(library.name, entry.name)] = {
        library: library.name,
        name: entry.name,
        ...(entry.extends ? { extends: entry.extends } : {}),
        wrapper: cppTypeName(library.name, entry.name),
        ...(entry.construct
          ? { constructor: { thunk: constructorThunk(library.name, entry.name), parameters: entry.construct.parameters ?? [] } }
          : {}),
        methods: Object.fromEntries(
          (entry.methods ?? []).map((entryMethod) => [
            entryMethod.name,
            {
              name: entryMethod.name,
              thunk: methodThunk(library.name, entry.name, entryMethod.name),
              returns: entryMethod.returns,
              parameters: entryMethod.parameters ?? [],
              ...(entryMethod.static ? { static: true } : {}),
            } satisfies WindowsBridgeMethodMetadata,
          ]),
        ),
        properties: Object.fromEntries(
          (entry.properties ?? []).map((property) => [
            property.name,
            {
              name: property.name,
              getter: propertyThunk(library.name, entry.name, property.name, 'get'),
              ...(property.readonly ? {} : { setter: propertyThunk(library.name, entry.name, property.name, 'set') }),
              type: property.type,
            } satisfies WindowsBridgePropertyMetadata,
          ]),
        ),
      }
    }
  }
  return metadata
}

// --- C++ types ---------------------------------------------------------------

export function cppType(type: WindowsTypeReference, localLibrary: string, preferLocalNames = false): string {
  if (type.kind === 'function') {
    const returns = cppType(type.returns, localLibrary, preferLocalNames)
    const params = (type.parameters ?? []).map((entry) => cppType(entry.type, localLibrary, preferLocalNames)).join(', ')
    return `std::function<${returns}(${params})>`
  }
  if (type.kind === 'array') return `std::vector<${cppType(type.element, localLibrary, preferLocalNames)}>`
  if (type.kind === 'primitive') {
    if (type.name === 'void') return 'void'
    if (type.name === 'boolean') return 'bool'
    if (type.name === 'number') return 'double'
    return 'std::string'
  }
  const [library, name] = splitQualifiedName(type.name, localLibrary)
  if (preferLocalNames && library === localLibrary) return name
  return cppTypeName(library, name)
}

function cppParameters(parameters: WindowsParameterDefinition[], localLibrary: string): string {
  return parameters.map((entry) => `${cppType(entry.type, localLibrary)} ${entry.name}`).join(', ')
}

function defaultInitializer(type: WindowsTypeReference): string {
  if (type.kind !== 'primitive') return ''
  if (type.name === 'number') return ' = 0'
  if (type.name === 'boolean') return ' = false'
  return ''
}

// --- native bridge header ----------------------------------------------------

/**
 * The C++ contract between generated programs and the Win32 target.
 *
 * Every wrapper struct carries a `double handle` into the target's object
 * table; a base class shares the handle storage of its root (the same layout
 * the Apple bridge uses, so upcasts are free and a boxed value round-trips
 * through `__gea_to_value`). Every function, method, property and constructor
 * the SDK states becomes one free-function prototype under
 * `gea::windows::<Library>`, which the target's `native/*.cpp` files define.
 * Structs are plain aggregates. Functions the SDK marks `inline` are defined
 * here in full.
 */
export function generateWindowsNativeBridgeHeader(metadata: WindowsBridgeMetadata): string {
  const lines: string[] = [
    '#pragma once',
    '// Generated by @geastack/windows -- the native bridge contract. Do not edit.',
    '',
    '#include <cstdint>',
    '#include <functional>',
    '#include <string>',
    '#include <type_traits>',
    '#include <vector>',
    '',
    '// The engine umbrella, declared the way the gea compiler plugin declares it:',
    '// GEA_HOST_DECLARED tells the compiler runtime header that the real host',
    '// types are present (instead of its stand-in handles). The compiler prelude',
    '// registers the gea user-agent stylesheet whether or not a program has gea',
    '// components; a windows-native program has none, so nothing else would pull',
    '// the engine headers in.',
    '#ifndef GEA_HOST_DECLARED',
    '#define GEA_HOST_DECLARED 1',
    '#include "gea/embedded.h"',
    '#endif',
    '',
    'struct gea_cpp_value;',
    '',
    'namespace gea::windows::handles {',
    '// Object table: a retained native object by handle. `object` returns',
    '// nullptr for 0 or an unknown handle; `release` drops the table entry.',
    'void *object(double handle);',
    'double retainRaw(void *object, void (*destroy)(void *));',
    'void release(double handle);',
    'template <typename __GeaT> double value_handle(const __GeaT &value) {',
    '  if constexpr (std::is_same_v<std::decay_t<__GeaT>, gea_cpp_value>) return static_cast<double>(value);',
    '  else return value.handle;',
    '}',
    '}',
    '',
    'namespace gea::windows::text {',
    'std::wstring toWide(const std::string &utf8);',
    'std::string fromWide(const std::wstring &wide);',
    'std::string fromWide(const wchar_t *wide);',
    '}',
    '',
  ]
  for (const library of metadata.libraries) {
    const structs = Object.values(metadata.structs).filter((entry) => entry.library === library.name)
    const classes = Object.values(metadata.classes).filter((entry) => entry.library === library.name)
    if (structs.length === 0 && classes.length === 0) continue
    lines.push(`namespace gea::windows::${library.name} {`)
    for (const entry of structs) {
      lines.push(`struct ${entry.name} {`)
      for (const field of entry.fields) {
        lines.push(`  ${cppType(field.type, library.name, true)} ${field.name}${defaultInitializer(field.type)};`)
      }
      lines.push('};', '')
    }
    for (const entry of classes) lines.push(...classDeclaration(entry, library.name))
    lines.push('}', '')
  }
  // Thunk prototypes, after every wrapper is complete so parameter types resolve.
  for (const library of metadata.libraries) {
    const declarations = bridgeDeclarationsForLibrary(metadata, library.name)
    if (declarations.length === 0) continue
    lines.push(`namespace gea::windows::${library.name} {`, ...declarations, '}', '')
  }
  return `${lines.join('\n').trimEnd()}\n`
}

function classDeclaration(entry: WindowsBridgeClassMetadata, library: string): string[] {
  const lines: string[] = []
  const base = entry.extends ? ` : ${cppType({ kind: 'class', name: entry.extends }, library)}` : ''
  lines.push(`struct ${entry.name}${base} {`)
  if (entry.extends) {
    const [, baseName] = splitQualifiedName(entry.extends, library)
    lines.push(`  using ${cppType({ kind: 'class', name: entry.extends }, library)}::${baseName};`)
    lines.push(`  ${entry.name}() = default;`)
    lines.push(`  explicit ${entry.name}(double rawHandle) : ${cppType({ kind: 'class', name: entry.extends }, library)}(rawHandle) {}`)
  } else {
    lines.push('  double handle = 0;')
    lines.push(`  ${entry.name}() = default;`)
    lines.push(`  explicit ${entry.name}(double rawHandle) : handle(rawHandle) {}`)
  }
  // A wrapper stored into a dynamic value carries its handle as the number, so
  // the generated program can hand it back to a thunk from a boxed slot.
  lines.push('  template <typename __GeaValue = gea_cpp_value> __GeaValue __gea_to_value() const { return __GeaValue(static_cast<double>(handle)); }')
  lines.push('  explicit operator bool() const { return handle != 0; }')
  lines.push('};', '')
  return lines
}

function bridgeDeclarationsForLibrary(metadata: WindowsBridgeMetadata, library: string): string[] {
  const lines: string[] = []
  for (const entry of Object.values(metadata.functions)) {
    if (entry.library !== library) continue
    const signature = `${cppType(entry.returns, library)} ${bareName(entry.thunk)}(${cppParameters(entry.parameters, library)})`
    if (entry.inline) lines.push(`inline ${signature} { ${entry.inline} }`)
    else lines.push(`${signature};`)
  }
  for (const entry of Object.values(metadata.classes)) {
    if (entry.library !== library) continue
    const construct = ownConstructor(entry)
    if (construct) lines.push(`double ${bareName(construct.thunk)}(${cppParameters(construct.parameters, library)});`)
    for (const entryMethod of Object.values(entry.methods)) {
      const params = entryMethod.static
        ? cppParameters(entryMethod.parameters, library)
        : [`${entry.wrapper} self`, cppParameters(entryMethod.parameters, library)].filter(Boolean).join(', ')
      lines.push(`${cppType(entryMethod.returns, library)} ${bareName(entryMethod.thunk)}(${params});`)
    }
    for (const property of Object.values(entry.properties)) {
      lines.push(`${cppType(property.type, library)} ${bareName(property.getter)}(${entry.wrapper} self);`)
      if (property.setter) lines.push(`void ${bareName(property.setter)}(${entry.wrapper} self, ${cppType(property.type, library)} value);`)
    }
  }
  return lines
}

// `constructor` is inherited from Object.prototype on any plain object, so a
// class that states none answers with a Function; own-property or nothing.
function ownConstructor(entry: WindowsBridgeClassMetadata): WindowsBridgeClassMetadata['constructor'] | undefined {
  return Object.prototype.hasOwnProperty.call(entry, 'constructor') ? entry.constructor : undefined
}

function bareName(thunk: string): string {
  const separator = thunk.lastIndexOf('::')
  return separator < 0 ? thunk : thunk.slice(separator + 2)
}

// --- native bridge support source -------------------------------------------

/**
 * The generic half of the bridge the target links: the handle table and the
 * UTF-8/UTF-16 conversions every thunk needs. Written beside the header so an
 * app's generated directory is self-contained; the per-class thunks stay in
 * the target, which is the only place that knows what an HWND is.
 */
export function generateWindowsNativeBridgeSource(): string {
  return [
    '// Generated by @geastack/windows -- handle table + text conversion. Do not edit.',
    '#include "gea/windows/native_bridge.h"',
    '',
    '#include <mutex>',
    '#include <unordered_map>',
    '',
    '#ifndef NOMINMAX',
    '#define NOMINMAX',
    '#endif',
    '#include <windows.h>',
    '',
    'namespace gea::windows::handles {',
    'namespace {',
    'struct Entry {',
    '  void *object = nullptr;',
    '  void (*destroy)(void *) = nullptr;',
    '};',
    'std::mutex &tableMutex() { static std::mutex mutex; return mutex; }',
    'std::unordered_map<std::uint64_t, Entry> &table() { static std::unordered_map<std::uint64_t, Entry> entries; return entries; }',
    'std::uint64_t &nextHandle() { static std::uint64_t next = 1; return next; }',
    '}  // namespace',
    '',
    'void *object(double handle) {',
    '  if (!(handle > 0)) return nullptr;',
    '  std::lock_guard<std::mutex> lock(tableMutex());',
    '  const auto found = table().find(static_cast<std::uint64_t>(handle));',
    '  return found == table().end() ? nullptr : found->second.object;',
    '}',
    '',
    'double retainRaw(void *object, void (*destroy)(void *)) {',
    '  if (!object) return 0;',
    '  std::lock_guard<std::mutex> lock(tableMutex());',
    '  const std::uint64_t handle = nextHandle()++;',
    '  table()[handle] = Entry{object, destroy};',
    '  return static_cast<double>(handle);',
    '}',
    '',
    'void release(double handle) {',
    '  if (!(handle > 0)) return;',
    '  Entry entry;',
    '  {',
    '    std::lock_guard<std::mutex> lock(tableMutex());',
    '    const auto found = table().find(static_cast<std::uint64_t>(handle));',
    '    if (found == table().end()) return;',
    '    entry = found->second;',
    '    table().erase(found);',
    '  }',
    '  if (entry.destroy) entry.destroy(entry.object);',
    '}',
    '}  // namespace gea::windows::handles',
    '',
    'namespace gea::windows::text {',
    'std::wstring toWide(const std::string &utf8) {',
    '  if (utf8.empty()) return std::wstring();',
    '  const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);',
    '  if (needed <= 0) return std::wstring();',
    '  std::wstring wide(static_cast<std::size_t>(needed), L\'\\0\');',
    '  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed);',
    '  return wide;',
    '}',
    '',
    'std::string fromWide(const wchar_t *wide) {',
    '  if (!wide || !*wide) return std::string();',
    '  const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);',
    '  if (needed <= 1) return std::string();',
    '  std::string utf8(static_cast<std::size_t>(needed - 1), \'\\0\');',
    '  WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), needed, nullptr, nullptr);',
    '  return utf8;',
    '}',
    '',
    'std::string fromWide(const std::wstring &wide) { return fromWide(wide.c_str()); }',
    '}  // namespace gea::windows::text',
    '',
  ].join('\n')
}
