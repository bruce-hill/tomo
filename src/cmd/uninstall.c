// `tomo uninstall`: remove installed Tomo programs and packages

#include "../config.h"
#include "../stdlib/lists.h"
#include "../stdlib/text.h"
#include "common.h"
#include "commands.h"

static List_t names = EMPTY_LIST;

static cli_arg_t uninstall_spec[] = {
    {"names", &names, List$info(&Text$info), .positional = true, .required = true, .metavar = "name",
     .description = "the programs or packages to uninstall"}, //
};

static int cmd_uninstall(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    if (names.length == 0) print_err("No names provided to uninstall!\n", self->usage);
    for (int64_t i = 0; i < (int64_t)names.length; i++) {
        Text_t *name = (Text_t *)(names.data + i * names.stride);
        xsystem(as_owner, "rm -rvf '", TOMO_PATH, "'/lib/tomo@", TOMO_VERSION, "/", *name, " '", TOMO_PATH, "'/bin/",
                *name, " '", TOMO_PATH, "'/man/man1/", *name, ".1");
        print("Uninstalled ", *name);
    }
    return 0;
}

cli_command_t uninstall_command = {
    .name = "uninstall",
    .summary = "Remove installed Tomo programs and packages",
    .description = "Removes the named entries from TOMO_PATH's lib/, bin/, and man/ trees.",
    .spec_len = sizeof(uninstall_spec) / sizeof(uninstall_spec[0]),
    .spec = uninstall_spec,
    .handler = cmd_uninstall,
};
