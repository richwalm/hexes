/*
	Hexes Terminal Library
	Common functions across platforms.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hexes.h"

int InitSub(int Stage);
int GetTerminalSize(int *W, int *H);
int IsUnicodeSupported();
int ColorsSupported();
void FreeSub();
void HexSetTitle(const char *Title, const char *Icon);

int Width, Height, Unicode, HexColors;

HexBuffer *Current, *Buffer;
char *Damage;
int HasDamage;

int HexWidth() { return Current->W; }
int HexHeight() { return Current->H; }
int HexUnicode() { return Unicode; }

int HexInit(int MinW, int MinH, int Flags)
{
	int Return;
	Width = Height = Unicode = HexColors = 0;

	Return = InitSub(0);
	if (Return != HEX_ERROR_NONE)
		return Return;

	if (!GetTerminalSize(&Width, &Height)) {
		InitSub(-1);
		return HEX_ERROR_SIZE;
	}

	if ((MinW && Width < MinW) ||
		(MinH && Height < MinH)) {
		InitSub(-1);
		return HEX_ERROR_TOO_SMALL;
	}

	Return = InitSub(1);
	if (Return != HEX_ERROR_NONE)
		return Return;

	if (Flags & HEX_INIT_NO_UNICODE_TEST)
		Unicode = Flags & HEX_INIT_FORCE_UNICODE;
	else {
		Unicode = IsUnicodeSupported();
		if (Unicode == -1 || (!Unicode && Flags & HEX_INIT_NEEDS_UNICODE)) {
			InitSub(-2);
			return HEX_ERROR_UNICODE;
		}
	}

	HexColors = (Flags & HEX_INIT_NO_COLOR_TEST) ? 0 : ColorsSupported();

	Return = InitSub(2);
	if (Return != HEX_ERROR_NONE)
		return Return;

	/* Create the primary buffers. */
	Buffer = HexNewBuffer(Width, Height);
	Current = HexNewBuffer(Width, Height);
	Damage = calloc(Width * Height, 1);
	if (!(Current && Buffer && Damage)) {
		HexFreeBuffer(Buffer);
		HexFreeBuffer(Current);
		free(Damage);
		InitSub(-3);
		return HEX_ERROR_MEMORY;
	}
	HasDamage = 0;

	InitSub(3);
	HexSetTitle("Hexes Terminal Application", NULL);

	return HEX_ERROR_NONE;
}

int ResizeBuffers()
{
	unsigned int DamageOffset;
	int W, H;
	HexBuffer *CurrentNew, *BufferNew;
	char *DamageNew;
	size_t Size;

	if (!GetTerminalSize(&W, &H))
		return 0;

	CurrentNew = HexResizeBuffer(Current, W, H);
	if (!CurrentNew)
		return 0;
	Current = CurrentNew;

	/* We'll have to adjust the damage before the Buffer so it can be updated. */
	Size = W * H;

	DamageNew = realloc(Damage, Size);
	if (!DamageNew)
		return 0;
	Damage = DamageNew;

	/* Set damage to everything except to the first line as that should remain the same. */
	DamageOffset = W < Width ? W : Width;
	if (DamageOffset < Size) {
		memset(&DamageNew[DamageOffset], 1, Size - DamageOffset);
		HasDamage = 1;
	}

	BufferNew = HexResizeBuffer(Buffer, W, H);
	if (!BufferNew)
		return 0;
	Buffer = BufferNew;

	Width = W;
	Height = H;

	return 1;
}

void HexFree()
{
	FreeSub();

	HexFreeBuffer(Current);
	HexFreeBuffer(Buffer);
	free(Damage);

	return;
}

int IsSameChar(const HexChar *A, const HexChar *B)
{
	if (!strncmp(A->CP, B->CP, UTF8_MAX_BYTES) &&
		A->FG == B->FG &&
		A->BG == B->BG &&
		A->Attr == B->Attr)
		return 1;

	return 0;
}

int GetU8Size(const unsigned char *Char)
{
	if (!Unicode || *Char < 0xC0)	/* Technically, 0x80 to 0xBF is invalid, but we'll just pass bad data. */
		return 1;
	else if (*Char < 0xE0)
		return 2;
	else if (*Char < 0xF0)
		return 3;

	return 4;
}

void HexClipCursor(int *X, int *Y)
{
	if (*X < 0)
		*X = 0;
	else if (*X >= Current->W)
		*X = Current->W - 1;

	if (*Y < 0)
		*Y = 0;
	else if (*Y >= Current->H)
		*Y = Current->H - 1;

	return;
}

HexBuffer *HexGetTerminalBuffer()
{
	return Buffer;
}
