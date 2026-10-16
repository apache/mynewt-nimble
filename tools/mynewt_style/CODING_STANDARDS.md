<!--
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
-->

# Apache Mynewt Coding Standards

This is the C coding standard for Apache Mynewt projects (apache-mynewt-core
and apache-mynewt-nimble). It is checked by `tools/mynewt_format.py`; every
guideline names the rule that checks it, e.g. [`sizeof-parens`], and
`python3 tools/mynewt_format.py --explain RULE` gives the reasoning with more
examples. Guidelines marked *(not checked)* are left to review.

The limits and options shown are the defaults; each repository may tune them
in its `newt-coding-rules` file.

> **Status:** this standard is being introduced. Until mynewt_format becomes
> the required check, the `clang-format` CI check and the `CODING_STANDARDS.md`
> at the repository root remain authoritative where they differ (most notably
> on the placement of function return types and the 79 column limit).

## Contents

1. [Checking your code](#checking-your-code)
2. [Files](#files)
3. [Whitespace](#whitespace)
4. [Braces and control statements](#braces-and-control-statements)
5. [Line length and wrapping](#line-length-and-wrapping)
6. [Functions](#functions)
7. [Declarations and types](#declarations-and-types)
8. [Comments](#comments)
9. [Preprocessor and macros](#preprocessor-and-macros)
10. [Expressions and safety](#expressions-and-safety)
11. [Naming](#naming)
12. [Exceptions](#exceptions)

## Checking your code

```sh
python3 tools/mynewt_format.py                      # your uncommitted changes
python3 tools/mynewt_format.py --git-diff upstream/master   # your branch
python3 tools/mynewt_format.py -i                   # review the fixes one by one
python3 tools/mynewt_format.py --list-rules         # all rules
```

Only lines you changed are checked, so code that predates a rule does not get
in the way; `--whole-files` checks everything. Most problems can be fixed
automatically (`-d` shows the fixes as a patch, `-f` applies them). Every pull
request is checked the same way by CI.

## Files

* New files start with the Apache License header. Files copied from elsewhere
  keep their original license header. [`license-header`]

```c
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * ...
 * under the License.
 */
```

* Header files are guarded against multiple inclusion; the `#ifndef` and the
  `#define` must use the same name. `#pragma once` is accepted. [`header-guard`]

```c
#ifndef H_BLE_FOO_
#define H_BLE_FOO_
...
#endif /* H_BLE_FOO_ */
```

* Public headers (in an `include/` directory) wrap their declarations for C++,
  after their own `#include` lines. [`extern-c`]

```c
#ifdef __cplusplus
extern "C" {
#endif
...
#ifdef __cplusplus
}
#endif
```

* Each header is included once per file. [`duplicate-include`]
* Files use Unix line endings and end with exactly one newline.
  [`line-endings`], [`eof-newline`]

## Whitespace

* Indent with 4 spaces; never use tabs. [`no-tabs`]
* No whitespace at the end of a line. [`trailing-whitespace`]
* Blank lines group code; at most one blank line in a row.
  [`max-blank-lines`]
* One space after `if`, `for`, `while` and `switch`, and around the braces of
  control statements. [`keyword-space`]
* No space between a function name and `(`, in calls and declarations.
  [`call-paren-spacing`]
* One space after a comma, none before it. [`comma-space`]
* Spaces around assignment (`=`, `+=`, ...), comparison (`==`, `!=`, `<=`,
  `>=`) and logical (`&&`, `||`) operators. Extra spaces that line up a column
  of assignments are fine. [`operator-spacing`]
* No spaces just inside parentheses or brackets, and none before `;`.
  [`space-inside-parens`], [`space-before-semicolon`]
* The `*` of a pointer belongs to the name; casts have no space before their
  operand. [`pointer-alignment`], [`cast-spacing`]

```c
/* Good */                              /* Bad */
rc = ble_foo(a, b);                     rc = ble_foo (a,b);
if (x == 1 && y != 2) {                 if(x==1&&y!=2){
struct os_mbuf *om;                     struct os_mbuf* om;
len = (uint16_t)om->om_len;             len = (uint16_t) om->om_len;
```

## Braces and control statements

* `if`, `else`, `for`, `while` and `do` bodies always use braces, even for a
  single statement. [`braces-required`]
* The opening brace of a control statement is on the same line as the
  statement, and `else` follows the closing brace on the same line (as does
  the `while` of a `do`/`while` loop). [`control-brace-placement`]
* The body of an `if`, `else` or loop is never an empty statement; use `{ }`
  for an intentionally empty loop. [`empty-body`]

```c
/* Good */                              /* Bad */
if (x) {                                if (x)
    rc = 0;                                 rc = 0;
} else {                                else
    rc = 1;                                     rc = 1;
}
```

## Line length and wrapping

* Aim for lines of at most 80 columns; never exceed 100. Lines between 80 and
  100 columns are warnings, longer lines errors. URLs and `#include` lines are
  exempt. [`line-length`]
* Only wrap what does not fit: function arguments and parameters that fit on
  one line within 100 columns are written on one line. [`unnecessary-wrap`]
* When a statement has to be wrapped, break after a `,`, `&&` or `||` and
  align the continuation with the opening parenthesis. Put the operator at the
  end of the line, not at the start of the next one. [`operator-at-line-end`]

```c
rc = ble_gattc_write_flat(conn_handle, attr_handle, value_buffer, value_length,
                          ble_gattc_write_complete_cb, callback_argument);

if (ble_foo_is_ready() &&
    conn_handle != BLE_HS_CONN_HANDLE_NONE) {
```

## Functions

* The return type is on the same line as the function name. If the signature
  does not fit, wrap the parameters and align them with the opening
  parenthesis. [`return-type-placement`]
* The opening brace of a function body is on its own line. Short `static
  inline` functions in headers may be written on one line.
  [`function-brace-newline`]

```c
static int ble_foo_init(uint8_t instance, const struct ble_foo_cfg *cfg,
                        ble_foo_event_fn *cb)
{
    ...
}
```

* Functions that report success or failure return 0 on success and a non-zero
  error code on failure. *(not checked)*
* Public APIs are documented with Doxygen comments describing their purpose,
  parameters and return values. *(not checked; see the Doxygen CI check)*

## Declarations and types

* Local variables are declared at the top of the function, before any
  statement, and not in `for` headers. An early `return` under `#if` that
  compiles a function out is allowed above them. [`declarations-at-top`]
* One blank line separates the declarations from the first statement.
  [`blank-line-after-declarations`]

```c
int ble_foo_count(const struct ble_foo *foo)
{
    const struct ble_foo_entry *entry;
    int count;
    int i;

    count = 0;
    for (i = 0; i < foo->num; i++) {
        ...
```

* Do not `typedef` structures; use `struct foo` so the type can stay opaque.
  [`no-struct-typedef`]
* Other `typedef` names end in `_t`; function and function pointer types end
  in `_fn` (or `_cb`). [`typedef-suffix`]

## Comments

* Use C comments `/* ... */`, never `//`. [`no-cpp-comments`]
* Inside functions, a comment goes on its own line above the code it
  describes, not at the end of the line. Comments after struct members, enum
  values and `#define`s are fine. [`comment-placement`]

```c
/* Good */                              /* Bad */
/* Reset the retry counter. */          retries = 0; /* reset counter */
retries = 0;
```

## Preprocessor and macros

* Directives start in column 1 with no space after `#`, also inside functions
  and nested `#if` blocks, and have exactly one space before their argument.
  [`directive-format`]
* The value of a `#define` is separated from its name by one space, or lined
  up in a column with neighbouring `#define`s. In a block of `#define`s on
  consecutive lines that uses padding, all values start in the same column,
  and the longest name in the block decides how far right that column is;
  giving every name the same number of spaces is not alignment.
  [`define-alignment`]

```c
#define BLE_ATT_OP_ERROR_RSP        0x01
#define BLE_ATT_OP_MTU_REQ          0x02
#define BLE_ATT_OP_MTU_RSP          0x03

#define BLE_FOO_MAX 4
```

* The line-continuation backslashes of a multi-line macro are aligned in one
  column (or all exactly one space after the code). [`macro-continuation`]
* Parenthesize macro parameters in the macro body. [`macro-arg-parens`]
* A function-like macro that expands to several statements is wrapped in
  `do { ... } while (0)`. [`macro-multi-statement`]

```c
#define BLE_FOO_DOUBLE(x) ((x) * 2)
#define BLE_FOO_RESET(foo)                \
    do {                                  \
        (foo)->a = 0;                     \
        (foo)->b = 0;                     \
    } while (0)
```

* Do not leave code disabled with `#if 0`; delete it, or make it a real
  option with `#if MYNEWT_VAL(...)`. [`no-if-0`]

## Expressions and safety

These rules target the bug classes that matter most in embedded and protocol
code; most come from MISRA C, CERT C and the Linux kernel.

* Always write `sizeof` with parentheses: `sizeof(x)`, `sizeof(*p)`.
  [`sizeof-parens`]
* `return` is not a function: `return rc;`, not `return (rc);`.
  [`return-parens`]
* Do not use functions that cannot be told the size of their destination
  (`sprintf`, `vsprintf`, `strcpy`, `strcat`, `strncpy`, `strncat`, `gets`,
  `strtok`, `atoi`, `alloca`). Use `snprintf`, `strlcpy`, `memcpy` with an
  explicit length, `strtol`, ... [`banned-functions`]
* Every `switch` has a `default:` label. [`switch-default`]
* A `case` that falls through to the next one says so with a
  `/* fall through */` comment. [`implicit-fallthrough`]
* No assignments inside `if` or `while` conditions. [`assign-in-condition`]
* No octal literals (`010` is eight). [`octal-literal`]
* Do not compare with `true` or `false`; test the value. [`bool-compare`]
* Integer literal suffixes are upper case: `10U`, `1UL`. [`literal-suffix`]
* No empty statements (`;;`, or `;` after a function body).
  [`stray-semicolon`]

```c
/* Good */                              /* Bad */
rc = ble_foo_read(buf, sizeof(buf));    rc = ble_foo_read(buf, sizeof buf);
snprintf(name, sizeof(name), "%d", i);  sprintf(name, "%d", i);
rc = ble_foo();                         if ((rc = ble_foo()) != 0) {
if (rc != 0) {
```

## Naming

* Names of functions, structures and variables are lower case. Names imposed
  by vendors (interrupt vectors, CMSIS and HAL callbacks) are exempt.
  [`function-naming`]
* Globally visible functions are prefixed with the name of their module:
  `os_callout_init()`, not `callout_init()`. The prefixes allowed in each part
  of a repository are configured in its `newt-coding-rules`.
  [`function-naming`]
* Names should be as short as possible, but no shorter. *(not checked)*
* Code must compile cleanly with `-Wall`. *(not checked; enforced by builds)*

## Exceptions

* Third-party and generated code keeps its own style; such files are listed
  in `newt-coding-rules-ignore` and are not checked.
* When a rule gives a wrong or unhelpful result for a piece of code, mark it
  and explain why in the pull request:

```c
x = some_long_call();    /* mynewt-format: ignore line-length */
/* mynewt-format: ignore-next-line */
/* mynewt-format: off */
...hand-formatted table...
/* mynewt-format: on */
```

  Existing `/* clang-format off */` / `/* clang-format on */` regions are
  honoured as well.

[`assign-in-condition`]: #expressions-and-safety
[`banned-functions`]: #expressions-and-safety
[`blank-line-after-declarations`]: #declarations-and-types
[`bool-compare`]: #expressions-and-safety
[`braces-required`]: #braces-and-control-statements
[`call-paren-spacing`]: #whitespace
[`cast-spacing`]: #whitespace
[`comma-space`]: #whitespace
[`comment-placement`]: #comments
[`control-brace-placement`]: #braces-and-control-statements
[`declarations-at-top`]: #declarations-and-types
[`define-alignment`]: #preprocessor-and-macros
[`directive-format`]: #preprocessor-and-macros
[`duplicate-include`]: #files
[`empty-body`]: #braces-and-control-statements
[`eof-newline`]: #files
[`extern-c`]: #files
[`function-brace-newline`]: #functions
[`function-naming`]: #naming
[`header-guard`]: #files
[`implicit-fallthrough`]: #expressions-and-safety
[`keyword-space`]: #whitespace
[`license-header`]: #files
[`line-endings`]: #files
[`line-length`]: #line-length-and-wrapping
[`literal-suffix`]: #expressions-and-safety
[`macro-arg-parens`]: #preprocessor-and-macros
[`macro-continuation`]: #preprocessor-and-macros
[`macro-multi-statement`]: #preprocessor-and-macros
[`max-blank-lines`]: #whitespace
[`no-cpp-comments`]: #comments
[`no-if-0`]: #preprocessor-and-macros
[`no-struct-typedef`]: #declarations-and-types
[`no-tabs`]: #whitespace
[`octal-literal`]: #expressions-and-safety
[`operator-at-line-end`]: #line-length-and-wrapping
[`operator-spacing`]: #whitespace
[`pointer-alignment`]: #whitespace
[`return-parens`]: #expressions-and-safety
[`return-type-placement`]: #functions
[`sizeof-parens`]: #expressions-and-safety
[`space-before-semicolon`]: #whitespace
[`space-inside-parens`]: #whitespace
[`stray-semicolon`]: #expressions-and-safety
[`switch-default`]: #expressions-and-safety
[`trailing-whitespace`]: #whitespace
[`typedef-suffix`]: #declarations-and-types
[`unnecessary-wrap`]: #line-length-and-wrapping
