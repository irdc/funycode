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

#ifndef FUNYCODE_H
#define FUNYCODE_H

#include <stddef.h>

#define FUNYCODE_ERR	((size_t) -1)

size_t		 funencode(char *enc, size_t enclen,
		     const char *name, size_t namelen);
size_t		 fundecode(char *name, size_t namelen,
		     const char *enc, size_t enclen);

#ifdef LC_GLOBAL_LOCALE
size_t		 funencode_l(char *enc, size_t enclen,
		     const char *name, size_t namelen, locale_t loc);
size_t		 fundecode_l(char *name, size_t namelen,
		     const char *enc, size_t enclen, locale_t loc);
#endif

#ifdef WCHAR_MAX
size_t		 wfunencode(char *enc, size_t enclen,
		     const wchar_t *name, size_t namelen);
size_t		 wfundecode(wchar_t *name, size_t namelen,
		     const char *enc, size_t enclen);
#endif

#endif /* FUNYCODE_H */
