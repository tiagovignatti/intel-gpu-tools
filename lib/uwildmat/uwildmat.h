
#ifndef UWILDMAT_H
#define UWILDMAT_H 1

#include <stdio.h>
#include <sys/types.h>
#include <stdbool.h>

/*
**  WILDMAT MATCHING
*/
enum uwildmat {
    UWILDMAT_FAIL   = 0,
    UWILDMAT_MATCH  = 1,
    UWILDMAT_POISON
};

extern bool             is_valid_utf8(const char *start);
extern bool             uwildmat(const char *text, const char *pat);
extern bool             uwildmat_simple(const char *text, const char *pat);
extern enum uwildmat    uwildmat_poison(const char *text, const char *pat);


#endif /* UWILDMAT_H */
