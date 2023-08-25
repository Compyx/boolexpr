/** \file   main.c
 * \brief   Test driver for boolexpr.{c,h}
 * \author  Bas Wassink <b.wassink@ziggo.nl>
 */

/* Copyright (C) 2023  Bas Wassink
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

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

/** \brief  Skip whitespace in string
 *
 * \param[in]   s   string
 *
 * \return  pointer to first non-whitespace character in \a s (can point to
 *          terminating nul character in \a s if only whitespace was encountered
 */
static char *skip_whitespace(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

/** \brief  Run test on expression
 *
 * Tokenize and evaluate \a text, comparing \a expected_errnum to \c bexpr_errno
 * and \a expected_result to the result of evaluation.
 *
 * \param[in]   text            expression text
 * \param[in]   expected_errnum expected error number
 * \param[in]   expected_result expected result of evaluation
 *
 * \return  \c true if test passed
 */
static bool run_test(const char *text, int expected_errnum, bool expected_result)
{
    bool result = false;

    bexpr_reset();
    bexpr_errno = 0;

    if (!bexpr_tokenize(text)) {
        printf("bexpr_tokenize() failed: ");
        if (expected_errnum == 0) {
            printf("FAIL: expected to pass\n");
            return false;
        } else {
            if (expected_errnum == bexpr_errno) {
                printf("PASS\n");
                return true;
            } else {
                printf("FAIL: errnum %d doesn't match expected %d\n",
                       bexpr_errno, expected_errnum);
                return false;
            }
        }
    }

    printf("Evaluating expression: ");
    if (bexpr_evaluate(&result)) {
        /* evaluation passed */
        if (result == expected_result) {
            printf(" %s: PASS.\n", result ? "true" : "false");
            return true;
        } else {
            printf(" FAIL: result %s doesn't match expected %s\n",
                   result ? "true" : "false",
                   expected_result ? "true" : "false");
            return false;
        }
    } else {
        if (bexpr_errno != expected_errnum) {
            printf("FAIL: errnum %d doesn't match expected %d\n",
                   bexpr_errno, expected_errnum);
            return false;
        } else {
            printf("errnum %d: PASS.\n", bexpr_errno);
        }
        return true;
    }
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
    int      total  = 0;
    int      passed = 0;

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

        if (run_test(curpos, (int)errnum_exp, result_exp)) {
            passed++;
        }
        lineno++;
        total++;
    }
cleanup:
    bexpr_free();
    fclose(fp);

    printf("Passed: %d out of %d\n", passed, total);

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
