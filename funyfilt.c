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

#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <unistd.h>
#include <err.h>
#include <stdlib.h>
#include <stdint.h>

#define nitems(arr)	(sizeof(arr) / sizeof((arr)[0]))

int
main(int argc, char *const *argv)
{
	int ch, eflag;
	size_t linecap = 0, namecap = 0, namelen;
	ssize_t linelen;
	char *name = NULL, *line = NULL;

	setlocale(LC_CTYPE, "");

#if defined(__OpenBSD__)
	if (pledge("stdio", "") < 0)
		err(1, "pledge");
#endif

	eflag = 0;
	while ((ch = getopt(argc, argv, "e")) != -1) {
		switch (ch) {
		case 'e':
			eflag = 1;
			break;

		case '?':
		default:
			fprintf(stderr, "Usage: %s [-e]\n", argv[0]);
			return 1;
		}
	}

	argc -= optind;
	argv += optind;

	while ((linelen = getline(&line, &linecap, stdin)) > 0) {
		while (line[linelen - 1] == '\n')
			line[--linelen] = '\0';

		namelen = (eflag ? funencode : fundecode)(name, namecap, line, linelen);
		while (namelen > namecap) {
			void *new;

			namecap = namecap == 0 ? 64 : (namecap * 2);
			if (namecap > UINT16_MAX)
				errx(1, "result too long (did you mean '-e'?)");

			new = realloc(name, namecap);
			if (new == NULL)
				err(1, "realloc");
	
			name = new;

			namelen = (eflag ? funencode : fundecode)(name, namecap, line, linelen);
		}

		printf("%s\n", name);
	}

	return 0;
}
