#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

#include "boolexpr.h"


/** \brief  Buffer used for reading and parsing lines of the input file
 */
static char line[256];

static char *prgname;


/** \brief  Print usage message on stdout
 *
 * \param[in]   argv0   contents of \c argv[0], the path to the executable
 */
static void usage(void)
{
    printf("Usage: %s <filename>\n", prgname);
}

/** \brief  Print I/O error number and message on stderr
 *
 * \param[in]   funcname    function name
 */
static void print_ioerror(void)
{
    fprintf(stderr,
            "%s: I/O error %d (%s).\n",
            prgname, errno, strerror(errno));
}

static char *skip_whitespace(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/** \brief  Parse file \a path to test boolean expression handling
 *
 * \param[in]   path    path to file to parse
 *
 *
 * \return  \c true on success
 */
static bool parse_file(const char *path)
{
    FILE    *fp;
    int      lineno = 1;
    bool     status = true;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        print_ioerror();
        return false;
    }

    bexpr_init();

    while (true) {
        char *curpos;
        char *endptr;
        long  errnum_exp = 0;   /* expected error number */
        bool  result_exp = false;   /* expected result of evaluation */

        bexpr_reset();

        memset(line, 0, sizeof line);
        if (fgets(line, (int)sizeof line, fp) == NULL) {
            if (!feof(fp)) {
                print_ioerror();
                status = false;
            }
            break;
        }

        /* trim trailing whitespace, including newline/carriage return */
        for (int i = (int)strlen(line) - 1; i >= 0 && isspace((unsigned char)(line[i])); i--) {
            line[i] = '\0';
        }

        /* skip leading whitespace */
        curpos = skip_whitespace(line);
        if (*curpos == '#' || *curpos == '\0') {
            /* comment or empty line: skip line */
            lineno++;
            continue;
        }

        /* get expected error number */
        errno = 0;
        errnum_exp = strtol(curpos, &endptr, 10);
        if ((errno == ERANGE) || curpos == endptr) {
            fprintf(stderr,
                    "%s:%d:%d: Expected error number.\n",
                    path, lineno, (int)(line - curpos));
            status = false;
            goto cleanup;
        }
        curpos = endptr;

        /* get expected result if expected result is 0 */
        if (errnum_exp == 0) {
            curpos = skip_whitespace(curpos);
            if (strncmp(curpos, "true", 4u) == 0) {
                curpos += 4;
                result_exp = true;
            } else if (strncmp(curpos, "false", 5u) == 0) {
                curpos += 5;
                result_exp = false;
            } else {
                fprintf(stderr,
                        "%s:%d:%d: Expected evaluation result (\"true\" or"
                        " \"false\").\n",
                        path, lineno, (int)(line - curpos));
                goto cleanup;
            }
        }

        curpos = skip_whitespace(curpos);
        printf("[%2d] Tokenizing \"%s\"\n", lineno, curpos);

        if (!bexpr_tokenize(curpos)) {
            printf("[%2d] Parse error.\n", lineno);
        } else {
            bool result = false;

            printf("[%2d] OK: ", lineno);
            bexpr_print();

            printf("[%2d] Evaluating:\n", lineno);
            if (bexpr_evaluate(&result)) {
                printf("[%2d] ok: %s\n", lineno, result ? "true" : "false");
            } else {
                printf("[%2d] fail.\n", lineno);
            }
            if ((result == result_exp) && (bexpr_errno == errnum_exp)) {
                printf("TEST PASS\n");
            } else {
                printf("TEST FAIL\n");
            }

        }
        putchar('\n');
        lineno++;
    }
cleanup:
    bexpr_free();
    fclose(fp);
    return status;
}


/** \brief  Test driver
 *
 * \param[in]   argc    argument count
 * \param[in]   argv    argument vector
 *
 * \return  \c EXIT_SUCCESS on success, \c EXIT_FAILURE on failure
 */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "--help") == 0) {
        usage();
        return EXIT_SUCCESS;
    }

    /* generate program name for messages */
    prgname = basename(argv[0]);

    printf("Parsing '%s':\n", argv[1]);
    if (parse_file(argv[1])) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
