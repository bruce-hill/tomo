// Logic for detecting whether console color should be used
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"
#include "color.h"

public bool USE_COLOR = true;

public void detect_color(void)
{
    USE_COLOR = getenv("COLOR") ? strcmp(getenv("COLOR"), "1") == 0 : isatty(STDOUT_FILENO);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
