/*
	Hexes Terminal Library
	Bad Apple!! demo.

	Written by Richard Walmsley <richwalm@gmail.com>
	Bad Apple!! by Anira & Alstroemeria Records

	Designed for the regular 80x24 terminal size. Makes uses of block characters to double that, though.
	Has a lower FPS than the source video, but that's just to keep the data file small.
	As there's no music, we'll ran it back at double the speed too.
*/

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <zlib.h>
#include <stdlib.h>
#include <string.h>
#include <hexes.h>

/* Data file stuff. */
#define	FILENAME	"badapple.dat"
#define	HEADER		0xBADA
#define	CHUNK		16384

static FILE *File;
static z_stream ZStream;
static unsigned char Input[CHUNK], Output[CHUNK];
static uint16_t *Next;
static size_t Length = 0;
static int Done = 0;

/* Screen stuff. */
#define SPEED		50

static unsigned int Width, Height, Cursor, Line;
static unsigned char *LB;	/* Line Buffer for the current two lines. */
static size_t LBSize;
static int IsWhite = 0;
static HexBuffer *Frame;
static int OffsetW, OffsetH;

static int LoadInput()
{
	ZStream.avail_in = fread(Input, 1, sizeof(Input), File);
	if (ferror(File))
		return 0;
	ZStream.next_in = Input;
	return 1;
}

static int LoadData(const char *Filename)
{
	int Return;

	File = fopen(Filename, "rb");
	if (!File)
		return errno;

	if (!LoadInput()) {
		fclose(File);
		return errno;
	}
	ZStream.zalloc = Z_NULL;
	ZStream.zfree = Z_NULL;
	ZStream.opaque = Z_NULL;
	Return = inflateInit(&ZStream);

	if (Return != Z_OK) {
		fclose(File);
		return Return;
	}

	return 0;
}

static void CleanUp()
{
	fclose(File);
	inflateEnd(&ZStream);
	return;
}

/* Obtain a single number from the data file. */
static unsigned int ReadNumber()
{
	uint16_t Number;

	/* Fill if empty. */
	if (Length < sizeof(uint16_t)) {
		int Return;
		unsigned char *OldOut;

		if (Done)
			return 0;

		if (!ZStream.avail_out) {
			ZStream.avail_out = sizeof(Output);
			ZStream.next_out = Output;
			Next = (uint16_t *)Output;
			Length = 0;
		}

		if (!ZStream.avail_in && !feof(File) && !LoadInput())
			return 0;

		OldOut = ZStream.next_out;
		Return = inflate(&ZStream, feof(File) ? Z_FINISH : Z_NO_FLUSH);
		switch (Return) {
			case Z_STREAM_END:
				Done = 1;
			case Z_OK:
			case Z_BUF_ERROR:
				break;
			default:
				return 0;
		}

		Return = ZStream.next_out - OldOut;
		Length += Return;
	}

	Number = *(uint16_t *)Next;
	Next++;
	Length -= sizeof(uint16_t);
	#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
	Number = __builtin_bswap16(Number);
	#endif

	return Number;
}

/* Fill as much as we can of the line buffer with the RLE number. */
static int FillBuffer(unsigned int *Number)
{
	unsigned int Amount;
	size_t Left = LBSize - Cursor;
	int Dump = 0;

	Amount = *Number;
	if (Amount >= Left) {
		Amount = Left;
		Dump = 1;
	}

	memset(&LB[Cursor], IsWhite, Amount);

	Cursor += Amount;
	if (Cursor >= LBSize)
		Cursor = 0;
	*Number -= Amount;
	return Dump;
}

static void DumpLineToFrame()
{
	unsigned int i, Cell;
	const char *CP = "";
	HexChar C = HEX_SET_CHAR("", 16, 1, 0);
	for (i = 0; i < Width; i += 2) {
		Cell = LB[i] | (LB[i + 1] << 1) | (LB[i + Width] << 2) | (LB[i + 1 + Width] << 3);
		switch (Cell) {
			case 0: CP = " "; break;
			case 1: CP = "▘"; break;
			case 2: CP = "▝"; break;
			case 3: CP = "▀"; break;
			case 4: CP = "▖"; break;
			case 5: CP = "▌"; break;
			case 6: CP = "▞"; break;
			case 7: CP = "▛"; break;
			case 8: CP = "▗"; break;
			case 9: CP = "▚"; break;
			case 10: CP = "▐"; break;
			case 11: CP = "▜"; break;
			case 12: CP = "▄"; break;
			case 13: CP = "▙"; break;
			case 14: CP = "▟"; break;
			case 15: CP = "█"; break;
		}
		strcpy(C.CP, CP);
		HexPutHexCharOffset(Frame, (i / 2) + ((Width * Line) / 4), &C);
	}

	return;
}

static int Core()
{
	unsigned int Number;
	HexBuffer *TB;
	const HexChar C = { "?", 2, 3, 0 };
	int Char;

	LBSize = Width * 2;	/* The two raw lines allows us to use block characters to compress down to a single line. */
	LB = malloc(LBSize);
	if (!LB)
		return 1;

	Frame = HexNewBuffer(Width / 2, Height / 2);
	if (!Frame) {
		free(LB);
		return 1;
	}
	HexFillRaw(Frame, 0, 0, Frame->W, Frame->H, &C, 0);	/* Debugging. */

	TB = HexGetTerminalBuffer();
	OffsetW = (TB->W / 2) - (Frame->W / 2);
	OffsetH = (TB->H / 2) - (Frame->H / 2);

	Cursor = Line = 0;
	do {
		Number = ReadNumber();
		if (!Number)
			break;

		do {
			if (FillBuffer(&Number)) {
				DumpLineToFrame();
				Line += 2;
				if (Line >= Height) {
					HexBlit(Frame, TB, 0, 0, OffsetW, OffsetH, Frame->W, Frame->H, 0);
					HexFlush(0, 0);
					Line = 0;

					Char = HexGetChar(SPEED, NULL);
					switch (Char) {
						case HEX_CHAR_EOF:
							break;
						default:
							Done = 1;
							goto End;
					}
				}
			}
		} while (Number);
		IsWhite = !IsWhite;
	} while (1);

	End:
	HexFreeBuffer(Frame);
	free(LB);
	return !Done;
}

int main(int argc, char *argv[])
{
	int Return;
	Return = LoadData(FILENAME);
	if (Return) {
		fprintf(stderr, "Failed to open data file; %d\n", Return);
		return 1;
	}

	if (ReadNumber() != HEADER) {
		fputs("Bad header on data file.\n", stderr);
		CleanUp();
		return 1;
	}

	Width = ReadNumber();
	Height = ReadNumber();
	if (!(Width && Height)) {
		fputs("Failed to obtain video size.\n", stderr);
		CleanUp();
		return 1;
	}

	Return = HexInit(Width / 2, Height / 2, 0);
	if (Return) {
		if (Return == HEX_ERROR_TOO_SMALL)
			fprintf(stderr, "Needs a terminal of at least %dx%d.\n", Width / 2, Height / 2);
		else
			fprintf(stderr, "Unable to load Hexes! Error code; %d.\n", Return);
		CleanUp();
		return 1;
	}
	HexSetTitle("Hexes Bad Apple Demo", NULL);
	Return = HEX_FLAG_DISPLAY_NO_CURSOR;
	HexChangeFlags(&Return);

	Return = Core();

	HexFree();
	CleanUp();

	if (Return) {
		fprintf(stderr, "Error occured; %d.\n", Return);
		return 1;
	}

	return 0;
}
