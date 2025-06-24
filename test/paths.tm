# Tests for file paths
func main()
    >> (/).exists()
    = yes
    >> (~/).exists()
    = yes

    >> (~/Downloads/file(1).txt)
    = (~/Downloads/file(1).txt)

    >> (/half\)paren)
    = (/half\)paren)

    >> filename := "example.txt"
    >> (~).child(filename)
    = (~/example.txt)

    >> tmpdir := (/tmp/tomo-test-path-XXXXXX).unique_directory()
    >> (/tmp).subdirectories().has(tmpdir)
    = yes

    >> tmpfile := (tmpdir++(./one.txt))
    >> tmpfile.write("Hello world")
    >> tmpfile.append("!")
    >> tmpfile.read()
    = "Hello world!"?
    >> tmpfile.read_bytes()!
    = [0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x21]
    >> tmpdir.files().has(tmpfile)
    = yes

    if tmp_lines := tmpfile.by_line() then
        >> [line for line in tmp_lines]
        = ["Hello world!"]
    else
        fail("Couldn't read lines in $tmpfile")

    >> (./does-not-exist.xxx).read()
    = none
    >> (./does-not-exist.xxx).read_bytes()
    = none
    if lines := (./does-not-exist.xxx).by_line() then
        fail("I could read lines in a nonexistent file")
    else
        pass

    >> tmpfile.remove()

    >> tmpdir.files().has(tmpfile)
    = no

    >> tmpdir.remove()

    >> p := (/foo/baz.x/qux.tar.gz)
    >> p.base_name()
    = "qux.tar.gz"
    >> p.parent()
    = (/foo/baz.x)
    >> p.extension()
    = "tar.gz"
    >> p.extension(full=no)
    = "gz"
    >> p.has_extension("gz")
    = yes
    >> p.has_extension(".gz")
    = yes
    >> p.has_extension("tar.gz")
    = yes
    >> p.has_extension("txt")
    = no
    >> p.has_extension("")
    = no
    >> (./foo).has_extension("")
    = yes
    >> (..).has_extension("")
    = yes
    >> (~/.foo).has_extension("foo")
    = no
    >> (~/.foo).extension()
    = ""
    >> (~/foo).extension()
    = ""

    >> (~/.foo.baz.qux).extension()
    = "baz.qux"

    >> (/).parent()
    = (/)
    >> (~/x/.).parent()
    = (~)
    >> (~/x).parent()
    = (~)
    >> (.).parent()
    = (..)
    >> (..).parent()
    = (../..)
    >> (../foo).parent()
    = (..)

    # Concatenation tests:
    say("Basic relative path concatenation:")
    >> (/foo) ++ (./baz)
    = (/foo/baz)

    say("Concatenation with a current directory (`.`):")
    >> (/foo/bar) ++ (./.)
    = (/foo/bar)

    say("Trailing slash in the first path:")
    >> (/foo/) ++ (./baz)
    = (/foo/baz)

    say("Trailing slash in the second path:")
    >> (/foo/bar) ++ (./baz/)
    = (/foo/bar/baz)

    say("Removing redundant current directory (`.`):")
    >> (/foo/bar) ++ (./baz/./qux)
    = (/foo/bar/baz/qux)

    say("Removing redundant parent directory (`..`):")
    >> (/foo/bar) ++ (./baz/qux/../quux)
    = (/foo/bar/baz/quux)

    say("Collapsing `..` to navigate up:")
    >> (/foo/bar/baz) ++ (../qux)
    = (/foo/bar/qux)

    say("Current directory and parent directory mixed:")
    >> (/foo/bar) ++ (././../baz)
    = (/foo/baz)

    say("Path begins with a `.`:")
    >> (/foo) ++ (./baz/../qux)
    = (/foo/qux)

    say("Multiple slashes:")
    >> (/foo) ++ (./baz//qux)
    = (/foo/baz/qux)

    say("Complex path with multiple `.` and `..`:")
    >> (/foo/bar/baz) ++ (./.././qux/./../quux)
    = (/foo/bar/quux)

    say("Globbing:")
    >> (./*.tm).glob()
