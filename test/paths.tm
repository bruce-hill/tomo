# Tests for file paths
func main()
    assert (/).exists()
    assert (~/).exists()

    assert (~/Downloads/file(1).txt) == (~/Downloads/file(1).txt)

    assert (/half\)paren) == (/half\)paren)

    >> filename := "example.txt"
    assert (~).child(filename) == (~/example.txt)

    >> tmpdir := (/tmp/tomo-test-path-XXXXXX).unique_directory()
    assert (/tmp).subdirectories().has(tmpdir)

    >> optional_path : Path? = (./foo)
    assert optional_path == (./foo)

    >> tmpfile := (tmpdir++(./one.txt))
    >> tmpfile.write("Hello world")!
    >> tmpfile.append("!")!
    assert tmpfile.read() == "Hello world!"
    assert tmpfile.read_bytes()! == [0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x21]
    assert tmpdir.files().has(tmpfile)

    if tmp_lines := tmpfile.by_line() then
        assert [line for line in tmp_lines] == ["Hello world!"]
    else
        fail("Couldn't read lines in $tmpfile")

    assert (./does-not-exist.xxx).read() == none
    assert (./does-not-exist.xxx).read_bytes() == none
    if (./does-not-exist.xxx).by_line()
        fail("I could read lines in a nonexistent file")
    else
        pass

    >> tmpfile.remove()!

    assert tmpdir.files().has(tmpfile) == no

    >> tmpdir.remove()!

    >> p := (/foo/baz.x/qux.tar.gz)
    assert p.base_name() == "qux.tar.gz"
    assert p.parent() == (/foo/baz.x)
    assert p.extension() == "tar.gz"
    assert p.extension(full=no) == "gz"
    assert p.has_extension("gz") == yes
    assert p.has_extension(".gz") == yes
    assert p.has_extension("tar.gz") == yes
    assert p.has_extension("txt") == no
    assert p.has_extension("") == no
    assert (./foo).has_extension("") == yes
    assert (..).has_extension("") == yes
    assert (~/.foo).has_extension("foo") == no
    assert (~/.foo).extension() == ""
    assert (~/foo).extension() == ""

    assert (~/.foo.baz.qux).extension() == "baz.qux"

    assert (~/x/.).parent() == (~)
    assert (~/x).parent() == (~)
    assert (.).parent() == (..)
    assert (..).parent() == (../..)
    assert (../foo).parent() == (..)
    assert (/).parent() == none

    # Concatenation tests:
    say("Basic relative path concatenation:")
    assert (/foo) ++ (./baz) == (/foo/baz)

    say("Concatenation with a current directory (`.`):")
    assert (/foo/bar) ++ (./.) == (/foo/bar)

    say("Trailing slash in the first path:")
    assert (/foo/) ++ (./baz) == (/foo/baz)

    say("Trailing slash in the second path:")
    assert (/foo/bar) ++ (./baz/) == (/foo/bar/baz)

    say("Removing redundant current directory (`.`):")
    assert (/foo/bar) ++ (./baz/./qux) == (/foo/bar/baz/qux)

    say("Removing redundant parent directory (`..`):")
    assert (/foo/bar) ++ (./baz/qux/../quux) == (/foo/bar/baz/quux)

    say("Collapsing `..` to navigate up:")
    assert (/foo/bar/baz) ++ (../qux) == (/foo/bar/qux)

    say("Current directory and parent directory mixed:")
    assert (/foo/bar) ++ (././../baz) == (/foo/baz)

    say("Path begins with a `.`:")
    assert (/foo) ++ (./baz/../qux) == (/foo/qux)

    say("Multiple slashes:")
    assert (/foo) ++ (./baz//qux) == (/foo/baz/qux)

    say("Complex path with multiple `.` and `..`:")
    assert (/foo/bar/baz) ++ (./.././qux/./../quux) == (/foo/bar/quux)

    say("Globbing:")
    >> (./*.tm).glob()

    assert (./foo).RelativePath
    assert (/foo).AbsolutePath
    assert (~/foo).HomePath
    assert (/foo/baz).components() == ["foo", "baz"]
    assert Path.RelativePath(["foo", "baz"]) == (./foo/baz)
