#ifndef SEXP_H
#define SEXP_H

/*
 * Atoms are either quoted or not quoted.
 *
 * Quoted atom allows the following escapes:
 *   \b \t \v \n \f \r \" \' \\ \<CR> \<LF> \<CRLF> \<LFCR>
 *   \ooo -- octal value ooo
 *   \xhh -- hex value hh
 *
 * Unquoted atom allows [a-zA-Z0-9] and the following chars:
 *     - . / _ : * + = 
 */

enum sexp_kind_t { SEXP_NONE, SEXP_LIST, SEXP_ATOM };
typedef enum sexp_kind_t sexp_kind_t;

typedef struct sexp_t sexp_t;
struct sexp_t {
	sexp_kind_t kind;			/* list or atom */
	union {
		char* atom;
		struct {
			sexp_t** elem;		/* elem[0..top) are valid */
			int top, max;
		} list;
		struct {
			char* ptr;			/* point to first char of atom; overlays sexp_t.atom */
			char* term;			/* (internal) point to NUL term of atom */
			char  quoted;		/* (internal) true if atom was quoted */
			char  escaped;		/* (internal) true if atom needs unescaping */
		} a; /* internal */
	} ;
};


/**
 * Parse buf and return a pointer to a s-exp tree with atoms pointing
 * into buf[], which is modified in place by unescaping and terminating 
 * the atoms with NUL.
 *
 * Caller must call sexp_free(ptr) after use.
 */
extern sexp_t* sexp_parse(char* buf, char* errmsg, int errmsglen);


/**
 *  Free the memory allocated in a s-exp tree.
 */
extern void sexp_free(sexp_t* ptr);


#endif /* SEXP_H */
