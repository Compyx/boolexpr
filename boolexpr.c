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
static bool is_valid_token(int id)
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
    return (is_valid_token(id)) &&
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
    if (is_valid_token(id)) {
        return &token_info[id];
    }
    return NULL;
}

/** \brief  Get string representing token
 *
 * \param[in]   id  token ID
 *
 * \return  token text
 */
static const char *token_text(int id)
{
    const token_t *token = token_get(id);
    if (token != NULL) {
        return token->text;
    } else {
        return "<invalid>";
    }
}

/* }}} */

/* {{{ Token stack */
/** \brief  Initial size of token stack */
#define STACK_INITIAL_SIZE  32u

/** \brief  Array of stack elements */
static int    *stack_elements = NULL;

/** \brief  Number of allocated elements for the stack */
static size_t  stack_size = 0;

/** \brief  Index in the stack of the top element
 *
 * This will be -1 to indicate the stack is empty.
 */
static int     stack_index = -1;


/** \brief  Initialize token stack
 *
 * (Re)initialize stack for use. When called the first time this will allocate
 * memory for a number of elements, which will grow when required. On subsequent
 * calls the stack pointer will be reset but the allocated elements kept intact
 * for reuse.
 */
static void stack_init(void)
{
    if (stack_elements == NULL) {
        stack_size     = STACK_INITIAL_SIZE;
        stack_elements = lib_malloc(sizeof *stack_elements * stack_size);
    }
    stack_index    = -1;    /* stack is empty */
}

/** \brief  Push token onto stack
 *
 * \param[in]   token   token ID
 */
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

/** \brief  Pull token from stack
 *
 * \return  token ID or \c BEXPR_INVALID if stack is empty
 */
static int stack_pull(void)
{
    if (stack_index < 0) {
        /* error, stack empty */
        return BEXPR_INVALID;
    } else {
        return stack_elements[stack_index--];
    }
}

/** \brief  Peek at top of stack
 *
 * Get token ID at top of stack but don't pull it from stack.
 *
 * \return  token ID or \c BEXPR_INVALID if stack is empty
 */
static int stack_peek(void)
{
    return stack_index < 0 ? BEXPR_INVALID : stack_elements[stack_index];
}

/** \brief  Check if stack is empty
 *
 * \return \c true is stack is empty
 */
static bool stack_is_empty(void)
{
    return stack_index < 0;
}

/** \brief  Free memory used by token stack */
static void stack_free(void)
{
    lib_free(stack_elements);
    stack_elements = NULL;
}

/** \brief  Print token stack on stdout
 *
 * \note    Doesn't add a newline so the user can print additional data after
 *          the stack on the same line.
 */
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
/* }}} */

/* {{{ Output queue */
/** \brief  Initial number of queue elements to allocate */
#define QUEUE_INITIAL_SIZE 32

/** \brief  Output queue elements */
static int    *queue_elements = NULL;

/** \brief  Number of allocated queue elements */
static size_t  queue_size = 0;

/** \brief  Index in queue of last element
 *
 * An index of -1 means the queue is empty.
 */
static int     queue_index = -1;


/** \brief  Initialize ouput queue
 *
 * (Re)initialize queue for use. When called the first time this will allocate
 * memory for a number of elements, which will grow when required. On subsequent
 * calls the tail pointer will be reset but the allocated elements kept intact
 * for reuse.
 */
static void queue_init(void)
{
    if (queue_elements == NULL) {
        queue_size     = QUEUE_INITIAL_SIZE;
        queue_elements = lib_malloc(sizeof *queue_elements * queue_size);
    }
    queue_index    = -1;
}

/** \brief  Print output queue on stdout
 *
 * \note    Doesn't print a newline
 */
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

/** \brief  Free memory used by output queue */
static void queue_free(void)
{
    lib_free(queue_elements);
    queue_elements = NULL;
}

/** \brief  Add token to the output queue
 *
 * \param[in]   token   token ID
 */
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
/** \brief  Get token from front of queue
 *
 * \return  token ID or \c BEXPR_INVALID if the queue is empty
 */
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
/* }}} */


#define TLIST_INIT { .tokens = NULL, .size = 0, .index = -1 }


/* Dynamic token list, implements stack and queue operations */

typedef struct token_list_s {
    const token_t **tokens; /**< array of token info pointers */
    size_t          size;   /**< size of \c tokens */
    int             index;  /**< index in \c tokens, -1 means list is empty */
} token_list_t;


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

static void token_list_resize(token_list_t *list)
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

static int token_list_length(token_list_t *list)
{
    return list->index + 1;
}

static bool token_list_push(token_list_t *list, int id)
{
    const token_t *token = token_get(id);
    if (token != NULL) {
        token_list_resize(list);
        list->tokens[++list->index] = token;
        return true;
    }
    return false;
}

#define token_list_enqueue(list, id) token_list_push(list, id)

static const token_t *token_list_pop(token_list_t *list)
{
    if (list->index >= 0) {
        return list->tokens[--list->index];
    }
    return NULL;
}

static const token_t* token_list_token_at(token_list_t *list, int index)
{
    if (index < 0 || index >= (int)list->size) {
        return NULL;
    }
    return list->tokens[index];
}


/** \brief  Copy of text fed to tokenizer */
static char   *expr_text;

#if 0
/** \brief  List of tokens making up the infix expression */
static int    *expr_tokens;

/** \brief  Length of expression */
static size_t  expr_length;

/** \brief  Number of tokens allocated */
static size_t  expr_avail;
#endif

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
//    expr_length = 0;
//    expr_avail  = 32;
//    expr_tokens = lib_malloc(sizeof *expr_tokens * expr_avail);
    stack_init();
    queue_init();

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
    stack_init();
    queue_init();
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
    stack_free();
    queue_free();
}


/** \brief  Add token to expression
 *
 * \param[in]   id  token ID
 *
 * \return  \c false if token \a id is invalid
 */
bool bexpr_add_token(int id)
{
    if (token_list_push(&expr_tokens, id)) {
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
    int  oper1;
    int  oper2;
    int  prec1;
    int  prec2;
    int  assoc1;

    for (int i = 0; i < token_list_length(&expr_tokens); i++) {
        const token_t *token = token_list_token_at(&expr_tokens, i);

        printf("\n%s(): stack: ", __func__);
        stack_print();
        putchar('\n');
        printf("%s(): queue: ", __func__);
        queue_print();
        putchar('\n');
        printf("%s(): token: '%s':\n",  __func__, token->text);

        if (is_operand(token->id)) {
            /* operands are added unconditionally to the output queue */
            queue_enqueue(token->id);
        } else {
            /* handle operators */
            oper1 = token->id;

            if (oper1 == BEXPR_LPAREN) {
                /* left parenthesis: onto the operator stack */
                stack_push(oper1);
            } else if (oper1 == BEXPR_RPAREN) {
                /* right parenthesis: while there's an operator on the stack
                 * and it's not a left parenthesis: pull from stack and add to
                 * the output queue */
                while (!stack_is_empty()) {
                    oper1 = stack_pull();
                    if (oper1 != BEXPR_LPAREN) {
                        queue_enqueue(oper1);
                    }
                }
                /* sanity check: must have a left parenthesis otherwise we
                 * have mismatched parenthesis */
                if (oper1 != BEXPR_LPAREN) {
                    SET_ERROR(BEXPR_ERR_EXPECTED_LPAREN);
                    return false;
                }

            } else {
                /* handle operator until a left parenthesis is on top of the
                 * operator stack */
                while (!stack_is_empty()) {
                    const token_t *tok1;
                    const token_t *tok2;

                    /* check for left parenthesis */
                    oper2 = stack_peek();
                    if (oper2 == BEXPR_LPAREN) {
                        break;
                    }

                    tok1 = token_get(oper1);
                    tok2 = token_get(oper2);
                    /* sanity check: shouldn't be true if the parser is correct */
                    if (tok1 == NULL || tok2 == NULL) {
                        SET_ERROR(BEXPR_ERR_FATAL);
                        return false;
                    }

                    prec1  = tok1->prec;
                    prec2  = tok2->prec;
                    assoc1 = tok1->assoc;

                    if ((prec2 > prec1) || ((prec2 == prec1) && assoc1 == BEXPR_LTR)) {
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
            /* unexpected left parenthesis */
            SET_ERROR(BEXPR_ERR_UNMATCHED_PARENS);
            return false;
        }
        queue_enqueue(oper1);
    }

    printf("%s(): output queue = ", __func__);
    queue_print();
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
    stack_init();
    /* reset output queue */
    queue_init();

    /* convert infix expression to postfix expression */
    if (!infix_to_postfix()) {
        /* error code already set */
        return false;
    }

    /* reset stack for use as operand stack */
    stack_init();

    /* try to evaluate the postfix expression in the queue */
    if (!eval_postfix(result)) {
        /* error code already set */
        return false;
    }

    return true;
}
