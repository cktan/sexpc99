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


static int octval(char* s)
{
	int val = 0;
	const char* p = "01234567";
	const char* pp = strchr(p, *s++);
	if (pp) {
		val = pp - p;
		pp = strchr(p, *s++);
	}
	if (pp) {
		val = (val << 3) | (pp - p);
		pp = strchr(p, *s);
	}
	return pp ? ((val << 3) | (pp - p)) : -1;
}

static int hexval(char* s)
{
	int val = 0;
	const char* p = "0123456789ABCDEF";
	const char* pp = strchr(p, toupper(*s));
	s++;
	if (pp) {
		val = pp - p;
		pp = strchr(p, toupper(*s));
	}
	return pp ? ((val << 4) | (pp - p)) : -1;
}



static inline sexp_t* mksexp(sexp_kind_t kind, const char** eb)
{
	sexp_t* p = calloc(sizeof(*p), 1);
	if (!p) {
		*eb = E_OUTOFMEMORY;
		return 0;
	}
	p->kind = kind;
	return p;
}

static sexp_t* reterr(const char* msg, const char* s, char** e, const char** eb, sexp_t* ex)
{
	sexp_free(ex);
	*eb = msg;
	*(const char**) e = s;
	return 0;
}

/**
 *  Parse a quoted string at s. 
 *  On success, return the s-exp; also the start of next expression in e.
 *  On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_qstring(char* s, char** e, const char** eb)
{
	assert(*s == '"');
	
	sexp_t* ex = mksexp(SEXP_ATOM, eb);
	if (!ex)
		return 0;

	ex->atom.quoted = 1;
	ex->atom.ptr = s;
	for (s++; *s; s++) {
		if (*s == '"') 
			break;
		if (*s == '\\') {
			ex->atom.escaped = 1;
			if (strchr("btvnfr\"'\\", s[1])) {
				s++;
				continue;
			}
			if (octval(s+1) >= 0) {
				s += 3;
				continue;
			}
			if ('x' == s[1] && hexval(s+2) >= 0) {
				s += 3;
				continue;
			}
			if ('\n' == s[1]) {
				s += ('\r' == s[2] ? 2 : 1);
				continue;
			}
			if ('\r' == s[1]) {
				s += ('\n' == s[2] ? 2 : 1);
				continue;
			}
			return reterr(E_BADESCAPE, s, e, eb, ex);
		}
	}

	if (*s != '"') {
		return reterr(E_ENDQUOTE, s, e, eb, ex);
	}
	
	s++;
	ex->atom.term = s;
	*(const char**) e = s;
	return ex;
}


/**
 *  Parse a symbol at s. 
 *  On success, return the s-exp; also the start of next expression in e.
 *  On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_symbol(char* s, char** e, const char** eb)
{
	sexp_t* ex = mksexp(SEXP_ATOM, eb);
	if (!ex)
		return 0;

	ex->atom.ptr = s;
	for (s++; *s; s++) {
		if (isspace(*s) || *s == ')')
			break;
		if (isalnum(*s) || strchr("-./_:*+=", *s))
			continue;
		return reterr(E_BADSYMBOL, s, e, eb, ex);
	}

	ex->atom.term = s;
	*(const char**) e = s;
	return ex;
}


/**
 *  Append kid to end of list in ex. 
 */
static sexp_t* append(sexp_t* ex, sexp_t* kid)
{
	assert(ex->kind == SEXP_LIST);
	
	/* expand? */
	if (ex->list.top == ex->list.max) {
		int newmax = ex->list.max + 8;
		sexp_t** p = realloc(ex->list.elem, newmax * sizeof(*p));
		if (!p) 
			return 0;
		ex->list.elem = p;
		ex->list.max = newmax;
	}

	/* add to end */
	ex->list.elem[ex->list.top++] = kid;
	return ex;
}


/**
 *  Parse a list at s. 
 *  On success, return the s-exp; also the start of next expression in e.
 *  On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_list(char* s, char** e, const char** eb)
{
	assert(*s == '(');
	
	sexp_t* ex = mksexp(SEXP_LIST, eb);
	if (!ex)
		return 0;

	for (s++; *s; ) {
		/* skip whitespaces */
		if (isspace(*s)) {
			s++;
			continue;
		}

		/* end of list? */
		if (*s == ')') {
			s++;
			break;
		}

		/* parse a child */
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

		/* add to tail */
		if (! append(ex, kid)) {
			return reterr(E_OUTOFMEMORY, s, e, eb, ex);
		}

		/* move forward to next kid */
		s = *e;
	}

	*(const char**) e = s;
	return ex;
}


/**
 *  Given a ptr into buf[], determine the lineno and charno of the
 *  char pointed by ptr in buf[]
 */
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



/**
 *  For the string bracketed by (p, q), unescape
 *  any escaped char in-place. The string will
 *  be shortened if there is any escape char.
 */
static char* unescape(char* p, char* q)
{
	char* s;
	for (s = p; p < q; p++) {
		if (*p == '\\') {
			const char* const pattern = "btvnfr\"'\\";
			char* x = strchr(pattern, p[1]);
			if (x) {
				*s++ = ("\b\t\v\n\f\r\"'\\")[x - pattern];
				p++;
				continue;
			}

			if (strchr("01234567", p[1])) {
				*s++ = octval(p+1);
				p += 3;
				continue;
			}

			if (p[1] == 'x') {
				*s++ = hexval(p+2);
				p += 3;
				continue;
			}

			fprintf(stderr, "panic at %s:%d", __FILE__, __LINE__);
			exit(1);
		}
		*s++ = *p;
	}
	*s = 0;
	return s;
}



/**
 *  Go through the s-exp tree and unescape any 
 *  quoted strings
 */
static sexp_t* touchup(sexp_t* ex)
{
	if (!ex) return 0;

	switch (ex->kind) {
	case SEXP_LIST:
		for (int i = 0; i < ex->list.top; i++) {
			touchup(ex->list.elem[i]);
		}
		break;

	case SEXP_ATOM: 
		*ex->atom.term = 0;
		if (ex->atom.quoted) {
			// unquote
			char* p = ++ex->atom.ptr;
			char* q = ex->atom.term - 1;
			*q = 0;
			// unescape
			if (ex->atom.escaped) {
				ex->atom.term = unescape(p, q);
			}
		}
		break;

	default:
		break;
	}

	return ex;
}



/**
 * Parse buf and return a pointer to a s-exp tree with atoms pointing
 * into buf[], which is modified in place by unescaping and terminating 
 * the atoms with NUL.
 *
 * Caller must call sexp_free(ptr) after use.
 */
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


/**
 *  Free the memory allocated in a s-exp tree.
 */
void sexp_free(sexp_t* ex)
{
	if (ex) {
		if (ex->kind == SEXP_LIST) {
			for (int i = 0; i < ex->list.top; i++) {
				sexp_free(ex->list.elem[i]);
			}
			free(ex->list.elem);
		}

		free(ex);
	}
}


