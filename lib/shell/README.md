# Shell

This module defines a `lang` for running shell scripts:

```tomo
use shell_v1.0

>> $Shell"
    seq 5
    echo DONE
":get_output()
= "1$\n2$\n3$\n4$\n5$\nDONE"
```
