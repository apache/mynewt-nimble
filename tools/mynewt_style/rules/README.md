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

# Style rules for `mynewt_format.py`

Every rule of `tools/mynewt_format.py` lives in this directory. Each `*.py`
file is imported automatically and every class decorated with
`@register_rule` becomes available, with no other registration needed.

The tool is shared by apache-mynewt-core (where it is maintained) and
apache-mynewt-nimble (which syncs `tools/mynewt_format.py` and
`tools/mynewt_style/` from core). Rules must therefore stay repository
neutral: anything specific to one code base (paths, name prefixes, ignored
files) belongs in that repository's `newt-coding-rules`.

| File                   | Rules                                                                    |
|------------------------|--------------------------------------------------------------------------|
| `whitespace.py`        | no-tabs, trailing-whitespace, line-endings, eof-newline, max-blank-lines |
| `line_length.py`       | line-length (warning over 80, error over 100), unnecessary-wrap          |
| `comments.py`          | no-cpp-comments, comment-placement                                       |
| `functions.py`         | return-type-placement, function-brace-newline, call-paren-spacing        |
| `control.py`           | keyword-space, control-brace-placement, braces-required                  |
| `spacing.py`           | comma-space, pointer-alignment, operator-at-line-end                     |
| `expressions.py`       | operator-spacing, space-inside-parens, space-before-semicolon, cast-spacing, sizeof-parens, return-parens, stray-semicolon, literal-suffix |
| `safety.py`            | banned-functions, octal-literal, bool-compare, assign-in-condition, switch-default, implicit-fallthrough, empty-body |
| `preprocessor.py`      | directive-format, define-alignment, macro-continuation, macro-multi-statement, macro-arg-parens, duplicate-include, no-if-0 |
| `declarations.py`      | declarations-at-top, blank-line-after-declarations, function-naming, typedef-suffix |
| `structure.py`         | license-header, header-guard, extern-c                                   |
| `no_struct_typedef.py` | no-struct-typedef (minimal example, a good template)                     |

Rules that only *edit text* can derive from `mynewt_style.helpers.EditRule`
and just yield `Edit(start, end, text, message)` objects; see `expressions.py`.

`python3 tools/mynewt_format.py --list-rules` prints them all and
`--explain RULE` prints the full description of one.
The standard they implement is described in
[`../CODING_STANDARDS.md`](../CODING_STANDARDS.md); keep it in sync when adding or
changing a rule.

## Design principles

* **Fix the violation, nothing else.** A rule never re-flows or "pretty
  prints" code. Line breaks, blank lines and alignment the author chose are
  left alone unless they break a specific rule. This is what makes the tool
  usable on hand-formatted code, unlike a whole-file formatter.
* **Fixes are proven cosmetic.** After each fix the engine compares the token
  stream (comments and layout ignored, preprocessor line structure included)
  before and after. A fix that changes the code is thrown away and reported.
  A rule that must add tokens declares them, e.g.
  `may_insert_tokens = ("{", "}")`.
* **When unsure, report but do not fix.** Return the violation with
  `fixable=False` instead of guessing.

## Writing a rule

```python
import re

from mynewt_style import FileContext, Rule, register_rule


@register_rule
class NoGotoFail(Rule):
    name = "no-goto-fail"            # id for --rules / config / suppressions
    category = "control"             # groups rules; rulesets can use "@control"
    summary = "Do not use 'goto fail'."  # one line for --list-rules
    explanation = """
        Why the rule exists, and examples (shown by --explain).

        Good:  rc = BLE_HS_EINVAL; goto done;
        Bad:   goto fail;
    """
    severity = "warning"             # "error" fails the run, "warning" does not
    fixable = False                  # set True and implement fix() to auto-fix
    enabled_by_default = True        # False: only runs when a ruleset names it
    options = {"label": "fail"}      # tunable from [rules.no-goto-fail]

    def check(self, ctx: FileContext):
        pattern = r"\bgoto\s+" + re.escape(self.opts["label"]) + r"\b"
        out = []
        for m in re.finditer(pattern, ctx.code_nopp):
            line, col = ctx.line_col(m.start())
            out.append(self.violation(ctx, line, col + 1, "'goto fail' is not allowed"))
        return out

    # def fix(self, ctx: FileContext):
    #     return new_content    # or None when there is nothing to change
```

`fix()` receives the current content and returns the complete new content.
The engine runs all fixable rules in passes until nothing changes, so a rule
can rely on other rules cleaning up after it. It also takes care of the
diff, interactive review, `--changed-lines` and suppressions.

### `FileContext` reference

| Attribute                 | Meaning                                                                              |
|---------------------------|--------------------------------------------------------------------------------------|
| `path`, `content`         | file path and full text                                                              |
| `lines`                   | lines without `\n` (0-based list; line numbers elsewhere are 1-based)                |
| `code`                    | `content` with comments and string/char contents blanked; same length and layout     |
| `code_lines`              | `code` split into lines                                                              |
| `code_nopp`               | like `code`, with preprocessor directives blanked too                                |
| `pp_lines`                | 1-based line numbers belonging to a directive (continuations included)               |
| `comment_lines`           | 1-based line numbers containing a comment                                            |
| `directives`              | `Directive(start_line, end_line, name)` for every `#...`                             |
| `functions`               | file-scope `FuncSig`s (name, type/name line, paren offsets, definition or prototype) |
| `line_col(offset)`        | absolute offset → (1-based line, 0-based column)                                     |
| `offset(line, col)`       | the reverse                                                                          |
| `settings`                | the `[settings]` table (`max_line_length`, `indent_width`, `tab_width`, ...)         |
| `is_header`, `is_source`  | by file extension                                                                    |

Searching `code` / `code_nopp` instead of `content` means patterns never
match inside comments or strings, and every offset found is still valid in
`content`.

### Testing a rule

Add a test class in `tools/mynewt_style/tests/test_mynewt_format.py` (see the existing
`RuleTestCase` subclasses; `assertFix` also checks the fix is clean and
idempotent), then:

```bash
python3 -m unittest discover -s tools/mynewt_style/tests
python3 tools/mynewt_format.py --explain no-goto-fail
python3 tools/mynewt_format.py -d --rules no-goto-fail --all
```

## Configuring rules

Rules are configured in `newt-coding-rules` at the repository root (TOML):

```toml
[rules.no-goto-fail]
enabled = true
severity = "error"
label = "out"

[rulesets]
paranoid = ["*", "no-goto-fail", "-license-header"]
```

Rule directories outside this one can be added with `rule_dirs` in
`[settings]` or `--rules-dir`.
