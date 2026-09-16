// `tomo eval`: evaluate a Tomo expression and print its result

#include <string.h>

#include "../config.h"
#include "../sha256.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/path.h"
#include "../stdlib/print.h"
#include "../stdlib/result.h"
#include "../stdlib/text.h"
#include "commands.h"
#include "common.h"

static Text_t expr = EMPTY_TEXT;

static cli_arg_t eval_spec[] = {
    {"expr", &expr, &Text$info, .positional = true, .required = true, .metavar = "'<expr>'",
     .description = "the Tomo expression to evaluate"},
    OPTIMIZATION_FLAG,
    VERBOSE_FLAG,
};

static int cmd_eval(cli_command_t *self, List_t extra_args) {
    (void)self;
    set_default_logs(0);

    Text_t program = Texts(">> ", expr, "\n");

    Path_t dir = Path$child(xdg_tomo_dir("XDG_STATE_HOME", "~/.local/state"), Texts("tomo@", TOMO_VERSION));
    Result_t created = Path$create_directory(dir, 0755, true);
    if (created.Failure.reason.tag != TEXT_NONE) print_err(created.Failure.reason);

    // Name the scratch file by a hash of its contents so that distinct
    // expressions never share a stale cached executable (the build system's
    // mtime staleness check has second granularity, which would otherwise
    // reuse the previous eval's binary for a different expression written in
    // the same second), while re-evaluating the same expression reuses its
    // cached build:
    const char *program_str = Text$as_c_string(program);
    char hash[SHA256_HEX_SIZE];
    sha256_hex(program_str, strlen(program_str), hash);
    hash[12] = '\0';

    Path_t eval_file = Path$child(dir, Texts("eval-", Text$from_str(hash), ".tm"));
    if (!Path$exists(eval_file)) {
        Result_t written = Path$write(eval_file, program, 0644);
        if (written.Failure.reason.tag != TEXT_NONE) print_err(written.Failure.reason);
    }

    return compile_and_exec(eval_file, extra_args);
}

cli_command_t eval_command = {
    .name = "eval",
    .summary = "Evaluate a Tomo expression and print its result",
    .description = "The expression is printed the way `>>` prints a value, with syntax coloring\n"
                   "when stdout is a terminal, e.g. `tomo eval '(1).to(10)'`.",
    .spec_len = sizeof(eval_spec) / sizeof(eval_spec[0]),
    .spec = eval_spec,
    .handler = cmd_eval,
};
