#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include "sexp.h"

#define STRINGIFY(x)  #x
#define TOSTRING(x)   STRINGIFY(x)

static const char* errstr(int e)
{
	switch (e) {
	case SEXP_EOUTOFMEMORY: return "out of memory";
	case SEXP_EBADESCAPE: return "bad escape";
	case SEXP_EENDQUOTE: return "missing end-quote in string";
	case SEXP_EBADSYMBOL: return "bad symbol";
	case SEXP_EINVALID: return "unexpected char";
	}
	
	return "unknown error";
}


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

static int wspace(const char* s)
{
	const char* p;
	for (p = s; isspace(*p); p++);
	return p - s;
}



static inline sexp_t* mksexp(int* perr)
{
	sexp_t* p = calloc(sizeof(*p), 1);
	if (!p) {
		*perr = SEXP_EOUTOFMEMORY;
		return 0;
	}
	return p;
}

static sexp_t* reterr(int err, const char* s, char** e, int* perr, sexp_t* ex)
{
	sexp_free(ex);
	*perr = err;
	*(const char**) e = s;
	return 0;
}

/**
 *  Parse a quoted string at s. 
 *  On success, return the s-exp; also the start of next expression in e.
 *  On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_qstring(char* s, char** e, int* perr)
{
	assert(*s == '"');
	
	sexp_t* ex = mksexp(perr);
	if (!ex)
		return 0;

	ex->flag |= SEXP_FLAG_QUOTED;
	ex->atom = s;
	for (s++; *s; s++) {
		if (*s == '"') 
			break;
		if (*s == '\\') {
			ex->flag |= SEXP_FLAG_ESCAPED;
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
				s += ('\r' == s[2]) ? 2 : 1;
				continue;
			}
			if ('\r' == s[1]) {
				s += ('\n' == s[2]) ? 2 : 1;
				continue;
			}
			return reterr(SEXP_EBADESCAPE, s, e, perr, ex);
		}
	}

	if (*s != '"') {
		return reterr(SEXP_EENDQUOTE, s, e, perr, ex);
	}
	
	s++;
	ex->len = s - ex->atom;
	*(const char**) e = s;
	return ex;
}


/**
 *  Parse a symbol at s. 
 *  On success, return the s-exp; also the start of next expression in e.
 *  On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_symbol(char* s, char** e, int* perr)
{
	sexp_t* ex = mksexp(perr);
	if (!ex)
		return 0;

	ex->atom = s;
	for (s++; *s; s++) {
		if (isspace(*s) || *s == ')')
			break;
		if (isalnum(*s) || strchr("-./_:*+=", *s))
			continue;
		return reterr(SEXP_EBADSYMBOL, s, e, perr, ex);
	}

	if (s == ex->atom) {
		return reterr(SEXP_EBADSYMBOL, s, e, perr, ex);
	}

	ex->len = s - ex->atom;
	*(const char**) e = s;
	return ex;
}


/**
 *  Append kid to end of list in ex. 
 */
static sexp_t* append(sexp_t* ex, sexp_t* kid)
{
	/* expand? */
	if (ex->len % 8 == 0) {
		int newmax = ex->len + 8;
		sexp_t** p = realloc(ex->list, newmax * sizeof(*p));
		if (!p) 
			return 0;
		ex->list = p;
	}

	/* add to end */
	ex->list[ex->len++] = kid;
	return ex;
}


/**
 *  Parse a list at s. 
 *  On success, return the s-exp; also the start of next expression in e.
 *  On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_list(char* s, char** e, int* perr)
{
	assert(*s == '(');
	
	sexp_t* ex = mksexp(perr);
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
			kid = parse_qstring(s, e, perr);
		else if (*s == '(') 
			kid = parse_list(s, e, perr);
		else 
			kid = parse_symbol(s, e, perr);
		
		if (! kid) {
			sexp_free(ex);
			return 0;
		}

		/* add to tail */
		if (! append(ex, kid)) {
			return reterr(SEXP_EOUTOFMEMORY, s, e, perr, ex);
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

			if (p[1] == '\n') {
				*s++ = '\n';
				p += ('\r' == p[2]) ? 2 : 1;
				continue;
			}

			if (p[1] == '\r') {
				*s++ = '\n';
				p += ('\n' == p[2]) ? 2 : 1;
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

	if (ex->list) {
		for (int i = 0; i < ex->len; i++) {
			touchup(ex->list[i]);
		}
	} else {
		ex->atom[ex->len] = 0;
			
		if (ex->flag & SEXP_FLAG_QUOTED) {
			// unquote
			char* p = ++ex->atom;
			p[ex->len -= 2] = 0;
			// unescape
			if (ex->flag & SEXP_FLAG_ESCAPED) {
				unescape(p, p + ex->len);
			}
		}
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
	int err;

	// skip whitespace
	s += wspace(s);

	// parse
	if (*s == '(') 
		ex = parse_list(s, &e, &err);
	else if (*s == '"') 
		ex = parse_qstring(s, &e, &err);
	else
		ex = parse_symbol(s, &e, &err);

	if (!ex) {
		int lineno, charno;
		location(buf, e, &lineno, &charno);
		snprintf(errmsg, errmsglen, "%s at L%d.%d", errstr(err), lineno, charno);
		return 0;
	}

	// skip whitespace
	s = e + wspace(e);

	// if not at end of buffer, we have unexpected chars.
	if (*s) {
		int lineno, charno;
		sexp_free(ex);
		location(buf, s, &lineno, &charno);
		snprintf(errmsg, errmsglen, "%s at L%d.%d", errstr(SEXP_EINVALID), lineno, charno);
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
		if (ex->list) {
			for (int i = 0; i < ex->len; i++) {
				sexp_free(ex->list[i]);
			}
			free(ex->list);
		}

		free(ex);
	}
}


