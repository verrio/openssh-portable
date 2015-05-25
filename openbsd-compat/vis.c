/*	$OpenBSD: vis.c,v 1.19 2005/09/01 17:15:49 millert Exp $ */
/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* OPENBSD ORIGINAL: lib/libc/gen/vis.c */

#include "includes.h"
#if !defined(HAVE_STRNVIS) || defined(BROKEN_STRNVIS)

#include <string.h>
#include <wchar.h>

#include "vis.h"

#define	isvisible(c) \
	((iswprint(c) &&  \
	(((flag & VIS_GLOB) == 0) || ((c) != (wint_t) '*' && (c) != (wint_t) '?' && (c) != (wint_t) '[' && (c) != (wint_t) '#')) && \
	((flag & VIS_SP) == 0 || (c) != (wint_t) ' ') && \
	((flag & VIS_TAB) == 0 || (c) != (wint_t) '\t') && \
	((flag & VIS_NL) == 0 || (c) != (wint_t) '\n')) || \
	((flag & VIS_SAFE) && ((c) == (wint_t) '\a' || (c) == (wint_t) '\b' || (c) == (wint_t) '\r' || (c) == (wint_t) '\n' || (c) == (wint_t) ' ')))

/**
 * fetch next UTF-8 character from buffer
 */
wint_t getUTF8(const char **src)
{
	const uint8_t **usrc = src;
	uint32_t ret;
	size_t enclen;

	if (**usrc < 0x80) {
		enclen = 1;
		ret = **usrc;
	} else if ((**usrc & 0xe0) == 0xc0) {
		enclen = 2;
		ret = **usrc & 0x1f;
	} else if ((**usrc & 0xf0) == 0xe0) {
		enclen = 3;
		ret = **usrc & 0x0f;
	} else if ((**usrc & 0xf8) == 0xf0) {
		enclen = 4;
		ret = **usrc & 0x07;
	} else {
		enclen = -1;
	}

	(*usrc)++;
	if (enclen < 0)
		return (wint_t) 0xfffd;

	for (--enclen; enclen > 0; --enclen) {
		if ((**usrc & 0xc0) != 0x80) {
			if (**usrc)
				(*usrc)++;
			return (wint_t) 0xfffd;
		}
		ret = (ret << 6) | (**usrc & 0x3f);
		(*usrc)++;
	}

	return (wint_t) ret;
}

/**
 * encode unicode point to UTF-8
 */
size_t putUTF8(wint_t c, char *dst)
{
	if (c < 0x80) {
		*dst = (char) c;
		return 1;
	} else if (c < 0x800) {
		*dst++ = 0xc0 + (c >> 6);
		*dst = 0x80 + (c & 0x3f);
		return 2;
	} else if (c > 0xdfff && c < 0xe000) {
		return 0;
	} else if (c < 0x10000) {
		*dst++ = 0xe0 + (c >> 12);
		*dst++ = 0x80 + ((c >> 6) & 0x3f);
		*dst = 0x80 + (c & 0x3f);
		return 3;
	} else if (c < 0x110000) {
		*dst++ = 0xf0 + (c >> 18);
		*dst++ = 0x80 + ((c >> 12) & 0x3f);
		*dst++ = 0x80 + ((c >> 6) & 0x3f);
		*dst = 0x80 + (c & 0x3f);
		return 4;
	}
	return 0;
}

/**
 * filter C0/C1 control characters and other annoying ranges
 */
int iswprint(wint_t wc) {
	/* U+0000–U+001F, U+0080–U+009F */
	if (wc < 0xff)
		return ((wc+1) & 0x7f) > 0x20;
	/* U+200E-U+200F */
	if (wc < 0x2010)
		return wc < 0x200e;
	/* U+2028-U+202E */
	if (wc < 0x202f)
		return wc < 0x2028;
	/* U+D800-U+DFFF*/
	if (wc < 0xe000)
		return wc < 0xd800;
	/* U+FFF9-U+FFFB */
	if (wc < 0xfffc)
		return wc < 0xfff9;
	return wc < 0x10ffff;
}

/**
 * univis - visually encode unicode characters
 */
size_t univis(wint_t c, char **dst, size_t buf_len, int flag)
{
	char tbuf[16];
	size_t len = 0;

	if (isvisible(c)) {
		len = putUTF8(c, tbuf);
		if (c == '\\' && (flag & VIS_NOSLASH) == 0) {
			tbuf[len++] = '\\';
		}
		goto done;
	}

	if (flag & VIS_CSTYLE) {
		switch(c) {
		case '\n':
			tbuf[0] = '\\';
			tbuf[1] = 'n';
			len = 2;
			goto done;
		case '\r':
			tbuf[0] = '\\';
			tbuf[1] = 'r';
			len = 2;
			goto done;
		case '\b':
			tbuf[0] = '\\';
			tbuf[1] = 'b';
			len = 2;
			goto done;
		case '\a':
			tbuf[0] = '\\';
			tbuf[1] = 'a';
			len = 2;
			goto done;
		case '\v':
			tbuf[0] = '\\';
			tbuf[1] = 'v';
			len = 2;
			goto done;
		case '\t':
			tbuf[0] = '\\';
			tbuf[1] = 't';
			len = 2;
			goto done;
		case '\f':
			tbuf[0] = '\\';
			tbuf[1] = 'f';
			len = 2;
			goto done;
		case ' ':
			tbuf[0] = '\\';
			tbuf[1] = 's';
			len = 2;
			goto done;
		case '\0':
			tbuf[0] = '\\';
			tbuf[1] = '0';
			len = 2;
			goto done;
		}
	}

	if (c < 0x20) {
		len = putUTF8(0x2400 + c, tbuf);
		goto done;
	}

	len = snprintf(tbuf, 16, "\\u%x", (uint32_t) (0xffffff & c));

	done:
		if (len > buf_len)
			return 0;
		size_t i;
		for (i = 0; i < len; i++, (*dst)++)
			**dst = tbuf[i];
		return len;
}

/**
 * visually encode UTF-8 characters from src into dst
 *
 * dst must be 4 times the size of src to account for possible
 * expansion.  The length of dst, not including the trailing NULL,
 * is returned.
 *
 * strnvis will write no more than siz-1 bytes (and will NULL terminate).
 * The number of bytes needed to fully encode the string is returned.
 */
int strnvis(char *dst, const char *src, size_t siz, int flag)
{
	char tbuf[16];
	char *tbufp = tbuf;
	char *start = dst;
	char *end = start + siz - 1;
	wint_t ucode;

	int i;
	while (*src && dst < end ) {
		ucode = getUTF8(&src);
		i = univis(ucode, &dst, end - dst, flag);
		if (i == 0)
			break;
	}

	if (dst < end)
		*dst = '\0';
	/* adjust return value for truncation */
	while (*src) {
		ucode = getUTF8(&src);
		dst += univis(ucode, &tbufp, sizeof(tbuf), flag);
		tbufp = tbuf;
	}

	return (dst - start);
}

#endif
