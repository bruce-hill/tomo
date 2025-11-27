#!/bin/env python3

# This script converts API YAML files into markdown documentation
# and prints it to standard out.

from datetime import datetime
import re
import yaml

def escape(text, spaces=False):
    """
    Escapes text for safe inclusion in groff documents.
    - Escapes backslashes as '\\e'
    - Escapes periods at the start of a line as '\\&.'
    """
    escaped_lines = []
    for line in text.splitlines():
        # Escape backslashes
        line = line.replace("\\", r"\[rs]")
        # Escape leading period or apostrophe (groff macro triggers)
        if line.startswith('.'):
            line = r'\&' + line
        elif line.startswith("'"):
            line = r'\&' + line
        if spaces:
            line = line.replace(' ', '\\ ')
        escaped_lines.append(line)
    return '\n'.join(escaped_lines)

def arg_signature(name, arg):
    sig = name
    if "type" in arg:
        sig = sig + ": " + arg["type"]
    if "default" in arg:
        sig = sig + " = " + arg["default"]
    return sig

def get_signature(name, info):
    if "type" in info:
        return ".BI " +escape(f'{name} : {info["type"]}', spaces=True)
    sig = f'{name} : func('
    if info["args"]:
        sig += ", ".join(arg_signature(name,arg) for name,arg in info["args"].items())
        sig += " -> " + info["return"].get("type", "Void")
    else:
        sig += "-> " + info["return"].get("type", "Void")
    sig += ')'
    return ".BI " + escape(sig, spaces=True)

template = ''''\\" t
.\\" Copyright (c) {year} Bruce Hill
.\\" All rights reserved.
.\\"
.TH {name} 3 {date} "Tomo man-pages"
.SH NAME
{name} \\- {short}
.SH LIBRARY
Tomo Standard Library
.SH SYNOPSIS
.nf
{signature}
.fi
.SH DESCRIPTION
{description}
'''

arg_prefix = '''
.TS
allbox;
lb lb lbx lb
l l l l.
Name	Type	Description	Default'''

def write_method(path, name, info):
    lines = []
    year = datetime.now().strftime("%Y")
    date = datetime.now().strftime("%Y-%m-%d")
    signature = get_signature(name, info)
    lines.append(template.format(year=year, date=date, name=name, short=info["short"], description=info["description"], signature=signature))

    if "args" in info and info["args"]:
        lines.append(".SH ARGUMENTS")
        lines.append(arg_prefix)
        for arg,arg_info in info["args"].items():
            default = escape(arg_info['default'], spaces=True) if 'default' in arg_info else '-'
            description = arg_info['description'].replace('\n', ' ')
            lines.append(f"{arg}\t{arg_info.get('type', '')}\t{description}\t{default}")
        lines.append(".TE")

    if "return" in info:
        lines.append(".SH RETURN")
        lines.append(info['return'].get('description', 'Nothing.'))

    if "note" in info:
        lines.append(".SH NOTES")
        lines.append(info["note"])

    if "errors" in info:
        lines.append(".SH ERRORS")
        lines.append(info["errors"])

    if "example" in info:
        lines.append(".SH EXAMPLES")
        lines.append(".EX")
        lines.append(escape(info['example']))
        lines.append(".EE")

    to_write = '\n'.join(lines) + '\n'
    try:
        with open(path, "r") as f:
            existing = f.read()
            if to_write.splitlines()[5:] == existing.splitlines()[5:]:
                return
    except FileNotFoundError:
        pass

    with open(path, "w") as f:
        f.write(to_write)
        print(f"Updated {path}")

def convert_to_markdown(yaml_doc:str)->str:
    data = yaml.safe_load(yaml_doc)

    for name,info in data.items():
        write_method(f"man/man3/tomo-{name}.3", name, data[name])

if __name__ == "__main__":
    import sys
    if len(sys.argv) > 1:
        all_files = ""
        for filename in sys.argv[1:]:
            with open(filename, "r") as f:
                all_files += f.read()
        convert_to_markdown(all_files)
    else:
        convert_to_markdown(sys.stdin.read())
