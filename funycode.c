/*
 * Copyright (c) 2022 Willemijn Coene
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
#include <locale.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define nitems(arr)	(sizeof(arr) / sizeof((arr)[0]))

#define BASE		62

#define BACKREF		0xd800

#define COPYBITS	4
#define COPYMASK	((1 << COPYBITS) - 1)
#define MINCOPY		4
#define MAXCOPY		((1 << COPYBITS) - 1 + MINCOPY)

#define DISTBITS	7
#define DISTMASK	(((1 << DISTBITS) - 1) << COPYBITS)
#define MINDIST		1
#define MAXDIST		((1 << DISTBITS) - 1 + MINDIST)

static bool
isname(wchar_t wc)
{
	return (wc >= L'0' && wc <= L'9') ||
	       (wc >= L'A' && wc <= L'Z') ||
	       (wc >= L'a' && wc <= L'z');
}

static bool
isbackref(wchar_t wc)
{
	return wc >= BACKREF && wc <= (BACKREF | COPYMASK | DISTMASK);
}

static size_t
satsub(size_t a, size_t b)
{
	return a > b ? a - b : 0;
}

static int
tresh(size_t pos)
{
	return pos == 0 ? 2 : 52;
}

static void
out(char *buf, size_t len, size_t pos, char ch)
{
	if (pos < len)
		buf[pos] = ch;
}

static void
wout(wchar_t *buf, size_t len, size_t pos, wchar_t ch)
{
	if (pos < len)
		buf[pos] = ch;
}


/*
 * Encode val as a base 62 digit.
 */

static void
put(char *buf, size_t len, int val)
{
	assert(val >= 0 && val <= BASE - 1);

	if (val >= 0 && val <= 9)
		out(buf, len, 0, val + '0');
	else if (val >= 10 && val <= 35)
		out(buf, len, 0, val - 10 + 'A');
	else if (val >= 36 && val <= 61)
		out(buf, len, 0, val - 36 + 'a');
}


/*
 * Encode a symbol as base 62.
 */

static size_t
encode(char *buf, size_t len, intmax_t sym)
{
	size_t pos = 0;
	int t;
	imaxdiv_t div;

	while (true) {
		t = tresh(pos);
		if (sym < t) {
			put(buf + pos, satsub(len, pos), (int) sym);
			pos++;
			break;
		}

		div = imaxdiv(sym - t, BASE - t);
		put(buf + pos, satsub(len, pos), (int) div.rem + t);
		pos++;

		sym = div.quot;
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

static unsigned int
hash(const wchar_t *buf, size_t len)
{
	size_t i;
	unsigned int h = 0;

	if (len < MINCOPY)
		return 0;

	for (i = 0; i < MINCOPY; i++)
		h = (h * 33) + buf[i];

	return h;
}

static size_t
cmp(const wchar_t *buf, size_t len, size_t a, size_t b)
{
	size_t i;

	if (a == b || len - a < MINCOPY || len - b < MINCOPY)
		return 0;

	for (i = 0; i < MAXCOPY && a + i < len && b + i < len; i++)
		if (buf[a + i] != buf[b + i])
			break;

	return i;
}

size_t
funencode_l(char *enc, size_t enclen, const char *name, size_t namelen,
    locale_t loc)
{
	mbstate_t mbs;
	wchar_t *wname = NULL, *wbuf = NULL;
	size_t namepos, encpos, htab[512] = { 0 };
	struct code *codes = NULL;
	int ncodes = 0, maxcodes = 0;

	/*
	 * Convert the input to wide characters.
	 */

	wname = malloc(namelen * sizeof(wchar_t));
	if (wname == NULL)
		goto fail;

	memset(&mbs, '\0', sizeof(mbs));
	namelen = mbsnrtowcs(wname, &name, namelen, namelen, &mbs);
	if (namelen == (size_t) -1)
		goto fail;

	/*
	 * Compress the input.
	 */

	wbuf = malloc(namelen * sizeof(wchar_t));
	if (wbuf == NULL)
		goto fail;

	encpos = 0;
	for (namepos = 0; namepos < namelen; ) {
		unsigned int h;
		size_t len;

		h = hash(wname + namepos, satsub(namelen, namepos)) % nitems(htab);
		if (namepos - htab[h] <= MAXDIST &&
		    (len = cmp(wname, namelen, htab[h], namepos)) >= MINCOPY) {
			wbuf[encpos++] = BACKREF +
			    (len - MINCOPY) +
			    ((namepos - htab[h] - MINDIST) << COPYBITS);
		} else {
			wbuf[encpos++] = wname[namepos];
			len = 1;
		}

		htab[h] = namepos;
		namepos += len;
	}

	free(wname);
	wname = wbuf;
	wbuf = NULL;
	namelen = encpos;

	/*
	 * Process all characters in the name, directly outputting all those
	 * that are valid in the target character set and gathering up all
	 * that need to be encoded.
	 */

	encpos = 0;
	for (namepos = 0; namepos < namelen; namepos++) {
		if (isname(wname[namepos])) {
			out(enc, enclen, encpos++, wctob(wname[namepos]));
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
				.wc = wname[namepos],
				.pos = namepos
			};
		}
	}

	free(wname);
	wname = NULL;

	if (ncodes) {
		size_t plen, rlen;
		intmax_t last, sym;
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
		 * Generate the suffix
		 */

		if (plen != 0)
			out(enc, enclen, encpos++, '_');

		last = -10 * (rlen + 1);
		for (i = 0; i < ncodes; i++) {
			sym = codes[i].wc * (rlen + 1) + codes[i].pos;
			encpos += encode(enc + encpos, satsub(enclen, encpos), sym - last);
			rlen++;
			last = codes[i].wc * (rlen + 1) + codes[i].pos + 1;
		}

		if (plen == 0)
			out(enc, enclen, encpos++, '_');

		free(codes);
		codes = NULL;
	}

	out(enc, enclen, encpos, '\0');

	return encpos;

fail:
	free(wname);
	free(wbuf);
	free(codes);

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
	}

	return 0;
}


/*
 * Decode a base 62-encoded symbol.
 */

static size_t
decode(const char *buf, size_t len, intmax_t *sym)
{
	size_t pos = 0;
	int t, v;
	intmax_t w = 1;

	*sym = 0;
	while (true) {
		t = tresh(pos);
		v = get(buf + pos, satsub(len, pos));
		if (v < 0)
			return FUNYCODE_ERR;

		*sym += (intmax_t) v * w;
		w *= BASE - t;
		pos++;

		if (v < t)
			break;
	}

	return pos;
}


size_t
fundecode_l(char *name, size_t namelen, const char *enc, size_t enclen,
    locale_t loc)
{
	mbstate_t mbs;
	wchar_t *wname = NULL, *wbuf = NULL;
	char *p;
	size_t namepos = 0, encpos = 0, plen, wnamelen, i;
	intmax_t last;

	wnamelen = (namelen > enclen ? namelen : enclen) * 2;
	wname = malloc(wnamelen * sizeof(wchar_t));
	if (wname == NULL)
		goto fail;

	/*
	 * Split the encoded string into the prefix and suffix (separated by
	 * an underscore), and output the prefix. Note that strings only
	 * containing a suffix have the underscore at the end, and strings
	 * only containing a prefix don't end in an underscore at all.
	 */

	p = memrchr(enc, '_', enclen);
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
		wout(wname, wnamelen, namepos++, enc[i]);

	/*
	 * Insert all encoded characters.
	 */

	last = -10 * (namepos + 1);
	while (encpos < enclen) {
		size_t len;
		intmax_t sym;
		imaxdiv_t div;

		len = decode(enc + encpos, satsub(enclen, encpos), &sym);
		if (len == FUNYCODE_ERR)
			return FUNYCODE_ERR;

		encpos += len;
		sym += last;

		div = imaxdiv(sym, namepos + 1);
		if (div.rem < wnamelen) {
			wmemmove(wname + div.rem + 1, wname + div.rem, wnamelen - div.rem - 1);
			wname[div.rem] = (wchar_t) div.quot;
		}

		namepos++;
		last = div.quot * (namepos + 1) + div.rem + 1;
	}

	/*
	 * Decompress the result
	 */

	wbuf = malloc(wnamelen * sizeof(wchar_t));
	if (wbuf == NULL)
		goto fail;

	encpos = 0;
	for (i = 0; i < namepos; i++) {
		if (isbackref(wname[i])) {
			size_t pos, len;

			pos = encpos - (((wname[i] & DISTMASK) >> COPYBITS) + MINDIST);
			len = (wname[i] & COPYMASK) + MINCOPY;

			while (len-- > 0) {
				if (encpos < wnamelen)
					wbuf[encpos] = wbuf[pos];
				encpos++;
				pos++;
			}
		} else {
			if (encpos < wnamelen)
				wbuf[encpos] = wname[i];
			encpos++;
		}
	}

	free(wname);
	wname = wbuf;
	wbuf = NULL;
	namepos = encpos;

	/*
	 * Convert the output to wide characters.
	 */

	memset(&mbs, '\0', sizeof(mbs));
	wbuf = wname;
	plen = wcsnrtombs(NULL, (const wchar_t **) &wbuf, namepos, 0, &mbs);

	wbuf = wname;
	i = wcsnrtombs(name, (const wchar_t **) &wbuf, namepos, namelen, &mbs);

	wbuf = NULL;
	if (i == (size_t) -1)
		goto fail;

	free(wname);
	out(name, namelen, i, '\0');

	return plen;

fail:
	free(wname);
	free(wbuf);

	return FUNYCODE_ERR;
}

size_t
fundecode(char *name, size_t namelen, const char *enc, size_t enclen)
{
	return fundecode_l(name, namelen, enc, enclen, LC_GLOBAL_LOCALE);
}
