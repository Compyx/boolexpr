#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "boolexpr.h"


#define ARRAY_LEN(arr)  (sizeof arr / sizeof arr[0])

int bexpr_errno = 0;

static char   *expr_text;
static int    *expr_tokens;
static size_t  expr_length;
static size_t  expr_avail;



/* Memory management: reimplementation of VICE's lib_foo() functions */

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
    const char *text;           /**< text */
    int         token;          /**< ID */
    int         arity;          /**< operator arity */
    int         associativity;  /**< operator associativity */
    int         precedence;     /**< operator precedence */
} token_t;

/** \brief  Maximum lenght of a token's text */
#define MAX_TOKEN_LEN   5

/** \brief  List of valid tokens
 *
 * Contains both operators and operands.
 */
static const token_t token_list[] = {
    { "false",  BEXPR_FALSE,    0,                  0,              0 },
    { "0",      BEXPR_FALSE,    0,                  0,              0 },
    { "true",   BEXPR_TRUE,     0,                  0,              0 },
    { "1",      BEXPR_TRUE,     0,                  0,              0 },
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
 * \param[in]   token   token ID
 *
 * \return  \c true if valid
 */
static bool is_valid_token(int token)
{
    return token >= BEXPR_FALSE && token <= BEXPR_RPAREN ? true : false;
}

#if 0
static bool is_operator(int token)
{
    return (is_valid_token(token)) &&
           ((token != BEXPR_FALSE) && (token != BEXPR_TRUE));
}
#endif

static bool is_operand(int token)
{
    return (is_valid_token(token)) &&
           ((token == BEXPR_FALSE) || (token == BEXPR_TRUE));
}

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
        return BEXPR_INVALID;
    }

    tlen = (size_t)(pos - text);
    if (tlen > MAX_TOKEN_LEN) {
        tlen = MAX_TOKEN_LEN;
    }
    //printf("%s(): testing token '%s'\n", __func__, token);

    /* look up token, reducing size (greedy matching) each time */
    while (tlen >= 1) {
        for (size_t i = 0; i < ARRAY_LEN(token_list); i++) {
            if (strncmp(token_list[i].text, text, tlen) == 0) {
                if (endptr != NULL) {
                    *endptr = text + tlen;
                }
                return token_list[i].token;
            }
        }
        tlen--;
    }
    return BEXPR_INVALID;
}

static const char *token_text(int token)
{
    if (is_valid_token(token)) {
        for (size_t i = 0; i < ARRAY_LEN(token_list); i++) {
            if (token_list[i].token == token) {
                return token_list[i].text;
            }
        }
    }
    return "<invalid>";
}

static const token_t *token_find(int token)
{
    for (size_t i = 0; ARRAY_LEN(token_list); i++) {
        if (token_list[i].token == token) {
            return &token_list[i];
        }
    }
    return NULL;
}

static int token_precedence(int token)
{
    const token_t *t = token_find(token);
    if (t != NULL) {
        return t->precedence;
    }
    return BEXPR_INVALID;
}

static int token_associativity(int token)
{
    const token_t *t = token_find(token);
    if (t != NULL) {
        return t->associativity;
    }
    return BEXPR_INVALID;
}

#if 0
static int token_arity(int token)
{
    const token_t *t = token_find(token);
    if (t != NULL) {
        return t->arity;
    }
    return BEXPR_INVALID;
}
#endif

/* Operator stack */

#define STACK_INITIAL_SIZE  32u

static int    *stack_elements;
static size_t  stack_size;
static int     stack_index;


static void stack_init(void)
{
    stack_size     = STACK_INITIAL_SIZE;
    stack_elements = lib_malloc(sizeof *stack_elements * stack_size);
    stack_index    = -1;    /* stack is empty */
}

static void stack_reset(void)
{
    stack_index = -1;
}

static void stack_push(int token)
{
    stack_index++;
    if ((size_t)stack_index == stack_size) {
        stack_size *= 2;
        stack_elements = lib_realloc(stack_elements,
                                     sizeof *stack_elements * stack_size);
    }
    stack_elements[stack_index] = token;
}

static int stack_pull(void)
{
    if (stack_index < 0) {
        /* error, stack empty */
        return BEXPR_INVALID;
    } else {
        return stack_elements[stack_index--];
    }
}

static int stack_peek(void)
{
    return stack_index < 0 ? BEXPR_INVALID : stack_elements[stack_index];
}

static bool stack_is_empty(void)
{
    return stack_index < 0;
}

static void stack_free(void)
{
    lib_free(stack_elements);
}

static void stack_print(void)
{
    putchar('[');
    for (int i = 0; i <= stack_index; i++) {
        int token = stack_elements[i];

        printf("'%s'", token_text(token));
        if (i < stack_index) {
            printf(", ");
        }
    }
    putchar(']');
}


#define QUEUE_INITIAL_SIZE 32

static int    *queue_elements;
static size_t  queue_size;
static int     queue_index;


static void queue_init(void)
{
    queue_size     = QUEUE_INITIAL_SIZE;
    queue_elements = lib_malloc(sizeof *queue_elements * queue_size);
    queue_index    = -1;
}

static void queue_reset(void)
{
    queue_index = -1;
}

static void queue_print(void)
{
    putchar('[');
    for (int i = 0; i <= queue_index; i++) {
        int token = queue_elements[i];

        printf("'%s'", token_text(token));
        if (i < queue_index) {
            printf(", ");
        }
    }
    putchar(']');
}

static void queue_free(void)
{
    lib_free(queue_elements);
}

static void queue_enqueue(int token)
{
    queue_index++;
    if ((size_t)queue_index == queue_size) {
        queue_size *= 2;
        queue_elements = lib_realloc(queue_elements,
                                     sizeof *queue_elements * queue_size);
    }
    queue_elements[queue_index] = token;
}

#if 0
static int queue_dequeue(void)
{
    int token;

    if (queue_index < 0) {
        token = BEXPR_INVALID;
    } else {
        token = queue_elements[0];
        for (int i = 0; i < queue_index; i++) {
            queue_elements[i] = queue_elements[i + 1];
        }
        if (queue_index >= 0) {
            queue_index--;
        }
    }
    return token;
}
#endif


void bexpr_init(void)
{
    expr_text   = NULL;
    expr_length = 0;
    expr_avail  = 32;
    expr_tokens = lib_malloc(sizeof *expr_tokens * expr_avail);
    stack_init();
    queue_init();
}


void bexpr_reset(void)
{
    lib_free(expr_text);
    expr_text   = NULL;
    expr_length = 0;
    stack_reset();
    queue_reset();
}


void bexpr_print(void)
{
    for (size_t i = 0; i < expr_length; i++) {
        printf("'%s'", token_text(expr_tokens[i]));
        if (i < expr_length - 1u) {
            printf(", ");
        }
    }
    putchar('\n');
}

void bexpr_free(void)
{
    lib_free(expr_tokens);
    lib_free(expr_text);
    stack_free();
    queue_free();
}


bool bexpr_add_token(int token)
{
    if (is_valid_token(token)) {
        /* do we need to resize the tokens array? */
        if (expr_length == expr_avail) {
            /* yes */
            expr_avail *= 2;
            expr_tokens = lib_realloc(expr_tokens,
                                      sizeof *expr_tokens * expr_avail);
        }
        expr_tokens[expr_length++] = token;
        return true;
    }
    return false;
}


bool bexpr_tokenize(const char *text)
{
    size_t len;

    text = skip_whitespace(text);
    len  = strlen(text);

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
            printf("%s(): parse error: invalid token\n", __func__);
            return false;
        }
        bexpr_add_token(token);
        text = endptr;
    }
    return true;
}


bool bexpr_evaluate(bool *result)
{
    bool status = true;
    int oper1;
    int oper2;
    int prec1;
    int prec2;
    int assoc1;

    if (expr_length <= 0) {
        *result = false;
        return false;
    }

    stack_reset();
    queue_reset();

    /* convert infix expression to postfix expression using the shunting yard
     * algoritm */
    for (size_t i = 0; i < expr_length; i++) {
        int token = expr_tokens[i];

        printf("\n%s(): stack: ", __func__);
        stack_print();
        putchar('\n');
        printf("%s(): queue: ", __func__);
        queue_print();
        putchar('\n');
        printf("%s(): token: '%s':\n",  __func__, token_text(token));

        if (is_operand(token)) {
            queue_enqueue(token);
        } else {
            if (token == BEXPR_LPAREN) {
                stack_push(token);
            } else if (token == BEXPR_RPAREN) {
                while (!stack_is_empty()) {
                    oper1 = stack_pull();
                    if (oper1 != BEXPR_LPAREN) {
                        queue_enqueue(oper1);
                    }
                }
            } else {
                oper1 = token;

                while (!stack_is_empty()) {
                    oper2 = stack_peek();
                    if (oper2 == BEXPR_LPAREN) {
                        break;
                    }

                    /* TODO: These lookups can be replace with token struct
                     *       lookups and getting their members, reducing the
                     *       number of lookups required.
                     */
                    prec1  = token_precedence(oper1);
                    prec2  = token_precedence(oper2);
                    assoc1 = token_associativity(oper1);
                    if ((prec2 > prec1) ||
                            ((prec2 == prec1) && assoc1 == BEXPR_LTR)) {

                        oper2 = stack_pull();
                        queue_enqueue(oper2);
                    } else {
                        break;
                    }
                }
                stack_push(oper1);
            }
        }
    }

    printf("%s(): operator stack = ", __func__);
    stack_print();
    putchar('\n');
    while (!stack_is_empty()) {
        oper1 = stack_pull();
        printf("%s(): pulled operator (%s,%d)\n",
               __func__, token_text(oper1), oper1);

        if (oper1 == BEXPR_LPAREN) {
            printf("%s(): unmatched left parenthesis\n", __func__);
            status = false;
            break;
        }
        queue_enqueue(oper1);
    }

    printf("%s(): output queue = ", __func__);
    queue_print();
    putchar('\n');


    *result = true;
    return status;
}
