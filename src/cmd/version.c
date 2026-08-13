// `tomo version`: print the Tomo compiler version

#include "../config.h"
#include "common.h"
#include "commands.h"

static cli_arg_t version_spec[] = {
    VERBOSE_FLAG, //
};

static int cmd_version(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    if (verbose) print(TOMO_VERSION, " ", GIT_VERSION);
    else print(TOMO_VERSION);
    return 0;
}

cli_command_t version_command = {
    .name = "version",
    .summary = "Print the Tomo compiler version (with -v: plus the git revision)",
    .spec_len = sizeof(version_spec) / sizeof(version_spec[0]),
    .spec = version_spec,
    .handler = cmd_version,
};
