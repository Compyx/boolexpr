/** \file   boolexpr.c
 * \brief   Boolean expression evaluation
 *
 * \author  Bas Wassink <b.wassink@ziggo.nl>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "boolexpr.h"


/** \brief  Array length helper
 *
 * Determine size of \a arr in number of elements.
 */
#define ARRAY_LEN(arr)  (sizeof arr / sizeof arr[0])

/** \brief  Set error code (and for now print error code and message on stderr
 *
 * \param[in]   errnum  error code
 */
#define SET_ERROR(errnum) \
    bexpr_errno = errnum; \
    fprintf(stderr, "%s(): error %d: %s\n", __func__, errnum, bexpr_strerror(errnum));

enum {
    BEXPR_LTR,  /**< left-to-right associativity */
    BEXPR_RTL,  /**< right-to-left associativity */
};

enum {
    BEXPR_UNARY  = 1,   /**< unary operator */
    BEXPR_BINARY = 2    /**< binary operator */
};

/** \brief  Token specification
 */
typedef struct token_s {
    const char *text;   /**< text */
    int         id;     /**< ID */
    int         arity;  /**< operator arity */
    int         assoc;  /**< operator associativity */
    int         prec;   /**< operator precedence */
} token_t;

/** \brief  Maximum lenght of a token's text */
#define MAX_TOKEN_LEN   5

/** \brief  List of valid tokens
 *
 * Contains both operators and operands.
 *
 * The \c id members here must match their array index.
 */
static const token_t token_info[] = {
    { "false",  BEXPR_FALSE,    0,                  0,              0 },
    { "true",   BEXPR_TRUE,     0,                  0,              0 },
    { "(",      BEXPR_LPAREN,   0,                  BEXPR_LTR,      4 },
    { ")",      BEXPR_RPAREN,   0,                  BEXPR_LTR,      4 },
    { "!",      BEXPR_NOT,      BEXPR_UNARY,        BEXPR_RTL,      3 },
    { "&&",     BEXPR_AND,      BEXPR_BINARY,       BEXPR_LTR,      2 },
    { "||",     BEXPR_OR,       BEXPR_BINARY,       BEXPR_LTR,      1 }
};

/** \brief  Valid characters in a token's text */
static const int token_chars[] = {
    '(', ')', '!', '&', '|', '0', '1', 'a', 'e', 'f', 'l', 'r', 's', 't', 'u'
};

/** \brief  Error messages */
static const char *error_messages[] = {
    "OK",
    "fatal error",
    "expected token",
    "invalid token",
    "expected left parenthesis",
    "expected right parenthesis",
    "unmatched parentheses",
    "expression is empty"
};


/** \brief  Error code */
int bexpr_errno = 0;


/** \brief  Get error message for error number
 *
 * \param[in]   errnum  error number
 *
 * \return  error message
 */
const char *bexpr_strerror(int errnum)
{
    if (errnum < 0 || errnum >= (int)ARRAY_LEN(error_messages)) {
        return "unknown error";
    } else {
        return error_messages[errnum];
    }
}


/* {{{ Memory management: reimplementation of VICE's lib_foo() functions */

static void lib_free(void *ptr);

static void *lib_malloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "fatal: failed to allocate %zu bytes, exiting.\n", size);
        exit(1);
    }
    return ptr;
}

/** \brief  Reallocate (resize) memory
 *
 * Like \c realloc(3), but call exit on error.
 *
 * \param[in]   ptr     memory to reallocate
 * \param[in]   size    new size for \a ptr
 *
 * \note    Like \c realloc(3), using \c NULL for \a ptr is equivalent to
 *          calling lib_malloc(\a size)
 * \note    Like \c realloc(3), using \c 0 for \a size is equivalent to calling
 *          lib_free(\a ptr)
 */
static void *lib_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) {
        ptr = lib_malloc(size);
    } else if (size == 0) {
        lib_free(ptr);
        ptr = NULL;
    } else {
        void *tmp = realloc(ptr, size);
        if (tmp == NULL) {
            /* realloc(3) can return NULL when reallocating to a smaller size,
             * in which case we should return the original pointer, but there's
             * no way to determine this.
             */
            fprintf(stderr, "fatal: failed to reallocate %zu bytes, exiting.\n", size);
            free(ptr);
            exit(1);
        }
        ptr = tmp;
    }
    return ptr;
}

/** \brief  Free memory
 *
 * \param[in]   ptr memory to free
 */
static void lib_free(void *ptr)
{
    free(ptr);
}
/* }}} */

/* {{{ Token handling */
/** \brief  Skip whitespace in string
 *
 * \param[in]   s   string
 *
 * \return  pointer to first non-whitepace character (can be the terminating
 *          nul character if \a s consists of only whitespace)
 */
static const char *skip_whitespace(const char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/** \brief  Determine if a character is a valid token text character
 *
 * \param[in]   ch  character to check
 *
 * \return  \c true if valid
 */
static bool is_token_char(int ch)
{
    for (size_t i = 0; i < ARRAY_LEN(token_chars); i++) {
        if (token_chars[i] == ch) {
            return true;
        }
    }
    return false;
}

/** \brief  Determine if token ID is valid
 *
 * \param[in]   id  token ID
 *
 * \return  \c true if valid
 */
static bool is_valid_token_id(int id)
{
    return id >= 0 && id < (int)ARRAY_LEN(token_info);
}

#if 0
static bool is_operator(int token)
{
    return (is_valid_token(token)) &&
           ((token != BEXPR_FALSE) && (token != BEXPR_TRUE));
}
#endif

/** \brief  Determine if a token is an operand
 *
 * \param[in]   id  token ID
 *
 * \return  \c true if token is an operand
 */
static bool is_operand(int id)
{
    return (is_valid_token_id(id)) &&
           ((id == BEXPR_FALSE) || (id == BEXPR_TRUE));
}

/** \brief  Parse text for a valid token
 *
 * Parse \a text looking for a valid token text, and if found return the ID.
 * A pointer to the first non-valid character in \a text is stored in \a endptr
 * if \a endptr isn't \c NULL.
 *
 * \param[in]   text    text to parse
 * \param[out]  endptr  location in \a text of first non-token character
 *
 * \return  token ID or \c BEXPR_INVALID on error
 *
 * \throw   BEXPR_ERR_EXPECTED_TOKEN
 * \throw   BEXPR_ERR_INVALID_TOKEN
 */
static int token_parse(const char *text, const char **endptr)
{
    const char *pos;
    size_t      tlen;

    pos = text = skip_whitespace(text);
    while (*pos != '\0' && is_token_char(*pos)) {
        pos++;
    }
    if (pos - text == 0) {
        if (endptr != NULL) {
            *endptr = NULL;
        }
        SET_ERROR(BEXPR_ERR_EXPECTED_TOKEN);
        return BEXPR_INVALID;
    }

    tlen = (size_t)(pos - text);
    if (tlen > MAX_TOKEN_LEN) {
        tlen = MAX_TOKEN_LEN;
    }

    /* look up token, reducing size (greedy matching) each time */
    while (tlen >= 1) {
        for (size_t i = 0; i < ARRAY_LEN(token_info); i++) {
            if (strncmp(token_info[i].text, text, tlen) == 0) {
                if (endptr != NULL) {
                    *endptr = text + tlen;
                }
                return token_info[i].id;
            }
        }
        tlen--;
    }
    SET_ERROR(BEXPR_ERR_INVALID_TOKEN);
    return BEXPR_INVALID;
}

/** \brief  Get pointer to element in token info array
 *
 * \param[in]   id  token ID
 *
 * \return  token info array element or \c NULL when not found
 */
static const token_t *token_get(int id)
{
    if (is_valid_token_id(id)) {
        return &token_info[id];
    }
    return NULL;
}


/* Dynamic token list, implements stack and queue operations */

typedef struct token_list_s {
    const token_t **tokens; /**< array of token info pointers */
    size_t          size;   /**< size of \c tokens */
    int             index;  /**< index in \c tokens, -1 means list is empty */
} token_list_t;

#define TLIST_INIT { .tokens = NULL, .size = 0, .index = -1 }

static void token_list_init(token_list_t *list)
{
    list->size   = 32u;
    list->index  = -1;
    list->tokens = lib_malloc(sizeof *(list->tokens) * list->size);
    for (size_t i = 0; i < list->size; i++) {
        list->tokens[i] = NULL;
    }
}

static void token_list_reset(token_list_t *list)
{
    list->index = -1;
}

static void token_list_resize_maybe(token_list_t *list)
{
    if ((size_t)(list->index + 1) == list->size) {
        list->size  *= 2;
        list->tokens = lib_realloc(list->tokens,
                                   sizeof *(list->tokens) * list->size);
    }
}

static void token_list_free(token_list_t *list)
{
    lib_free(list->tokens);
}

static int token_list_length(const token_list_t *list)
{
    return list->index + 1;
}

static bool token_list_is_empty(const token_list_t *list)
{
    return (bool)(list->index < 0);
}

static void token_list_print(const token_list_t *list)
{
    int index;
    int length = token_list_length(list);

    putchar('[');
    for (index = 0; index < length; index++) {
        printf("%s", list->tokens[index]->text);
        if (index < length - 1) {
            printf(", ");
        }
    }
    putchar(']');
}

static void token_list_push(token_list_t *list, const token_t *token)
{
    token_list_resize_maybe(list);
    list->tokens[++list->index] = token;
}

static bool token_list_push_id(token_list_t *list, int id)
{
    const token_t *token = token_get(id);
    if (token != NULL) {
        token_list_push(list, token);
        return true;
    }
    return false;
}

#define token_list_enqueue(list, id) token_list_push(list, id)

static const token_t *token_list_peek(const token_list_t *list)
{
    if (list->index >= 0) {
        return list->tokens[list->index];
    } else {
        return NULL;
    }
}

static const token_t *token_list_pull(token_list_t *list)
{
    if (list->index >= 0) {
        return list->tokens[list->index--];
    }
    return NULL;
}

static const token_t* token_list_token_at(const token_list_t *list, int index)
{
    if (index < 0 || index >= (int)list->size) {
        return NULL;
    }
    return list->tokens[index];
}


/** \brief  Copy of text fed to tokenizer */
static char   *expr_text;

static token_list_t expr_tokens = TLIST_INIT;
static token_list_t token_stack = TLIST_INIT;
static token_list_t token_queue = TLIST_INIT;


/** \brief  Initialize expression for use
 *
 * Allocate memory for tokenizer and evaluator.
 */
void bexpr_init(void)
{
    token_list_init(&expr_tokens);
    token_list_init(&token_stack);
    token_list_init(&token_queue);
    expr_text  = NULL;
}


/** \brief  Reset tokenizer and evaluator for new expression
 *
 * Reset internal data structures for new expression, without freeing and then
 * allocating resources again.
 *
 * If using this module multiple times during the lifetime of a program (which
 * is likely), do not call bexpr_free() followed by bexpr_init() when having to
 * handle another expression, but call this function instead.
 */
void bexpr_reset(void)
{
    lib_free(expr_text);
    expr_text  = NULL;
    token_list_reset(&expr_tokens);
    token_list_reset(&token_stack);
    token_list_reset(&token_queue);
}


/** \brief  Print tokenized expression on stdout */
void bexpr_print(void)
{
    int index;
    int length = token_list_length(&expr_tokens);

    for (index = 0; index < length; index++) {
        const token_t *token = token_list_token_at(&expr_tokens, index);

        printf("'%s'", token->text);
        if (index < length) {
            printf(", ");
        }
    }
    putchar('\n');
}


/** \brief  Free memory used by expression
 *
 * Free all memory used by the expression, including the operator stack and the
 * output queue.
 */
void bexpr_free(void)
{
    token_list_free(&expr_tokens);
    token_list_free(&token_stack);
    token_list_free(&token_queue);
    lib_free(expr_text);
}


/** \brief  Add token to expression
 *
 * \param[in]   id  token ID
 *
 * \return  \c false if token \a id is invalid
 */
bool bexpr_add_token(int id)
{
    if (token_list_push_id(&expr_tokens, id)) {
        return true;
    } else {
        SET_ERROR(BEXPR_ERR_INVALID_TOKEN);
        return false;
    }
}


/** \brief  Generate expression from a string
 *
 * Parse \a text and tokenize into an expression. There is no syntax checking
 * performed, only splitting the \a text into tokens for the evaluator.
 *
 * \param[in]   text    string to tokenize
 *
 * \return  \c true on success
 */
bool bexpr_tokenize(const char *text)
{
    size_t len;

    text = skip_whitespace(text);
    len  = strlen(text);

    /* make copy of input text */
    expr_text = lib_malloc(len + 1u);
    memcpy(expr_text, text, len + 1u);

    //printf("%s(): parsing '%s':\n", __func__, text);

    while (*text != '\0') {
        const char *endptr;
        int         token;

        text  = skip_whitespace(text);
        token = token_parse(text, &endptr);
        //printf("%s(): token ID: %d\n", __func__, token);
        if (token == BEXPR_INVALID) {
            /* error code already set */
            return false;
        }
        bexpr_add_token(token);
        text = endptr;
    }
    return true;
}


/** \brief  Convert infix expression to postfix expression
 *
 * Use the Shunting yard algorithm to convert an infix expression to postfix
 * ("reverse polish notation").
 *
 * \return  \c true on succces
 */
static bool infix_to_postfix(void)
{
    const token_t *oper1 = NULL;
    const token_t *oper2 = NULL;

    for (int i = 0; i < token_list_length(&expr_tokens); i++) {
        oper1 = token_list_token_at(&expr_tokens, i);

        printf("\n%s(): stack: ", __func__);
        token_list_print(&token_stack);
        putchar('\n');
        printf("%s(): queue: ", __func__);
        token_list_print(&token_queue);
        putchar('\n');
        printf("%s(): token: '%s':\n",  __func__, oper1->text);

        if (is_operand(oper1->id)) {
            /* operands are added unconditionally to the output queue */
            token_list_enqueue(&token_queue, oper1);
        } else {
            /* handle operators */
            if (oper1->id == BEXPR_LPAREN) {
                /* left parenthesis: onto the operator stack */
                token_list_push(&token_stack, oper1);
            } else if (oper1->id == BEXPR_RPAREN) {
                /* right parenthesis: while there's an operator on the stack
                 * and it's not a left parenthesis: pull from stack and add to
                 * the output queue */

                while (!token_list_is_empty(&token_stack)) {
                    oper1 = token_list_pull(&token_stack);
                    if (oper1->id != BEXPR_LPAREN) {
                        token_list_enqueue(&token_queue, oper1);
                    }
                }
                /* sanity check: must have a left parenthesis otherwise we
                 * have mismatched parenthesis */
                if (oper1->id != BEXPR_LPAREN) {
                    SET_ERROR(BEXPR_ERR_EXPECTED_LPAREN);
                    return false;
                }

            } else {
                /* handle operator until a left parenthesis is on top of the
                 * operator stack */
                while (!token_list_is_empty(&token_stack)) {

                    /* check for left parenthesis */
                    oper2 = token_list_peek(&token_stack);
                    if (oper2->id == BEXPR_LPAREN) {
                        break;
                    }

                    if ((oper2->prec > oper1->prec) ||
                            ((oper2->prec == oper1->prec) && oper1->assoc == BEXPR_LTR)) {
                        oper2 = token_list_pull(&token_stack);
                        token_list_enqueue(&token_queue, oper2);
                    } else {
                        break;
                    }
                }
                token_list_push(&token_stack, oper1);
            }
        }
    }

    printf("%s(): operator stack = ", __func__);
    token_list_print(&token_stack);
    putchar('\n');
    while (!token_list_is_empty(&token_stack)) {
        oper1 = token_list_pull(&token_stack);
        printf("%s(): pulled operator (%s,%d)\n",
               __func__, oper1->text, oper1->id);

        if (oper1->id == BEXPR_LPAREN) {
            /* unexpected left parenthesis */
            SET_ERROR(BEXPR_ERR_UNMATCHED_PARENS);
            return false;
        }
        token_list_enqueue(&token_queue, oper1);
    }

    printf("%s(): output queue = ", __func__);
    token_list_print(&token_queue);
    putchar('\n');

    return true;
}


static bool eval_postfix(bool *result)
{
    *result = true;
    return true;
}


/** \brief  Evaluate boolean expression
 *
 * Evaluate boolean expression, either obtained by bexpr_parse() or by adding
 * tokens with bexpr_add_token().
 *
 * \param[out]  result  result of evaluation
 *
 * \return  \c true on succes
 */
bool bexpr_evaluate(bool *result)
{
    *result = false;

    if (token_list_length(&expr_tokens) <= 0) {
        SET_ERROR(BEXPR_ERR_EMPTY_EXPRESSION);
        return false;
    }

    /* reset stack for use as operand stack */
    token_list_reset(&token_stack);
    /* reset output queue */
    token_list_reset(&token_queue);

    /* convert infix expression to postfix expression */
    if (!infix_to_postfix()) {
        /* error code already set */
        return false;
    }

    /* reset stack for use as operand stack */
    token_list_reset(&token_stack);

    /* try to evaluate the postfix expression in the queue */
    if (!eval_postfix(result)) {
        /* error code already set */
        return false;
    }

    return true;
}
