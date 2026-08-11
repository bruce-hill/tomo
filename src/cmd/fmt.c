// `tomo fmt`: format Tomo source code

#include "../formatter/formatter.h"
#include "../stdlib/bools.h"
#include "../stdlib/lists.h"
#include "common.h"
#include "commands.h"

static List_t files = EMPTY_LIST;
static OptionalBool_t in_place = false;

static cli_arg_t fmt_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "file.tm",
     .description = "the files to format"}, //
    {"in-place", &in_place, &Bool$info, .short_flag = 'i',
     .description = "rewrite the files instead of printing to stdout"}, //
};

static int cmd_fmt(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    files = normalize_tm_paths(files);
    if (files.length == 0) print_err("No files provided to format!\n", self->usage);
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);
        Text_t formatted = format_file(Path$as_c_string(path));
        if (in_place) {
            print("Formatted ", path);
            Path$write(path, formatted, 0644);
        } else {
            print_inline(formatted);
        }
    }
    return 0;
}

cli_command_t fmt_command = {
    .name = "fmt",
    .summary = "Format Tomo source code",
    .spec_len = sizeof(fmt_spec) / sizeof(fmt_spec[0]),
    .spec = fmt_spec,
    .handler = cmd_fmt,
};
