# Colorful Lang

Colorful is a `lang` that lets you write colorful text for the terminal without
having to stress about managing state for color highlighting.

## Grammar

The grammar looks like this:

```
colorful <- ("@(at)" / "@(lparen)" / "@(rparen)" # Escapes
             / "@(" attributes ":" colorful ")"  # Colorful text
             / .)*                               # Plain text

attributes <- (attribute ("," attribute)*)?

attribute <- color                   # Color defaults to foreground
             / "fg=" color           # Foreground color
             / "bg=" color           # Background color
             / "ul=" color           # Underline color
             / "b" / "bold"
             / "d" / "dim"
             / "u" / "underline"
             / "i" / "italic"
             / "B" / "blink"
             / "r" / "reverse"
             # These are rarely supported by terminals:
             / "fraktur"
             / "frame"
             / "encircle"
             / "overline"
             / "super" / "superscript"
             / "sub" / "subscript"

color <- "black" / "red" / "green" / "yellow" / "blue" / "magenta" / "cyan" / "white"
          # All caps colors are "bright" colors (not always supported):
          / "BLACK" / "RED" / "GREEN" / "YELLOW" / "BLUE" / "MAGENTA" / "CYAN" / "WHITE"
          / "default"
          / "#" 6 hex  # Values 0x000000-0xFFFFFF
          / "#" 3 hex  # Values 0x000-0xFFF
          / 1-3 digit  # Values 0-255
```

## Command Line Usage

You can run `colorful` as a standalone executable to render colorful text with
ANSI escape sequences so it looks nice on a terminal.

```
colorful [--help] [texts...] [--by-line] [--files ...]
```

## Library Usage

`colorful` can also be used as a Tomo library:

```ini
# modules.ini
[colorful]
version=v1.0
```

```tomo
use colorful

$Colorful"
    @(blue:Welcome to the @(bold:party)!)
    We have @(green,bold:colors)!
".print()
```
