#!/usr/bin/python

import os.path
import re
import subprocess
import tempfile
import sys


def get_lines_range(m: re.Match) -> range:
    first = int(m.group(1))

    if m.group(2) is not None:
        last = first + int(m.group(2))
    else:
        last = first + 1

    return range(first, last)


def run_cmd(cmd: str) -> list[str]:
    out = subprocess.check_output(cmd, text=True, shell=True)
    return out.splitlines()


def check_file(fname: str, commit: str, upstream: str) -> list[str]:
    ret = []

    diff_lines = set()
    for s in run_cmd(f"git diff -U0 {upstream} {commit} -- {fname}"):
        m = re.match(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", s)
        if not m:
            continue
        diff_lines.update(get_lines_range(m))

    with tempfile.NamedTemporaryFile(suffix=os.path.basename(fname)) as tmpf:
        lines = subprocess.check_output(f"git show {commit}:{fname}",
                                        shell=True)
        tmpf.write(lines)

        in_chunk = False

        for s in run_cmd(f"uncrustify -q -c uncrustify.cfg -f {tmpf.name} | "
                         f"diff -u0 -p {tmpf.name} - || true"):
            m = re.match(r"^@@ -(\d+)(?:,(\d+))? \+\d+(?:,\d+)? @@", s)
            if not m:
                if in_chunk:
                    ret.append(s)
                continue

            in_chunk = len(diff_lines & set(get_lines_range(m))) != 0

            if in_chunk:
                ret.append(s)

    return ret


def is_ignored(fname: str, ign_dirs: list[str]) -> bool:
    if not re.search(r"\.(c|cpp|h|hpp)$", fname):
        return True

    for d in ign_dirs:
        if fname.startswith(d):
            return True

    return False


def main() -> bool:
    if len(sys.argv) > 1:
        commit = sys.argv[1]
    else:
        commit = "HEAD"
    if len(sys.argv) > 2:
        upstream = sys.argv[2]
    else:
        upstream = "origin/master"

    mb = run_cmd(f"git merge-base {upstream} {commit}")
    upstream = mb[0]

    has_error = False

    cfg_fname = os.path.join(os.path.dirname(__file__), "../style_ign_dirs")
    with open(cfg_fname, "r") as x:
        ign_dirs = [s.strip() for s in x.readlines() if
                    s.strip() and not s.startswith("#")]

    files = run_cmd(f"git diff --diff-filter=AM --name-only {upstream} {commit}")
    for cfg_fname in files:
        if is_ignored(cfg_fname, ign_dirs):
            print(f"\033[90m- {cfg_fname}\033[0m")
            continue

        diff = check_file(cfg_fname, commit, upstream)
        if len(diff) > 0:
            print(f"\033[31m! {cfg_fname}\033[0m")
            print()
            print("\n".join(diff))
            print()
            has_error = True
        else:
            print(f"+ {cfg_fname}")

    return not has_error


if __name__ == "__main__":
    if not main():
        sys.exit(1)
