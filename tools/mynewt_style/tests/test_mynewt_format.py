#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

"""
Tests for tools/mynewt_format.py.

    python3 -m unittest discover -s tools/mynewt_style/tests -v
"""

import contextlib
import io
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(TOOLS))

from mynewt_style import FileContext, Rule, registry  # noqa: E402
from mynewt_style import cparse  # noqa: E402
from mynewt_style.cli import main, map_changed_lines  # noqa: E402
from mynewt_style.config import Config, ConfigError, load_config  # noqa: E402
from mynewt_style.engine import Engine, apply_hunks, compute_hunks, merge_hunks  # noqa: E402
from mynewt_style.interactive import QuitReview, Reviewer  # noqa: E402
from mynewt_style.report import Colors  # noqa: E402

registry.load_directory(TOOLS / "mynewt_style" / "rules")
Colors.disable()

LICENSE = "/* Licensed to the Apache Software Foundation (ASF) */\n"


def src(text: str) -> str:
    return textwrap.dedent(text).lstrip("\n")


def rule(name: str, **options) -> Rule:
    return registry.get(name)(options)


def check(name: str, code: str, path: str = "t.c", **options):
    return rule(name, **options).check(FileContext(Path(path), code))


def fix(name: str, code: str, path: str = "t.c", **options):
    """Run the rule through the engine so the token-safety check applies."""
    eng = Engine([rule(name, **options)], {"fix_passes": 5})
    fixed, errors = eng.fix(Path(path), code)
    assert not errors, errors
    return fixed


class RuleTestCase(unittest.TestCase):
    rule_name = ""

    def assertViolations(self, code, lines, path="t.c", **options):
        got = sorted(v.line for v in check(self.rule_name, code, path, **options))
        self.assertEqual(got, lines)

    def assertFix(self, code, expected, path="t.c", **options):
        fixed = fix(self.rule_name, code, path, **options)
        self.assertEqual(fixed, expected)
        self.assertEqual(check(self.rule_name, fixed, path, **options), [],
                         "fixed code must be clean")
        # Idempotent.
        self.assertEqual(fix(self.rule_name, fixed, path, **options), fixed)


# ---------------------------------------------------------------- lexing ----

class TestCParse(unittest.TestCase):
    def test_mask_keeps_layout(self):
        code = 'int a = "x // y"; /* c\n d */ b(\'"\'); // e\n#define X \\\n  1\n'
        m = cparse.mask_source(code)
        self.assertEqual(len(m.code), len(code))
        self.assertNotIn("//", m.code)
        self.assertNotIn("x", m.code.split("\n")[0][8:16])
        self.assertEqual(m.pp_lines, {3, 4})
        self.assertEqual(m.comment_lines, {1, 2})
        self.assertEqual(len(m.line_comments), 1)

    def test_fingerprint(self):
        f = cparse.strip_code_tokens
        self.assertEqual(f("if(x){a=b;}"), f("if (x) {\n    a = b; /* c */\n}"))
        self.assertNotEqual(f("int x;"), f("intx;"))
        self.assertNotEqual(f("a + +b"), f("a++b"))
        self.assertNotEqual(f('s = "a b";'), f('s = "ab";'))
        # Moving a token into a preprocessor line changes the meaning.
        self.assertNotEqual(f("x = a\n#if B\n|| c\n#endif\n"),
                            f("x = a\n#if B ||\nc\n#endif\n"))

    def test_function_signatures(self):
        code = src("""
            static int
            foo(void)
            {
            }
            struct bar *baz(int a,
                            int b);
            TEST_CASE_SELF(qux)
            {
            }
            static void (*cb)(void);
        """)
        sigs = FileContext(Path("t.c"), code).functions
        self.assertEqual([(s.name, s.split, s.is_definition) for s in sigs],
                         [("foo", True, True), ("baz", False, False)])


# ---------------------------------------------------------------- rules -----

class TestNoTabs(RuleTestCase):
    rule_name = "no-tabs"

    def test(self):
        self.assertViolations("\tint a;\nint b;\n", [1])
        # Expands to tab stops, so tab-aligned columns stay aligned.
        self.assertFix("\tint a;\n#define A\t1\n", "    int a;\n#define A   1\n")


class TestTrailingWhitespace(RuleTestCase):
    rule_name = "trailing-whitespace"

    def test(self):
        self.assertViolations("a; \nb;\t\nc;\n", [1, 2])
        self.assertFix("a;  \nb;\n", "a;\nb;\n")


class TestEofNewline(RuleTestCase):
    rule_name = "eof-newline"

    def test(self):
        self.assertViolations("a;", [1])
        self.assertViolations("a;\n\n\n", [2])
        self.assertViolations("a;\n", [])
        self.assertFix("a;", "a;\n")
        self.assertFix("a;\n\n\n", "a;\n")


class TestMaxBlankLines(RuleTestCase):
    rule_name = "max-blank-lines"

    def test(self):
        code = "a;\n\n\nb;\n\n\n\n\nc;\n"
        self.assertViolations(code, [3, 6])
        self.assertFix(code, "a;\n\nb;\n\nc;\n")
        self.assertViolations(code, [7], max=2)
        self.assertFix(code, "a;\n\n\nb;\n\n\nc;\n", max=2)


class TestLineLength(RuleTestCase):
    rule_name = "line-length"

    def test_limits(self):
        code = "x;\n" + "a" * 90 + ";\n" + "b" * 101 + ";\n"
        v = check(self.rule_name, code)
        self.assertEqual([(x.line, x.severity) for x in v], [(2, "warning"), (3, "error")])
        self.assertViolations(code, [3], report_target=False)
        self.assertViolations("/* https://example.com/" + "c" * 100 + " */\n", [])

    def test_wrap_aligns_after_paren(self):
        code = ("        rc = ble_gattc_write_flat(conn_handle, attr_handle, value_buffer, "
                "value_len, write_callback, NULL);\n")
        self.assertGreater(len(code), 101)
        # Greedy: break after the last ',' that still fits in 100 columns.
        self.assertFix(code,
                       "        rc = ble_gattc_write_flat(conn_handle, attr_handle, value_buffer, "
                       "value_len, write_callback,\n"
                       "                                  NULL);\n", report_target=False)

    def test_wrap_after_logical_operator(self):
        code = ("        if (first_condition_is_true(argument) && second_condition_is_also_true(argument) "
                "&& third_one(c)) {\n")
        self.assertGreater(len(code), 101)
        self.assertFix(code,
                       "        if (first_condition_is_true(argument) && second_condition_is_also_true(argument) "
                       "&&\n"
                       "            third_one(c)) {\n", report_target=False)

    def test_unbreakable_is_not_fixable(self):
        v = check(self.rule_name, "    x = " + "y" * 100 + ";\n")
        self.assertEqual(len(v), 1)
        self.assertFalse(v[0].fixable)


class TestUnnecessaryWrap(RuleTestCase):
    rule_name = "unnecessary-wrap"

    def test_joins_what_fits(self):
        code = src("""
            int f(void)
            {
                rc = ble_foo(conn_handle, attr_handle,
                             value, len);
                rc = ble_bar(
                    a, b);
                return rc;
            }
            static int ble_baz(int a,
                               int b)
            {
            }
            int g(void)
            {
                rc = one(a,
                         b) + two(c,
                                  d);
            }
        """)
        # two(...) first; one(...) can only be joined once two(...) is.
        self.assertViolations(code, [3, 5, 9, 16])
        self.assertFix(code, src("""
            int f(void)
            {
                rc = ble_foo(conn_handle, attr_handle, value, len);
                rc = ble_bar(a, b);
                return rc;
            }
            static int ble_baz(int a, int b)
            {
            }
            int g(void)
            {
                rc = one(a, b) + two(c, d);
            }
        """))

    def test_leaves_alone(self):
        long_args = ", ".join(f"argument_number_{i}" for i in range(6))
        code = src(f"""
            int f(void)
            {{
                rc = too_long({long_args[:60]},
                              {long_args[60:]});
                rc = with_comment(a, /* why */
                                  b);
                printf("first part "
                       "second part");
                if (a &&
                    b) {{
                }}
                rc = with_pp(a,
            #if X
                             b,
            #endif
                             c);
            }}
            #define M(a, b) foo(a, \\
                                b)
        """)
        self.assertViolations(code, [])

    def test_inner_list_joined_when_outer_does_not_fit(self):
        code = ("    rc = outer_function_with_a_long_name(first_argument_value, "
                "second_argument_value,\n"
                "                                         third_argument_value, inner(a,\n"
                "                                                                     b));\n")
        fixed = fix(self.rule_name, code)
        self.assertIn("inner(a, b));", fixed)
        self.assertEqual(len(fixed.splitlines()), 2)


class TestNoCppComments(RuleTestCase):
    rule_name = "no-cpp-comments"

    def test(self):
        code = 'a = 1; // one\nb = "http://x"; /* // not */\n// two\n'
        self.assertViolations(code, [1, 3])
        self.assertFix(code, 'a = 1; /* one */\nb = "http://x"; /* // not */\n/* two */\n')


class TestReturnTypePlacement(RuleTestCase):
    rule_name = "return-type-placement"

    def test_same_line(self):
        code = src("""
            static void *
            function(int var1, int var2)
            {
            }

            int
            calc(int a,
                 int b);

            void good(void)
            {
            }
        """)
        self.assertViolations(code, [1, 6])
        self.assertFix(code, src("""
            static void *function(int var1, int var2)
            {
            }

            int calc(int a,
                     int b);

            void good(void)
            {
            }
        """))

    def test_reflow_when_too_long(self):
        code = src("""
            static struct ble_gattc_proc *
            ble_gattc_extract_with_rx_entry(uint16_t conn_handle, uint16_t cid,
                                            const void *rx_entries, int num_rx_entries,
                                            const void **out_rx_entry)
            {
            }
        """)
        fixed = fix(self.rule_name, code)
        self.assertTrue(all(len(l) <= 100 for l in fixed.split("\n")))
        self.assertTrue(fixed.startswith("static struct ble_gattc_proc *ble_gattc_extract"))
        self.assertEqual(check(self.rule_name, fixed), [])

    def test_own_line_style(self):
        code = "static int foo(int a,\n               int b)\n{\n}\n"
        self.assertViolations(code, [1], style="own-line")
        self.assertFix(code, "static int\nfoo(int a,\n    int b)\n{\n}\n", style="own-line")

    def test_not_confused_by_macros_and_pointers(self):
        code = src("""
            TEST_CASE_SELF(ble_foo_test)
            {
            }

            static void (*cb)(void);
            STATS_SECT_START(ble_foo)
            STATS_SECT_END
            struct __attribute__((packed)) {
                int a;
            } packed_var;
        """)
        self.assertViolations(code, [])


class TestFunctionBraceNewline(RuleTestCase):
    rule_name = "function-brace-newline"

    def test(self):
        code = "int foo(void) {\n    return 0;\n}\nstatic inline int bar(void) { return 1; }\n"
        self.assertViolations(code, [1])
        self.assertFix(code, "int foo(void)\n{\n    return 0;\n}\n"
                             "static inline int bar(void) { return 1; }\n")


class TestCallParenSpacing(RuleTestCase):
    rule_name = "call-paren-spacing"

    def test(self):
        code = "x = foo (a);\nif (x) {\n}\nvoid (*cb)(void);\nsz = sizeof (x);\n"
        self.assertViolations(code, [1])
        self.assertFix(code, code.replace("foo (a)", "foo(a)"))


class TestKeywordSpace(RuleTestCase):
    rule_name = "keyword-space"

    def test(self):
        code = "if(x){\n}else{\n}\nwhile(y) {\n}\nswitch (z){\n}\n"
        self.assertEqual(len(check(self.rule_name, code)), 6)
        self.assertFix(code, "if (x) {\n} else {\n}\nwhile (y) {\n}\nswitch (z) {\n}\n")


class TestControlBracePlacement(RuleTestCase):
    rule_name = "control-brace-placement"

    def test(self):
        code = src("""
            if (x)
            {
                a();
            }
            else {
                b();
            }
            do {
            }
            while (y);
        """)
        self.assertViolations(code, [2, 5, 10])
        self.assertFix(code, src("""
            if (x) {
                a();
            } else {
                b();
            }
            do {
            } while (y);
        """))

    def test_comment_in_between_is_not_joined(self):
        code = "if (x) /* why */\n{\n}\n"
        v = check(self.rule_name, code)
        self.assertEqual(len(v), 1)
        self.assertFalse(v[0].fixable)


class TestBracesRequired(RuleTestCase):
    rule_name = "braces-required"

    def test(self):
        code = src("""
            void f(void)
            {
                if (x)
                    return;
                for (;;) a();
                while (busy());
                do {
                } while (0);
                if (y) {
                } else if (z) {
                }
            }
        """)
        self.assertViolations(code, [3, 5])
        self.assertFix(code, src("""
            void f(void)
            {
                if (x) {
                    return;
                }
                for (;;) {
                    a();
                }
                while (busy());
                do {
                } while (0);
                if (y) {
                } else if (z) {
                }
            }
        """))

    def test_preprocessor_in_body_is_not_fixed(self):
        code = "if (x)\n#if A\n    a();\n#else\n    b();\n#endif\n"
        v = check(self.rule_name, code)
        self.assertEqual(len(v), 1)
        self.assertFalse(v[0].fixable)

    def test_together_with_brace_placement(self):
        code = "if (x)\n    a();\nelse\n    b();\n"
        eng = Engine([rule("braces-required"), rule("control-brace-placement")], {})
        fixed, errors = eng.fix(Path("t.c"), code)
        self.assertEqual(errors, [])
        self.assertEqual(fixed, "if (x) {\n    a();\n} else {\n    b();\n}\n")


class TestCommaSpace(RuleTestCase):
    rule_name = "comma-space"

    def test(self):
        code = 'foo(a,b , c);\nprintf("%d,%d", x, y);\n#define M(a,b) (a)\nFOO(a,,b);\n'
        self.assertViolations(code, [1, 1, 3])
        self.assertFix(code, 'foo(a, b, c);\nprintf("%d,%d", x, y);\n#define M(a, b) (a)\n'
                             'FOO(a,,b);\n')


class TestPointerAlignment(RuleTestCase):
    rule_name = "pointer-alignment"

    def test(self):
        code = "char* a;\nstruct os_mbuf * om;\nx = (uint8_t*)p;\nr = a * b;\nr = a* b;\n"
        self.assertViolations(code, [1, 2, 3])
        self.assertFix(code, "char *a;\nstruct os_mbuf *om;\nx = (uint8_t *)p;\n"
                             "r = a * b;\nr = a* b;\n")


class TestOperatorAtLineEnd(RuleTestCase):
    rule_name = "operator-at-line-end"

    def test(self):
        code = "if (x\n    && y\n    ||\n    z) {\n}\n"
        self.assertViolations(code, [2, 3])
        self.assertFix(code, "if (x &&\n    y ||\n    z) {\n}\n")

    def test_not_into_preprocessor_line(self):
        code = "x = (a\n#ifdef B\n     || b\n#endif\n     );\n"
        v = check(self.rule_name, code)
        self.assertEqual(len(v), 1)
        self.assertFalse(v[0].fixable)


class TestDirectiveFormat(RuleTestCase):
    rule_name = "directive-format"

    def test(self):
        code = "    #if A\n#  define B 1\n#include<x.h>\n#define  C 2\n#endif  /* A */\n"
        self.assertViolations(code, [1, 2, 3, 4])
        self.assertFix(code, "#if A\n#define B 1\n#include <x.h>\n#define C 2\n#endif  /* A */\n")


class TestDefineAlignment(RuleTestCase):
    rule_name = "define-alignment"

    def test_aligned_groups_are_respected(self):
        code = src("""
            #define BLE_A               0x01
            #define BLE_BBBBBB          0x02
            /* comment */
            #define BLE_C 3
            struct foo {
                int a;
            };
            #define BLE_FAR_AWAY        0x04
        """)
        self.assertViolations(code, [])

    def test_stray_values(self):
        code = src("""
            #define A_ONE           (1)
            #define A_TWO           (2)
            #define A_THREE          (3)

            #define NEAR      4
            int x;
            #define LONELY      7
        """)
        self.assertViolations(code, [3, 5, 7])
        # Strays join the column of their neighbours (blank lines do not
        # separate a group); a define with no aligned neighbours gets one space.
        self.assertFix(code, src("""
            #define A_ONE           (1)
            #define A_TWO           (2)
            #define A_THREE         (3)

            #define NEAR            4
            int x;
            #define LONELY 7
        """))


    def test_near_misses_are_lined_up(self):
        code = "#define DEMO_ONE        1\n#define DEMO_TWO         2\n"
        # Tie between the two columns: the rightmost wins, only ONE moves.
        self.assertViolations(code, [1])
        self.assertFix(code, "#define DEMO_ONE         1\n#define DEMO_TWO         2\n")

    def test_block_with_fixed_padding_is_aligned(self):
        # Same number of spaces after every name is not alignment
        # (apps/bttester/src/btp/btp.h).
        code = src("""
            #define BTP_SERVICE_ID_CORE    0
            #define BTP_SERVICE_ID_GAP    1
            #define BTP_SERVICE_ID_GATT    2
            #define BTP_SERVICE_ID_L2CAP    3
            #define BTP_SERVICE_ID_MESH    4
            #define BTP_SERVICE_ID_A_VERY_LONG_NAME  5

            #define BTP_STATUS_SUCCESS    0x00
            #define BTP_STATUS_UNKNOWN_CMD  0x02
        """)
        # The longest name sets the column; the rest of the block follows.
        self.assertViolations(code, [1, 2, 3, 4, 5, 6, 8])
        self.assertFix(code, src("""
            #define BTP_SERVICE_ID_CORE             0
            #define BTP_SERVICE_ID_GAP              1
            #define BTP_SERVICE_ID_GATT             2
            #define BTP_SERVICE_ID_L2CAP            3
            #define BTP_SERVICE_ID_MESH             4
            #define BTP_SERVICE_ID_A_VERY_LONG_NAME 5

            #define BTP_STATUS_SUCCESS      0x00
            #define BTP_STATUS_UNKNOWN_CMD  0x02
        """))

    def test_long_names_widen_the_block(self):
        # apps/bttester/src/btp/btp_gap.h: several names longer than the
        # column the others use.
        code = src("""
            #define BTP_GAP_SETTINGS_POWERED        0
            #define BTP_GAP_SETTINGS_FAST_CONNECTABLE 2
            #define BTP_GAP_SETTINGS_SC             11
            #define BTP_GAP_SETTINGS_EXTENDED_ADVERTISING 17
        """)
        self.assertFix(code, src("""
            #define BTP_GAP_SETTINGS_POWERED              0
            #define BTP_GAP_SETTINGS_FAST_CONNECTABLE     2
            #define BTP_GAP_SETTINGS_SC                   11
            #define BTP_GAP_SETTINGS_EXTENDED_ADVERTISING 17
        """))

    def test_line_limit_keeps_long_names_sticking_out(self):
        long_name = "BLE_" + "X" * 70
        code = ("#define A_ONE    (1)\n#define A_TWO    (2)\n"
                f"#define {long_name} (some + long + value + expression)\n")
        # Aligning to the long name would exceed 100 columns: leave it.
        self.assertViolations(code, [])

    def test_consistent_blocks_are_untouched(self):
        code = src("""
            #define A_ONE                     1
            #define A_TWO                     2
            #define A_LONGER_NAME_THAN_COLUMN 3

            #define B_X 1
            #define B_YY 2
            /* comment */
            #define C_LONE      7
            #define C_LONE_TWO  8
        """)
        self.assertViolations(code, [])


class TestMacroContinuation(RuleTestCase):
    rule_name = "macro-continuation"

    def test(self):
        aligned = "#define A(x)     \\\n    do {         \\\n    } while (0)\n"
        single = "#define A(x) \\\n    do { \\\n    } while (0)\n"
        self.assertViolations(aligned, [])
        self.assertViolations(single, [])
        bad = "#define A(x)       \\\n    do {  \\\n        x;\\\n    } while (0)\n"
        self.assertViolations(bad, [2])
        self.assertFix(bad, "#define A(x)       \\\n    do {           \\\n"
                            "        x;         \\\n    } while (0)\n")


class TestMacroMultiStatement(RuleTestCase):
    rule_name = "macro-multi-statement"

    def test(self):
        code = ("#define BAD(x) a(x); b(x)\n"
                "#define GOOD(x) do { a(x); b(x); } while (0)\n"
                "#define DECL(n) static int n; static int n##_2\n")
        self.assertViolations(code, [1])


class TestLicenseHeader(RuleTestCase):
    rule_name = "license-header"

    def test(self):
        self.assertViolations("int a;\n", [1])
        self.assertViolations(LICENSE + "int a;\n", [])
        self.assertViolations("/* SPDX-License-Identifier: Apache-2.0 */\nint a;\n", [])
        fixed = fix(self.rule_name, "int a;\n")
        self.assertIn("Licensed to the Apache Software Foundation", fixed)
        self.assertTrue(fixed.endswith("*/\n\nint a;\n"))


class TestHeaderGuard(RuleTestCase):
    rule_name = "header-guard"

    def test(self):
        self.assertViolations("#ifndef H_A_\n#define H_A_\nint a;\n#endif\n", [], path="a.h")
        self.assertViolations("#pragma once\n", [], path="a.h")
        self.assertViolations("#ifndef H_A_\n#define H_B_\n#endif\n", [2], path="a.h")
        self.assertViolations("#include <x.h>\n", [1], path="a.h")
        self.assertViolations("int a;\n", [], path="a.c")


class TestNoStructTypedef(RuleTestCase):
    rule_name = "no-struct-typedef"

    def test(self):
        self.assertViolations("typedef struct {\n    int a;\n} foo_t;\n/* typedef struct */\n",
                              [1])


# ------------------------------------------------------- expressions -------

class TestOperatorSpacing(RuleTestCase):
    rule_name = "operator-spacing"

    def test(self):
        code = "x=a;\ny += 1;\nif (a==b&&c!=d) {\n}\nz    = 2;\n#define M(a) ((a)<=1)\n"
        self.assertViolations(code, [1, 1, 3, 3, 3, 3, 3, 3, 6, 6])
        self.assertFix(code, "x = a;\ny += 1;\nif (a == b && c != d) {\n}\nz    = 2;\n"
                             "#define M(a) ((a) <= 1)\n")

    def test_include_untouched(self):
        self.assertViolations('#include <a=b.h>\nchar *s = "a=b";\n', [])


class TestSpaceInsideParens(RuleTestCase):
    rule_name = "space-inside-parens"

    def test(self):
        code = "foo( a, b );\nx = buf[ i ];\nFOO(a, );\nfor (;;) {\n}\n#define X(a) (      \\\n    a)\n"
        self.assertViolations(code, [1, 1, 2, 2])
        self.assertFix(code, "foo(a, b);\nx = buf[i];\nFOO(a, );\nfor (;;) {\n}\n"
                             "#define X(a) (      \\\n    a)\n")


class TestSpaceBeforeSemicolon(RuleTestCase):
    rule_name = "space-before-semicolon"

    def test(self):
        code = "rc = foo() ;\nfor (i = 0 ; ; i++) {\n}\n"
        self.assertViolations(code, [1, 2])
        self.assertFix(code, "rc = foo();\nfor (i = 0; ; i++) {\n}\n")


class TestCastSpacing(RuleTestCase):
    rule_name = "cast-spacing"

    def test(self):
        code = ("x = (uint16_t) y;\n(void) foo();\np = (struct os_mbuf *) q;\n"
                "n = sizeof(int) * 4;\nif (flag_t) x = 1;\nreturn (int) v;\n")
        self.assertViolations(code, [1, 2, 3, 6])
        self.assertFix(code, "x = (uint16_t)y;\n(void)foo();\np = (struct os_mbuf *)q;\n"
                             "n = sizeof(int) * 4;\nif (flag_t) x = 1;\nreturn (int)v;\n")


class TestSizeofParens(RuleTestCase):
    rule_name = "sizeof-parens"

    def test(self):
        code = ("a = sizeof x;\nb = sizeof *p + 1;\nc = sizeof s.f[2];\nd = sizeof (int);\n"
                "e = sizeof(y);\nf = sizeof p->q;\n")
        self.assertViolations(code, [1, 2, 3, 4, 6])
        self.assertFix(code, "a = sizeof(x);\nb = sizeof(*p) + 1;\nc = sizeof(s.f[2]);\n"
                             "d = sizeof(int);\ne = sizeof(y);\nf = sizeof(p->q);\n")


class TestReturnParens(RuleTestCase):
    rule_name = "return-parens"

    def test(self):
        code = ("return (rc);\nreturn(a + b);\nreturn (a + b) * c;\nreturn (uint8_t)x;\n"
                "return rc;\n")
        self.assertViolations(code, [1, 2])
        self.assertFix(code, "return rc;\nreturn a + b;\nreturn (a + b) * c;\n"
                             "return (uint8_t)x;\nreturn rc;\n")


class TestStraySemicolon(RuleTestCase):
    rule_name = "stray-semicolon"

    def test(self):
        code = "void f(void)\n{\n    foo();;\n    for (;;) {\n    }\n};\n"
        self.assertViolations(code, [3, 6])
        self.assertFix(code, "void f(void)\n{\n    foo();\n    for (;;) {\n    }\n}\n")


class TestLiteralSuffix(RuleTestCase):
    rule_name = "literal-suffix"

    def test(self):
        code = "a = 10u;\nb = 0xffUl;\nc = 1UL;\nd = 1.5f;\nid1u = 2;\n"
        self.assertViolations(code, [1, 2])
        self.assertFix(code, "a = 10U;\nb = 0xffUL;\nc = 1UL;\nd = 1.5f;\nid1u = 2;\n")


# ------------------------------------------------------------ safety -------

class TestBannedFunctions(RuleTestCase):
    rule_name = "banned-functions"

    def test(self):
        code = 'sprintf(b, "%d", 1);\nsnprintf(b, 4, "x");\n/* strcpy(a, b) */\nmy_strcpy(a, b);\n'
        self.assertViolations(code, [1])
        self.assertViolations("strcpy(a, b);\n", [], functions={})


class TestOctalLiteral(RuleTestCase):
    rule_name = "octal-literal"

    def test(self):
        self.assertViolations("a = 010;\nb = 0;\nc = 0x10;\nd = 0.5;\ne = 00;\nf = 10;\n", [1])


class TestBoolCompare(RuleTestCase):
    rule_name = "bool-compare"

    def test(self):
        self.assertViolations("if (a == true) {\n}\nif (false != b) {\n}\nif (a) {\n}\n", [1, 3])


class TestAssignInCondition(RuleTestCase):
    rule_name = "assign-in-condition"

    def test(self):
        code = ("if ((rc = foo()) != 0) {\n}\nwhile ((x = next())) {\n}\n"
                "if (a == b && c <= d) {\n}\nfor (i = 0; i < 1; i++) {\n}\n")
        self.assertViolations(code, [1, 3])


class TestSwitchDefault(RuleTestCase):
    rule_name = "switch-default"

    def test(self):
        code = src("""
            switch (a) {
            case 1:
                switch (b) {
                default:
                    break;
                }
                break;
            }
        """)
        self.assertViolations(code, [1])


class TestImplicitFallthrough(RuleTestCase):
    rule_name = "implicit-fallthrough"

    def test(self):
        code = src("""
            switch (op) {
            case 1:
            case 2:
                a();
                break;
            case 3:
                b();
            case 4:
                c();
                /* fall through */
            case 5:
                if (x) {
                    return 1;
                } else if (y) {
                    return 2;
                } else {
                    break;
                }
            case 6:
                if (x) {
                    return 1;
                }
            case 7:
                {
                    d();
                    goto out;
                }
            case 8:
                e();
                /* no break */
            default:
                abort();
            }
        """)
        self.assertViolations(code, [8, 23])


class TestEmptyBody(RuleTestCase):
    rule_name = "empty-body"

    def test(self):
        code = "if (rc != 0);\nwhile (busy());\ndo {\n} while (0);\nfor (;;) {\n}\n"
        self.assertViolations(code, [1, 2])


# ------------------------------------------------------ preprocessor -------

class TestMacroArgParens(RuleTestCase):
    rule_name = "macro-arg-parens"

    def test(self):
        code = ("#define D(x) x * 2\n"
                "#define OK1(x) ((x) * 2)\n"
                "#define OK2(a, b) foo(a, b)\n"
                "#define OK3(p, f) ((p)->f)\n"
                "#define OK4(n) #n\n"
                "#define OK5(t, n) t n\n"
                "#define OK6(i) arr[i]\n"
                "#define BAD(word) ((word >> 8) & 0xff)\n")
        self.assertViolations(code, [1, 8])


class TestDuplicateInclude(RuleTestCase):
    rule_name = "duplicate-include"

    def test(self):
        code = ('#include "a.h"\n#include "b.h"\n#include "a.h"\n'
                '#if X\n#include "c.h"\n#else\n#include "c.h"\n#endif\n'
                '#if Y\n#include "b.h"\n#endif\n')
        self.assertViolations(code, [3, 10])


class TestNoIfZero(RuleTestCase):
    rule_name = "no-if-0"

    def test(self):
        self.assertViolations("#if 0\nx;\n#endif\n#if 0 /* old */\n#endif\n#if 01\n#endif\n",
                              [1, 4])


# ------------------------------------------------------ declarations -------

class TestDeclarationsAtTop(RuleTestCase):
    rule_name = "declarations-at-top"

    def test(self):
        code = src("""
            int f(void)
            {
            #if !MYNEWT_VAL(FEATURE)
                return BLE_HS_ENOTSUP;
            #endif

                static bssnz_t uint8_t buf[4];
                struct os_mbuf *om;
                int rc;

                rc = 0;
                int late;
                if (rc) {
                    int inner;
                }
                for (int i = 0; i < 2; i++) {
                }
                return rc;
            }
        """)
        self.assertViolations(code, [12, 14, 16])
        self.assertViolations(code, [12, 16], allow_block_scope=True)
        self.assertViolations(code, [12], allow_block_scope=True, allow_for_init=True)

    def test_else_branch(self):
        code = src("""
            int f(void)
            {
            #if A
                return g();
            #else
                int rc;

                rc = h();
                return rc;
            #endif
            }
        """)
        self.assertViolations(code, [])


class TestBlankLineAfterDeclarations(RuleTestCase):
    rule_name = "blank-line-after-declarations"

    def test(self):
        code = "int f(void)\n{\n    int a;\n    int b = 1;\n    a = b;\n    return a;\n}\n"
        self.assertViolations(code, [5])
        self.assertFix(code, "int f(void)\n{\n    int a;\n    int b = 1;\n\n    a = b;\n"
                             "    return a;\n}\n")
        self.assertViolations("int f(void)\n{\n    int a;\n}\n", [])


class TestFunctionNaming(RuleTestCase):
    rule_name = "function-naming"

    def test(self):
        code = ("int ble_ok(void)\n{\n}\nstatic int helper(void)\n{\n}\n"
                "int gap_connect(void)\n{\n}\nint BleBad(void)\n{\n}\n"
                "void RADIO_IRQHandler(void)\n{\n}\nvoid HAL_InitTick(void)\n{\n}\n"
                "void SystemClock_Config(void)\n{\n}\n")
        ctx_settings = {"repo_root": "/repo"}
        prefixes = {"prefixes": {"nimble/**/test/**": [], "nimble/**": ["ble_", "nimble_"]}}
        rule = registry.get(self.rule_name)(prefixes)
        v = rule.check(FileContext(Path("/repo/nimble/host/src/x.c"), code, ctx_settings))
        self.assertEqual([x.line for x in v], [7, 10])
        # Outside configured paths only the lower case check applies.
        v = rule.check(FileContext(Path("/repo/apps/foo/src/main.c"), code, ctx_settings))
        self.assertEqual([x.line for x in v], [10])
        v = rule.check(FileContext(Path("/repo/nimble/host/test/src/t.c"), code, ctx_settings))
        self.assertEqual([x.line for x in v], [10])


class TestTypedefSuffix(RuleTestCase):
    rule_name = "typedef-suffix"

    def test(self):
        code = ("typedef uint8_t ble_flags;\ntypedef uint8_t ble_flags_t;\n"
                "typedef int ble_foo_fn(void *arg);\ntypedef void (*ble_cb)(void);\n"
                "typedef int (hci_frame_cb)(void *data);\ntypedef struct foo foo;\n"
                "typedef void (*ble_handler)(void);\n")
        self.assertViolations(code, [1, 7])


# ---------------------------------------------------------- comments -------

class TestCommentPlacement(RuleTestCase):
    rule_name = "comment-placement"

    def test(self):
        code = src("""
            int f(void)
            {
                int a; /* counter */
                if (a) { // check
                    x = y /* mid */ + z;
                }
                return a; /* mynewt-format: ignore */
            }
            struct s {
                int a; /* fine outside functions */
            };
        """)
        self.assertViolations(code, [3, 4])
        self.assertFix(code, src("""
            int f(void)
            {
                /* counter */
                int a;
                /* check */
                if (a) {
                    x = y /* mid */ + z;
                }
                return a; /* mynewt-format: ignore */
            }
            struct s {
                int a; /* fine outside functions */
            };
        """))


# ---------------------------------------------------------------- engine ----

class BrokenRule(Rule):
    """Deliberately changes code tokens; the engine must refuse its fix."""
    name = "test-broken"
    summary = "test"
    fixable = True

    def check(self, ctx):
        return []

    def fix(self, ctx):
        return ctx.content.replace("a", "b")


class TestEngine(unittest.TestCase):
    def engine(self, *names):
        return Engine([rule(n) for n in names], {})

    def test_token_changing_fix_is_rejected(self):
        eng = Engine([BrokenRule()], {})
        fixed, errors = eng.fix(Path("t.c"), "int a;\n")
        self.assertEqual(fixed, "int a;\n")
        self.assertEqual(len(errors), 1)

    def test_hunks_roundtrip(self):
        a = "1\n2\n3\n4\n5\n6\n7\n8\n"
        b = "1\nX\n3\n4\n5\n6\nY\nZ\n8\n"
        hunks = compute_hunks(a, b)
        self.assertEqual(len(hunks), 2)
        self.assertEqual(apply_hunks(a, hunks), b)
        self.assertEqual(apply_hunks(a, hunks[:1]), "1\nX\n3\n4\n5\n6\n7\n8\n")

    def test_hunks_are_per_line_when_line_count_is_unchanged(self):
        hunks = compute_hunks("a \nb \nc\n", "a\nb\nc\n")
        self.assertEqual([(h.a_start, h.a_end) for h in hunks], [(0, 1), (1, 2)])
        merged = merge_hunks("a \nb \nc\n", hunks)
        self.assertEqual([(h.a_start, h.a_end) for h in merged], [(0, 2)])

    def test_suppressions(self):
        code = src("""
            int a;\t/* mynewt-format: ignore */
            int b;\t/* mynewt-format: ignore no-tabs */
            /* mynewt-format: ignore-next-line */
            int c;\t
            int d;\t/* mynewt-format: ignore line-length */
            /* mynewt-format: off */
            int e;\t
            /* mynewt-format: on */
            int f;\t
            /* clang-format off */
            int g;\t
            /* clang-format on */
        """)
        res = self.engine("no-tabs", "trailing-whitespace").process(Path("t.c"), code)
        self.assertEqual(sorted({v.line for v in res.violations}), [5, 9])
        self.assertIn("int g;\t\n", res.proposed)
        # Fixes never touch suppressed lines.
        self.assertIn("int c;\t\n", res.proposed)
        self.assertIn("int e;\t\n", res.proposed)
        self.assertIn("int f;\n", res.proposed)

    def test_changed_lines_filter(self):
        code = "a;\t\nb;\t\nc;\t\n"
        res = self.engine("no-tabs").process(Path("t.c"), code, changed_lines={2})
        self.assertEqual([v.line for v in res.violations], [2])
        self.assertEqual(res.proposed, "a;\t\nb;  \nc;\t\n")

    def test_map_changed_lines(self):
        self.assertEqual(map_changed_lines("a\nb\nc\n", "a\nB1\nB2\nc\n", {3}), {2, 3, 4})


class TestInteractive(unittest.TestCase):
    def result(self):
        eng = Engine([rule("trailing-whitespace")], {})
        return eng.process(Path("t.c"), "a; \nb;\nc;\nd;\ne;\nf;\ng; \n")

    def reviewer(self, answers):
        it = iter(answers)
        return Reviewer(None, ask=lambda _: next(it), say=lambda _: None)

    def test_pick_some(self):
        res = self.result()
        self.assertEqual(len(res.hunks), 2)
        self.assertEqual(self.reviewer(["n", "y"]).review(res),
                         "a; \nb;\nc;\nd;\ne;\nf;\ng;\n")

    def test_skip_all(self):
        self.assertIsNone(self.reviewer(["s"]).review(self.result()))

    def test_help_then_all(self):
        self.assertEqual(self.reviewer(["?", "a"]).review(self.result()),
                         "a;\nb;\nc;\nd;\ne;\nf;\ng;\n")

    def test_quit_keeps_accepted(self):
        with self.assertRaises(QuitReview) as cm:
            self.reviewer(["y", "q"]).review(self.result())
        self.assertEqual(cm.exception.content, "a;\nb;\nc;\nd;\ne;\nf;\ng; \n")


# ---------------------------------------------------------------- config ----

class TestConfig(unittest.TestCase):
    def config(self, text):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "newt-coding-rules"
            p.write_text(text)
            cfg = load_config(p, Path(d))
        cfg.validate()
        return cfg

    def test_rulesets(self):
        cfg = self.config(textwrap.dedent("""
            [rulesets]
            ws = ["@whitespace", "-no-tabs"]
            more = ["ruleset:ws", "line-length"]
            [rules.max-blank-lines]
            enabled = false
        """))
        self.assertEqual(cfg.resolve_ruleset("ws"),
                         {"eof-newline", "line-endings", "trailing-whitespace"})
        self.assertIn("line-length", cfg.resolve_ruleset("more"))
        self.assertNotIn("max-blank-lines", cfg.resolve_ruleset("default"))
        self.assertIn("extern-c", cfg.resolve_ruleset("default"))

    def test_options_and_severity(self):
        cfg = self.config('[rules.max-blank-lines]\nmax = 1\nseverity = "warning"\n')
        (r,) = cfg.build_rules(only=["max-blank-lines"])
        self.assertEqual(r.opts["max"], 1)
        self.assertEqual(r.severity, "warning")

    def test_errors(self):
        for bad in ('[rules.no-such-rule]\n', '[rules.no-tabs]\nfoo = 1\n',
                    '[rulesets]\nx = ["@nope"]\n', '[settings]\nnope = 1\n',
                    '[rulesets]\na = ["ruleset:b"]\nb = ["ruleset:a"]\n'):
            with self.assertRaises(ConfigError, msg=bad):
                self.config(bad)

    def test_repo_config_is_valid(self):
        repo = TOOLS.parent
        cfg = load_config(repo / "newt-coding-rules", repo)
        cfg.validate()
        for name in cfg.rulesets:
            self.assertTrue(cfg.build_rules(name))


# ---------------------------------------------------------------- CLI -------

def run_cli(*args, cwd):
    out, err = io.StringIO(), io.StringIO()
    old = os.getcwd()
    os.chdir(cwd)
    try:
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = main(list(args))
    finally:
        os.chdir(old)
    return code, out.getvalue(), err.getvalue()


class TestCli(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "newt-coding-rules").write_text('[rulesets]\ndefault = ["@whitespace"]\n')
        self.file = self.dir / "a.c"
        self.file.write_text(LICENSE + "int a;  \n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_check_diff_fix(self):
        code, out, _ = run_cli("a.c", cwd=self.dir)
        self.assertEqual(code, 1)
        self.assertIn("a.c:2:7: error: trailing whitespace [trailing-whitespace]", out)

        code, out, _ = run_cli("-d", "a.c", cwd=self.dir)
        self.assertEqual(code, 1)
        self.assertIn("-int a;  \n+int a;", out)
        self.assertEqual(self.file.read_text(), LICENSE + "int a;  \n")

        code, _, _ = run_cli("-f", "a.c", cwd=self.dir)
        self.assertEqual(code, 0)
        self.assertEqual(self.file.read_text(), LICENSE + "int a;\n")
        self.assertEqual(run_cli("a.c", cwd=self.dir)[0], 0)

    def test_report_styles(self):
        self.file.write_text(LICENSE + "int a;  \nx = sizeof y;\nsprintf(b, \"x\");\n")
        (self.dir / "newt-coding-rules").write_text(
            '[rulesets]\ndefault = ["trailing-whitespace", "sizeof-parens", "banned-functions"]\n')

        # Default: source line, caret and the proposed fix under each problem.
        code, out, _ = run_cli("a.c", cwd=self.dir)
        self.assertEqual(code, 1)
        self.assertIn("a.c:2:7: error: trailing whitespace [trailing-whitespace]", out)
        self.assertIn("  2 | int a;  \n    |       ^", out)
        self.assertIn("  suggested fix:\n  - int a;  \n  + int a;", out)
        self.assertIn("  - x = sizeof y;\n  + x = sizeof(y);", out)
        # Not fixable: code and caret, but no fix.
        self.assertIn("  4 | sprintf(b, \"x\");\n    | ^\n\n", out)

        # list: one line per problem, nothing else.
        code, out, _ = run_cli("--report", "list", "a.c", cwd=self.dir)
        self.assertEqual(code, 1)
        self.assertNotIn(" | ", out)
        self.assertNotIn("suggested fix", out)
        self.assertEqual(len([l for l in out.splitlines() if ": error: " in l]), 3)

    def test_one_fix_for_several_problems(self):
        (self.dir / "newt-coding-rules").write_text(
            '[rulesets]\ndefault = ["operator-spacing"]\n')
        self.file.write_text(LICENSE + "x=y;\n")
        code, out, _ = run_cli("a.c", cwd=self.dir)
        self.assertEqual(out.count("error: missing space"), 2)
        self.assertEqual(out.count("suggested fix:"), 1)
        # The fix follows the last problem it resolves.
        self.assertLess(out.index("after '='"), out.index("suggested fix:"))

    def test_list_and_explain(self):
        code, out, _ = run_cli("--list-rules", cwd=self.dir)
        self.assertEqual(code, 0)
        for cls in registry.all():
            self.assertIn(cls.name, out)
        code, out, _ = run_cli("--explain", "define-alignment", cwd=self.dir)
        self.assertEqual(code, 0)
        self.assertIn("aligned", out)
        self.assertEqual(run_cli("--explain", "nope", cwd=self.dir)[0], 2)

    def test_changed_lines_with_git(self):
        git = ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "init.defaultBranch=m"]
        subprocess.run(git + ["init", "-q"], cwd=self.dir, check=True)
        self.file.write_text(LICENSE + "int a;  \nint b;\n")
        subprocess.run(git + ["add", "."], cwd=self.dir, check=True)
        subprocess.run(git + ["commit", "-qm", "x"], cwd=self.dir, check=True)
        self.file.write_text(LICENSE + "int a;  \nint b; \n")

        code, out, _ = run_cli("--changed-lines", cwd=self.dir)
        self.assertEqual(code, 1)
        self.assertIn("a.c:3:", out)
        self.assertNotIn("a.c:2:", out)

        run_cli("-f", "--changed-lines", cwd=self.dir)
        self.assertEqual(self.file.read_text(), LICENSE + "int a;  \nint b;\n")

    def test_changed_lines_is_the_default(self):
        git = ["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "init.defaultBranch=m"]
        subprocess.run(git + ["init", "-q"], cwd=self.dir, check=True)
        self.file.write_text(LICENSE + "int a;  \nint b;\n")
        other = self.dir / "b.c"
        other.write_text(LICENSE + "int c;  \n")
        subprocess.run(git + ["add", "."], cwd=self.dir, check=True)
        subprocess.run(git + ["commit", "-qm", "x"], cwd=self.dir, check=True)
        self.file.write_text(LICENSE + "int a;  \nint b; \n")

        # Modified files, changed lines only: line 3 yes, untouched line 2 no.
        code, out, _ = run_cli(cwd=self.dir)
        self.assertEqual(code, 1)
        self.assertIn("a.c:3:", out)
        self.assertNotIn("a.c:2:", out)
        self.assertIn("changed lines only", out)

        # Naming files does not widen the scope; an unchanged file is clean.
        code, out, _ = run_cli("a.c", "b.c", cwd=self.dir)
        self.assertNotIn("a.c:2:", out)
        self.assertNotIn("b.c", out)

        # --whole-files (and --all) check everything.
        for flags in (["--whole-files", "a.c", "b.c"], ["--all"]):
            code, out, _ = run_cli(*flags, cwd=self.dir)
            self.assertIn("a.c:2:", out, flags)
            self.assertIn("b.c:2:", out, flags)
            self.assertNotIn("changed lines only", out)

        # New, untracked files are checked completely.
        (self.dir / "c.c").write_text(LICENSE + "int d;  \n")
        code, out, _ = run_cli(cwd=self.dir)
        self.assertIn("c.c:2:", out)

        # A fix by default only touches the changed line.
        run_cli("-f", cwd=self.dir)
        self.assertEqual(self.file.read_text(), LICENSE + "int a;  \nint b;\n")


class TestEveryRuleIsDocumented(unittest.TestCase):
    def test(self):
        for cls in registry.all():
            self.assertTrue(cls.summary, cls.name)
            self.assertTrue(cls.explanation.strip(), f"{cls.name} has no explanation")


if __name__ == "__main__":
    unittest.main()
