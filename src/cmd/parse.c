// `tomo parse`: print the parse tree of Tomo files as S-expressions

#include "../ast.h"
#include "../parse/files.h"
#include "../stdlib/lists.h"
#include "common.h"
#include "commands.h"

static List_t files = EMPTY_LIST;

static cli_arg_t parse_spec[] = {
    {"files", &files, List$info(&Path$info), .positional = true, .required = true, .metavar = "file.tm",
     .description = "the files to parse"}, //
    VERBOSE_FLAG, //
};

static int cmd_parse(cli_command_t *self, List_t extra_args) {
    (void)extra_args;
    set_default_logs(0);
    files = normalize_tm_paths(files);
    if (files.length == 0) print_err("No files provided to parse!\n", self->usage);
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t path = *(Path_t *)(files.data + i * files.stride);
        ast_t *ast = parse_file(Path$as_c_string(path), NULL);
        print(ast_to_sexp_str(ast));
    }
    return 0;
}

cli_command_t parse_command = {
    .name = "parse",
    .summary = "Print the parse tree of Tomo files as S-expressions",
    .spec_len = sizeof(parse_spec) / sizeof(parse_spec[0]),
    .spec = parse_spec,
    .handler = cmd_parse,
};
