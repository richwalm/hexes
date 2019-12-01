/*
	Hexes Terminal Library
	Windows console handling functions.

	The Windows console is quite limited and performance is subpar.
	The larger the terminal size, the slower its speed.
	Any cursor optimization we do on *unix is wasted here. Pity.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <windows.h>
#include <stdio.h>

#include "hexes.h"

static UINT OldCP;
static HANDLE ConsoleHandle, ConsoleInputHandle;
static DWORD InitialMode, CurrentMode;
static CHAR_INFO *WinConsoleBuffer;

static MOUSE_EVENT_RECORD MouseEvent;

extern HexBuffer *Buffer, *Current;
extern int Width, Height;
extern char *Damage;
extern int HasDamage;

int IsSameChar(HexChar *A, HexChar *B);
int GetU8Size(const unsigned char *Char);
void HexClipCursor(int *X, int *Y);
int ResizeBuffers();

int GetTerminalSize(int *W, int *H)
{
	CONSOLE_SCREEN_BUFFER_INFO ConsoleInfo;

	if (!GetConsoleScreenBufferInfo(ConsoleHandle, &ConsoleInfo))
		return 0;

	*W = ConsoleInfo.srWindow.Right - ConsoleInfo.srWindow.Left + 1;
	*H = ConsoleInfo.srWindow.Bottom  - ConsoleInfo.srWindow.Top + 1;

	return 1;
}

/* We'll actually enable UTF-8 on Windows, rather than just test for it. */
int IsUnicodeSupported()
{
	if (!SetConsoleOutputCP(CP_UTF8))
		return 0;

	return 1;
}

int ColorsSupported()
{
	return HEX_COL_OFFSET_256 - 1;
}

void HexSetTitle(const char *Title, const char *Icon)
{
	if (Title)
		/* According to MSDN, will use the set code page here, so no conversion needed. */
		SetConsoleTitleA(Title);

	/* TODO: Bit of a hack, but we can change the Window icon with this;
	https://support.microsoft.com/en-us/help/124103/how-to-obtain-a-console-window-handle-hwnd
	*/

	return;
}

static int MoveCursor(int X, int Y)
{
	COORD Pos;
	Pos.X = X; Pos.Y = Y;

	if (!SetConsoleCursorPosition(ConsoleHandle, Pos))
		return 0;

	return 1;
}

static void ResetConsole()
{
	COORD Pos;
	DWORD Length, Written;
	WORD Attr;

	Length = Width * Height;
	Pos.X = 0; Pos.Y = 0;
	
	FillConsoleOutputCharacterA(ConsoleHandle, ' ', Length, Pos, &Written);

	Attr = FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED;
	FillConsoleOutputAttribute(ConsoleHandle, Attr, Length, Pos, &Written);

	MoveCursor(0, 0);

	return;
}

static int HexRawInputMode(int Enable)
{
	if (!Enable) {
		if (!SetConsoleMode(ConsoleInputHandle, CurrentMode))
			return 0;
	} else {
		DWORD Mode;

		Mode = CurrentMode;
		Mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);

		if (!SetConsoleMode(ConsoleInputHandle, Mode))
			return 0;
	}

	return 1;
}

static int GetColorAttrs(unsigned int Color, unsigned int Default)
{
	const int Colors[] = {
		0,
		FOREGROUND_RED,
		FOREGROUND_GREEN,
		FOREGROUND_RED | FOREGROUND_GREEN,
		FOREGROUND_BLUE,
		FOREGROUND_RED | FOREGROUND_BLUE,
		FOREGROUND_GREEN | FOREGROUND_BLUE,
		FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
	};
	int Attr;

	if (Color > (HEX_COL_OFFSET_256 - 1) || !Color)
		Color = Default;
	else
		Color--;

	Attr = Colors[Color % 8];

	if (Color >= 8)
		Attr |= FOREGROUND_INTENSITY;

	return Attr;
}

static int GetAttributes(HexChar *Char)
{
	int Attr;
	Attr = GetColorAttrs(Char->FG, 7);
	Attr |= (GetColorAttrs(Char->BG, 0) << 4);

	if (Char->Attr & HEX_ATTR_UNDERLINE)
		Attr |= COMMON_LVB_UNDERSCORE;
	if (Char->Attr & HEX_ATTR_INVERSE)
		Attr |= COMMON_LVB_REVERSE_VIDEO;

	return Attr;
}

static void HexCharToWinConsole(CHAR_INFO *C, HexChar *HC)
{
	/* It appears that the Windows' console is limited to a single UTF-16, two bytes. */
	if (*HC->CP)
		if (*HC->CP < 0x80)
			C->Char.UnicodeChar = *HC->CP;
		else
			MultiByteToWideChar(CP_UTF8, 0, HC->CP, GetU8Size((const unsigned char *)HC->CP), &C->Char.UnicodeChar, sizeof(WCHAR));
	else
		C->Char.UnicodeChar = L' ';

	C->Attributes = GetAttributes(HC);

	return;
}

static int ResizeWinConsoleBuffer()
{
	size_t Total;
	CHAR_INFO *B;
	int I;

	Total = Width * Height;

	B = realloc(WinConsoleBuffer, Total * sizeof(CHAR_INFO));
	if (!B)
		return 0;

	if (WinConsoleBuffer) {
		HexChar *CD;

		CD = Current->Data;
		for (I = 0; I < Total; I++)
			HexCharToWinConsole(&B[I], &CD[I]);
	} else {
		const CHAR_INFO C = { { L' ' }, 0 };

		for (I = 0; I < Total; I++)
			memcpy(&B[I], &C, sizeof(CHAR_INFO));
	}

	WinConsoleBuffer = B;

	return 1;
}

int InitSub(int Stage)
{
	switch (Stage) {
		/* Basic display. */
		case 0:
			ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
			ConsoleInputHandle = GetStdHandle(STD_INPUT_HANDLE);
			if (ConsoleHandle == INVALID_HANDLE_VALUE || ConsoleInputHandle == INVALID_HANDLE_VALUE)
				return HEX_ERROR_GENERIC;

			if (!GetConsoleMode(ConsoleHandle, &InitialMode))
				return HEX_ERROR_GENERIC;
			CurrentMode = InitialMode | ENABLE_WINDOW_INPUT;

			OldCP = GetConsoleOutputCP();

		/* Detections and quirks, but not used in Windows. */
		case 1:
			break;

		/* Start input and create a Windows console buffer. */
		case 2:
			WinConsoleBuffer = NULL;
			if (!ResizeWinConsoleBuffer()) {
				SetConsoleOutputCP(OldCP);
				return HEX_ERROR_MEMORY;
			}
			if (!HexRawInputMode(1)) {
				SetConsoleOutputCP(OldCP);
				free(WinConsoleBuffer);
				return HEX_ERROR_GENERIC;
			}
			break;

		/* Final stage. Create a new screen. */
		case 3:
			ResetConsole();
			break;

		/* Cleanup for when errors occur outside of this function. */
		case -3:
			SetConsoleOutputCP(OldCP);
			free(WinConsoleBuffer);
		case -2:
		case -1:
			break;
	}

	return HEX_ERROR_NONE;
}

void FreeSub()
{
	ResetConsole();
	SetConsoleMode(ConsoleInputHandle, InitialMode);
	free(WinConsoleBuffer);
	SetConsoleOutputCP(OldCP);
	return;
}

int HexFlush(int CurX, int CurY)
{
	int I;
	const size_t Total = Width * Height;
	char *D = Damage;
	HexChar *B = Current->Data, *BD = Buffer->Data;

	for (I = 0; I < Total; I++) {
		if (*D) {
			*D = 0;

			if (IsSameChar(&B[I], &BD[I])) {
				D++;
				continue;
			}

			HexCharToWinConsole(&WinConsoleBuffer[I], &BD[I]);

			B[I] = BD[I];
		}
		D++;
	}

	HasDamage = 0;

	if (!HexFullFlush(0, CurX, CurY))
		return 0;
	return 1;
}

int HexFullFlush(int UseBuffer, int CurX, int CurY)
{
	if (!UseBuffer) {
		COORD Size = { Width, Height };
		const COORD Coord = { 0, 0 };
		SMALL_RECT Region;

		Region.Left = 0;
		Region.Top = 0;
		Region.Right = Width;
		Region.Bottom = Height;

		if (!WriteConsoleOutputW(ConsoleHandle, WinConsoleBuffer, Size, Coord, &Region))
			return 0;
	} else {
		memset(Damage, 0, Width * Height);
		HasDamage = 0;
		if (!HexFlush(CurX, CurY))
			return 0;
	}

	if (CurX >= 0) {
		HexClipCursor(&CurX, &CurY);
		MoveCursor(CurX, CurY);
	}

	return 1;
}

/* TODO */
int HexChangeFlags(int *NewFlags)
{
	return 0;
}

static int NewTimeout(DWORD Start, DWORD Timeout)
{
	DWORD CurrentTime, Change, Milliseconds;

	/* Recalculate the timeout. */
	CurrentTime = GetTickCount();
	if (Start > CurrentTime)	/* Overflow. */
		Change = (UINT_MAX - Start) + CurrentTime;
	else
		Change = CurrentTime - Start;

	if (Timeout - Change > 0)
		Milliseconds = Timeout - Change;
	else
		Milliseconds = 0;

	return Milliseconds;
}

int HexGetChar(int Timeout, int *Mods)
{
	DWORD Start, Milliseconds;

	Milliseconds = Timeout == -1 ? INFINITE : Timeout;

	Start = 0;	/* Prevents a GCC warning. */
	if (Timeout > 0)
		Start = GetTickCount();

	do {
		INPUT_RECORD InputRecord;
		DWORD Amount, Return;

		Return = WaitForSingleObject(ConsoleInputHandle, Milliseconds);
		if (Return == WAIT_FAILED)
			return HEX_CHAR_ERROR;

		if (Return == WAIT_TIMEOUT)
			return HEX_CHAR_EOF;

		if (!ReadConsoleInput(ConsoleInputHandle, &InputRecord, 1, &Amount))
			return HEX_CHAR_ERROR;

		switch (InputRecord.EventType) {
			case KEY_EVENT:
				/* We don't do anything on release events. So we'll try again. */
				if (!InputRecord.Event.KeyEvent.bKeyDown) {
					if (Timeout > 0)
						Milliseconds = NewTimeout(Start, Timeout);
					continue;
				}

				if (Mods) {
					int Mod = 0;
					if (InputRecord.Event.KeyEvent.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
						Mod = HEX_MOD_ALT;
					if (InputRecord.Event.KeyEvent.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
						Mod |= HEX_MOD_CTRL;
					if (InputRecord.Event.KeyEvent.dwControlKeyState & SHIFT_PRESSED)
						Mod |= HEX_MOD_SHIFT;
					*Mods = Mod;
				}
				
				/* From here; https://msdn.microsoft.com/en-us/library/windows/desktop/dd375731(v=vs.85).aspx */
				switch (InputRecord.Event.KeyEvent.wVirtualKeyCode) {
					case VK_BACK:
						return '\x7F';
					case VK_ESCAPE:
						return HEX_CHAR_ESCAPE;
					case VK_TAB:
						return '\t';
					case VK_RETURN:
						return '\n';

					case VK_UP:
						return HEX_CHAR_UP;
					case VK_DOWN:
						return HEX_CHAR_DOWN;
					case VK_LEFT:
						return HEX_CHAR_LEFT;
					case VK_RIGHT:
						return HEX_CHAR_RIGHT;

					case VK_HOME:
						return HEX_CHAR_HOME;
					case VK_END:
						return HEX_CHAR_END;
					case VK_INSERT:
						return HEX_CHAR_INSERT;
					case VK_DELETE:
						return HEX_CHAR_DELETE;

					case VK_PRIOR:
						return HEX_CHAR_PGUP;
					case VK_NEXT:
						return HEX_CHAR_PGDOWN;

					/* Don't track these alone. */
					case VK_MENU:
					case VK_CONTROL:
					case VK_SHIFT:
					case VK_CAPITAL:
					case VK_NUMLOCK:
					case VK_SCROLL:
					case VK_LAUNCH_APP1:
					case VK_LAUNCH_APP2:
						if (Timeout > 0)
							Milliseconds = NewTimeout(Start, Timeout);
						continue;
				}
				
				if (InputRecord.Event.KeyEvent.uChar.AsciiChar)
					return InputRecord.Event.KeyEvent.uChar.AsciiChar;
				break;

			case WINDOW_BUFFER_SIZE_EVENT:
				if (!ResizeBuffers() || !ResizeWinConsoleBuffer())
					return HEX_CHAR_ERROR;
				return HEX_CHAR_RESIZE;

			case MOUSE_EVENT:
				if (InputRecord.Event.MouseEvent.dwEventFlags == MOUSE_HWHEELED) {	/* We won't report these. */
					if (Timeout > 0)
						Milliseconds = NewTimeout(Start, Timeout);
					continue;
				}
				MouseEvent = InputRecord.Event.MouseEvent;
				return HEX_CHAR_MOUSE;
		}

	} while (1);

	return HEX_CHAR_UNKNOWN;
}

/* Not a thing on Windows. */
const unsigned char *HexGetRawKey(size_t *Size)
{
	return NULL;
}

int HexPushChar(const char *Data, size_t Size)
{
	INPUT_RECORD Record;
	unsigned int I;

	Record.EventType = KEY_EVENT;
	Record.Event.KeyEvent.bKeyDown = TRUE;
	Record.Event.KeyEvent.wRepeatCount = 1;
	Record.Event.KeyEvent.wVirtualScanCode = 0;	/* Not used by us. */

	for (I = 0; I < Size; I++) {
		DWORD Return;
		SHORT KeyScan;
		BYTE ShiftState;

		KeyScan = VkKeyScanA(Data[I]);
		if (LOBYTE(KeyScan) == -1)
			continue;

		Record.Event.KeyEvent.uChar.AsciiChar = Data[I];
		Record.Event.KeyEvent.wVirtualKeyCode = LOBYTE(KeyScan);

		ShiftState = HIBYTE(KeyScan);
		Record.Event.KeyEvent.dwControlKeyState = 0;
		if (ShiftState & 1)
			Record.Event.KeyEvent.dwControlKeyState = SHIFT_PRESSED;
		if (ShiftState & 2)
			Record.Event.KeyEvent.dwControlKeyState |= LEFT_CTRL_PRESSED;
		if (ShiftState & 4)
			Record.Event.KeyEvent.dwControlKeyState |= LEFT_ALT_PRESSED;

		if (!WriteConsoleInput(ConsoleInputHandle, &Record, 1, &Return))
			return 0;
	}

	return 1;
}

/* TODO */
int HexGetMouse(HexMouseEvent *ME)
{
	return 0;
}

/* TODO */
int HexEnableMouse(int Type)
{
	return 0;
}

/* TODO */
int HexResize(int *W, int *H)
{
	return 0;
}
