#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <ctype.h>

#include "boolexpr.h"


/** \brief  Buffer used for reading and parsing lines of the input file
 */
static char line[256];


/** \brief  Print usage message on stdout
 *
 * \param[in]   argv0   contents of \c argv[0], the path to the executable
 */
static void usage(char *argv0)
{
    printf("Usage: %s <filename>\n", basename(argv0));
}

/** \brief  Print I/O error number and message on stderr
 *
 * \param[in]   funcname    function name
 */
static void print_ioerror(const char *funcname)
{
    fprintf(stderr,
            "%s(): I/O error %d (%s).\n",
            funcname, errno, strerror(errno));
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
        print_ioerror(__func__);
        return false;
    }

    bexpr_init();

    while (true) {
        bexpr_reset();

        memset(line, 0, sizeof line);
        if (fgets(line, (int)sizeof line, fp) == NULL) {
            if (!feof(fp)) {
                print_ioerror(__func__);
                status = false;
            }
            break;
        }
        /* trim trailing whitespace, including newline/carriage return */
        for (int i = (int)strlen(line) - 1; i >= 0 && isspace((unsigned char)(line[i])); i--) {
            line[i] = '\0';
        }
        printf("[%2d] Tokenizing \"%s\"\n", lineno, line);

        if (!bexpr_tokenize(line)) {
            printf("[%2d] Parse error.\n", lineno);
        } else {
            bool result = false;
            printf("[%2d] OK: ", lineno);
            bexpr_print();

            printf("[%2d] Evaluating:\n", lineno);
            if (bexpr_evaluate(&result)) {
                printf("[%2d] OK: %s\n", lineno, result ? "true" : "false");
            } else {
                printf("[%2d] FAIL.\n", lineno);
            }
        }
        putchar('\n');
        lineno++;
    }

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
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }

    printf("Parsing '%s':\n", argv[1]);
    if (parse_file(argv[1])) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
