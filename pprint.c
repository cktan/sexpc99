/*
  SEXPC99 - S-Expression parser in C
  Copyright (c) 2019-2020 CK Tan
  cktanx@gmail.com

  SEXPC99 can be used for free under the GNU General Public License
  version 3, where anything released into public must be open source,
  or under a commercial license. The commercial license does not
  cover derived or ported versions created by third parties under
  GPL. To inquire about commercial license, please send email to
  cktanx@gmail.com.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include "sexp.h"

void indent(int level)
{
	for (int i = 0; i < level; i++) {
		printf("  ");
	}
}

void pesc(char* p)
{
	for ( ; *p; p++) {
		if (*p == '\\' || *p == '"') {
			putchar('\\');
			putchar(*p);
			continue;
		}
		if (!isprint(*p)) {
			printf("\\x%02x", *p);
			continue;
		}
		
		putchar(*p);
	}
}

void pprint(sexp_t* ex, int level)
{
	if (!ex) return;

	if (ex->list) {
		indent(level);
		puts("(");
		for (int i = 0; i < ex->len; i++) {
			pprint(ex->list[i], level+1);
		}
		indent(level);
		puts(")");
		
	} else {
		
		indent(level);
		if (ex->flag & SEXP_FLAG_QUOTED) {
			putchar('"');
			pesc(ex->atom);
			puts("\"");
		} else {
			puts(ex->atom);
		}
	}
}


char* read_stdin()
{
	char* buf = 0;
	int top = 0;
	int max = 0;

	while (1) {
		if (top == max) {
			int n = max + 100;
			char* p = realloc(buf, n);
			if (!p) {
				perror("realloc");
				exit(1);
			}
			buf = p;
			max = n;
		}

		int n = read(0, buf + top, max - top);
		if (n == 0)
			break;
		if (n == -1) {
			perror("read");
			exit(1);
		}
		top += n;
	}

	if (top == max) {
		int n = max + 1;
		char* p = realloc(buf, n);
		if (!p) {
			perror("realloc");
			exit(1);
		}
		buf = p;
		max = n;
	}

	buf[top++] = 0;
	return buf;
}


int main()
{
	char* buf = read_stdin();
	sexp_err_t sexp_err;
	sexp_t* ex = sexp_parse(buf, &sexp_err);
	if (!ex) {
		fprintf(stderr, "error: %s\n", sexp_err.errmsg);
		exit(1);
	}

	pprint(ex, 0);
	sexp_free(ex);
	free(buf);
	return 0;
}

