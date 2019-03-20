/*
	Hexes Terminal Library
	Basic drawing functions.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "hexes.h"

#define GetOffset(X, Y, W) ((Y) * (W) + (X))

extern HexBuffer *Buffer;
extern char *Damage;
extern int HasDamage;

int GetU8Size(const char *Char);

void HexLocate(HexBuffer *B, int X, int Y)
{
	B->X = X; B->Y = Y;
	return;
}

void HexTabStop(HexBuffer *B, unsigned int TabStop)
{
	if (!B->TabStop)
		B->TabStop = TabStop;
	else
		B->TabStop = HEX_DEFAULT_TAB_STOP;
	return;
}

void HexColor(HexBuffer *B, int FG, int BG)
{
	if (FG >= 0)
		B->FG = FG;
	if (BG >= 0)
		B->BG = BG;
	return;
}

void HexAttr(HexBuffer *B, unsigned int Attr)
{
	B->Attr = Attr;
	return;
}

static void UpdateCursor(HexBuffer *B)
{
	B->X++;
	if (B->X >= B->W) {
		B->X = 0;
		B->Y++;
	}

	return;
}

int HexPutChar(HexBuffer *B, const char *CP)
{
	int Size;

	Size = GetU8Size(CP);

	if (!(B->X < 0 || B->Y < 0 || B->X >= B->W || B->Y >= B->H || (Size == 1 && iscntrl((int)*CP)))) {
		int I;
		HexChar *C;

		I = GetOffset(B->X, B->Y, B->W);
		C = &B->Data[I];
		strncpy(C->CP, CP, Size);
		if (Size < UTF8_MAX_BYTES)
			C->CP[Size] = '\0';
		C->Attr = B->Attr;
		C->FG = B->FG;
		C->BG = B->BG;

		if (B == Buffer)
			HasDamage = Damage[I] = 1;
	}

	UpdateCursor(B);

	return Size;
}

int HexPrint(HexBuffer *B, const char *String, size_t Length)
{
	unsigned int I;

	if (!Length)
		Length = -1;

	I = 0;
	do {
		int Size;

		Size = 1;
		switch (*String) {
			case '\0':
				return ++I;
			case '\n':
				B->Y++;
			case '\r':
				B->X = 0;
				break;
			case '\f':
			case '\v':
				B->Y++;
				break;
			case '\b':
				if (B->X > 0)
					B->X--;
				break;
			case '\t':
				B->W += B->TabStop - (B->X % B->TabStop);
				break;
			default:
				Size = HexPutChar(B, String);
				break;
		}

		String += Size;
		I += Size;
	} while (I < Length);

	return I;
}

void HexPutHexChar(HexBuffer *D, int X, int Y, const HexChar *Char)
{
	unsigned int DOffset;

	DOffset = GetOffset(X, Y, D->W);

	D->Data[DOffset] = *Char;

	if (D == Buffer)
		HasDamage = Damage[DOffset] = 1;

	return;
}

/* Like blitting, may be called directly provided input is safe. */
void HexFillRaw(HexBuffer *D, int DX, int DY, int W, int H, const HexChar *Char, unsigned int Flags)
{
	int Y;
	unsigned int DOffset;
	HexChar *DD;

	if (!Flags)
		Flags = ~0;

	DOffset = GetOffset(DX, DY, D->W);
	DD = D->Data;

	for (Y = 0; Y < H; Y++) {
		int X;

		for (X = 0; X < W; X++) {
			HexChar *C;

			C = &DD[DOffset + X];

			if (Flags & HEX_DRAW_CP) {
				int Size = GetU8Size(Char->CP);

				strncpy(C->CP, Char->CP, Size);
				if (Size < UTF8_MAX_BYTES)
					C->CP[Size] = '\0';
			}
			if (Flags & HEX_DRAW_FG)
				C->FG = Char->FG;
			if (Flags & HEX_DRAW_BG)
				C->BG = Char->BG;
			if (Flags & HEX_DRAW_ATTR)
				C->Attr = Char->Attr;

			if (D == Buffer)
				HasDamage = Damage[DOffset + X] = 1;
		}
		DOffset += D->W;
	}

	return;
}

void HexFill(HexBuffer *D, int DX, int DY, int W, int H, const HexChar *Char, unsigned int Flags)
{
	if (DX < 0) {
		W += DX;
		DX = 0;
	}
	if (DY < 0) {
		H += DY;
		DY = 0;
	}

	if (DX + W > D->W)
		W -= (DX + W) - D->W;
	if (DY + H > D->H)
		H -= (DY + H) - D->H;

	if (W < 0 || H < 0)
		return;

	HexFillRaw(D, DX, DY, W, H, Char, Flags);

	return;
}

