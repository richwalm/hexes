/*
	Hexes Terminal Library
	Unix input handling functions.

	If loaded, this'll make some use of the terminfo data.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>

#ifdef linux
#include <sys/epoll.h>
#else
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#include "hexes.h"

#define ESC	"\x1B"

static struct termios TTYInitialState;
static int DefaultStdinFlags;

static int EFD;
#ifdef linux
static struct epoll_event Event;
#else
static struct kevent Event;
#endif

static unsigned char InputBuffer[64], *InputChar;
static size_t InputSize;
static int InputPending;	/* Will point to the FD that has pending data. -1 otherwise. */

static int UserPipe[2];

static unsigned char EscapeBuffer[128];
static size_t EscapeBufferSize;

static int MouseType;

extern int Flags;

extern volatile sig_atomic_t GotResizeSignal, GotContinueSignal;
int ContinueHandler();

int ExtendOutputBuffer();
int ResizeBuffers();

const char *GetTermInfoName();
const char *GetTermInfoString(unsigned int N);

enum UnixHints {
	HINT_STR_KEYPAD_LOCAL = 88,	/* smkx */
	HINT_STR_KEYPAD_XMIT = 89,	/* rmkx */

	HINT_STR_KEY_A1 = 139,
	HINT_STR_KEY_UP = 87,
	HINT_STR_KEY_A3 = 140,
	HINT_STR_KEY_LEFT = 79,
	HINT_STR_KEY_B2 = 141,
	HINT_STR_KEY_RIGHT = 83,
	HINT_STR_KEY_C1 = 142,
	HINT_STR_KEY_DOWN = 61,
	HINT_STR_KEY_C3 = 143,

	HINT_STR_KEY_NPAGE = 81,
	HINT_STR_KEY_PPAGE = 82,
	HINT_STR_KEY_HOME = 76,
	HINT_STR_KEY_END = 164,
	HINT_STR_KEY_INSERT = 77,
	HINT_STR_KEY_DELETE = 59,

	HINT_STR_KEY_ENTER = 165,

	HINT_STR_KEY_F1 = 66,
	HINT_STR_KEY_F2 = 68,
	HINT_STR_KEY_F3 = 69,
	HINT_STR_KEY_F4 = 70,
	HINT_STR_KEY_F5 = 71,
	HINT_STR_KEY_F6 = 72,
	HINT_STR_KEY_F7 = 73,
	HINT_STR_KEY_F8 = 74,
	HINT_STR_KEY_F9 = 75,
	HINT_STR_KEY_F10 = 67,
	HINT_STR_KEY_F11 = 216,
	HINT_STR_KEY_F12 = 217
};

#define KEY_HINTS	28
static int ValidKeys;

struct KeyHint {
	unsigned int Hint;
	int Key, Mod;
} KeyHint;

static const struct KeyHint KeyHints[KEY_HINTS] = {
	{ HINT_STR_KEY_A1, HEX_CHAR_A1, 0 },
	{ HINT_STR_KEY_UP, HEX_CHAR_UP, 0 },
	{ HINT_STR_KEY_A3, HEX_CHAR_A3, 0 },
	{ HINT_STR_KEY_LEFT, HEX_CHAR_LEFT, 0 },
	{ HINT_STR_KEY_B2, HEX_CHAR_B2, 0 },
	{ HINT_STR_KEY_RIGHT, HEX_CHAR_RIGHT, 0 },
	{ HINT_STR_KEY_C1, HEX_CHAR_C1, 0 },
	{ HINT_STR_KEY_DOWN, HEX_CHAR_DOWN, 0 },
	{ HINT_STR_KEY_C3, HEX_CHAR_C3, 0 },

	{ HINT_STR_KEY_PPAGE, HEX_CHAR_PGUP, 0 },
	{ HINT_STR_KEY_NPAGE, HEX_CHAR_PGDOWN, 0 },
	{ HINT_STR_KEY_HOME, HEX_CHAR_HOME, 0 },
	{ HINT_STR_KEY_END, HEX_CHAR_END, 0 },
	{ HINT_STR_KEY_INSERT, HEX_CHAR_INSERT, 0 },
	{ HINT_STR_KEY_DELETE, HEX_CHAR_DELETE, 0 },

	{ HINT_STR_KEY_ENTER, '\n', 0 },	/* On xterm, keypad. */

	{ HINT_STR_KEY_F1, HEX_CHAR_F(1), 0 },
	{ HINT_STR_KEY_F2, HEX_CHAR_F(2), 0 },
	{ HINT_STR_KEY_F3, HEX_CHAR_F(3), 0 },
	{ HINT_STR_KEY_F4, HEX_CHAR_F(4), 0 },
	{ HINT_STR_KEY_F5, HEX_CHAR_F(5), 0 },
	{ HINT_STR_KEY_F6, HEX_CHAR_F(6), 0 },
	{ HINT_STR_KEY_F7, HEX_CHAR_F(7), 0 },
	{ HINT_STR_KEY_F8, HEX_CHAR_F(8), 0 },
	{ HINT_STR_KEY_F9, HEX_CHAR_F(9), 0 },
	{ HINT_STR_KEY_F10, HEX_CHAR_F(10), 0 },
	{ HINT_STR_KEY_F11, HEX_CHAR_F(11), 0 },
	{ HINT_STR_KEY_F12, HEX_CHAR_F(12), 0 }
};

static struct Key {
	const struct KeyHint *Hint;
	const char *Code;
} Keys[KEY_HINTS];

static int HexRawInputMode(int Enable)
{
	if (!Enable) {
		if (tcsetattr(STDIN_FILENO, TCSANOW, &TTYInitialState) == -1)
			return 0;
	} else {
		struct termios State;

		State = TTYInitialState;
		State.c_lflag &= ~(ICANON | ECHO);
		State.c_cc[VMIN] = 1;

		if (tcsetattr(STDIN_FILENO, TCSANOW, &State) == -1)
			return 0;
	}

	return 1;
}

static int FillInputBuffer()
{
	ssize_t Return;

	do {
		Return = read(InputPending, &InputBuffer, sizeof(InputBuffer));
		if (Return == -1) {
			switch (errno) {
				case EAGAIN:
				#if EAGAIN != EWOULDBLOCK
				case EWOULDBLOCK:
				#endif
					InputPending = -1;
					return 0;
				default:
					return -1;
			}
		}
	} while (Return < 0);

	InputSize = Return;
	InputChar = InputBuffer;

	return 1;
}

/* Obtains only from FDs. */
static int GetChar(int NoEscape)
{
	int Return;

	if (!InputSize) {
		Return = FillInputBuffer();
		switch (Return) {
			case -1:
				return HEX_CHAR_ERROR;
			case 0:
				return HEX_CHAR_EOF;
		}
	}

	Return = *InputChar;
	if (NoEscape && Return == HEX_CHAR_ESCAPE)
		return HEX_CHAR_EOF;

	InputSize--;
	InputChar++;

	return Return;
}

/* Doesn't work for CTRL+Numbers across the top of keyboard. */
static void ConvertCTRLKey(int *Char, int *Mod)
{
	if ((unsigned)*Char <= 0x1F) {
		switch (*Char) {
			case '\t':
			case '\n':
			case '\r':
				return;
			case 0:
				*Char = '~';
				break;
			default:
				*Char += '@';
				break;
		}
		*Mod |= HEX_MOD_CTRL;
	}

	return;
}

static int LocateKey(const void *Key, const void *Element)
{
	const struct Key *ElemKey = Element;
	return strcmp(Key, ElemKey->Code);
}

const unsigned char *HexGetRawKey(size_t *Size)
{
	if (Size)
		*Size = EscapeBufferSize;
	return EscapeBuffer;
}

/* Full buffer isn't used as we need a string terminator for strcmp() in LocateKey. */
static int GetEscapeKey(int *Mods)
{
	unsigned int I;
	int Char;
	const struct Key *K;

	for (I = 1; I < sizeof(EscapeBuffer) - 1; I++) {
		Char = GetChar(1);
		if (Char == HEX_CHAR_EOF)
			break;
		else if (Char == HEX_CHAR_ERROR)
			return HEX_CHAR_ERROR;

		EscapeBuffer[I] = Char;
	}

	EscapeBuffer[I] = '\0';
	EscapeBufferSize = I;

	if (I >= sizeof(EscapeBuffer) - 1)	/* Overlong. */
		return HEX_CHAR_UNKNOWN;
	else if (I <= 1)
		return HEX_CHAR_ESCAPE;

	K = bsearch(&EscapeBuffer[1], Keys, ValidKeys, sizeof(struct Key), LocateKey);
	if (K) {
		*Mods |= K->Hint->Mod;
		return K->Hint->Key;
	}

	if (EscapeBuffer[1] == '[') {
		if (I == 3) {
			switch (EscapeBuffer[2]) {
				case 'O':
					return HEX_CHAR_FOCUS_OUT;
				case 'I':
					return HEX_CHAR_FOCUS_IN;
			}
		}
		/* Mouse protocols. */
		else if (EscapeBuffer[2] == '<' ||		/* SGR. Recommended. */
			EscapeBuffer[I - 1] == 'M' ||		/* urxvt. */
			(EscapeBuffer[2] == 'M' && I == 6))	/* Normal. */
			return HEX_CHAR_MOUSE;
	}

	if (I == 2) {
		Char = *EscapeBuffer;
		*Mods |= HEX_MOD_ALT;
		ConvertCTRLKey(&Char, *&Mods);
		return Char;
	}

	return HEX_CHAR_UNKNOWN;
}

/* Translate escape codes into key codes. */
static int GetCharFiltered(int *Mods)
{
	int Char, Mod;

	Mod = 0;
	Char = GetChar(0);

	if (Char == HEX_CHAR_ESCAPE)
		Char = GetEscapeKey(&Mod);
	else
		ConvertCTRLKey(&Char, &Mod);

	if (Mods)
		*Mods = Mod;
	return Char;
}

static int SetupPolling()
{
	#ifdef linux
	struct epoll_event NewEvent;
	#else
	struct kevent NewEvents[2];
	#endif
	int PipeFlags;

	/* Set the reading user pipe and stdin to non-blocking. */
	PipeFlags = fcntl(UserPipe[0], F_GETFL, 0);
	if (fcntl(UserPipe[0], F_SETFL, PipeFlags | O_NONBLOCK) == -1)
		return 0;
	if (fcntl(STDIN_FILENO, F_SETFL, DefaultStdinFlags | O_NONBLOCK) == -1)
		return 0;

	#ifdef linux
	EFD = epoll_create1(0);
	#else
	EFD = kqueue();
	#endif
	if (EFD == -1) {
		fcntl(STDIN_FILENO, F_SETFL, DefaultStdinFlags);
		return 0;
	}

	#ifdef linux
	memset(&NewEvent, 0, sizeof(NewEvent));
	NewEvent.data.fd = STDIN_FILENO;
	NewEvent.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(EFD, EPOLL_CTL_ADD, STDIN_FILENO, &NewEvent) == -1)
		goto Err;

	NewEvent.data.fd = UserPipe[0];
	if (epoll_ctl(EFD, EPOLL_CTL_ADD, UserPipe[0], &NewEvent) == -1)
		goto Err;
	#else
	EV_SET(&NewEvents[0], STDIN_FILENO, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);
	EV_SET(&NewEvents[1], UserPipe[0], EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, 0);

	if (kevent(EFD, NewEvents, 2, NULL, 0, NULL) == -1)
		goto Err;
	#endif

	return 1;

	Err:
	close(EFD);
	fcntl(STDIN_FILENO, F_SETFL, DefaultStdinFlags);
	return 0;
}

int ContinueInputHandler()
{
	const char *K;
	int PreviousMouseType;

	if (fcntl(STDIN_FILENO, F_SETFL, DefaultStdinFlags | O_NONBLOCK) == -1)
		return 0;
	if (!HexRawInputMode(1))
		return 0;

	/* No way of knowing what state the mouse type is in, so reset. */
	PreviousMouseType = MouseType;
	MouseType = HEX_MOUSE_NONE;
	if (PreviousMouseType)
		HexEnableMouse(PreviousMouseType);
	else {
		HexEnableMouse(HEX_MOUSE_NORMAL);
		HexEnableMouse(HEX_MOUSE_NONE);
	}

	K = GetTermInfoString(HINT_STR_KEYPAD_XMIT);
	if (K)
		printf("%s", K);

	return 1;
}

static int CompareKeys(const void *A, const void *B)
{
	const struct Key *KA = A, *KB = B;

	if (!KA->Code && !KB->Code)
		return 0;
	else if (!KA->Code)
		return 1;
	else if (!KB->Code)
		return -1;

	return strcmp(KA->Code, KB->Code);
}

static void LoadInputHints()
{
	int I, Valid;
	const char *K;

	K = GetTermInfoString(HINT_STR_KEYPAD_XMIT);
	if (K)
		printf("%s", K);

	Valid = 0;

	for (I = 0; I < KEY_HINTS; I++) {
		Keys[I].Hint = &KeyHints[I];

		K = GetTermInfoString(KeyHints[I].Hint);
		if (K && *K == HEX_CHAR_ESCAPE) {
			Keys[I].Code = &K[1];
			Valid++;
		} else
			Keys[I].Code = NULL;
	}

	qsort(Keys, KEY_HINTS, sizeof(struct Key), CompareKeys);
	ValidKeys = Valid;

	return;
}

int InitInput(int Stage)
{
	switch (Stage) {
		case 0:
			/* Before we touch it, we'll make a copy of the current state. */
			if (tcgetattr(STDIN_FILENO, &TTYInitialState) == -1)
				return 0;
			DefaultStdinFlags = fcntl(STDIN_FILENO, F_GETFL, 0);
			
			/* Setup the user pipe. */
			if (pipe(UserPipe) == -1)
				return 0;

			/* Setup polling with epoll()/kqueue(). */
			if (!SetupPolling()) {
				close(UserPipe[0]);
				close(UserPipe[1]);
				return 0;
			}
			InputSize = 0;
			InputPending = -1;
			InputChar = InputBuffer;

			if (!HexRawInputMode(1)) {
				fcntl(STDIN_FILENO, F_SETFL, DefaultStdinFlags);
				close(EFD);
				close(UserPipe[0]);
				close(UserPipe[1]);
				return 0;
			}

			break;

		case 1:
			/* We can generally turn off any previous mouse setting by enabling and disabling. */
			MouseType = HEX_MOUSE_NONE;
			HexEnableMouse(HEX_MOUSE_NORMAL);
			HexEnableMouse(HEX_MOUSE_NONE);

			ValidKeys = 0;
			if (GetTermInfoName())
				LoadInputHints();
			
			EscapeBuffer[0] = HEX_CHAR_ESCAPE;
			EscapeBuffer[1] = '\0';
			EscapeBufferSize = 0;
			break;

		/* Cleanup for when errors occur outside of this function. */
		case -1:
			HexRawInputMode(0);
			fcntl(STDIN_FILENO, F_SETFL, DefaultStdinFlags);
			close(EFD);
			close(UserPipe[0]);
			close(UserPipe[1]);
			break;
	}

	return 1;
}

static int PollWait(int Timeout)
{
	#ifdef linux
	return epoll_wait(EFD, &Event, 1, Timeout);
	#else
	struct timespec Expiry, *E;

	if (Timeout == -1)
		E = NULL;
	else {
		Expiry.tv_sec = Timeout / 1000;
		Expiry.tv_nsec = (Timeout % 1000) * 1000000;
		E = &Expiry;
	}

	return kevent(EFD, NULL, 0, &Event, 1, E);
	#endif
}

static int GetPendingIOFD()
{
	#ifdef linux
	return Event.data.fd;
	#else
	return Event.ident;
	#endif
}

/* Timeout in milliseconds. */
int HexGetChar(int Timeout, int *Mod)
{
	int Ready;

	if (InputPending >= 0) {
		int Char;

		Char = GetCharFiltered(Mod);
		if (Char != HEX_CHAR_EOF)
			return Char;
	}

	Ready = PollWait(Timeout);
	if (Ready > 0)
		InputPending = GetPendingIOFD();

	if (GotContinueSignal) {
		GotContinueSignal = 0;
		return ContinueHandler() ? HEX_CHAR_RESTORE : HEX_CHAR_ERROR;
	}

	if (GotResizeSignal) {
		GotResizeSignal = 0;
		if (!ResizeBuffers())
			return HEX_CHAR_ERROR;
		ExtendOutputBuffer();
		return HEX_CHAR_RESIZE;
	}

	if (Ready == -1)
		return HEX_CHAR_ERROR;

	if (InputPending >= 0)
		return GetCharFiltered(Mod);

	return HEX_CHAR_EOF;
}

/* Used for terminal detections. */
int GetCharTimeout(int Timeout)
{
	int Ready;

	if (InputPending >= 0) {
		int Char;

		Char = GetChar(0);
		if (Char != HEX_CHAR_EOF)
			return Char;
	}

	Ready = PollWait(Timeout);
	switch (Ready) {
		case -1:
			return HEX_CHAR_ERROR;
		case 0:
			return HEX_CHAR_EOF;
		default:
			InputPending = GetPendingIOFD();	/* Should only be from stdin's FD. */
			return GetChar(0);
	}
}

void FreeInput()
{
	const char *K;

	close(UserPipe[0]);
	close(UserPipe[1]);
	close(EFD);

	fcntl(STDIN_FILENO, F_SETFL, DefaultStdinFlags);
	HexRawInputMode(0);

	HexEnableMouse(HEX_MOUSE_NONE);

	K = GetTermInfoString(HINT_STR_KEYPAD_LOCAL);
	if (K)
		printf("%s", K);

	return;
}

/* Designed to be called from another thread. */
int HexPushChar(const char *Data, size_t Size)
{
	ssize_t Return;

	Return = write(UserPipe[1], Data, Size);

	return Return;
}

static int GetMouseDecimalInput(const char *C, int *State, int *RX, int *RY)
{
	int X, Y;

	*State = atoi(C);
	C = strchr(C, ';');
	if (!C)
		return 0;
	C++;

	X = atoi(C);
	if (X <= 0)
		return 0;
	X--;
	C++;

	C = strchr(C, ';');
	if (!C)
		return 0;
	C++;

	Y = atoi(C);
	if (Y <= 0)
		return 0;
	Y--;
	
	*RX = X;
	*RY = Y;

	return 1;
}

static int ParseMouseButtonState(HexMouseEvent *ME, int State)
{
	/*
	1	Button Bit 1
	2	Button Bit 2	4 = Release
	4	Shift
	8	Meta
	16	Ctrl
	32	Mouse Motion
	64	Wheel (Up = Button 0, Down Button 1)
	*/
	int Button, Mod;

	Button = (3 & State) + 1;
	Mod = (60 & State) >> 2;

	if (Button == 4) {
		Button = 0;
		Mod |= HEX_MOD_RELEASE;
	}
	
	if (State & 64)
		Button -= 3;	/* -2 page up, -1 page down. */

	ME->Button = Button;
	ME->Mod = Mod;

	return 1;
}

/* To be called after receiving an HEX_CHAR_MOUSE. */
int HexGetMouse(HexMouseEvent *ME)
{
	size_t Size;
	const unsigned char *Raw = HexGetRawKey(&Size);

	if (Raw[1] != '[')
		return 0;

	/* SGR protocol. Recommended over the others. */
	if (Raw[2] == '<') {
		int State;

		if (!GetMouseDecimalInput((const char *)&Raw[3], &State, &ME->X, &ME->Y))
			return 0;

		if (!ParseMouseButtonState(ME, State))
			return 0;

		if (Raw[Size - 1] == 'm')
			ME->Mod |= HEX_MOD_RELEASE;
		return 1;
	}

	/* urxvt protocol. */
	if (Raw[Size - 1] == 'M') {
		int State;

		if (!GetMouseDecimalInput((const char *)&Raw[2], &State, &ME->X, &ME->Y))
			return 0;

		State -= 0x20;
		if (!ParseMouseButtonState(ME, State))
			return 0;

		ME->Mod |= HEX_MOD_UNRELIABLE;
		return 1;
	}

	/* Normal X10 protocol.
	   Noticed on some terminals like tilix, this can cause a CTRL+C to be sent. */
	if (Raw[2] == 'M' && Size == 6) {
		unsigned char State, X, Y;

		State = Raw[3] - 0x20;
		X = Raw[4] - 33;
		Y = Raw[5] - 33;

		if (!ParseMouseButtonState(ME, State))
			return 0;

		ME->X = X;
		ME->Y = Y;
		ME->Mod |= HEX_MOD_UNRELIABLE;
		return 1;
	}

	return 0;
}

int HexEnableMouse(int Type)
{
	const int Codes[] = { 9, 1000, 1002, 1003 };

	if (Type > 0) {
		/* Set mouse protocol. */
		if (MouseType == HEX_MOUSE_NONE)
			printf(ESC "[?1015h" ESC "[?1006h");
		printf(ESC "[?%dh", Codes[Type - 1]);
	} else if (MouseType != HEX_MOUSE_NONE)
		printf(ESC "[?%dl", Codes[MouseType - 1]);

	MouseType = Type;

	return 1;
}
