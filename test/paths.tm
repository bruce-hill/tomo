# Tests for file paths
func main():
    >> (/):exists()
    = yes
    >> (~/):exists()
    = yes

    >> (~/Downloads/file(1).txt)
    = (~/Downloads/file(1).txt)

    >> (/half\)paren)
    = (/half\)paren)

    >> filename := "example.txt"
    >> (~):child(filename)
    = (~/example.txt)

    >> tmpdir := (/tmp/tomo-test-path-XXXXXX):unique_directory()
    >> (/tmp):subdirectories():has(tmpdir)
    = yes

    >> tmpfile := (tmpdir++(./one.txt))
    >> tmpfile:write("Hello world")
    >> tmpfile:append("!")
    >> tmpfile:read()
    = "Hello world!"?
    >> tmpfile:read_bytes()
    = [:Byte, 0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x21]?
    >> tmpdir:files():has(tmpfile)
    = yes

    if tmp_lines := tmpfile:by_line():
        >> [line for line in tmp_lines]
        = ["Hello world!"]
    else:
        fail("Couldn't read lines in $tmpfile")

    >> (./does-not-exist.xxx):read()
    = none : Text
    >> (./does-not-exist.xxx):read_bytes()
    = none : [Byte]
    if lines := (./does-not-exist.xxx):by_line():
        fail("I could read lines in a nonexistent file")
    else:
        pass

    >> tmpfile:remove()

    >> tmpdir:files():has(tmpfile)
    = no

    >> tmpdir:remove()

    >> p := (/foo/baz.x/qux.tar.gz)
    >> p:base_name()
    = "qux.tar.gz"
    >> p:parent()
    = (/foo/baz.x)
    >> p:extension()
    = "tar.gz"
    >> p:extension(full=no)
    = "gz"
    >> (~/.foo):extension()
    = ""
    >> (~/foo):extension()
    = ""

    >> (~/.foo.baz.qux):extension()
    = "baz.qux"

    >> (/):parent()
    = (/)
    >> (~/x/.):parent()
    = (~)
    >> (~/x):parent()
    = (~)
    >> (.):parent()
    = (..)
    >> (..):parent()
    = (../..)
    >> (../foo):parent()
    = (..)

    # Concatenation tests:
    !! Basic relative path concatenation:
    >> (/foo) ++ (./baz)
    = (/foo/baz)

    !! Concatenation with a current directory (`.`):
    >> (/foo/bar) ++ (./.)
    = (/foo/bar)

    !! Trailing slash in the first path:
    >> (/foo/) ++ (./baz)
    = (/foo/baz)

    !! Trailing slash in the second path:
    >> (/foo/bar) ++ (./baz/)
    = (/foo/bar/baz)

    !! Removing redundant current directory (`.`):
    >> (/foo/bar) ++ (./baz/./qux)
    = (/foo/bar/baz/qux)

    !! Removing redundant parent directory (`..`):
    >> (/foo/bar) ++ (./baz/qux/../quux)
    = (/foo/bar/baz/quux)

    !! Collapsing `..` to navigate up:
    >> (/foo/bar/baz) ++ (../qux)
    = (/foo/bar/qux)

    !! Current directory and parent directory mixed:
    >> (/foo/bar) ++ (././../baz)
    = (/foo/baz)

    !! Path begins with a `.`:
    >> (/foo) ++ (./baz/../qux)
    = (/foo/qux)

    !! Multiple slashes:
    >> (/foo) ++ (./baz//qux)
    = (/foo/baz/qux)

    !! Complex path with multiple `.` and `..`:
    >> (/foo/bar/baz) ++ (./.././qux/./../quux)
    = (/foo/bar/quux)

    !! Globbing:
    >> (./*.tm):glob()
