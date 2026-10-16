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

"""File structure: license header, include guards, C++ wrappers."""

import re

from mynewt_style import FileContext, Rule, register_rule

ASF_HEADER = """\
/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
"""

# Any of these near the top means the file carries *some* license, which
# must be kept as is (CODING_STANDARDS.md: copied files keep their header).
LICENSE_MARKERS = (
    "Licensed to the Apache Software Foundation",
    "SPDX-License-Identifier",
    "Copyright",
    "Permission is hereby granted",
    "Licensed under",
)


@register_rule
class LicenseHeader(Rule):
    name = "license-header"
    category = "structure"
    summary = "Files start with the Apache License header (or keep their original license)."
    explanation = """
        CODING_STANDARDS.md: "All files that are newly written, should have the
        Apache License clause at the top of them." Files copied from elsewhere
        keep their original license header.

        The rule only fires when there is no license text at all in the first
        'search_lines' lines. The fix inserts the standard ASF header.
    """
    severity = "warning"
    fixable = True
    options = {"search_lines": 40}

    def _missing(self, ctx: FileContext) -> bool:
        if not ctx.content.strip():
            return False
        top = "\n".join(ctx.lines[: int(self.opts["search_lines"])])
        return not any(marker in top for marker in LICENSE_MARKERS)

    def check(self, ctx: FileContext):
        if self._missing(ctx):
            return [self.violation(ctx, 1, 1, "missing Apache License header")]
        return []

    def fix(self, ctx: FileContext):
        if not self._missing(ctx):
            return None
        return ASF_HEADER + "\n" + ctx.content.lstrip("\r\n")


@register_rule
class HeaderGuard(Rule):
    name = "header-guard"
    category = "structure"
    summary = "Headers are protected by a matching #ifndef/#define guard (or #pragma once)."
    explanation = """
        CODING_STANDARDS.md: header files contain "#ifdef aliasing, to prevent
        multiple includes":

            #ifndef H_BLE_FOO_
            #define H_BLE_FOO_
            ...
            #endif

        The first directive of the header must be '#ifndef X' immediately
        followed by '#define X' with the *same* X (a typo there silently
        disables the guard). '#pragma once' is accepted as well.
    """
    severity = "warning"

    def check(self, ctx: FileContext):
        if not ctx.is_header:
            return []
        directives = ctx.directives
        if not directives:
            return [self.violation(ctx, 1, 1, "header has no include guard")]
        first = directives[0]
        text = ctx.lines[first.start_line - 1].strip()
        if re.match(r"#\s*pragma\s+once\b", text):
            return []
        m = re.match(r"#\s*(?:ifndef\s+(\w+)|if\s+!\s*defined\s*\(?\s*(\w+))", text)
        if not m:
            return [self.violation(ctx, first.start_line, 1,
                                   "first directive of a header must start the include guard")]
        guard = m.group(1) or m.group(2)
        if len(directives) < 2:
            return [self.violation(ctx, first.start_line, 1, "include guard has no #define")]
        second = directives[1]
        m2 = re.match(r"#\s*define\s+(\w+)", ctx.lines[second.start_line - 1].strip())
        if not m2 or m2.group(1) != guard:
            got = m2.group(1) if m2 else ctx.lines[second.start_line - 1].strip()
            return [self.violation(ctx, second.start_line, 1,
                                   f"include guard mismatch: '#ifndef {guard}' followed by "
                                   f"'{got}'")]
        last = directives[-1]
        if last.name != "endif":
            return [self.violation(ctx, last.start_line, 1,
                                   "last directive of a guarded header should be #endif")]
        return []


@register_rule
class ExternC(Rule):
    name = "extern-c"
    category = "structure"
    summary = "Public headers wrap their declarations in extern \"C\" for C++ users."
    explanation = """
        CODING_STANDARDS.md: public headers carry the C++ wrapper after their
        #include lines:

            #ifdef __cplusplus
            extern "C" {
            #endif

        Only headers below an 'include/' directory are checked.
    """
    severity = "warning"

    def check(self, ctx: FileContext):
        if not ctx.is_header or "include" not in ctx.path.parts:
            return []
        if "__cplusplus" in ctx.content and re.search(r'extern\s+"C"', ctx.content):
            return []
        return [self.violation(ctx, 1, 1, "public header lacks the extern \"C\" wrapper")]
