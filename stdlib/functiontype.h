#pragma once

// Logic for handling function type values

void register_function(void *fn, Text_t name);
Text_t *get_function_name(void *fn);
Text_t Func$as_text(const void *fn, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
