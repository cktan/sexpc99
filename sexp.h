#ifndef SEXP_H
#define SEXP_H


enum sexp_kind_t { SEXP_NONE, SEXP_LIST, SEXP_ATOM };
typedef enum sexp_kind_t sexp_kind_t;

typedef struct sexp_t sexp_t;
struct sexp_t {
	sexp_kind_t kind;
	union {
		struct {
			char* ptr;			/* point to first char of atom */
			char* term;			/* point to NUL term of atom */
			char  quoted;		/* true if atom was quoted */
			char  escaped;		/* true if atom needs unescaping */
		} atom;
		struct {
			sexp_t** elem;
			int top, max;
		} list;
	} u;
};


extern sexp_t* sexp_parse(char* buf, char* errmsg, int errmsglen);
extern void sexp_free(sexp_t* ptr);



#endif /* SEXP_H */
