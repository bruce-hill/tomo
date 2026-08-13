// `tomo vendor`: copy packages' verified sources into ./vendor/ (or undo
// that with --unvendor)

#include "../packages.h"
#include "../stdlib/bools.h"
#include "../stdlib/lists.h"
#include "../stdlib/text.h"
#include "commands.h"
#include "common.h"

static List_t names = EMPTY_LIST;
static OptionalBool_t editable = false, unvendor = false;

static cli_arg_t vendor_spec[] = {
    {"names", &names, List$info(&Text$info), .positional = true, .required = true, .metavar = "package",
     .description = "the packages to vendor"}, //
    {"editable", &editable, &Bool$info, .short_flag = 'e',
     .description = "extract the package's sources for editing instead of copying its verified source archive"}, //
    {"unvendor", &unvendor, &Bool$info, .short_flag = 'u',
     .description = "undo vendoring: restore the package to its non-vendored source (re-pinning the digest if "
                    "needed) and delete the vendored copy"}, //
    VERBOSE_FLAG, //
};

static int cmd_vendor(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    set_default_logs(0);
    if (names.length == 0) print_err("No packages provided to vendor!\n", self->usage);
    if (unvendor && editable) print_err("--unvendor and --editable can't be combined");
    for (int64_t i = 0; i < (int64_t)names.length; i++) {
        Text_t *name = (Text_t *)(names.data + i * names.stride);
        if (unvendor) unvendor_package(Text$as_c_string(*name));
        else vendor_package(Text$as_c_string(*name), editable);
    }
    return 0;
}

cli_command_t vendor_command = {
    .name = "vendor",
    .summary = "Copy packages' verified sources into ./vendor/",
    .spec_len = sizeof(vendor_spec) / sizeof(vendor_spec[0]),
    .spec = vendor_spec,
    .handler = cmd_vendor,
};
