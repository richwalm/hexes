/*
	Hexes Terminal Library
	Unix terminal hints functions. Loads a terminfo database.

	Written by Richard Walmsley <richwalm@gmail.com>
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifndef __BYTE_ORDER__
#include <endian.h>
#endif
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>

static char *Names, *Booleans, *Strings;
static int *Intergers, *Offsets;

static unsigned int BooleanAmount;
static unsigned int StringSize;
static unsigned int IntergerAmount;
static unsigned int OffsetAmount;

#ifndef le16toh
static uint16_t le16toh(uint16_t little_endian_16bits)
{
	#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
	little_endian_16bits = __builtin_bswap16(little_endian_16bits);
	#endif
	return little_endian_16bits;
}
#endif
#ifndef le32toh
static uint32_t le32toh(uint32_t little_endian_32bits)
{
	#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
	little_endian_32bits = __builtin_bswap32(little_endian_32bits);
	#endif
	return little_endian_32bits;
}
#endif

const char *GetTermInfoName()
{
	return Names;
}

int GetTermInfoBool(unsigned int N)
{
	if (N >= BooleanAmount)
		return 0;

	return Booleans[N];
}

int GetTermInfoInt(unsigned int N)
{
	if (N >= IntergerAmount)
		return -1;

	return Intergers[N];
}

const char *GetTermInfoString(unsigned int N)
{
	int Offset;
	if (N >= OffsetAmount)
		return NULL;

	Offset = Offsets[N];
	if (Offset < 0)
		return NULL;

	return &Strings[Offset];
}

/* Documented in term(5). */
static int LoadTermInfo(const char *Filename)
{
	FILE *File;
	struct {
		int16_t Magic;
		int16_t NameSize;
		int16_t BoolSize;
		int16_t NumberAmount;
		int16_t StringOffsetAmount;
		int16_t StringSize;
	} Header;
	int Extended;
	size_t Size;
	int I;

	File = fopen(Filename, "rb");
	if (!File)
		return 0;

	if (fread(&Header, sizeof(Header), 1, File) != 1) {
		fclose(File);
		return 0;
	}

	/* Basic checks. */
	switch (le16toh(Header.Magic)) {
		case 0432:
			Extended = 0;
			break;
		case 01036:
			Extended = 1;
			break;
		default:
			fclose(File);
			return 0;
	}

	Header.NameSize = le16toh(Header.NameSize);
	Header.BoolSize = le16toh(Header.BoolSize);
	Header.NumberAmount = le16toh(Header.NumberAmount);
	Header.StringOffsetAmount = le16toh(Header.StringOffsetAmount);
	Header.StringSize = le16toh(Header.StringSize);
	if (Header.NameSize <= 0 ||
		Header.BoolSize <= 0 ||
		Header.NumberAmount <= 0 ||
		Header.StringOffsetAmount <= 0 ||
		Header.StringSize <= 0) {
		fclose(File);
		return 0;
	}

	Size = Header.NameSize + Header.BoolSize + Header.StringSize;
	Names = malloc(Size);
	if (!Names) {
		fclose(File);
		return 0;
	}
	Booleans = &Names[Header.NameSize];
	Strings = &Names[Header.NameSize + Header.BoolSize];

	/* Number allocation. */
	Size = (Header.NumberAmount + Header.StringOffsetAmount) * sizeof(int);
	Intergers = malloc(Size);
	if (!Intergers) {
		free(Names);
		fclose(File);
		return 0;
	}
	Offsets = &Intergers[Header.NumberAmount];

	/* Names. */
	if (fread(Names, Header.NameSize, 1, File) != 1 ||
		Names[Header.NameSize - 1])
		goto Err;

	/* Booleans. */
	if (fread(Booleans, Header.BoolSize, 1, File) != 1)
		goto Err;

	/* Padding byte. */
	if ((Header.NameSize + Header.BoolSize) & 1)
		getc(File);

	/* Numbers. */
	for (I = 0; I < Header.NumberAmount; I++) {
		int32_t N;

		if (fread(&N, Extended ? sizeof(int32_t) : sizeof(int16_t), 1, File) != 1)
			goto Err;

		Intergers[I] = Extended ? le32toh(N) : le16toh(N);
	}

	/* Offsets. */
	for (I = 0; I < Header.StringOffsetAmount; I++) {
		int16_t O;

		if (fread(&O, sizeof(O), 1, File) != 1)
			goto Err;
		O = le16toh(O);

		if (O >= Header.StringSize)
			goto Err;

		Offsets[I] = O;
	}

	/* Strings. */
	if (fread(Strings, Header.StringSize, 1, File) != 1 ||
		Strings[Header.StringSize - 1])
		goto Err;

	fclose(File);

	BooleanAmount = Header.BoolSize;
	StringSize = Header.StringSize;
	IntergerAmount = Header.NumberAmount;
	OffsetAmount = Header.StringOffsetAmount;

	return 1;

	Err:
	free(Names);
	free(Intergers);
	fclose(File);
	return 0;
}

void FreeHints()
{
	free(Names);
	free(Intergers);

	return;
}

static int LocateTermInfoWithinDir(const char *Term, const char *Dir)
{
	char TermFile[2048];
	int Output;

	Output = snprintf(TermFile, sizeof(TermFile), "%s/%c/%s", Dir, Term[0], Term);
	if (Output <= 0 || Output >= sizeof(TermFile))
		return 0;

	return LoadTermInfo(TermFile);
}

/* Documented in terminfo(5). */
int LocateAndLoadTermInfo()
{
	char *Env;
	char TermEnv[128], Path[2048];
	int ScannedEtc;
	const char DefaultPath[] = "/etc/terminfo";

	Env = getenv("TERM");
	if (!Env || strlen(Env) >= sizeof(TermEnv))
		goto End;
	strcpy(TermEnv, Env);

	Env = getenv("TERMINFO");
	if (Env)
		return LocateTermInfoWithinDir(TermEnv, Env);

	Env = getenv("HOME");
	if (!Env) {
		struct passwd *PW;

		PW = getpwuid(getuid());
		if (PW)
			Env = PW->pw_dir;
	}
	if (Env) {
		int Output;

		Output = snprintf(Path, sizeof(Path), "%s/.terminfo", Env);
		if (Output > 0 && Output < sizeof(Path))
			if (LocateTermInfoWithinDir(TermEnv, Path))
				return 1;
	}

	ScannedEtc = 0;
	Env = getenv("TERMINFO_DIRS");
	if (Env) {
		int Done;

		Done = 0;
		do {
			char *C;
			int S;

			C = strchr(Env, ':');
			if (!C) {
				C = strchr(Env, '\0');
				Done = 1;
			}

			S = C - Env;
			if (!S) {
				if (!ScannedEtc) {
					if (LocateTermInfoWithinDir(TermEnv, DefaultPath))
						return 1;
					ScannedEtc = 1;
				}
			} else {
				if (S < sizeof(Path)) {
					strncpy(Path, Env, S);
					if (LocateTermInfoWithinDir(TermEnv, Path))
						return 1;
				}
			}

			Env = C + 1;
		} while (!Done);
	}

	if (!ScannedEtc && LocateTermInfoWithinDir(TermEnv, DefaultPath))
		return 1;
	if (LocateTermInfoWithinDir(TermEnv, "/lib/terminfo"))
		return 1;
	if (LocateTermInfoWithinDir(TermEnv, "/usr/share/terminfo"))
		return 1;

	End:
	BooleanAmount = StringSize = IntergerAmount = OffsetAmount = 0;
	Names = NULL;
	Intergers = NULL;
	return 0;
}

