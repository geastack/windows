export declare function MakePoint(x: number, y: number): POINT
export declare function MakeSize(cx: number, cy: number): SIZE
export declare function MakeRect(left: number, top: number, right: number, bottom: number): RECT
export declare function RectWidth(rect: RECT): number
export declare function RectHeight(rect: RECT): number

export interface POINT {
  x: number
  y: number
}

export interface SIZE {
  cx: number
  cy: number
}

export interface RECT {
  left: number
  top: number
  right: number
  bottom: number
}
