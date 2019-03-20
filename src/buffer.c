/*
	Hexes Terminal Library
	Buffer functions. Includes blitting.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "hexes.h"

#define GetOffset(X, Y, W) ((Y) * (W) + (X))

extern HexBuffer *Buffer;
extern char *Damage;
extern int HasDamage;

int GetU8Size(const char *Char);

/* We place the buffer at the end of the allocated memory. */
HexBuffer *HexNewBuffer(int W, int H)
{
	size_t Size;
	HexBuffer *B;

	Size = sizeof(HexBuffer) + ((W * H) * sizeof(HexChar));

	B = calloc(Size, 1);
	if (!B)
		return NULL;

	B->W = W;
	B->H = H;
	B->TabStop = HEX_DEFAULT_TAB_STOP;
	B->Data = (HexChar *)&B[1];

	return B;
}

/* Do the actual drawing. May be called directly if the bounds are safe. */
void HexBlitRaw(const HexBuffer *S, HexBuffer *D, int SX, int SY, int DX, int DY, int W, int H, unsigned int Flags)
{
	int Y;
	unsigned int SOffset, DOffset;

	HexChar *SD = S->Data, *DD = D->Data, *SC, *DC;
	SOffset = GetOffset(SX, SY, S->W);
	DOffset = GetOffset(DX, DY, D->W);

	if (!Flags)
		Flags = ~HEX_DRAW_TRANSPARENT;

	for (Y = 0; Y < H; Y++) {
		int X;

		for (X = 0; X < W; X++) {
			SC = &SD[SOffset + X];
			DC = &DD[DOffset + X];

			if (Flags & HEX_DRAW_CP) {
				size_t Size;

				if (Flags & HEX_DRAW_TRANSPARENT && !*SC->CP)
					continue;
				Size = GetU8Size(SC->CP);
				strncpy(DC->CP, SC->CP, Size);
				if (Size < UTF8_MAX_BYTES)
					DC->CP[Size] = '\0';
			}
			if (Flags & HEX_DRAW_FG)
				DC->FG = SC->FG;
			if (Flags & HEX_DRAW_BG)
				DC->BG = SC->BG;
			if (Flags & HEX_DRAW_ATTR)
				DC->Attr = SC->Attr;

			if (D == Buffer)
				HasDamage = Damage[DOffset + X] = 1;
		}

		SOffset += S->W;
		DOffset += D->W;
	}

	return;
}

const HexChar *HexGetHexChar(HexBuffer *D, int X, int Y)
{
	unsigned int DOffset;

	DOffset = GetOffset(X, Y, D->W);

	return &D->Data[DOffset];
}

void HexBlit(const HexBuffer *S, HexBuffer *D, int SX, int SY, int DX, int DY, int W, int H, unsigned int Flags)
{
	if (SX < 0 || SY < 0)
		return;

	if (DX < 0) {
		SX -= DX;
		W += DX;
		DX = 0;
	}
	if (DY < 0) {
		SY -= DY;
		H += DY;
		DY = 0;
	}

	if (SX >= S->W || SY >= S->H)
		return;

	if (SX + W > S->W)
		W -= (SX + W) - S->W;
	if (SY + H > S->H)
		H -= (SY + H) - S->Y;

	if (DX + W > D->W)
		W -= (DX + W) - D->W;
	if (DY + H > D->H)
		H -= (DY + H) - D->H;

	if (W < 0 || H < 0)
		return;

	HexBlitRaw(S, D, SX, SY, DX, DY, W, H, Flags);

	return;
}


void HexFreeBuffer(HexBuffer *B)
{
	free(B);
	return;
}

HexBuffer *HexResizeBuffer(HexBuffer *Original, int W, int H)
{
	size_t OldSize, Size;
	int OldW, OldH;
	HexBuffer *New;

	OldW = Original->W;
	OldH = Original->H;

	OldSize = sizeof(HexBuffer) + ((OldW * OldH) * sizeof(HexChar));
	Size = sizeof(HexBuffer) + ((W * H) * sizeof(HexChar));

	if (OldW == W) {
		New = realloc(Original, Size);
		if (!New)
			return NULL;

		if (Size > OldSize) {
			Size -= OldSize;
			memset((char *)New + OldSize, 0, Size);
		}

		New->W = W;
		New->H = H;
		New->Data = (HexChar *)&New[1];
	} else {
		New = HexNewBuffer(W, H);
		if (!New)
			return NULL;

		HexBlitRaw(Original, New, 0, 0, 0, 0, OldW < W ? OldW : W, OldH < H ? OldH : H, ~0);

		New->X = Original->X;
		New->Y = Original->Y;
		New->FG = Original->FG;
		New->BG = Original->BG;
		New->Attr = Original->Attr;

		HexFreeBuffer(Original);
	}

	return New;
}

