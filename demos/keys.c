/*
	Hexes Terminal Library
	Key demo.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <hexes.h>
#include <stdlib.h>

#define	INPUT_TIMEOUT	2000

static const char *KeyName(int Char)
{
	switch (Char) {
		case '\n':
			return "Enter";
		case '\t':
			return "Tab";
		case '\x7F':
			return "Backspace";
		case ' ':
			return "Space";

		case HEX_CHAR_ESCAPE:
			return "Escape";

		case HEX_CHAR_UP:
			return "Up";
		case HEX_CHAR_DOWN:
			return "Down";
		case HEX_CHAR_LEFT:
			return "Left";
		case HEX_CHAR_RIGHT:
			return "Right";
		case HEX_CHAR_HOME:
			return "Home";

		case HEX_CHAR_A1:
			return "A1";
		case HEX_CHAR_A3:
			return "A3";
		case HEX_CHAR_B2:
			return "B2";
		case HEX_CHAR_C1:
			return "C1";
		case HEX_CHAR_C3:
			return "C3";

		case HEX_CHAR_END:
			return "End";

		case HEX_CHAR_DELETE:
			return "Delete";
		case HEX_CHAR_INSERT:
			return "Insert";

		case HEX_CHAR_PGUP:
			return "Page Up";
		case HEX_CHAR_PGDOWN:
			return "Page Down";

		case HEX_CHAR_RESIZE:
			return "Resize";

		case HEX_CHAR_FOCUS_IN:
			return "Focus In";
		case HEX_CHAR_FOCUS_OUT:
			return "Focus Out";

		case HEX_CHAR_RESTORE:
			return "Restore";

		case HEX_CHAR_ERROR:
		default:
			return NULL;
	}
}

int main(int argc, char *argv[])
{
	HexBuffer *Buffer;
	int Flags;
	int Focused;
	char Status[128];
	int Quit;
	int Char;
	const HexChar C = { "", 9, 0, 0 };
	char *Env;

	if (HexInit(0, 0, 0)) {
		fputs("Unable to load Hexes!", stderr);
		return 1;
	}

	Buffer = HexGetTerminalBuffer();

	HexSetTitle("Hexes Keys Demo", NULL);
	Flags = HEX_FLAG_EVENT_FOCUS;
	HexChangeFlags(&Flags);
	Focused = 1;
	HexEnableMouse(HEX_MOUSE_DRAG);

	HexColor(Buffer, 16, -1);
	sprintf(Status, "{Unicode: %s} {Colors: %d}", HexUnicode() ? "✔" : "No", HexColors);
	HexPrint(Buffer, Status, 0);
	Env = getenv("TERM");
	if (Env) {
		sprintf(Status, " {TERM: %s}", Env);
		HexPrint(Buffer, Status, 0);
	}

	HexFlush(-1, 0);

	Quit = 0;

	do {
		const char *Name;

		Char = HexGetChar(Focused ? INPUT_TIMEOUT : -1, NULL);
		Buffer = HexGetTerminalBuffer();
		Name = KeyName(Char);

		if (!Name) {
			if (Char >= HEX_CHAR_F(0) && Char <= HEX_CHAR_F(12)) {
				HexColor(Buffer, 2, -1);
				sprintf(Status, " F%d", Char - HEX_CHAR_F0);
			} else if (Char == HEX_CHAR_MOUSE) {
				HexMouseEvent ME;
				HexColor(Buffer, 6, -1);
				if (HexGetMouse(&ME)) {
					sprintf(Status, " (%d %d %dx%d)", ME.Button, ME.Mod, ME.X, ME.Y);
				} else {
					sprintf(Status, " (Mouse Error)");
				}
			} else if (isprint(Char)) {
				HexColor(Buffer, 5, -1);
				sprintf(Status, " %c", Char);
			} else if (Char == HEX_CHAR_EOF) {
				HexColor(Buffer, 9, -1);
				sprintf(Status, " .");
			} else if (Char == HEX_CHAR_UNKNOWN) {
				HexColor(Buffer, 3, -1);
				sprintf(Status, " <UNKNOWN; '%s'>", &HexGetRawKey(NULL)[1]);
			} else {
				HexColor(Buffer, 7, -1);
				sprintf(Status, " <0x%X>", Char);
			}
		}
		else {
			HexColor(Buffer, 4, -1);
			sprintf(Status, " [%s]", Name);
		}

		if (Buffer->Y >= Buffer->H) {
			Buffer->X = Buffer->Y = 0;
			HexFillRaw(Buffer, 0, 0, Buffer->W, Buffer->H, &C, HEX_DRAW_FG);
		}

		HexPrint(Buffer, Status, 0);
		HexFlush(-1, 0);

		switch (Char) {
			case HEX_CHAR_FOCUS_IN:
				Focused = 1;
				break;
			case HEX_CHAR_FOCUS_OUT:
				Focused = 0;
				break;
			case HEX_CHAR_RESTORE:
			case HEX_CHAR_RESIZE:
			case HEX_CHAR_F(5):
				HexFullFlush(0, -1, 0);
				break;
			case HEX_CHAR_ESCAPE:
			case HEX_CHAR_ERROR:
				Quit = 1;
				break;
		}
	} while (!Quit);

	HexFree();

	if (Char == HEX_CHAR_ERROR)
		puts("Reached error!");

	return 0;
}
