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
 * Unquoted atom allows the following chars:
 *     [a-zA-Z0-9] 
 *     - . / _ : * + = 
 */

#define SEXP_FLAG_ESCAPED 1
#define SEXP_FLAG_QUOTED  2

#define SEXP_EOUTOFMEMORY -1
#define SEXP_EBADESCAPE   -2
#define SEXP_EENDQUOTE    -3
#define SEXP_EBADSYMBOL   -4
#define SEXP_EINVALID     -5

typedef struct sexp_t sexp_t;
struct sexp_t {
	char* atom;			/* one of atom or list will be NUL */
	sexp_t** list;
	int len;			/* length of atom or list */
	int flag;			/* SEXP_FLAG_XXX */
};

typedef struct sexp_err_t sexp_err_t;
struct sexp_err_t {
	int errno;
	char errmsg[80];
};


/**
 * Parse buf and return a pointer to a s-exp tree with atoms pointing
 * into buf[], which is modified in place by unescaping and terminating 
 * the atoms with NUL.
 *
 * Caller must call sexp_free(ptr) after use.
 */
extern sexp_t* sexp_parse(char* buf, sexp_err_t* err);


/**
 *  Free the memory allocated in a s-exp tree.
 */
extern void sexp_free(sexp_t* ptr);


#endif /* SEXP_H */
