#!/usr/bin/python

import configparser
import getopt
import os
import subprocess
import sys


def run_cmd(cmd: str) -> list[str]:
    out = subprocess.check_output(cmd, text=True, shell=True)
    return out.splitlines()


def run_build_cmd(cmd: str) -> bool:
    cp = subprocess.run(cmd, stdout=subprocess.DEVNULL, text=True, shell=True)

    return cp.returncode == 0


def build_target(target: str, variant: str = None) -> bool:
    if variant is None:
        print(f"\033[33m{target}\033[0m")
        ret = run_build_cmd(f"newt build {target}")
    else:
        print(f"\033[33m{target} ({variant})\033[0m")
        ret = run_build_cmd(f"newt build -STESTS_VARIANT={variant} {target}")

    return ret


def main() -> bool:
    prefix = "@apache-mynewt-nimble/"

    try:
        opts, args = getopt.getopt(sys.argv[1:], "a")
        for o, a in opts:
            if o == "-a":
                prefix = ""
    except getopt.GetoptError as err:
        print(err)
        sys.exit(1)

    filters_inc = tuple([f"{prefix}{f}" for f in args if not f.startswith("~")])
    filters_exc = tuple([f"{prefix}{f[1:]}" for f in args if f.startswith("~")])

    cfg_fname = os.path.join(os.path.dirname(__file__), "../build_variants")
    vcfg = configparser.ConfigParser(allow_no_value=True)
    vcfg.read(cfg_fname)

    has_error = False

    targets = run_cmd("newt target list -a")
    for target in targets:
        if len(filters_inc) > 0 and not target.startswith(filters_inc):
            continue
        if len(filters_exc) > 0 and target.startswith(filters_exc):
            continue

        if not build_target(target):
            has_error = True

        if vcfg.has_section(target):
            for variant, _ in vcfg.items(target):
                if not build_target(target, variant):
                    has_error = True

    return not has_error


if __name__ == "__main__":
    if not main():
        sys.exit(1)
