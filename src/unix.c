/*
	Hexes Terminal Library
	Unix handling functions. Uses ANSI.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <stdio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>

#include "hexes.h"

#define GetOffset(X, Y, W) ((Y) * (W) + (X))
#define ESC	"\x1B"
#define DETECT_TIMEOUT_MS		100
#define	BUF_BYTES_PER_CELL		64

extern HexBuffer *Buffer, *Current;
extern int Width, Height;
extern char *Damage;
extern int HasDamage;
int Flags;

static size_t OutputBufferSize;

static int OnRightEdge;

static struct sigaction OldResizeHandler, OldContinueHandler;

volatile sig_atomic_t GotResizeSignal, GotContinueSignal;

enum TermQuirks {
	QUIRK_LINE_CODES = 1,
	QUIRK_ABS_COL_CODE = 2,
	QUIRK_WRAPPING_FIX = 4,
	QUIRK_CURSOR_POS_CODE = 8,
	QUIRK_NO_DEFAULT_COLORS_CODES = 16
} TermQuirks;
static int Quirks;

int ExtendOutputBuffer();
int ResizeBuffers();
int IsSameChar(const HexChar *A, const HexChar *B);
void HexClipCursor(int *X, int *Y);

int InitInput(int Stage);
void FreeInput();
int ContinueInputHandler();
int GetCharTimeout(int Timeout);

/* We don't use all of it. */
enum UnixHints {
	HINT_BOOL_CA_NOT_RESTORE = 24,	/* nrrmc */

	HINT_INT_COLUMNS = 0,		/* cols */
	HINT_INT_LINES = 2,		/* lines */
	HINT_INT_MAX_COLORS = 13,	/* colors */

	HINT_STR_ENTER_CA_MODE = 28,	/* smcup */
	HINT_STR_EXIT_CA_MODE = 40,	/* rmcup */

	HINT_STR_CURSOR_INVISIBLE = 13,	/* civis */
	HINT_STR_CURSOR_NORMAL = 16,	/* cnorm */
	HINT_STR_CURSOR_VISIBLE = 20	/* cvvis */
} UnixHints;
int GetTermInfoBool(unsigned int N);
int GetTermInfoInt(unsigned int N);
const char *GetTermInfoString(unsigned int N);
int LocateAndLoadTermInfo();
void FreeHints();

int GetTerminalSize(int *W, int *H)
{
	struct winsize Size;
	int X, Y;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &Size) != -1) {
		*W = Size.ws_col;
		*H = Size.ws_row;
		return 1;
	}

	X = GetTermInfoInt(HINT_INT_COLUMNS);
	Y = GetTermInfoInt(HINT_INT_LINES);
	if (X > 0 && Y > 0) {
		*W = X;
		*H = Y;
		return 1;
	}

	return 0;
}

static int WaitForReply(int EndChar, char *Buffer, size_t Size)
{
	unsigned int I = 0;
	int Char;

	do {
		Char = GetCharTimeout(DETECT_TIMEOUT_MS);
		if (Char == HEX_CHAR_ERROR)
			return -1;
		if (Char == HEX_CHAR_EOF)
			break;

		if (Buffer && I < Size - 1) {
			Buffer[I] = Char;
			I++;
		}
		if (Char == EndChar)
			break;
	} while (1);

	if (Buffer)
		Buffer[I] = '\0';
	
	/* Timeout reached. */
	if (Char == HEX_CHAR_EOF)
		return 1;

	return 0;
}

static int ParseCursorPosition(const char *B, int *DX, int *DY)
{
	char *C;
	int X, Y;

	C = strchr(B, '[');
	if (!C)
		return 0;
	C++;

	Y = atoi(C);
	if (Y <= 0)
		return 0;
	Y--;
	C++;

	C = strchr(C, ';');
	if (!C)
		return 0;
	C++;

	X = atoi(C);
	if (X <= 0)
		return 0;
	X--;

	*DX = X; *DY = Y;

	return 1;
}

static int DetectQuirks()
{
	const char *TermEnv;
	int Return;
	char Buf[16];
	int X, Y, NewY;

	Quirks = 0;
	
	/* Noticed that NetBSD's VT won't support the default color attribute. No way to detect this, so we'll have to hardcode. */
	TermEnv = getenv("TERM");
	if (TermEnv && strcasecmp("VT100", TermEnv) == 0)
		Quirks |= QUIRK_NO_DEFAULT_COLORS_CODES;

	/* Minimal size needed for these tests. */
	if (Width < 3 || Height < 2)
		return 0;

	/* Can we get the position of the cursor? If not, unable to continue these tests. */
	printf(ESC "[6n");
	fflush(stdout);
	Return = WaitForReply('R', Buf, sizeof(Buf));
	if (Return)
		return Return; /* We appear to be running on a potato. */

	Quirks |= QUIRK_CURSOR_POS_CODE;

	/* CHA */
	if (!ParseCursorPosition(Buf, &X, &Y))
		return -1;
	if (!X)
		printf(ESC "[C");
	printf(ESC "[G" ESC "[6n");
	fflush(stdout);
	Return = WaitForReply('R', Buf, sizeof(Buf));
	if (Return)
		return Return;

	if (!ParseCursorPosition(Buf, &X, &Y))
		return -1;
	if (!X) {
		Quirks |= QUIRK_ABS_COL_CODE;
		printf(ESC "[C");
	}

	/* CNL & CPL. We'll presume it has both if either works. */
	printf(ESC "[%c" ESC "[6n", Y ? 'E' : 'F');
	fflush(stdout);
	Return = WaitForReply('R', Buf, sizeof(Buf));
	if (Return)
		return Return;

	NewY = Y ? Y-- : Y++;

	if (!ParseCursorPosition(Buf, &X, &Y))
		return -1;

	if (!X && Y == NewY)
		Quirks |= QUIRK_LINE_CODES;

	/* Wrapping fix. Can OnRightEdge be cleared with a single instruction. */
	printf(ESC "[?7h" ESC "[%dC " ESC "[C " ESC "[6n\r", Width);
	fflush(stdout);
	Return = WaitForReply('R', Buf, sizeof(Buf));
	if (Return)
		return Return;

	if (!ParseCursorPosition(Buf, &X, &Y))
		return -1;

	if (X != Width - 1)
		Quirks |= QUIRK_WRAPPING_FIX;

	return 1;
}

/* Put a Unicode character and check how far the cursor moves. Only reliable way to check. */
int IsUnicodeSupported()
{
	int Return;
	char Buf[16];
	int X, Y;

	if (!(Quirks & QUIRK_CURSOR_POS_CODE) || Width < 3)
		return -1;

	printf("\r\xe2\x98\x83" ESC "[6n" ESC "[1K\r");
	fflush(stdout);

	Return = WaitForReply('R', Buf, sizeof(Buf));
	if (Return)
		return Return;

	if (!ParseCursorPosition(Buf, &X, &Y))
		return -1;

	if (X == 1)
		return 1;

	return 0;
}

static int WaitForXTermColorReply(int Color)
{
	/* Was using an '\a' to terminate the string here but NetBSD's VT (VT100) doesn't understand it. */
	printf(ESC "]4;%d;?" ESC "\\", Color);
	fflush(stdout);

	return WaitForReply('\\', NULL, 0);
}

/* Detects possible 256 colors using the xterm extension. Dog slow on GNOME Terminal so we'll use a binary search. */
static int DetectColors()
{
	int Return, Min, Max;

	Return = WaitForXTermColorReply(0);
	if (Return == -1)
		return -1;

	if (Return)		/* Not supported. */
		return 0;

	Min = 0; Max = UCHAR_MAX;
	do {
		int I;

		I = (Min + Max) / 2;
		Return = WaitForXTermColorReply(I);
		if (Return == -1)
			return -1;

		Return ? (Max = I) : (Min = I);
	} while ((Min + 1) < Max);

	return Max + HEX_COL_OFFSET_256;
}

/* This isn't reliable as detecting Unicode, however most terminals will simply discard unsupported color codes. */
int ColorsSupported()
{
	const char *TrueColorEnv;
	int Return;

	TrueColorEnv = getenv("COLORTERM");
	if (TrueColorEnv) {
		/* Detection method from here; https://gist.github.com/XVilka/8346728#detection */
		if (strcmp(TrueColorEnv, "truecolor") == 0 || strcmp(TrueColorEnv, "24bit") == 0)
			return HEX_TRUECOLOR;
	}

	Return = DetectColors();
	if (Return > 0)
		return Return;

	/* As a fallback, we'll simply try to obtain it from terminfo. */
	Return = GetTermInfoInt(HINT_INT_MAX_COLORS);
	if (Return > 0)
		return Return + HEX_COL_OFFSET_256;

	return 0;
}

void HexSetTitle(const char *Title, const char *Icon)
{
	if (Title)
		printf(ESC "]2;%s" ESC "\\", Title);
	if (Icon)
		printf(ESC "]1;%s" ESC "\\", Icon);

	return;
}

static void ContinueEvent(int Sig)
{
	GotContinueSignal = 1;
	return;
}

static void ResizeEvent(int Sig)
{
	GotResizeSignal = 1;
	return;
}

int InitSub(int Stage)
{
	struct sigaction NewHandler;
	const char *Hint;
	int NewFlags;

	switch (Stage) {
		/* Basic display. */
		case 0:
			LocateAndLoadTermInfo();

			GotResizeSignal = 0;

			memset(&NewHandler, 0, sizeof(NewHandler));
			NewHandler.sa_handler = ResizeEvent;
			sigemptyset(&NewHandler.sa_mask);
			if (sigaction(SIGWINCH, &NewHandler, &OldResizeHandler) == -1)
				return HEX_ERROR_SIGNAL;

			break;

		/* Start terminal detections. Need to read stdin for this. */
		case 1:
			if (!InitInput(0)) {
				sigaction(SIGWINCH, &OldResizeHandler, NULL);
				FreeHints();
				return HEX_ERROR_INPUT;
			}

			DetectQuirks();

			break;

		/* Finish detections. Start input. */
		case 2:
			if (!InitInput(1)) {
				sigaction(SIGWINCH, &OldResizeHandler, NULL);
				FreeHints();
				return HEX_ERROR_INPUT;
			}

			GotContinueSignal = 0;

			/* Needed to restore terminal to a proper state. */
			memset(&NewHandler, 0, sizeof(NewHandler));
			NewHandler.sa_handler = ContinueEvent;
			sigemptyset(&NewHandler.sa_mask);
			if (sigaction(SIGCONT, &NewHandler, &OldContinueHandler) == -1) {
				InitInput(-1);
				sigaction(SIGWINCH, &OldResizeHandler, NULL);
				FreeHints();
				return HEX_ERROR_SIGNAL;
			}

			break;

		/* Final stage. Create a new screen. */
		case 3:
			Hint = GetTermInfoString(HINT_STR_ENTER_CA_MODE);
			if (Hint)
				printf("%s" ESC "[H", Hint);
			else
				printf(ESC "c");

			OutputBufferSize = 0;
			ExtendOutputBuffer();

			Flags = ~(Flags & 0);
			NewFlags = 0;
			HexChangeFlags(&NewFlags);

			OnRightEdge = 0;

			fflush(stdout);

			break;

		/* Cleanup for when errors occur outside of this function. */
		case -3:
			sigaction(SIGCONT, &OldContinueHandler, NULL);
		case -2:
			InitInput(-1);
		case -1:
			sigaction(SIGWINCH, &OldResizeHandler, NULL);
			FreeHints();
			break;
	}

	return HEX_ERROR_NONE;
}

void FreeSub()
{
	const char *Hint;
	int NewFlags;

	Flags = ~(Flags & 0);
	NewFlags = 0;
	HexChangeFlags(&NewFlags);

	sigaction(SIGWINCH, &OldResizeHandler, NULL);
	sigaction(SIGCONT, &OldContinueHandler, NULL);

	Hint = GetTermInfoString(HINT_STR_EXIT_CA_MODE);
	if (Hint)
		printf("%s", Hint);

	if (!Hint || GetTermInfoBool(HINT_BOOL_CA_NOT_RESTORE))
		printf(ESC "c");

	fflush(stdout);

	FreeInput();
	FreeHints();

	return;
}

/* Returns 1 if the bold attribute should be enabled. */
static int GetColorCode(char *Output, unsigned int Color, int IsFG)
{
	int Code, NeedsBold = 0;

	/* Standard colors. */
	if (Color < HEX_COL_OFFSET_256) {
		if (IsFG) {
			if (Color <= 8) {
				if (!Color)
					/* These two quirk workarounds can still trigger a cursor change when it's not needed,
					   but probably not worth the CPU usage fixing this correctly. */
					Color = (Quirks & QUIRK_NO_DEFAULT_COLORS_CODES) ? 8 : 10;
				Code = Color + 29;
			} else {
				Code = Color + 21;
				NeedsBold = 1;
			}
		} else {
			if (Color <= 8) {
				if (!Color)
					Color = (Quirks & QUIRK_NO_DEFAULT_COLORS_CODES) ? 1 : 10;
				Code = Color + 39;
			} else
				Code = Color + 91;
		}

		sprintf(Output, "%d", Code);
	}
	/* 256-colors. */
	else if (Color < HEX_COL_OFFSET_TRUE) {
		Code = Color - HEX_COL_OFFSET_256;
		sprintf(Output, "%c8;5;%d", IsFG ? '3' : '4', Code);
	/* True color. */
	} else {
		int R, G, B;

		Code = Color - HEX_COL_OFFSET_TRUE;
		R = (Code >> 16) & ((1 << 8) - 1);
		G = (Code >> 8) & ((1 << 8) - 1);
		B = Code & ((1 << 8) - 1);

		sprintf(Output, "%c8;2;%d;%d;%d", IsFG ? '3' : '4', R, G, B);
	}

	return NeedsBold;
}

static void GetAttributeCode(char *Output, unsigned int Attribute, int Unset)
{
	int Code;

	Code = ffs(Attribute);
	if (Code > 5)	/* Skip rapid blink, which we don't support. Neither do most terminals. */
		Code++;

	if (Unset)
		Code += 20;

	sprintf(Output, "%d", Code);
	return;
}

/* Primary function to change output format style. Works out the difference between the current and requested cursor. */
static int ChangeCursor(int FG, int BG, unsigned int Attributes)
{
	char EscapeString[64] = ESC "[", *Char = &EscapeString[2];
	int I, Color, CurrentColor;

	/* Colors first, so we can force bold if required. */
	for (I = 0, CurrentColor = Current->FG, Color = FG; I < 2; I++, CurrentColor = Current->BG, Color = BG) {
		if (Color == CurrentColor)
			continue;

		if (GetColorCode(Char, Color, !I))
			Attributes |= HEX_ATTR_BOLD;
		Char = strchr(Char, '\0');
		*Char = ';';
		Char++;
	}

	/* Attributes. */
	if (Attributes != Current->Attr) {
		int Diff, A;

		Diff = Attributes ^ Current->Attr;

		/* Xterm doesn't support the bold off code, instead makes it underline.
		Instead, we'll unset faint & bold which shouldn't be together anyway. */
		if (Diff & HEX_ATTR_BOLD && Current->Attr & HEX_ATTR_BOLD) {
			Diff &= ~HEX_ATTR_BOLD;
			Diff |= HEX_ATTR_FAINT;
			Current->Attr |= HEX_ATTR_FAINT;
		}

		A = 1;
		for (I = 0; I < HEX_MAX_ATTRIBUTES; I++) {

			if (A & Diff) {
				GetAttributeCode(Char, A, A & Current->Attr);
				Char = strchr(Char, '\0');
				*Char = ';';
				Char++;
			}

			A <<= 1;
		}

	}

	Char[-1] = 'm';	/* Replace the previous semicolon. */
	Char[0] = '\0';

	printf("%s", EscapeString);
	Current->FG = FG; Current->BG = BG; Current->Attr = Attributes;

	return 1;
}

static int NeedsCursorChange(HexChar *Char)
{
	unsigned int FG, Attributes;

	FG = Char->FG;
	Attributes = Char->Attr;

	if (FG >= 8 && FG < HEX_COL_OFFSET_256)
		Attributes |= HEX_ATTR_BOLD;

	if (FG != Current->FG ||
		Char->BG != Current->BG ||
		Attributes != Current->Attr)
		return 1;

	return 0;
}

/* A NewFlags of 0 will reset to the default terminal settings. */
int HexChangeFlags(int *NewFlags)
{
	int Diff;
	const char *Hint;
	
	if (!NewFlags)
		return Flags;

	Diff = *NewFlags ^ Flags;
	Flags = *NewFlags;

	Hint = NULL;
	if (Diff & HEX_FLAG_DISPLAY_NO_CURSOR) {
		if (Flags & HEX_FLAG_DISPLAY_NO_CURSOR) {
			Hint = GetTermInfoString(HINT_STR_CURSOR_INVISIBLE);
			if (!Hint)
				Hint = ESC "[?25l";
		} else {
			if (!(Flags & HEX_FLAG_DISPLAY_BRIGHT_CURSOR))
				Hint = GetTermInfoString(HINT_STR_CURSOR_NORMAL);
			else
				Hint = GetTermInfoString(HINT_STR_CURSOR_VISIBLE);
			if (!Hint)
				Hint = ESC "[?25h";
		}
	}
	else if (Diff & HEX_FLAG_DISPLAY_BRIGHT_CURSOR && !(Flags & HEX_FLAG_DISPLAY_NO_CURSOR))
		Hint = GetTermInfoString(HINT_STR_CURSOR_VISIBLE);
	if (Hint)
		printf("%s", Hint);

	if (Diff & HEX_FLAG_DISPLAY_REVERSE_VIDEO)
		/* I particularly dislike terminfo here, never exposing the individual codes for this feature.
		   Rather it has them contained in a single 'flash' code which could also be a beep on some terminals. */
		printf(ESC "[?5%c", (Flags & HEX_FLAG_DISPLAY_REVERSE_VIDEO) ? 'h' : 'l');

	if (Diff & HEX_FLAG_EVENT_FOCUS)
		printf(ESC "[?1004%c", (Flags & HEX_FLAG_EVENT_FOCUS) ? 'h' : 'l');

	return Flags;
}

static char *NS(char *Output, int Number)
{
	if (Number == 1)
		return "";

	sprintf(Output, "%d", Number);
	return Output;
}

/* Primary cursor move function. Aims for the most efficient way possible, in output size. X & Y begin at zero, unlike ANSI. */
static int MoveCursor(int X, int Y)
{
	char EscapeString[64] = ESC "[", *Char = &EscapeString[2];
	char N[24], N2[24];
	int CX = Current->X, CY = Current->Y, Change;

	/* CHA*, CUF & CUB */
	if (Y == CY) {
		if (X == CX)
			return 0;

		Change = X - CX;
		if (Quirks & QUIRK_ABS_COL_CODE && abs(Change) != 1 && X < 8)
			sprintf(Char, "%sG", NS(N, X + 1));
		else
			sprintf(Char, "%s%c", NS(N, abs(Change)), Change > 0 ? 'C' : 'D');
	} else if (!(Y || X)) {
		Char[0] = 'H';
		Char[1] = '\0';
	/* CNL & CPL */
	} else if (!X && Quirks & QUIRK_LINE_CODES) {
		Change = Y - CY;
		sprintf(Char, "%s%c", NS(N, abs(Change)), Change > 0 ? 'E' : 'F');
	/* CUU & CUD */
	} else if (X == CX) {
		if (OnRightEdge && Quirks & QUIRK_WRAPPING_FIX)
			printf(ESC "[D" ESC "[C");
		Change = Y - CY;
		sprintf(Char, "%s%c", NS(N, abs(Change)), Change > 0 ? 'B' : 'A');
	/* CUP* */
	} else
		sprintf(Char, "%s;%sH", NS(N, Y + 1), NS(N2, X + 1));

	printf("%s", EscapeString);
	OnRightEdge = 0;
	Current->X = X; Current->Y = Y;

	return 1;
}

static void UpdateOutputCursor()
{
	Current->X++;
	if (Current->X >= Width) {
		if (!OnRightEdge) {
			Current->X = Width - 1;
			OnRightEdge = 1;
			return;
		}
		OnRightEdge = 0;

		Current->X = 1;
		Current->Y++;
	}

	return;
}

static void Output(const char *CP)
{
	if (*CP)
		printf("%.*s", UTF8_MAX_BYTES, CP);
	else
		putchar(' ');
	return;
}

/* Core screen output function. */
int HexFlush(int CurX, int CurY)
{
	if (HasDamage) {
		unsigned int I;
		const size_t Total = Current->W * Current->H;
		char *D = Damage;
		HexChar *B = Current->Data, *BD = Buffer->Data;
		unsigned int Cursor;
		int X, Y, First = 1;

		Cursor = GetOffset(Current->X, Current->Y, Width);

		for (I = 0; I < Total; I++) {
			if (*D) {
				*D = 0;

				if (IsSameChar(&B[I], &BD[I])) {
					D++;
					continue;
				}

				if (Cursor != I) {
					X = I % Width;
					Y = I / Width;
					MoveCursor(X, Y);
				} else if (First && OnRightEdge) {
					/* If on edge, we'll need to send a NOOP move code in order for the cursor to remain in place. */
					if (Quirks & QUIRK_WRAPPING_FIX)
						printf(ESC "[D");
					printf(ESC "[C");
					OnRightEdge = 0;
				}
				First = 0;

				if (NeedsCursorChange(&BD[I]))
					ChangeCursor(BD[I].FG, BD[I].BG, BD[I].Attr);

				B[I] = BD[I];
				Output(BD[I].CP);
				UpdateOutputCursor();
				Cursor = I + 1;
			}
			D++;
		}

	}

	if (CurX >= 0) {
		HexClipCursor(&CurX, &CurY);
		MoveCursor(CurX, CurY);
	}

	HasDamage = 0;
	fflush(stdout);
	return 1;
}

/* Full redraw. Should be called after a resize or restore event. */
int HexFullFlush(int UseBuffer, int CurX, int CurY)
{
	unsigned int I;
	const size_t Total = Current->W * Current->H;
	HexChar *BD, *C;

	BD = UseBuffer ? Buffer->Data : Current->Data;
	C = BD;

	/* We can't be certain where the cursor is, so we'll just reset. */
	printf(ESC "[H");

	for (I = 0; I < Total; I++) {
		if (NeedsCursorChange(C))
			ChangeCursor(C->FG, C->BG, C->Attr);
		Output(C->CP);
		C++;
	}

	if (UseBuffer) {
		memcpy(Current->Data, BD, Total * sizeof(HexChar));
		memset(Damage, 0, Total);
		HasDamage = 0;
	}

	Current->X = Current->W - 1;
	Current->Y = Current->H - 1;

	if (CurX >= 0) {
		HexClipCursor(&CurX, &CurY);
		MoveCursor(CurX, CurY);
	}

	fflush(stdout);
	return 1;
}

/* There's no real way we can ensure that we're the right buffer size for a heavy screen flush, but this should be good enough. */
int ExtendOutputBuffer()
{
	size_t Size;

	Size = (Width * Height) * BUF_BYTES_PER_CELL;

	if (Size > OutputBufferSize) {
		if (setvbuf(stdout, NULL, _IOFBF, Size))
			return 0;
		OutputBufferSize = Size;
	}
	
	return 1;
}

/* Doesn't seem that we can rely on the terminal being in any state, so we'll reset all. */
int ContinueHandler()
{
	struct sigaction NewHandler;
	int PreviousFlags;
	const char *Hint;

	/* As we have no way of knowing if the terminal has been resized during the down time, we'll force one. */
	GotResizeSignal = 0;
	if (!ResizeBuffers())
		return 0;
	ExtendOutputBuffer();

	memset(&NewHandler, 0, sizeof(NewHandler));
	NewHandler.sa_handler = ContinueEvent;
	sigemptyset(&NewHandler.sa_mask);
	if (sigaction(SIGCONT, &NewHandler, NULL) == -1)
		return 0;
	NewHandler.sa_handler = ResizeEvent;
	if (sigaction(SIGWINCH, &NewHandler, NULL) == -1)
		return 0;

	ChangeCursor(0, 0, 0);

	Hint = GetTermInfoString(HINT_STR_ENTER_CA_MODE);
	if (Hint)
		printf("%s", Hint);

	PreviousFlags = Flags;
	Flags = ~Flags;
	HexChangeFlags(&PreviousFlags);
	
	if (!ContinueInputHandler())
		return 0;

	return 1;
}

/* No reliable way to ensure that the terminal resized, even to what the user wanted, so return the size as well. */
int HexResize(int *W, int *H)
{
	struct winsize Size;
	int Return;

	printf(ESC "[8;%d;%dt", *H, *W);
	fflush(stdout);

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &Size) == -1)
		return 0;

	Return = 0;
	if (Size.ws_col >= *W && Size.ws_row >= *H)
		Return = 1;

	*W = Size.ws_col;
	*H = Size.ws_row;

	return Return;
}
