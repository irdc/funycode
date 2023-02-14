/*
 * Copyright (c) 2022, 2023 Willemijn Coene
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "funycode.h"

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef __APPLE__
# include <xlocale.h>
#else
# include <locale.h>
#endif

#define nitems(arr)	(sizeof(arr) / sizeof((arr)[0]))

/*
 * Bootstring parameters. These were empirically determined give generally
 * good results.
 */

#define BASE		62
#define TMIN		1
#define TMAX		52
#define SKEW		208
#define DAMP		700
#define INITIAL_BIAS	(BASE * 2 - TMAX / 2)
#define INITIAL_N	32

#define OUT(buf, len, pos, val)                                             \
	do {                                                                \
		size_t p_ = (pos);                                          \
		int v_ = (val);                                             \
		if (p_ < (len))                                             \
			(buf)[p_] = v_;                                     \
	} while (0)

/*
 * Compress symbols using a simple algorithm based on LSRW1-A. Abuse
 * surrogate pairs (which should never occur in well-formed input anyway)
 * for encoding matches.
 */

#define BACKREF		0xd800

#define COPYBITS	4
#define COPYMASK	((1 << COPYBITS) - 1)
#define MINCOPY		4
#define MAXCOPY		((1 << COPYBITS) - 1 + MINCOPY)

#define DISTBITS	7
#define DISTMASK	(((1 << DISTBITS) - 1) << COPYBITS)
#define MINDIST		1
#define MAXDIST		((1 << DISTBITS) - 1 + MINDIST)

/* FNV hash (see http://www.isthe.com/chongo/tech/comp/fnv/) */
#if SIZE_MAX == UINT32_MAX
# define FNVHASH	uint32_t
# define FNVBASIS	0x811c9dc5
# define FNVPRIME	0x01000193
#else
# define FNVHASH	uint64_t
# define FNVBASIS	0xcbf29ce484222325ULL
# define FNVPRIME	0x100000001b3ULL
#endif

static int
hash(const wchar_t *buf)
{
	size_t i;
	FNVHASH h = FNVBASIS;

	for (i = 0; i < MINCOPY - 1; i++)
		h = (h ^ buf[i]) * FNVPRIME;

	return (int) h;
}

static size_t
prefix(const wchar_t *src, size_t srclen, size_t a, size_t b)
{
	size_t len;

	for (len = 0;
	     len < MAXCOPY && a + len < srclen && b + len < srclen;
	     len++)
		if (src[a + len] != src[b + len])
			break;

	return len;
}

static size_t
compress(wchar_t *dst, size_t dstlen, const wchar_t *src, size_t srclen)
{
	size_t srcpos, dstpos, htab[512];

	srcpos = dstpos = 0;
	while (srcpos + MINCOPY < srclen && srcpos < MINCOPY) {
		int h;

		OUT(dst, dstlen, dstpos, src[srcpos]);
		h = hash(src + srcpos) % nitems(htab);
		htab[h] = srcpos++;
		dstpos++;
	}

	while (srcpos + MINCOPY < srclen) {
		int h;
		size_t len;

		h = hash(src + srcpos) % nitems(htab);
		if (srcpos - htab[h] >= MINDIST &&
		    srcpos - htab[h] <= MAXDIST &&
		    (len = prefix(src, srclen, srcpos, htab[h])) >= MINCOPY) {
			OUT(dst, dstlen, dstpos, BACKREF + (len - MINCOPY) +
			    ((srcpos - htab[h] - MINDIST) << COPYBITS));
		} else {
			OUT(dst, dstlen, dstpos, src[srcpos]);
			len = 1;
		}

		dstpos++;

		while (1) {
			htab[h] = srcpos++;

			if (--len == 0)
				break;

			h = hash(src + srcpos) % nitems(htab);
		}
	}

	while (srcpos < srclen)
		OUT(dst, dstlen, dstpos++, src[srcpos++]);

	return dstpos;
}

static size_t
decompress(wchar_t *dst, size_t dstlen, const wchar_t *src, size_t srclen)
{
	size_t srcpos, dstpos;

	srcpos = dstpos = 0;
	while (srcpos < srclen) {
		wchar_t ch;

		ch = src[srcpos++];
		if ((ch & ~(COPYMASK | DISTMASK)) == BACKREF) {
			size_t pos, len;

			pos = dstpos - (((ch & DISTMASK) >> COPYBITS) + MINDIST);
			len = (ch & COPYMASK) + MINCOPY;

			while (len-- > 0)
				OUT(dst, dstlen, dstpos++, dst[pos++]);
		} else {
			OUT(dst, dstlen, dstpos++, ch);
		}
	}

	return dstpos;
}


static size_t
satsub(size_t a, size_t b)
{
	return a > b ? a - b : 0;
}

static intmax_t
tresh(size_t pos, intmax_t bias)
{
	intmax_t t;

	t = (pos + 1) * BASE - bias;

	return t < TMIN ? TMIN : t > TMAX ? TMAX : t;
}

static intmax_t
adapt(intmax_t delta, size_t outpos, bool first)
{
	intmax_t k;

	delta = (first ? delta / DAMP : delta / 2) + (delta / outpos);
	for (k = 0; delta > (BASE - TMIN) * TMAX / 2; k += BASE) {
		delta /= BASE - TMIN;
	}

	return k + (BASE - TMIN + 1) * delta / (delta + SKEW);
}


/*
 * Encode val as a base 62 digit.
 */

static void
put(char *buf, size_t len, int val)
{
	int ch;

	assert(val >= 0 && val <= BASE - 1);

	if (val >= 0 && val <= 9)
		ch = val + '0';
	else if (val >= 10 && val <= 35)
		ch = val - 10 + 'A';
	else if (val >= 36 && val <= 61)
		ch = val - 36 + 'a';
	else
		return;

	OUT(buf, len, 0, ch);
}


/*
 * Encode a delta as base 62.
 */

static size_t
encode(char *buf, size_t len, intmax_t bias, intmax_t delta)
{
	size_t pos = 0;
	int t;
	imaxdiv_t div;

	while (true) {
		t = tresh(pos, bias);
		if (delta < t) {
			put(buf + pos, satsub(len, pos), (int) delta);
			pos++;
			break;
		}

		div = imaxdiv(delta - t, BASE - t);
		put(buf + pos, satsub(len, pos), (int) div.rem + t);
		pos++;

		delta = div.quot;
	}

	return pos;
}

struct code {
	wchar_t		 wc;
	size_t		 pos;
};

static int
codecmp(const void *a, const void *b)
{
	const struct code *ac = a, *bc = b;
	int cmp;

	cmp = (int) ac->wc - (int) bc->wc;
	if (cmp == 0)
		cmp = ac->pos < bc->pos ? -1 : ac->pos > bc->pos ? 1 : 0;

	return cmp;
}

size_t
wfunencode(char *enc, size_t enclen, const wchar_t *name, size_t namelen)
{
	wchar_t *buf = NULL;
	size_t namepos, encpos;
	struct code *codes = NULL;
	int ncodes = 0, maxcodes = 0;

	/*
	 * Compress the input.
	 */

	buf = malloc(namelen * sizeof(wchar_t));
	if (buf == NULL)
		goto fail;

	namelen = compress(buf, namelen, name, namelen);
	if (namelen == FUNYCODE_ERR)
		goto fail;

	/*
	 * Process all characters in the name, directly outputting all those
	 * that are valid C symbol characters and gathering up all that need
	 * to be encoded. In addition, forcibly encode leading digits, to
	 * ensure we always produce a valid C symbol.
	 */

	encpos = 0;
	for (namepos = 0; namepos < namelen; namepos++) {
		if ((buf[namepos] >= L'A' && buf[namepos] <= L'Z') ||
		    (buf[namepos] >= L'a' && buf[namepos] <= L'z') ||
		    (buf[namepos] >= L'0' && buf[namepos] <= L'9' && encpos != 0)) {
			OUT(enc, enclen, encpos++, wctob(buf[namepos]));
		} else {
			if (ncodes >= maxcodes) {
				struct code *new;

				maxcodes = maxcodes == 0 ? 4 : maxcodes * 2;
				new = realloc(codes, maxcodes * sizeof(codes[0]));
				if (new == NULL)
					goto fail;

				codes = new;
			}

			codes[ncodes++] = (struct code) {
				.wc = buf[namepos],
				.pos = namepos
			};
		}
	}

	free(buf);
	buf = NULL;

	if (ncodes) {
		size_t plen, rlen;
		intmax_t bias, last, delta;
		int i, j;

		rlen = plen = encpos;

		/*
		 * Sort all code points to optimise the encoded string and correct
		 * the insertion position to take insertion order into account.
		 */

		qsort(codes, ncodes, sizeof(codes[0]), codecmp);
		for (i = 0; i < ncodes; i++) {
			size_t ofs = 0;

			for (j = i + 1; j < ncodes; j++)
				if (codes[j].pos < codes[i].pos)
					ofs++;

			codes[i].pos -= ofs;
		}

		/*
		 * Generate the suffix. If we don't have a prefix, make sure
		 * the suffix never starts with a digit.
		 */

		if (plen != 0)
			OUT(enc, enclen, encpos++, '_');

		bias = INITIAL_BIAS;
		last = INITIAL_N * (rlen + 1);
		if (plen == 0)
			last -= 10 * (rlen + 1);
		for (i = 0; i < ncodes; i++) {
			delta = codes[i].wc * (rlen + 1) + codes[i].pos - last;
			encpos += encode(enc + encpos, satsub(enclen, encpos), bias, delta);
			rlen++;
			last = codes[i].wc * (rlen + 1) + codes[i].pos + 1;
			bias = adapt(delta, rlen, i == 0);
		}

		if (plen == 0)
			OUT(enc, enclen, encpos++, '_');

		free(codes);
		codes = NULL;
	}

	OUT(enc, enclen, encpos, '\0');

	return encpos;

fail:
	free(buf);
	free(codes);

	return FUNYCODE_ERR;
}

size_t
funencode_l(char *enc, size_t enclen, const char *name, size_t namelen,
    locale_t loc)
{
	mbstate_t mbs;
	wchar_t *wname;
	size_t len;

	wname = malloc(namelen * sizeof(wchar_t));
	if (wname == NULL)
		goto fail;

	memset(&mbs, '\0', sizeof(mbs));
	namelen = mbsnrtowcs(wname, &name, namelen, namelen, &mbs);
	if (namelen == (size_t) -1)
		goto fail;

	len = wfunencode(enc, enclen, wname, namelen);
	if (len == FUNYCODE_ERR)
		goto fail;

	return len;

fail:
	free(wname);

	return FUNYCODE_ERR;
}

size_t
funencode(char *enc, size_t enclen, const char *name, size_t namelen)
{
	return funencode_l(enc, enclen, name, namelen, LC_GLOBAL_LOCALE);
}


/*
 * Decode a base 62 digit.
 */

static int
get(const char *buf, size_t len)
{
	if (len > 0) {
		if (*buf >= '0' && *buf <= '9')
			return *buf - '0';
		else if (*buf >= 'A' && *buf <= 'Z')
			return *buf - 'A' + 10;
		else if (*buf >= 'a' && *buf <= 'z')
			return *buf - 'a' + 36;
		else
			return -1;
	}

	return 0;
}


/*
 * Decode a base 62-encoded delta.
 */

static size_t
decode(const char *buf, size_t len, intmax_t bias, intmax_t *delta)
{
	size_t pos = 0;
	int t, v;
	intmax_t w = 1;

	*delta = 0;
	while (true) {
		t = tresh(pos, bias);
		v = get(buf + pos, satsub(len, pos));
		if (v < 0)
			return FUNYCODE_ERR;

		*delta += (intmax_t) v * w;
		w *= BASE - t;
		pos++;

		if (v < t)
			break;
	}

	return pos;
}

size_t
wfundecode(wchar_t *name, size_t namelen, const char *enc, size_t enclen)
{
	wchar_t *buf = NULL;
	char *p;
	size_t buflen, namepos = 0, encpos = 0, plen, i, len;
	intmax_t bias, last;

	buflen = namelen > enclen * 2 ? namelen : enclen * 2;
	buf = malloc(buflen * sizeof(wchar_t));
	if (buf == NULL)
		goto fail;

	/*
	 * Split the encoded string into the prefix and suffix (separated by
	 * an underscore), and output the prefix. Note that strings only
	 * containing a suffix have the underscore at the end, and strings
	 * only containing a prefix don't end in an underscore at all.
	 */

	p = memchr(enc, '_', enclen);
	if (p == enc + enclen - 1) {
		plen = 0;
		enclen--;
	} else if (p != NULL) {
		encpos = p - enc + 1;
		plen = encpos - 1;
	} else {
		encpos = enclen;
		plen = enclen;
	}

	for (i = 0; i < plen; i++)
		OUT(buf, buflen, namepos++, enc[i]);

	/*
	 * Insert all encoded characters. Handle the fact that a suffix
	 * without a prefix can never start with a digit.
	 */

	bias = INITIAL_BIAS;
	last = INITIAL_N * (namepos + 1);
	if (plen == 0)
		last -= 10 * (namepos + 1);
	for (i = 0; i < enclen - encpos; i += len) {
		intmax_t delta;
		imaxdiv_t div;

		len = decode(enc + encpos + i, satsub(enclen, encpos + i), bias, &delta);
		if (len == FUNYCODE_ERR)
			return FUNYCODE_ERR;

		div = imaxdiv(delta + last, namepos + 1);
		if (div.rem < buflen) {
			wmemmove(buf + div.rem + 1, buf + div.rem, buflen - div.rem - 1);
			buf[div.rem] = (wchar_t) div.quot;
		}

		namepos++;
		last = div.quot * (namepos + 1) + div.rem + 1;
		bias = adapt(delta, namepos, i == 0);
	}

	/*
	 * Decompress the result
	 */

	namepos = decompress(name, namelen, buf, namepos);
	if (namepos == FUNYCODE_ERR)
		goto fail;

	free(buf);
	OUT(name, namelen, namepos, '\0');

	return namepos;

fail:
	free(buf);

	return FUNYCODE_ERR;
}

size_t
fundecode_l(char *name, size_t namelen, const char *enc, size_t enclen,
    locale_t loc)
{
	mbstate_t mbs;
	wchar_t *wname, *p;
	size_t wnamelen, wlen, len;

	wnamelen = namelen > enclen * 2 ? namelen : enclen * 2;
	wname = malloc(wnamelen * sizeof(wchar_t));
	if (wname == NULL)
		goto fail;

	wlen = wfundecode(wname, wnamelen, enc, enclen);
	if (wlen == FUNYCODE_ERR)
		goto fail;

	memset(&mbs, '\0', sizeof(mbs));
	p = wname;
	len = wcsnrtombs(NULL, (const wchar_t **) &p, wlen, 0, &mbs);

	p = wname;
	if (wcsnrtombs(name, (const wchar_t **) &p, wlen, namelen, &mbs) == (size_t) -1)
		goto fail;

	free(wname);
	OUT(name, namelen, len, '\0');

	return len;

fail:
	free(wname);

	return FUNYCODE_ERR;
}

size_t
fundecode(char *name, size_t namelen, const char *enc, size_t enclen)
{
	return fundecode_l(name, namelen, enc, enclen, LC_GLOBAL_LOCALE);
}
