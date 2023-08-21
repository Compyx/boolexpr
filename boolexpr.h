#ifndef BOOLEXPR_H
#define BOOLEXPR_H

#include <stdbool.h>

/* Token IDs */
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

/* Error codes */
enum {
    BEXPR_ERR_OK = 0,           /**< OK (no error) */
    BEXPR_ERR_FATAL,            /**< fatal error, should not normally happen */
    BEXPR_ERR_EXPECTED_TOKEN,   /**< parser expected a token */
    BEXPR_ERR_INVALID_TOKEN,    /**< parser didn't recognize token */
    BEXPR_ERR_EXPECTED_LPAREN,  /**< expected left parenthesis */
    BEXPR_ERR_EXPECTED_RPAREN,  /**< expected right parenthesis */
    BEXPR_ERR_UNMATCHED_PARENS, /**< unmatched parenthesis */
    BEXPR_ERR_EMPTY_EXPRESSION, /**< empty expression */

    BEXPR_ERROR_COUNT
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
