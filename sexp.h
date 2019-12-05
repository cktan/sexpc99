#ifndef SEXP_H
#define SEXP_H


enum sexp_kind_t { SEXP_NONE, SEXP_LIST, SEXP_QSTRING, SEXP_SYMBOL };
typedef enum sexp_kind_t sexp_kind_t;

typedef struct sexp_t sexp_t;
struct sexp_t {
	sexp_kind_t kind;
	union {
		struct {
			char* start;
			char* stop;
		} qstring, symbol;
		struct {
			sexp_t** elem;
			int top, max;
		} list;
	} u;
};


extern sexp_t* sexp_parse(char* buf, char* errmsg, int errmsglen);
extern void sexp_free(sexp_t* ptr);



#endif /* SEXP_H */
