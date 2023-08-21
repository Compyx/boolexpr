#ifndef BOOLEXPR_H
#define BOOLEXPR_H

#include <stdbool.h>

enum {
    BEXPR_INVALID = -1, /**< invalid token */
    BEXPR_FALSE,        /**< FALSE constant */
    BEXPR_TRUE,         /**< TRUE constant */
    BEXPR_AND,          /**< logical AND operator '!'*/
    BEXPR_OR,           /**< logical OR operator '||' */
    BEXPR_NOT,          /**< logical NOT operator '&&' */
    BEXPR_LPAREN,       /**< left parenthesis '(' */
    BEXPR_RPAREN        /**< right parenthesis ')' */
};

extern int  bexpr_errno;
const char *bexpr_strerror(int errnum);

void bexpr_init (void);
void bexpr_reset(void);
void bexpr_free (void);
void bexpr_print(void);

bool bexpr_add_token(int token);
bool bexpr_tokenize (const char *text);
bool bexpr_evaluate (bool *result);


#endif
