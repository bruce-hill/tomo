# Tomo's gdb integration, loaded by `tomo run --debug` (and usable by hand on
# any program built with `tomo build --debug`: `gdb -x tomo-gdb.py ./prog`).
#
# gdb can already follow a Tomo program: the generated C carries `#line`
# directives back to the .tm file, so breakpoints, stepping, and backtraces are
# in Tomo source already. Two things it can't do on its own:
#
#   Show a Tomo value. An Int is a tagged small-int-or-bignum, a Text is a rope,
#   and a List or Table doesn't record in its C type what it holds. Printing
#   them means calling Tomo's own generic_as_text() in the stopped program, with
#   the value's TypeInfo -- which for a type-erased value is only available
#   because a `--debug` build emits a `_$x$typeinfo` beside each variable `_$x`
#   (see compile_debug_typeinfo() in src/compile/statements.c).
#
#   Use Tomo's names. A function is `fib$prog_a1b2c3d4` and a variable `x` is
#   `_$x`, so a bare Tomo name typed at gdb finds either nothing or some
#   unrelated C symbol -- `print log` in a program with a `log` variable answers
#   with libm's log().
#
# Commands: `tlocals`, `tframe`, and replacements for `p` (gdb's alias for
# `print`) and `backtrace`/`bt`/`where`. `print` and every other command still
# take C names; gdb's own backtrace is still there as `info stack`.

import os
import re
import gdb

NUM_INFO = "&Num$info"
NUM_DIGITS = 10  # fractional digits in the decimal shown beside an exact Num
ESCAPE = r"\x1b\[[0-9;]*m"


def attempt(thunk, default=None):
    """`thunk()`, or `default` if it couldn't be had.

    Nearly everything here asks a question of a stopped program that may be in
    no state to answer, or of a gdb whose state moved underneath it. A failure
    has to be an answer rather than an exception, so this is what wraps them."""
    try:
        return thunk()
    except Exception:
        return default


# --- Output ----------------------------------------------------------------


def use_color():
    if os.environ.get("NO_COLOR"):
        return False
    return attempt(lambda: bool(gdb.parameter("style enabled")), True)


def dim(text):
    return "\033[2m" + text + "\033[m" if use_color() else text


def bold(text):
    return "\033[1m" + text + "\033[m" if use_color() else text


def out(text=""):
    gdb.write(text + "\n")


def call(expression):
    """A string-valued expression evaluated in the stopped program, or None."""
    return attempt(lambda: gdb.parse_and_eval(expression).string(errors="replace"))


def command(name, kind=gdb.COMMAND_DATA):
    """Register a function as a gdb command, saving each one a class."""

    def register(function):
        type(name, (gdb.Command,), {
            "__doc__": function.__doc__,
            "__init__": lambda self: gdb.Command.__init__(self, name, kind),
            "invoke": lambda self, argument, from_tty: function(argument.strip()),
        })()
        return function

    return register


# --- Names -----------------------------------------------------------------

_module_ids = None


def demangle(name):
    """The Tomo name a generated C symbol came from ('fib$prog_a1b2c3d4' -> 'fib').

    Tomo mangles a top-level name as `Namespace$name$<module id>`. Rather than
    guess at the id's shape, read the real ones out of the program: every module
    emits exactly one `$initialize$<module id>`."""
    global _module_ids
    if _module_ids is None:
        listing = attempt(lambda: gdb.execute("info functions initialize", to_string=True), "")
        _module_ids = set(re.findall(r"\$initialize\$(\w+)", listing))
    if not name:
        return name
    for module_id in _module_ids:
        if name.endswith("$" + module_id):
            name = name[: -(len(module_id) + 1)]
            break
    # `Foo$bar` is Tomo's `Foo.bar`; the doubled `$` of the generated CLI
    # wrapper (`parse_and_run$$main`) is a single separator.
    return name.replace("$$", ".").replace("$", ".")


# --- TypeInfo --------------------------------------------------------------

# A Tomo struct or enum compiles to `Foo$$struct$<module>` / `Shape$$type$<module>`
# beside a `Foo$$info$<module>` holding its TypeInfo.
NAMED_TYPE = re.compile(r"^(.+)\$\$(?:struct|type)\$(\w+)$")

_typeinfo_cache = {}


def typeinfo_for(gdb_type):
    """The TypeInfo of a C type, if its name says which Tomo type it is.

    The naming is regular enough to be a rule rather than a table: `Int_t` has
    `Int$info`, and a struct or enum has the `$$info$` beside it. The rule also
    excludes the right types by itself -- `List$info` and `Table$info` are
    macros rather than symbols, so a type-erased value finds nothing here and
    falls back to the variable's companion, which is the only thing that knows
    what it holds."""
    names = attempt(lambda: (gdb_type.name, gdb_type.strip_typedefs().name), ())
    for name in names:
        if name is None:
            continue
        if name not in _typeinfo_cache:
            _typeinfo_cache[name] = _lookup_typeinfo(name)
        if _typeinfo_cache[name]:
            return _typeinfo_cache[name]
    return None


def _lookup_typeinfo(type_name):
    if type_name == "number":
        symbol = "Num$info"  # `Num_t` is a typedef the compiled program doesn't record
    elif type_name.endswith("_t") or type_name.endswith("_s"):
        # `_t` is the typedef; `_s` is the struct tag, which is what gdb hands
        # back for a value it has no typedef for (`finish`'s return value).
        symbol = type_name[:-2] + "$info"
    else:
        match = NAMED_TYPE.match(type_name)
        if not match:
            return None
        symbol = "%s$$info$%s" % match.groups()
    found = attempt(lambda: gdb.lookup_global_symbol(symbol) or gdb.lookup_static_symbol(symbol))
    return "&" + symbol if found else None


# --- Formatting values -----------------------------------------------------


def truncate(text, limit=None):
    """`text` cut to `limit` (or gdb's `print elements`), counting visible characters.

    A Tomo value is formatted whole -- a list is one line however long it is --
    and the variables in scope are printed at every stop, so one big value would
    otherwise bury the screen. The regex takes a prefix of at most `limit`
    visible characters without ever splitting an ANSI escape, which would leave
    the terminal wearing the value's color."""
    if limit is None:
        limit = attempt(lambda: gdb.parameter("print elements"), 200)
    if text is None or not limit or len(text) <= limit:
        return text  # len() is an upper bound on the visible length
    kept = re.match(r"(?:%s)*(?:[^\x1b](?:%s)*){0,%d}" % (ESCAPE, ESCAPE, limit), text).group(0)
    if len(kept) == len(text):
        return text  # it was only the escapes that made it look too long
    return kept + ("\033[m" if "\x1b" in kept else "") + dim("…")


def format_value(address, typeinfo):
    """Tomo's rendering of the value at `address`, or None if it can't be had."""
    text = call(
        "Text$as_c_string(generic_as_text((void*)(%s), %d, (const TypeInfo_t*)(%s)))"
        % (address, 1 if use_color() else 0, typeinfo)
    )
    # An exact Num that no decimal expresses (`32768/3`, `pi`) gets one beside
    # it; one already written as a decimal has nothing to approximate. The
    # TypeInfo is compared by pointer because it usually arrives as the name of
    # a variable's companion -- and an optional `Num?` deliberately doesn't
    # match, since its TypeInfo is the optional's and a `none` is not a number.
    same = "(const void *)(%s) == (const void *)&Num$info" % typeinfo
    if text is None or (typeinfo != NUM_INFO and not attempt(lambda: int(gdb.parse_and_eval(same)), 0)):
        return text
    decimal = call("number_to_string(*(struct number*)(%s), %d, 0)" % (address, NUM_DIGITS))
    if decimal is None or decimal == re.sub(ESCAPE, "", text):
        return text
    return text + dim(" ≈ " + decimal)


def scratch_copy(value):
    """A pointer to a copy of `value` in the program's heap, or None.

    Some values gdb hands out have no address -- the one `finish` reports comes
    back in a register -- and Tomo's formatter takes a pointer. GC memory, so
    nothing has to free it. GC_malloc has no debug info, hence the cast."""

    def allocate():
        size = value.type.sizeof
        pointer = int(gdb.parse_and_eval("((void *(*)(unsigned long))GC_malloc)(%d)" % size))
        gdb.selected_inferior().write_memory(pointer, value.bytes, size)
        return pointer

    return attempt(allocate) or None


class Rendered(object):
    """All of gdb's pretty-printer protocol that this needs."""

    def __init__(self, text):
        self.text = text

    def to_string(self):
        return self.text


def pretty_printer(value):
    """gdb's pretty-printer lookup, so `print`, `info locals`, and `finish` show
    Tomo values too.

    The formatting happens here rather than in the printer: a printer, once
    returned, is the only thing that gets to print the value, so one that turned
    out not to be able to would leave the user with a blank."""
    typeinfo = typeinfo_for(value.type)
    if typeinfo is None:
        return None
    if typeinfo == NUM_INFO:
        # A `Num?` is the same C type as a `Num`, and only its TypeInfo -- which
        # a bare value doesn't carry -- tells them apart. Its `none` is the one
        # bit pattern no real Num has, and formatting that as a number hangs.
        if attempt(lambda: int(value["bits"]), None) == 0:
            return Rendered(dim("none"))
    address = value.address if value.address is not None else scratch_copy(value)
    text = format_value(str(address), typeinfo) if address is not None else None
    return Rendered(truncate(text)) if text is not None else None


# --- Frames ----------------------------------------------------------------

try:
    from gdb.FrameDecorator import FrameDecorator
except ImportError:
    FrameDecorator = None


if FrameDecorator is not None:

    class TomoArgument(object):
        """A frame argument under its Tomo name, with the value elided."""

        def __init__(self, name):
            self.name = name

        def symbol(self):
            return self.name

        def value(self):
            return "..."

    class TomoFrame(FrameDecorator):
        """Tomo names for everywhere gdb names a frame: `bt`, `frame`, `info threads`."""

        def function(self):
            name = FrameDecorator.function(self)
            return demangle(name) if isinstance(name, str) else name  # else an address

        def frame_args(self):
            # A frame filter takes over argument printing entirely, so without
            # this a backtrace reads `Foo.doop (_$f=...)`. Names only: see the
            # `frame-arguments` note in configure() for why no value.
            arguments = FrameDecorator.frame_args(self)
            renamed = []
            for argument in arguments or []:
                name = getattr(argument.symbol(), "name", None) or ""
                renamed.append(TomoArgument(name[2:]) if name.startswith("_$") else argument)
            return renamed or arguments

    class TomoFrameFilter(object):
        name, priority, enabled = "tomo", 100, True

        def filter(self, frames):
            return (TomoFrame(frame) for frame in frames)


def is_tomo_frame(frame):
    sal = frame.find_sal()
    return bool(sal and sal.symtab and sal.symtab.filename.endswith(".tm"))


def tomo_frame():
    """The frame to answer questions about: the selected one, or the innermost
    Tomo frame above it when the stop landed in the runtime (a `breakpoint()`
    lands in tomo_debug_breakpoint, a fatal signal wherever it was raised)."""
    frame = attempt(gdb.selected_frame)
    if frame is None:
        raise gdb.GdbError("The program is not running.")
    while frame is not None and not is_tomo_frame(frame):
        frame = attempt(frame.older)
    return frame


def frame_variables(frame):
    """[(tomo_name, c_name, typeinfo)] for the variables visible in `frame`.

    Innermost block first, each name taken the first time it is seen, so a
    shadowed variable resolves to the one in scope. A variable is paired only
    with a companion from its own block: the two are emitted together, so one
    from an enclosing block belongs to a different variable of the same name."""
    block, found, seen = attempt(frame.block), [], set()
    while block is not None:
        symbols = [s for s in block if (s.is_variable or s.is_argument) and s.name.startswith("_$")]
        companions = set(s.name for s in symbols if s.name.endswith("$typeinfo"))
        for symbol in symbols:
            name = symbol.name
            if name.endswith("$typeinfo") or name in seen:
                continue  # a companion itself, or shadowed by an inner block
            seen.add(name)
            companion = name + "$typeinfo"
            found.append((name[2:], name, companion if companion in companions else typeinfo_for(symbol.type)))
        if block.function is not None:
            break
        block = block.superblock
    return found + captured_variables(frame, set(name for name, _, _ in found))


def captured_variables(frame, already_shown):
    """The variables a lambda closed over, which are fields of its `userdata`
    rather than locals -- without these, stopping inside a lambda shows only its
    arguments. Captured functions (`say`, `breakpoint`) are left out: they are
    how a lambda calls anything at all, not something the user put there."""
    userdata = attempt(lambda: frame.read_var("userdata"))
    fields = attempt(lambda: userdata.type.target().fields(), [])
    found = []
    for field in fields or []:
        pointed = attempt(lambda: field.type.target().code)
        if field.name in already_shown or pointed == gdb.TYPE_CODE_FUNC:
            continue
        # A `--debug` build emits a companion for a captured variable too,
        # which is the only way to see inside a captured list or table.
        companion = "_$%s$typeinfo" % field.name
        if attempt(lambda: frame.read_var(companion)) is None:
            companion = typeinfo_for(field.type)
        found.append((field.name, "userdata->" + field.name, companion))
    return found


def describe(c_name, typeinfo):
    """One variable's value, however well we can manage it."""
    text = format_value("&" + c_name, typeinfo) if typeinfo is not None else None
    if text is not None:
        return truncate(text)
    raw = attempt(lambda: str(gdb.parse_and_eval(c_name)))
    return dim(truncate(raw)) if raw is not None else dim("<unavailable>")


def show_frame(frame, locals_too=True):
    """Where the program is, Tomo-side: function, source, variables in scope."""
    frame.select()
    sal = frame.find_sal()
    out(bold("%s (%s:%d)" % (demangle(frame.name() or "?"), sal.symtab.filename, sal.line)))
    lines = attempt(lambda: open(sal.symtab.fullname()).read().split("\n"), [])
    width = len(str(min(len(lines), sal.line + 2)))
    for n in range(max(1, sal.line - 2), min(len(lines), sal.line + 2) + 1):
        here = n == sal.line
        marker = ("\033[91;1m>\033[m" if use_color() else ">") if here else " "
        out(" %s %s %s" % (marker, dim("%*d\u2502" % (width, n)), bold(lines[n - 1]) if here else dim(lines[n - 1])))
    variables = frame_variables(frame) if locals_too else []
    if variables:
        out()
    for tomo_name, c_name, typeinfo in variables:
        out("   %s = %s" % (bold(tomo_name), describe(c_name, typeinfo)))


def show_selected(locals_too=True):
    """Report the selected frame, if it is Tomo code.

    Only the frame actually selected -- walking out to some enclosing Tomo frame
    would describe a different one than the user moved to, and when stepping it
    would describe somewhere other than where execution is."""
    frame = attempt(gdb.selected_frame)
    if frame is not None and is_tomo_frame(frame):
        show_frame(frame, locals_too)


# --- Commands --------------------------------------------------------------

# A Tomo variable `x` is `_$x` in the generated C. Identifiers include `$` so
# that a C name typed directly (`_$x`) is one token and passes through; strings
# and member names are matched only to be skipped over.
IDENTIFIER = re.compile(r'"(?:[^"\\]|\\.)*"|(?:\.|->)\s*\w+|[A-Za-z_][\w$]*')


def in_tomo_names(frame, expression):
    """`expression` with Tomo variable names rewritten to their C names."""
    names = {tomo: c_name for tomo, c_name, _ in frame_variables(frame)} if frame else {}
    if not names:
        return expression
    return IDENTIFIER.sub(lambda m: names.get(m.group(0), m.group(0)), expression)


@command("tlocals")
def tomo_locals(argument):
    """tlocals -- the Tomo variables in scope, printed the way Tomo prints them."""
    frame = tomo_frame()
    if frame is None:
        raise gdb.GdbError("No Tomo frame on the stack.")
    variables = frame_variables(frame)
    if not variables:
        out(dim("(no variables in scope)"))
    for tomo_name, c_name, typeinfo in variables:
        out("%s = %s" % (bold(tomo_name), describe(c_name, typeinfo)))


@command("p")
def tomo_print(argument):
    """p [/FMT] EXPR -- gdb's `print`, in Tomo's names.

    Replaces `p`, which gdb defines as an alias for `print`. `print` itself is
    untouched and still takes C names, and anything this doesn't recognize is
    handed to it unchanged, so format letters and value history still work."""
    if not argument:
        raise gdb.GdbError("Usage: p [/FMT] EXPR")
    fmt = ""
    if argument.startswith("/"):  # a C rendering was asked for; the names still translate
        fmt, _, argument = argument.partition(" ")
        argument, fmt = argument.strip(), fmt + " "
    frame = attempt(tomo_frame)
    if not fmt:
        for tomo_name, c_name, typeinfo in frame_variables(frame) if frame else []:
            if tomo_name == argument:
                text = describe(c_name, typeinfo)
                out("%s = %s" % (bold(tomo_name), text))
                if text.endswith(dim("…")):
                    out(dim("(cut off; `set print elements unlimited` for the rest)"))
                return
    rewritten = in_tomo_names(frame, argument)
    try:
        gdb.execute("print %s%s" % (fmt, rewritten))
    except gdb.error as e:
        if rewritten == argument:
            raise gdb.GdbError(str(e))
        # What gdb evaluates is C: a Tomo Int is a tagged struct rather than a C
        # integer, and Tomo's operators are not things gdb can call.
        raise gdb.GdbError(
            "%s\n(gdb evaluates C, not Tomo: a variable can be printed, but Tomo "
            "operators and methods aren't available in an expression.)" % e
        )


# How much of an argument's value a backtrace line carries. A frame line is a
# summary; `tlocals` is where a value is read properly.
ARGUMENT_WIDTH = 24


def frame_arguments(frame):
    """[(name, address, typeinfo)] for a Tomo frame's arguments.

    Addresses are collected now and rendered later, by the caller: rendering
    calls into the program, and that invalidates the frame objects a walk up
    the stack is holding."""
    block = attempt(frame.block)
    while block is not None and block.function is None:
        block = block.superblock
    found = []
    for symbol in block or []:
        if not symbol.is_argument or not symbol.name.startswith("_$"):
            continue
        # Both are resolved to plain addresses here rather than left as names:
        # a companion is a local of *this* frame, and by the time the value is
        # rendered the selected frame is somewhere else entirely.
        companion = attempt(lambda: int(frame.read_var(symbol.name + "$typeinfo")))
        found.append((
            symbol.name[2:],
            attempt(lambda: int(symbol.value(frame).address)),
            str(companion) if companion else typeinfo_for(symbol.type),
        ))
    return found


@command("backtrace", gdb.COMMAND_STACK)
def tomo_backtrace(argument):
    """backtrace [N] -- the stack, in Tomo's terms. Also `bt` and `where`.

    This replaces gdb's, so there is one backtrace to know rather than two: the
    frames are the same ones, named and rendered the way Tomo writes them. `N`
    shows the innermost N frames and `-N` the outermost N; any other argument is
    handed to gdb's own backtrace, which is still there as `info stack`."""
    if argument and not re.match(r"^-?\d+$", argument):
        gdb.execute("info stack " + argument)
        return
    if attempt(gdb.selected_frame) is None:
        raise gdb.GdbError("The program is not running.")

    frames, frame = [], gdb.newest_frame()
    while frame is not None:
        sal = frame.find_sal()
        location = "%s:%d" % (sal.symtab.filename, sal.line) if sal and sal.symtab else None
        frames.append((demangle(frame.name() or "?"), location,
                       frame_arguments(frame) if is_tomo_frame(frame) else None))
        frame = attempt(frame.older)

    count = int(argument) if argument else 0
    chosen = list(enumerate(frames))
    chosen = chosen[:count] if count > 0 else chosen[count:] if count < 0 else chosen
    for level, (name, location, arguments) in chosen:
        # A frame that is not Tomo code (the runtime, the generated
        # command-line wrapper) gets no argument list: there is no Tomo
        # rendering of one to give.
        shown = ""
        if arguments is not None:
            shown = "(%s)" % ", ".join(
                "%s=%s" % (n, truncate(format_value(a, t) if a and t else None, ARGUMENT_WIDTH) or "?")
                for n, a, t in arguments
            )
        out(" ".join(x for x in (dim("#%d" % level), bold(name) + shown, dim("at " + location) if location else "") if x))


@command("tframe", gdb.COMMAND_STACK)
def tomo_show_frame(argument):
    """tframe -- show where the program is stopped, with the variables in scope.

    gdb runs this after `frame`, `up`, and `down` too (see configure): those
    print a line built from the raw C symbol and nothing about what is in
    scope."""
    show_selected()


@command("tomo-run", gdb.COMMAND_RUNNING)
def tomo_run(argument):
    """tomo-run -- run the program, and leave gdb when it finishes on its own.

    What `tomo run --debug` starts with. A program that runs to completion should
    behave like `tomo run` did, exit code and all, rather than leaving the user
    at a debugger prompt with nothing to debug."""
    gdb.execute("run " + argument)
    if not gdb.selected_inferior().threads():
        gdb.execute("quit %d" % _exit_code[0])


# --- Stopping --------------------------------------------------------------

# Watchpoints arrive as breakpoint events too, and calling one a breakpoint when
# the user set it with `watch` is only confusing.
WATCHPOINTS = (gdb.BP_WATCHPOINT, gdb.BP_HARDWARE_WATCHPOINT, gdb.BP_READ_WATCHPOINT, gdb.BP_ACCESS_WATCHPOINT)

_exit_code = [0]


def on_exited(event):
    _exit_code[0] = getattr(event, "exit_code", 0) or 0


def on_stop(event):
    """Report a stop in Tomo's terms.

    A stop the program didn't ask for -- a breakpoint, a signal -- is one the
    user is about to look around from, so it lists what is in scope too.
    Stepping doesn't: `step` through a long function should show the line it
    reached, not reprint every variable at each one. It also shows the frame
    directly rather than running `tframe`, since re-entering gdb's command loop
    from inside a stop handler crashes it."""
    if isinstance(event, gdb.BreakpointEvent):
        out()
        for b in event.breakpoints or ():
            if b.number > 0:
                out(dim("%s %d" % ("Watchpoint" if b.type in WATCHPOINTS else "Breakpoint", b.number)))
    elif isinstance(event, gdb.SignalEvent):
        out()  # gdb has already named the signal
    elif isinstance(event, gdb.StopEvent):
        show_selected(locals_too=False)  # stepping
        return
    else:
        return
    frame = attempt(tomo_frame)
    show_frame(frame) if frame else out(dim("(stopped outside Tomo code -- `bt` for the stack)"))


def silence(breakpoint):
    """gdb announces a hit with the raw C symbol ("Breakpoint 1, main$prog_a1b2c3d4
    () at prog.tm:9") -- frame filters don't reach that line. Silence it and let
    the stop handler report the stop instead."""
    attempt(lambda: setattr(breakpoint, "silent", True))


# --- Setup -----------------------------------------------------------------


def configure():
    def quietly(command_line):
        attempt(lambda: gdb.execute(command_line, to_string=True))

    quietly("set debuginfod enabled off")  # nothing here has debug info to download
    quietly("set confirm off")
    quietly("set print pretty on")
    # A frame line names its arguments but never shows one. Rendering a Tomo
    # value means calling into the stopped program, and gdb caches the frame it
    # is printing across that call -- during `finish` it then fails to reinflate
    # it and dies with an internal error. `tlocals` has the values.
    quietly("set print frame-arguments none")

    # Stop on the ways a Tomo program comes apart. `fail()` raises SIGABRT under
    # TOMO_CORE_DUMP, which `tomo run --debug` sets; -O0 traps undefined
    # behavior, which arrives as SIGILL. Each is still passed on, so continuing
    # gets the runtime's own error report. Not SIGTRAP: that is how gdb
    # implements breakpoints, and handing it to a program that installs a
    # fatal-signal handler would kill it the first time it was stepped. SIGINT
    # is the user interrupting to look around, so it stops but is not delivered.
    for signal in ("SIGABRT", "SIGSEGV", "SIGFPE", "SIGILL", "SIGBUS", "SIGSYS"):
        quietly("handle %s stop print pass" % signal)
    quietly("handle SIGINT stop print nopass")

    # `step` should step through the program, not down into the runtime that
    # implements it. `skip disable` turns this off; `info skip` shows it.
    quietly("skip -gfi src/stdlib/*.c")
    quietly("skip -gfi src/stdlib/*.h")

    # Moving around the stack should show as much as arriving somewhere does.
    # `bt` goes through the frame filter; `frame`, `up`, and `down` do not.
    for command_name in ("frame", "up", "down"):
        quietly("define hookpost-%s\ntframe\nend" % command_name)

    # Where `breakpoint()` lands (see src/stdlib/debugger.c):
    if attempt(lambda: gdb.lookup_global_symbol("tomo_debug_breakpoint")):
        silence(attempt(lambda: gdb.Breakpoint("tomo_debug_breakpoint", internal=True)))

    gdb.pretty_printers.append(pretty_printer)
    if FrameDecorator is not None:
        gdb.frame_filters["tomo"] = TomoFrameFilter()
    gdb.events.stop.connect(on_stop)
    gdb.events.exited.connect(on_exited)
    if hasattr(gdb.events, "breakpoint_created"):
        gdb.events.breakpoint_created.connect(silence)


configure()
out(
    bold("Tomo debugger")
    + dim(" -- break FILE.tm:LINE, step, next, finish, continue, bt, p VAR, tlocals, tframe, help")
)
