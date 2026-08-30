#!/usr/bin/env python3
"""Emit a compile_commands.json for editors and language servers.

Usage: compile_commands.py <source>... -- <compiler flag>...

The flags arrive already split by the shell, exactly as the compiler receives
them, so a -D value that carries quotes or spaces of its own stays one
argument. They are written out as JSON `arguments` rather than a `command`
string for the same reason: nothing re-splits them.
"""

import json
import os
import sys

sources = sys.argv[1 : sys.argv.index("--")]
flags = sys.argv[sys.argv.index("--") + 1 :]
cwd = os.getcwd()

json.dump(
    [
        {"directory": cwd, "file": src, "arguments": ["cc", *flags, "-c", src]}
        for src in sources
    ],
    sys.stdout,
    indent=1,
)
print()
