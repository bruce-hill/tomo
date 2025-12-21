#/bin/sh
awk '
    BEGIN { printf "const char *'$1' = " }
    {
        gsub(/\\/,"\\\\");
        gsub(/"/,"\\\"");
        printf "\"%s\\n\"\n", $0
    }
    END { print ";" }
'
