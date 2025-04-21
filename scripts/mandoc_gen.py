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

def write_method(f, name, info):
    def add_line(line): f.write(line + "\n")
    year = datetime.now().strftime("%Y")
    date = datetime.now().isoformat()
    signature = get_signature(name, info)
    add_line(template.format(year=year, date=date, name=name, short=info["short"], description=info["description"], signature=signature))

    if "args" in info and info["args"]:
        add_line(".SH ARGUMENTS")
        add_line(arg_prefix)
        for arg,arg_info in info["args"].items():
            default = escape(arg_info['default'], spaces=True) if 'default' in arg_info else '-'
            description = arg_info['description'].replace('\n', ' ')
            add_line(f"{arg}\t{arg_info.get('type', '')}\t{description}\t{default}")
        add_line(".TE")

    if "return" in info:
        add_line(".SH RETURN")
        add_line(info['return'].get('description', 'Nothing.'))

    if "note" in info:
        add_line(".SH NOTES")
        add_line(info["note"])

    if "errors" in info:
        add_line(".SH ERRORS")
        add_line(info["errors"])

    if "example" in info:
        add_line(".SH EXAMPLES")
        add_line(".EX")
        add_line(escape(info['example']))
        add_line(".EE")

def convert_to_markdown(yaml_doc:str)->str:
    data = yaml.safe_load(yaml_doc)

    for name,info in data.items():
        with open(f"man/man3/tomo-{name}.3", "w") as f:
            print(f"Wrote to man/man3/tomo-{name}.3")
            write_method(f, name, data[name])

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
