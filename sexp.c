#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include "sexp.h"

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

/* return offset of UPPER(ch) in templ */
static int match(const char* templ, char ch)
{
	char* p = strchr(templ, toupper(ch));
	return p ? p - templ : -1;
}

/* return octal value of s[0],s[1],s[2]. -1 if invalid. */
static int octval(char* s)
{
	const char* templ = "01234567";
	int val = match(templ, *s++);
	if (val >= 0) {
		val = (val << 3) | match(templ, *s++);
		if (val >= 0) 
			val = (val << 3) | match(templ, *s++);
	}
	return val;
}

/* return hex value of s[0],s[1]. -1 if invalid. */
static int hexval(char* s)
{
	const char* templ = "0123456789ABCDEF";
	int val = match(templ, *s++);
	if (val >= 0) {
		val = (val << 4) | match(templ, *s++);
	}
	return val;
}

/* return # whitespaces at start of s */
static int wspace(const char* s)
{
	const char* p;
	for (p = s; isspace(*p); p++);
	return p - s;
}


/* alloc a sexp */
static inline sexp_t* mksexp(int* perr)
{
	sexp_t* p = calloc(sizeof(*p), 1);
	if (!p) {
		*perr = SEXP_EOUTOFMEMORY;
		return 0;
	}
	return p;
}

/* return error */
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
static sexp_t* parse_qstring(char* sp, char** ep, int* perr)
{
	assert(*sp == '"');
	
	sexp_t* ex = mksexp(perr);
	if (!ex)
		return 0;

	ex->flag |= SEXP_FLAG_QUOTED;
	ex->atom = sp;
	for (sp++; *sp; sp++) {
		if (*sp == '"') 
			break;
		if (*sp == '\\') {
			ex->flag |= SEXP_FLAG_ESCAPED;
			if (strchr("btvnfr\"'\\", sp[1])) {
				sp++;
				continue;
			}
			if (octval(sp+1) >= 0) {
				sp += 3;
				continue;
			}
			if ('x' == sp[1] && hexval(sp+2) >= 0) {
				sp += 3;
				continue;
			}
			if ('\n' == sp[1]) {
				sp += ('\r' == sp[2]) ? 2 : 1;
				continue;
			}
			if ('\r' == sp[1]) {
				sp += ('\n' == sp[2]) ? 2 : 1;
				continue;
			}
			return reterr(SEXP_EBADESCAPE, sp, ep, perr, ex);
		}
	}

	if (*sp != '"') {
		return reterr(SEXP_EENDQUOTE, sp, ep, perr, ex);
	}
	
	sp++;
	ex->len = sp - ex->atom;
	*(const char**) ep = sp;
	return ex;
}


/**
 *  Parse a symbol at s. 
 *  On success, return the s-exp; also the start of next expression in e.
 *  On error, return NULL, and the error message in eb.
 */
static sexp_t* parse_symbol(char* sp, char** ep, int* perr)
{
	sexp_t* ex = mksexp(perr);
	if (!ex)
		return 0;

	ex->atom = sp;
	for (sp++; *sp; sp++) {
		if (isspace(*sp) || *sp == ')')
			break;
		if (isalnum(*sp) || strchr("-./_:*+=", *sp))
			continue;
		return reterr(SEXP_EBADSYMBOL, sp, ep, perr, ex);
	}

	if (sp == ex->atom) {
		return reterr(SEXP_EBADSYMBOL, sp, ep, perr, ex);
	}

	ex->len = sp - ex->atom;
	*(const char**) ep = sp;
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
static sexp_t* parse_list(char* sp, char** ep, int* perr)
{
	assert(*sp == '(');
	
	sexp_t* ex = mksexp(perr);
	if (!ex)
		return 0;

	for (sp++; *sp; ) {
		/* skip whitespaces */
		if (isspace(*sp)) { sp++; continue; }

		/* end of list? */
		if (*sp == ')') { sp++; break; }

		/* parse a child */
		sexp_t* kid = 0;
		if (*sp == '"') 
			kid = parse_qstring(sp, ep, perr);
		else if (*sp == '(') 
			kid = parse_list(sp, ep, perr);
		else 
			kid = parse_symbol(sp, ep, perr);
		
		if (! kid) {
			sexp_free(ex);
			return 0;
		}

		/* add to tail */
		if (! append(ex, kid)) {
			return reterr(SEXP_EOUTOFMEMORY, sp, ep, perr, ex);
		}

		/* move forward to next kid */
		sp = *ep;
	}

	*(const char**) ep = sp;
	return ex;
}


/**
 *  Fill in sexp_err_t. Given a ptr into buf[], determine the lineno
 *  and charno of the char pointed by ptr in buf[].
 */
static sexp_t* fill_err(sexp_err_t* err, const char* buf, const char* ptr)
{
	int lineno, charno;
	lineno = charno = 0;
	for (const char* s = buf; *s && s < ptr; s++, charno++) {
		if (*s == '\n') {
			lineno++;
			charno = 0;
		}
	}
	
	sprintf(err->location, "L%d.%d", lineno+1, charno+1);
	err->errmsg = errstr(err->errno);
	return 0;
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
		if (*p != '\\') {
			*s++ = *p;
			continue;
		}

		/* check for escape chars */
		const char* const templ = "btvnfr\"'\\";
		char* x = strchr(templ, p[1]);
		if (x) {
			*s++ = ("\b\t\v\n\f\r\"'\\")[x - templ];
			p++;
			continue;
		}

		/* check for \ooo */
		if (strchr("01234567", p[1])) {
			*s++ = octval(p+1);
			p += 3;
			continue;
		}

		/* check for \xhh */
		if (p[1] == 'x') {
			*s++ = hexval(p+2);
			p += 3;
			continue;
		}

		/* check for <CR> and <CR><LF> */
		if (p[1] == '\n') {
			*s++ = '\n';
			p += ('\r' == p[2]) ? 2 : 1;
			continue;
		}

		/* check for <LF> and <LF><CR> */
		if (p[1] == '\r') {
			*s++ = '\n';
			p += ('\n' == p[2]) ? 2 : 1;
			continue;
		}

		fprintf(stderr, "panic at %s:%d", __FILE__, __LINE__);
		exit(1);
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
				ex->len = unescape(p, p + ex->len) - p;
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
sexp_t* sexp_parse(char* buf, sexp_err_t* err)
{
	char* sp = buf;
	char* ep = 0;
	sexp_t* ex = 0;

	// skip whitespace
	sp += wspace(sp);

	// parse
	if (*sp == '(') 
		ex = parse_list(sp, &ep, &err->errno);
	else if (*sp == '"') 
		ex = parse_qstring(sp, &ep, &err->errno);
	else
		ex = parse_symbol(sp, &ep, &err->errno);

	if (!ex) {
		return fill_err(err, buf, ep);
	}

	// skip whitespace
	sp = ep + wspace(ep);

	// if not at end of buffer, we have unexpected chars.
	if (*sp) {
		sexp_free(ex);
		return fill_err(err, buf, ep);
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


