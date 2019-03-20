#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <hexes.h>

#define MAX_BULLETS_RATIO		3	/* Maximum cells on screen divided by this number. */
#define	MIN_WIDTH			80
#define	MIN_HEIGHT			24
#define	SLEEP_TIME			33	/* In milliseconds. Should keep it roughly running at a maximum of 30 FPS. */

#define	TS_PASSED(A, B)			((int)((B) - (A)) <= 0)

typedef unsigned int Time;
static Time LastUpdate;
static Time StartTime = 0;

/* FPS */
static unsigned int Frames;

typedef struct Bullet {
	int X, Y, VX, VY;
	unsigned int Life, ToSpawn, Speed;
	HexChar C;
	struct Bullet *Next, *Prev;
} Bullet;

static Bullet *Full, *Used, *Free;
static const HexChar EmptyChar = HEX_SET_CHAR("", 0, 0, 0);

static unsigned int Width, Height;

/* Returns a bullet from the free list. */
static Bullet *NewBullet()
{
	Bullet *B;
	
	if (!Free)
		return NULL;
	B = Free;

	Free = Free->Next;
	if (Free)
		Free->Prev = NULL;

	B->Next = Used;
	if (Used)
		Used->Prev = B;
	Used = B;

	return B;
}

/* Places a used bullet back to free list. */
static void FreeBullet(Bullet *B)
{
	if (B == Used)
		Used = B->Next;

	if (B->Prev)
		B->Prev->Next = B->Next;
	if (B->Next)
		B->Next->Prev = B->Prev;

	B->Prev = NULL;
	B->Next = Free;
	if (Free)
		Free->Prev = B;
	Free = B;

	return;
}

static void RandomizeBullet(Bullet *B)
{
	B->Life = rand() % 10 + 5;
	B->ToSpawn = rand() % 7 + 3;
	B->Speed = rand() % 50 + 20;
	
	B->C.CP[0] = rand() % 93 + 33;
	B->C.FG = rand() % 7 + 10;

	return;
}

static int SetupBullets()
{
	unsigned int Max;
	int I;
	Bullet *B;
	int VX, VY;

	Max = (Width * Height) / MAX_BULLETS_RATIO;
	
	Full = malloc(sizeof(Bullet) * Max);
	if (!Full)
		return 0;
	
	for (I = 0; I < Max; I++) {
		Full[I].Prev = &Full[I - 1];
		Full[I].Next = &Full[I + 1];
		Full[I].C.CP[1] = '\0';
		Full[I].C.BG = 0;
		Full[I].C.Attr = 0;
	}
	Full[0].Prev = NULL;
	Full[Max - 1].Next = NULL;

	Used = NULL;
	Free = Full;
	
	/* Initial bullet. */
	B = NewBullet();
	RandomizeBullet(B);

	do {
		VX = rand() % 3 - 1;
		VY = rand() % 3 - 1;
	} while (!(VX || VY));
	
	B->VX = VX;
	B->VY = VY;
	B->X = rand() % Width;
	B->Y = rand() % Height;

	return 1;
}

static int DoHit(Bullet *B)
{
	Bullet *New;

	if (B->ToSpawn) {
		B->ToSpawn--;
		
		New = NewBullet();
		if (New) {
			int VX, VY;

			New->X = B->X;
			New->Y = B->Y;
			RandomizeBullet(New);

			do {
				VX = rand() % 3 - 1;
				VY = rand() % 3 - 1;
			} while (!(VX || VY));

			New->VX = VX;
			New->VY = VY;

			if ((New->X <= 0 && New->VX < 0) ||
				(New->X >= Width - 1 && New->VX > 0))
				New->VX = -New->VX;
			if ((New->Y <= 0 && New->VY < 0) ||
				(New->Y >= Height - 1 && New->VY > 0))
				New->VY = -New->VY;
		}
	}

	B->Life--;
	if (!B->Life) {
		FreeBullet(B);
		return 1;
	}

	return 0;
}

static unsigned int UpdatesPassed(Time LastUpdate, Time Now, unsigned int Speed)
{
	unsigned int Remaining;
	Time NextEvent;

	Remaining = Speed - (LastUpdate % Speed);
	NextEvent = LastUpdate + Remaining;
	if (!TS_PASSED(Now, NextEvent))
		return 0;

	Now -= NextEvent;
	return (Now / Speed) + 1;
}

static int UpdateBullet(Bullet *B, Time Now)
{
	unsigned int Updates;

	Updates = UpdatesPassed(LastUpdate, Now, B->Speed);
	if (Updates) {
		unsigned int I;
		HexBuffer *TB;
	
		TB = HexGetTerminalBuffer();

		HexPutHexChar(TB, B->X, B->Y, &EmptyChar);

		for (I = 0; I < Updates; I++) {
			int Hit = 0;

			B->X += B->VX;
			B->Y += B->VY;

			if (B->X < 0) {
				B->X = 0;
				Hit = 1;
				B->VX = -B->VX;
			}
			else if (B->X >= Width) {
				B->X = Width - 1;
				Hit = 1;
				B->VX = -B->VX;
			}

			if (B->Y < 0) {
				B->Y = 0;
				Hit = 1;
				B->VY = -B->VY;
			}
			else if (B->Y >= Height) {
				B->Y = Height - 1;
				Hit = 1;
				B->VY = -B->VY;
			}
			
			if (Hit && DoHit(B))
				return 1;
		}

		HexPutHexChar(TB, B->X, B->Y, &B->C);
	}
	
	return 0;
}

static void UpdateBullets(Time Now)
{
	Bullet *B = Used;

	while (B) {
		Bullet *BN = B->Next;
		UpdateBullet(B, Now);
		B = BN;
	}

	return;
}

static Time GetTS()
{
	struct timespec TimeSpec;
	Time TS;

	if (clock_gettime(CLOCK_MONOTONIC, &TimeSpec) == -1)
		return 0;

	TS = TimeSpec.tv_sec * 1000 + TimeSpec.tv_nsec / 1000000;

	return TS - StartTime;
}

int main(int argc, char *argv[])
{
	int Return, Flags, Char, Quit;
	HexBuffer *TB;
	Time Now, LastFPSUpdate = 0;

	Return = HexInit(MIN_WIDTH, MIN_HEIGHT, HEX_INIT_NO_UNICODE_TEST);
	if (Return) {
		if (Return == HEX_ERROR_TOO_SMALL)
			fprintf(stderr, "Needs a terminal of at least %dx%d.\n", MIN_WIDTH, MIN_HEIGHT);
		else
			fprintf(stderr, "Unable to load Hexes! Error code; %d.\n", Return);
		return 1;
	}
	HexSetTitle("Hexes Bullets Demo", NULL);
	Flags = HEX_FLAG_DISPLAY_NO_CURSOR;
	HexChangeFlags(&Flags);

	Width = HexWidth();
	Height = HexHeight() - 1;

	Now = StartTime = GetTS();
	srand(StartTime);
	if (!SetupBullets()) {
		HexFree();
		fputs("Failed to allocate memory for the bullets.", stderr);
		return 1;
	}
	
	TB = HexGetTerminalBuffer();
	HexFillRaw(TB, 0, 0, Width, Height, &EmptyChar, 0);
	HexLocate(TB, 0, Height);
	HexPrint(TB, "FPS:", 0);

	Quit = Frames = 0;
	do {
		Char = HexGetChar(33, NULL);
		switch (Char) {
			default:
				Quit = 1;
			case HEX_CHAR_EOF:
				break;
		}

		LastUpdate = Now;
		Now = GetTS();

		UpdateBullets(Now);

		/* A basic FPS counter. */
		if (UpdatesPassed(LastUpdate, Now, 1000)) {
			float FPS;
			char Output[64];

			FPS = ((float)Frames / (Now - LastFPSUpdate)) * 1000;
			sprintf(Output, "%.6f", FPS);

			HexLocate(TB, 5, Height);
			HexPrint(TB, Output, 0);
			
			Frames = 0;
			LastFPSUpdate = Now;
		}

		HexFlush(-1, -1);
		
		Frames++;
	} while (!Quit);

	HexFree();

	if (Char == HEX_CHAR_ERROR)
		puts("Reached error!");

	free(Full);
	return 0;
}
