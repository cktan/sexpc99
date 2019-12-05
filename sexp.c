#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include "sexp.h"

#define E_OUTOFMEMORY "out of memory"
#define E_BADESCAPE   "bad escape"
#define E_ENDQUOTE    "missing end quote in string"
#define E_BADSYMBOL   "bad symbol"
#define E_UNEXPECTED  "unexpected char"



static inline sexp_t* mksexp(const char** eb)
{
	sexp_t* p = calloc(sizeof(*p), 1);
	if (!p) {
		*eb = E_OUTOFMEMORY;
		return 0;
	}
	return p;
}

static sexp_t* reterr(const char* msg, const char* s, char** e, const char** eb, sexp_t* ex)
{
	sexp_free(ex);
	*eb = msg;
	*(const char**) e = s;
	return 0;
}

/* Parse a quoted string at s. 
 * On success, return the s-exp; also the start of next expression in e.
 * On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_qstring(char* s, char** e, const char** eb)
{
	assert(*s == '"');
	*e = 0;
	
	sexp_t* ex = mksexp(eb);
	if (!ex)
		return 0;

	ex->kind = SEXP_ATOM;
	ex->u.atom.quoted = 1;
	ex->u.atom.ptr = s;
	for (s++; *s; s++) {
		if (*s == '"') 
			break;
		if (*s == '\\') {
			ex->u.atom.escaped = 1;
			if (s[1] == '\\' || s[1] == '"') {
				s++;
				continue;
			}
			return reterr(E_BADESCAPE, s, e, eb, ex);
		}
	}

	if (*s != '"') {
		return reterr(E_ENDQUOTE, s, e, eb, ex);
	}
	
	s++;
	ex->u.atom.term = s;
	*(const char**) e = s;
	return ex;
}


/* Parse a symbol at s. 
 * On success, return the s-exp; also the start of next expression in e.
 * On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_symbol(char* s, char** e, const char** eb)
{
	sexp_t* ex = mksexp(eb);
	if (!ex)
		return 0;

	ex->kind = SEXP_ATOM;
	ex->u.atom.ptr = s;
	for (s++; *s; s++) {
		if (isspace(*s) || *s == ')')
			break;
		if (isalnum(*s) || strchr("+-_.", *s))
			continue;
		return reterr(E_BADSYMBOL, s, e, eb, ex);
	}

	ex->u.atom.term = s;
	*(const char**) e = s;
	return ex;
}


/* Append kid to end of list in ex. */
static sexp_t* append(sexp_t* ex, sexp_t* kid)
{
	/* expand? */
	if (ex->u.list.top == ex->u.list.max) {
		int n = ex->u.list.max + 8;
		sexp_t** p = realloc(ex->u.list.elem, n * sizeof(sexp_t*));
		if (!p) 
			return 0;
		ex->u.list.elem = p;
		ex->u.list.max = n;
	}

	/* add to end */
	ex->u.list.elem[ex->u.list.top++] = kid;
	return ex;
}


/* Parse a list at s. 
 * On success, return the s-exp; also the start of next expression in e.
 * On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_list(char* s, char** e, const char** eb)
{
	sexp_t* ex = mksexp(eb);
	if (!ex)
		return 0;

	assert(*s == '(');
	ex->kind = SEXP_LIST;
	for (s++; *s; ) {
		if (isspace(*s)) {
			s++;
			continue;
		}

		if (*s == ')') {
			s++;
			break;
		}

		sexp_t* kid = 0;
		if (*s == '"') 
			kid = parse_qstring(s, e, eb);
		else if (*s == '(') 
			kid = parse_list(s, e, eb);
		else 
			kid = parse_symbol(s, e, eb);
		
		if (! kid) {
			sexp_free(ex);
			return 0;
		}
		
		if (! append(ex, kid)) {
			return reterr(E_OUTOFMEMORY, s, e, eb, ex);
		}
		
		s = *e;
	}

	*(const char**) e = s;
	return ex;
}

static void location(const char* buf, const char* ptr, int* ret_lineno, int* ret_charno)
{
	int lineno, charno;
	lineno = charno = 0;
	for (const char* s = buf; *s && s < ptr; s++, charno++) {
		if (*s == '\n') {
			lineno++;
			charno = 0;
		}
	}

	*ret_lineno = lineno + 1;
	*ret_charno = charno + 1;
}


static char* unescape(char* p, char* q)
{
	char* s;
	for (s = p; p < q; p++) {
		if (*p == '\\') {
			p++;
		}
		*s++ = *p;
	}
	*s = 0;
	return s;
}



static sexp_t* touchup(sexp_t* ex)
{
	if (!ex) return 0;

	if (ex->kind == SEXP_LIST) {
		for (int i = 0; i < ex->u.list.top; i++) {
			touchup(ex->u.list.elem[i]);
		}
	} else if (ex->kind == SEXP_ATOM) {
		*ex->u.atom.term = 0;
		if (ex->u.atom.quoted) {
			// unquote
			char* p = ++ex->u.atom.ptr;
			char* q = ex->u.atom.term - 1;
			*q = 0;
			// unescape
			if (ex->u.atom.escaped) {
				ex->u.atom.term = unescape(p, q);
			}
		}
	}

	return ex;
}



sexp_t* sexp_parse(char* buf, char* errmsg, int errmsglen)
{
	char* s = buf;
	char* e = 0;
	sexp_t* ex = 0;
	const char* eb = 0;

	// skip whitespace
	for ( ; *s && isspace(*s); s++);

	// parse
	if (*s == '(') 
		ex = parse_list(s, &e, &eb);
	else if (*s == '"') 
		ex = parse_qstring(s, &e, &eb);
	else
		ex = parse_symbol(s, &e, &eb);

	if (!ex) {
		int lineno, charno;
		location(buf, e, &lineno, &charno);
		snprintf(errmsg, errmsglen, "%s at L%d.%d", eb, lineno, charno);
		return 0;
	}

	// skip whitespace
	for (s = e; *s && isspace(*s); s++);

	// if not at end of buffer, we have unexpected chars.
	if (*s) {
		int lineno, charno;
		sexp_free(ex);
		location(buf, s, &lineno, &charno);
		snprintf(errmsg, errmsglen, "%s at L%d.%d", E_UNEXPECTED, lineno, charno);
		return 0;
	}


	return touchup(ex);
}


void sexp_free(sexp_t* ex)
{
	if (ex) {
		if (ex->kind == SEXP_LIST) {
			for (int i = 0; i < ex->u.list.top; i++) {
				sexp_free(ex->u.list.elem[i]);
			}
			free(ex->u.list.elem);
		}

		free(ex);
	}
}


