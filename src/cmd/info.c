// `tomo info`: inspect the build info (and embedded sources) of compiled
// Tomo binaries

#include "../stdlib/bool.h"
#include "../stdlib/list.h"
#include "commands.h"
#include "common.h"
#include "compilation.h"

static List_t files = EMPTY_LIST;
static OptionalBool_t extract_source = false;

static cli_arg_t info_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "binary",
     .description = "the compiled binaries (or package.a archives) to inspect"}, //
    {"extract-source", &extract_source, &Bool$info, .short_flag = 'x',
     .description = "extract the embedded source files into a <binary>-source directory instead"}, //
    VERBOSE_FLAG, //
};

static int cmd_info(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    set_default_logs(0);
    if (files.length == 0) print_err("No files provided!\n", self->usage);
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);
        if (extract_source) extract_embedded_source(path);
        else print_build_info(path);
    }
    return 0;
}

cli_command_t info_command = {
    .name = "info",
    .summary = "Print the build info embedded in compiled binaries",
    .spec_len = sizeof(info_spec) / sizeof(info_spec[0]),
    .spec = info_spec,
    .handler = cmd_info,
};
