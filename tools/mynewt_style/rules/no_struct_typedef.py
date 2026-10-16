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
no-struct-typedef: a minimal, check-only rule. Use it as a template.

Every *.py file in tools/rules/ is imported automatically; @register_rule is
all it takes to make a rule show up in --list-rules.
"""

import re

from mynewt_style import FileContext, Rule, register_rule


@register_rule
class NoStructTypedef(Rule):
    name = "no-struct-typedef"
    category = "types"
    summary = "Do not typedef structures; use 'struct foo' directly."
    explanation = """
        CODING_STANDARDS.md: "Do not use typedefs for structures. This makes
        it impossible for applications to use pointers to those structures
        opaquely."

        Good:  struct ble_foo { int a; };
        Bad:   typedef struct { int a; } ble_foo_t;
    """
    severity = "warning"

    def check(self, ctx: FileContext):
        out = []
        # ctx.code has comments and string contents blanked out, so the
        # pattern cannot match inside them; offsets still match ctx.content.
        for m in re.finditer(r"\btypedef\s+struct\b", ctx.code_nopp):
            line, col = ctx.line_col(m.start())
            out.append(self.violation(ctx, line, col + 1, "typedef of a struct"))
        return out
