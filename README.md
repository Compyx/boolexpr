# Boolean expression evaluator

A simple evaluator for infix boolean expressions, for handling conditionals in
parsers and similar code. This one was written to be used in
[VICE](https://sourceforge.net/projects/vice-emu/) to handle contionals in
hotkeys files.

The evaluator supports seven different tokens: `false`, `true`, `&&` (and),
`||`, (or), `!` (not), `(` and `)`. Precedence of operators is the same as used
in C, so in order of higher to lower precedence: `!`, `&&`, `||`.

## Building

Simply run `make`. Some POSIX-specific code is present in `main.c`, the test
driver, which serves my purposes just fine. The "library" (`boolexpr.c` and
`boolexpr.h`) should be clean C99.

### Running the tests

After running `make`, an executable `expr-test` will be present that can be used
to parse a text file with tests. Usage is as follows: `expr-test <filename>`.

The input file is expected to contain boolean expressions, prefixed with expected
error code and expected result (if error code is 0).
For example:

~~~
0   false   true && false
0   true    false || true && true
4           true && false )
6           ( true && false
~~~

Empty lines are allowed in the file, as are comments starting with **`#`**.

## API

Use of the API is straightforward: initialize for use, feed expression, attempt
to evaluate, clean up when done.

### Initializiation

Declare a `boolexpr_t` object and initialize it using `bexpr_init()`:
```c
#include <boolexpr.h>

bexpr_init();
```

This allocates memory for the evaluator and initializes some internal state.

### Evaluating an expression

An expression can be either parsed from text, using `bexpr_tokenize()`, or fed
to the evaluator token by token with `bexpr_add_token()`.

Once an expression is made available through either method, the expression can
be evaluated with `bexpr_evaluate()`.

For example:
```c
bool result;

/* tokenize text */
if (!bexpr_tokenize("false || true")) {
    fprintf(stderr, "oops!\n");
    exit(1);
}

/* try to evaluate expression */
if (bexpr_evaluate(&result)) {
    printf("result = %s\n", result ? "true" : "false");
} else{
    fprintf(stderr, "oops!");
    exit(1);
}
```

### Reusing the evaluator and cleaning up

The memory used by the evaluator must be freed after use with `bexpr_free()`.

When evaluating multiple expressions during the lifetime of a program, the
evaluator can be reset for reuse without freeing and reallocating internal
resources with `bexpr_reset()`. Cleaning up (on program exit or so) still
requires `bexpr_free()` to properly release used resources.

## Error codes and messages

A simple mechanism is provided to check for types of errors and translating them
into user-readable messages:

The variable `extern int bexpr_errno` provides the error code set by the
evaluator in case of errors.
Error messages can be obtained with `bexpr_strerror(int errnum)`.

Currently there is no possibility to pinpoint where in an expression the error
occurred, so reporting the column number in a source line won't work.


