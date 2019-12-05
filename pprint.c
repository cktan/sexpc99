#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sexp.h"

void indent(int level)
{
	for (int i = 0; i < level; i++) {
		printf("  ");
	}
}

void pprint(sexp_t* ex, int level)
{
	if (!ex) return;

	if (ex->kind == SEXP_LIST) {
		indent(level);
		puts("(");
		for (int i = 0; i < ex->u.list.top; i++) {
			pprint(ex->u.list.elem[i], level+1);
		}
		indent(level);
		puts(")");
		return;
	}

	if (ex->kind == SEXP_QSTRING) {
		indent(level);
		printf("\"%s\"\n", ex->u.qstring.start);
		return;
	}
	if (ex->kind == SEXP_SYMBOL) {
		indent(level);
		puts(ex->u.qstring.start);
		return;
	}
}


char* readstdin()
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
	char* buf = readstdin();
	char errmsg[200];
	sexp_t* ex = sexp_parse(buf, errmsg, sizeof(errmsg));
	if (!ex) {
		fprintf(stderr, "error: %s\n", errmsg);
		exit(1);
	}


	pprint(ex, 0);
	sexp_free(ex);
	free(buf);
	return 0;
}
