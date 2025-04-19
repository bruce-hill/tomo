#!/bin/env python3

# This script converts API YAML files into markdown documentation
# and prints it to standard out.

import yaml
import re

def arg_signature(name, arg):
    sig = name
    if "type" in arg:
        sig = sig + ": " + arg["type"]
    if "default" in arg:
        sig = sig + " = " + arg["default"]
    return sig

def get_signature(name, info):
    if "type" in info:
        return name + " : " + info["type"]
    sig = name + " : func("
    if info["args"]:
        sig += ", ".join(arg_signature(name,arg) for name,arg in info["args"].items())
        sig += " -> " + info["return"].get("type", "Void")
    else:
        sig += "-> " + info["return"].get("type", "Void")
    sig += ")"
    return sig

def print_method(name, info):
    print(f"## {name}")
    print(f"\n```tomo\n{get_signature(name, info)}\n```\n")
    if "description" in info:
        print(info["description"])
    if "note" in info:
        print(info["note"])
    if "errors" in info:
        print(info["errors"])

    if "args" in info and info["args"]:
        print("Argument | Type | Description | Default")
        print("---------|------|-------------|---------")
        for arg,arg_info in info["args"].items():
            default = '**Default:** `'+arg_info['default']+'`' if 'default' in arg_info else ''
            description = arg_info['description'].replace('\n', ' ')
            print(f"{arg} | `{arg_info.get('type', '')}` | {description} | {default}")
            #print(f"- **{arg}:** {arg_info['description']}")

    if "return" in info:
        print(f"\n**Return:** {info['return'].get('description', 'Nothing.')}")

    if "example" in info:
        print(f"\n**Example:**\n```tomo\n{info['example']}\n```")

def convert_to_markdown(yaml_doc:str)->str:
    data = yaml.safe_load(yaml_doc)

    print("# Builtins")
    for name in sorted([k for k in data.keys() if "." not in k]):
        print_method(name, data[name])

    section = None
    for name in sorted([k for k in data.keys() if "." in k]):
        if section is None or not name.startswith(section + "."):
            match = re.match(r"(\w+)\.", name)
            section = match.group(1)
            print(f"\n# {section}")

        print_method(name, data[name])

if __name__ == "__main__":
    import sys
    print("% API\n")
    if len(sys.argv) > 1:
        all_files = ""
        for filename in sys.argv[1:]:
            with open(filename, "r") as f:
                all_files += f.read()
        convert_to_markdown(all_files)
    else:
        convert_to_markdown(sys.stdin.read())
