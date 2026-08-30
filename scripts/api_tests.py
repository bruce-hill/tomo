#!/bin/env python3

# This script converts API YAML files into markdown documentation
# and prints it to standard out.

import yaml
import re

def single_test(name:str, test)->str:
    if "example" not in test: return
    body = test["example"].strip()
    if name.startswith("Path.") or name in ['exit', 'say', 'print', 'sleep', 'fail', 'getenv', 'setenv', 'at_cleanup', 'ask']:
        # Compile-only: these examples have side effects (I/O, exit, ...) that
        # must not actually run, so guard the body with `if no`. It typechecks
        # but never executes, and the test passes as long as it compiles.
        return f'test "{name}"\n    if no\n        ' + body.replace('\n', '\n        ')
    return f'test "{name}"\n    ' + body.replace('\n', '\n    ')

def print_tests(yaml_doc:str):
    data = yaml.safe_load(yaml_doc)

    all_tests = []
    for name in sorted([k for k in data.keys()]):
        t = single_test(name, data[name])
        if t: all_tests.append(t)

    print("# This file contains auto-generated tests from the examples in api/*.yaml\n")
    print('\n\n'.join(all_tests))

if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        all_files = ""
        for filename in sys.argv[1:]:
            with open(filename, "r") as f:
                all_files += f.read()
        print_tests(all_files)
    else:
        print_tests(sys.stdin.read())
