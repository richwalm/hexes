#ifndef HEXES_H
#define HEXES_H

/* Colors */

/* We'll expose this to the programmer as they may have a better idea of the actual value, allowing them to set it. */
extern int HexColors;

#define HEX_TRUECOLOR	(256 * 256 * 256)

typedef enum HexColorsOffset {
	HEX_COL_OFFSET_256 = 17,
	HEX_COL_OFFSET_TRUE = 273
} HexColorsOffset;

#define HEX_COL_256(C)		(HEX_COL_OFFSET_256 + (C))
#define HEX_COL_TRUE(R, G, B)	(HEX_COL_OFFSET_TRUE + ((R) << 16) + ((G) << 8) + (B))

#define HEX_MAX_ATTRIBUTES	8
typedef enum HexAttributes {
	HEX_ATTR_NORMAL = 0,
	HEX_ATTR_BOLD = 1,
	HEX_ATTR_FAINT = 2,
	HEX_ATTR_ITALIC = 4,
	HEX_ATTR_UNDERLINE = 8,
	HEX_ATTR_BLINK = 16,
	HEX_ATTR_INVERSE = 32,
	HEX_ATTR_INVISIBLE = 64,
	HEX_ATTR_CROSSED = 128
} HexAttributes;

int HexWidth();
int HexHeight();
int HexUnicode();

/* Buffers. */
#define	UTF8_MAX_BYTES	4
typedef struct HexChar {
	char CP[UTF8_MAX_BYTES];
	unsigned int FG, BG, Attr;
} HexChar;

/* The following does nothing at present, but could be useful if we extend HexChar. */
#define HEX_SET_CHAR(CP, FG, BG, Attr) { CP, FG, BG, Attr }

#define HEX_DEFAULT_TAB_STOP	4
typedef struct HexBuffer {
	int W, H;
	int X, Y;		/* Cursor */
	unsigned int FG, BG;	/* Colors. */
	unsigned int Attr;
	unsigned char TabStop;
	HexChar *Data;
} HexBuffer;

HexBuffer *HexNewBuffer(int W, int H);
HexBuffer *HexResizeBuffer(HexBuffer *Original, int W, int H);
void HexFreeBuffer(HexBuffer *Buffer);
const HexChar *HexGetHexChar(HexBuffer *D, int X, int Y);

typedef enum HexFlags {
	HEX_FLAG_DISPLAY_NO_CURSOR = 1,
	HEX_FLAG_DISPLAY_REVERSE_VIDEO = 2,
	HEX_FLAG_DISPLAY_BRIGHT_CURSOR = 4,
	HEX_FLAG_EVENT_FOCUS = 8
} HexFlags;

int HexChangeFlags(int *Flags);

/* Init. */
typedef enum HexInitFlags {
	HEX_INIT_NO_UNICODE_TEST = 1,
	HEX_INIT_NO_COLOR_TEST = 2,
	HEX_INIT_NEEDS_UNICODE = 4,
	HEX_INIT_FORCE_UNICODE = 8
} HexInitFlags;

typedef enum HexErrors {
	HEX_ERROR_NONE,
	HEX_ERROR_GENERIC,
	HEX_ERROR_SIGNAL,
	HEX_ERROR_MEMORY,
	HEX_ERROR_SIZE,
	HEX_ERROR_INPUT,
	HEX_ERROR_UNICODE,
	HEX_ERROR_TOO_SMALL
} HexErrors;

int HexInit(int MinW, int MinH, int Flags);
HexBuffer *HexGetTerminalBuffer();
void HexSetTitle(const char *Title, const char *Icon);
int HexResize(int *W, int *H);
void HexFree();

/* Drawing. */
void HexLocate(HexBuffer *B, int X, int Y);
void HexColor(HexBuffer *B, int FG, int BG);
void HexAttr(HexBuffer *B, unsigned int Attr);
void HexTabStop(HexBuffer *B, unsigned int TabStop);
int HexPutChar(HexBuffer *B, const char *CP);
int HexPrint(HexBuffer *B, const char *String, size_t Length);

typedef enum HexDrawFlags {
	HEX_DRAW_CP = 1,
	HEX_DRAW_FG = 2,
	HEX_DRAW_BG = 4,
	HEX_DRAW_ATTR = 8,
	HEX_DRAW_TRANSPARENT = 16
} HexDrawFlags;

void HexBlitRaw(const HexBuffer *S, HexBuffer *D, int SX, int SY, int DW, int DY, int W, int H, unsigned int Flags);
void HexBlit(const HexBuffer *S, HexBuffer *D, int SX, int SY, int DW, int DY, int W, int H, unsigned int Flags);
void HexFillRaw(HexBuffer *D, int DX, int DY, int W, int H, const HexChar *Char, unsigned int Flags);
void HexFill(HexBuffer *D, int DX, int DY, int W, int H, const HexChar *Char, unsigned int Flags);
void HexPutHexCharOffset(HexBuffer *D, unsigned int DOffset, const HexChar *Char);
void HexPutHexChar(HexBuffer *D, int X, int Y, const HexChar *Char);

/* Flushing. */
int HexFlush(int CurX, int CurY);
int HexFullFlush(int UseBuffer, int CurX, int CurY);

/* Input. We try to copy curses codes where possible, for ease of porting. */
typedef enum HexChars {
	HEX_CHAR_ERROR = -1,
	HEX_CHAR_ESCAPE = '\x1B',

	HEX_CHAR_DOWN = 0402,
	HEX_CHAR_UP,
	HEX_CHAR_LEFT,
	HEX_CHAR_RIGHT,
	HEX_CHAR_HOME,

	HEX_CHAR_F0 = 0410,

	HEX_CHAR_A1 = 0534,
	HEX_CHAR_A3,
	HEX_CHAR_B2,
	HEX_CHAR_C1,
	HEX_CHAR_C3 = 0540,

	HEX_CHAR_END = 0550,

	HEX_CHAR_DELETE = 0512,
	HEX_CHAR_INSERT,

	HEX_CHAR_PGDOWN = 0522,	
	HEX_CHAR_PGUP,

	HEX_CHAR_MOUSE = 0631,
	HEX_CHAR_RESIZE,

	/* No curses equivalents. */
	HEX_CHAR_FOCUS_IN = -6,
	HEX_CHAR_FOCUS_OUT,

	HEX_CHAR_RESTORE,
	HEX_CHAR_UNKNOWN,
	HEX_CHAR_EOF
} HexChars;
#define	HEX_CHAR_F(X)	(HEX_CHAR_F0 + (X))

typedef enum HexCharMods {
	HEX_MOD_SHIFT = 1,
	HEX_MOD_ALT = 2,
	HEX_MOD_CTRL = 4,
	HEX_MOD_MOTION = 8,
	HEX_MOD_RELEASE = 16,
	HEX_MOD_UNRELIABLE = 32
} HexCharMods;

typedef struct HexMouseEvent {
	int Button;
	int Mod;
	int X, Y;
} HexMouseEvent;

int HexGetChar(int Timeout, int *Mods);
int HexPushChar(const char *Data, size_t Size);
const unsigned char *HexGetRawKey(size_t *Size);

/* Mouse. */
typedef enum HexMouseStates {
	HEX_MOUSE_NONE,
	HEX_MOUSE_NORMAL,	/* 9 */
	HEX_MOUSE_RELEASE,	/* 1000 */
	HEX_MOUSE_DRAG,		/* 1002 */
	HEX_MOUSE_ALL		/* 1003 */
} HexMouseStates;
int HexGetMouse(HexMouseEvent *ME);
int HexEnableMouse(int Type);

#endif
