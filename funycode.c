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
#define IN(buf, len, pos)                                                   \
	({                                                                  \
		size_t p_= (pos);                                           \
		p_ < (len) ? (buf)[p_] : 0;                                 \
	})

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

static int
hash(const wchar_t *buf)
{
	size_t i;
	uint64_t h;

	/* FNV-1a hash (see http://www.isthe.com/chongo/tech/comp/fnv/) */
	h = UINT64_C(0xcbf29ce484222325);
	for (i = 0; i < MINCOPY - 1; i++)
		h = (h ^ buf[i]) * UINT64_C(0x100000001b3);

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
	size_t srcpos, dstpos, htab[512] = { 0 };

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
 * Check if a character is to be encoded. Digits are only encoded if they
 * appear before any alphabetical characters.
 */

static bool
isenc(wchar_t ch, bool first)
{
	if ((ch >= L'A' && ch <= L'Z') ||
	    (ch >= L'a' && ch <= L'z'))
		return false;

	if (!first && ch >= L'0' && ch <= L'9')
		return false;

	return true;
}

/*
 * Encode value as a base 62 digit.
 */

static char
encode_value(int val)
{
	assert(val >= 0 && val <= BASE - 1);

	if (val >= 0 && val <= 9)
		return val + '0';
	else if (val >= 10 && val <= 35)
		return val - 10 + 'A';
	else if (val >= 36 && val <= 61)
		return val - 36 + 'a';

	return 0;
}


/*
 * Encode a delta as base 62.
 */

static int
encode(char *buf, size_t len, size_t pos, intmax_t bias, intmax_t delta)
{
	int i;
	intmax_t t;
	imaxdiv_t div;

	for (i = 0; ; i++) {
		t = (i + 1) * BASE - bias;
		t = t < TMIN ? TMIN : t > TMAX ? TMAX : t;

		if (delta < t) {
			OUT(buf, len, pos + i, encode_value((int) delta));
			break;
		}

		div = imaxdiv(delta - t, BASE - t);
		OUT(buf, len, pos + i, encode_value((int) div.rem + t));
		delta = div.quot;
	}

	return i + 1;
}

size_t
wfunencode(char *enc, size_t enclen, const wchar_t *name, size_t namelen)
{
	wchar_t *buf = NULL;
	size_t i, encpos, declen;
	wchar_t n, next;
	intmax_t bias, last;

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
	 * Directly output all characters that are valid in C symbols. We'll
	 * encode the rest later on. Note that leading digits always get
	 * encoded, to ensure we always produce a valid C symbol.
	 */

	encpos = 0;
	for (i = 0; i < namelen; i++)
		if (!isenc(buf[i], encpos == 0))
			OUT(enc, enclen, encpos++, wctob(buf[i]));

	if (encpos == namelen)
		goto done;

	/*
	 * Encode the remaining characters as part of the suffix.
	 */

	declen = encpos;
	if (encpos != 0)
		OUT(enc, enclen, encpos++, '_');

	bias = -1;
	last = INITIAL_N * (declen + 1);
	if (encpos == 0)
		last -= 10;

	for (n = INITIAL_N, next = WCHAR_MAX;
	     n < WCHAR_MAX;
	     n = next, next = WCHAR_MAX) {
		bool first = true;
		size_t decpos;

		for (i = 0, decpos = 0; i < namelen; i++) {
			wchar_t ch;
			intmax_t delta;

			ch = buf[i];
			if (!isenc(ch, first)) {
				first = false;
				decpos++;
			} else if (ch < n) {
				decpos++;
			} else if (ch > n && ch < next) {
				next = ch;
			}

			if (ch != n)
				continue;

			delta = ch * (declen + 1) + decpos - last;
			encpos += encode(enc, enclen, encpos,
			    bias < 0 ? INITIAL_BIAS : bias, delta);

			last = ch * (++declen + 1) + ++decpos;
			bias = adapt(delta, declen, bias < 0);
		}

		if (next == WCHAR_MAX && first)
			OUT(enc, enclen, encpos++, '_');
	}

done:
	free(buf);
	OUT(enc, enclen, encpos, '\0');

	return encpos;

fail:
	free(buf);

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
decode_value(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	else if (ch >= 'A' && ch <= 'Z')
		return ch - 'A' + 10;
	else if (ch >= 'a' && ch <= 'z')
		return ch - 'a' + 36;
	else
		return -1;
}

/*
 * Decode a base 62-encoded delta.
 */

static int
decode(const char *buf, size_t len, size_t pos, intmax_t bias, intmax_t *delta)
{
	int i, v;
	intmax_t t;
	intmax_t w = 1;

	*delta = 0;
	for (i = 0; ; i++) {
		t = (i + 1) * BASE - bias;
		t = t < TMIN ? TMIN : t > TMAX ? TMAX : t;

		v = decode_value(IN(buf, len, pos + i));
		if (v < 0)
			return -1;

		*delta += (intmax_t) v * w;
		w *= BASE - t;

		if (v < t)
			break;
	}

	return i + 1;
}

size_t
wfundecode(wchar_t *name, size_t namelen, const char *enc, size_t enclen)
{
	wchar_t *buf;
	size_t buflen, namepos, encpos;
	intmax_t bias, last;

	buflen = namelen > enclen * 2 ? namelen : enclen * 2;
	buf = malloc(buflen * sizeof(wchar_t));
	if (buf == NULL)
		goto fail;

	/*
	 * Output the unencoded part of the string (the prefix). Note that
	 * strings only containing an encoded suffix have the underscore at
	 * the end, and strings only containing a prefix don't contain an
	 * underscore at all.
	 */

	encpos = namepos = 0;
	if (IN(enc, enclen, enclen - 1) == '_') {
		/* suffix only */
		enclen--;
	} else {
		while (encpos < enclen && IN(enc, enclen, encpos) != '_')
			OUT(buf, buflen, namepos++, IN(enc, enclen, encpos++));

		if (IN(enc, enclen, encpos) == '_')
			encpos++;
	}

	/*
	 * Insert all encoded characters. Handle the fact that a suffix
	 * without a prefix can never start with a digit.
	 */

	bias = -1;
	last = INITIAL_N * (namepos + 1);
	if (namepos == 0)
		last -= 10;

	while (encpos < enclen) {
		int len;
		intmax_t delta;
		imaxdiv_t div;

		len = decode(enc, enclen, encpos,
		    bias < 0 ? INITIAL_BIAS : bias, &delta);
		if (len < 0)
			goto fail;
		encpos += len;

		div = imaxdiv(delta + last, namepos + 1);
		if (div.rem < buflen) {
			wmemmove(buf + div.rem + 1, buf + div.rem, buflen - div.rem - 1);
			buf[div.rem] = (wchar_t) div.quot;
		}

		last = div.quot * (++namepos + 1) + div.rem + 1;
		bias = adapt(delta, namepos, bias < 0);
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
