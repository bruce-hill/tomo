# Tests for file paths

test "root paths exist"
    >> (/).exists()
    >> (~/).exists()
    assert (/).exists()
    assert (~/).exists()

test "path literals with special characters"
    >> (~/Downloads/file(1).txt)
    >> (/half\)paren)
    assert (~/Downloads/file(1).txt) == ~/Downloads/file(1).txt
    assert (/half\)paren) == /half\)paren

test "child paths"
    >> filename := "example.txt"
    >> (~).child(filename)
    assert (~).child(filename) == ~/example.txt

test "unique directory and subdirectories"
    >> tmpdir := (/tmp/tomo-test-path-XXXXXX).unique_directory()
    >> (/tmp).subdirectories().has(tmpdir)
    assert (/tmp).subdirectories().has(tmpdir)

test "optional path"
    >> optional_path : Path? = ./foo
    >> optional_path
    assert optional_path == ./foo

test "reading and writing files"
    >> tmpdir := (/tmp/tomo-test-path-XXXXXX).unique_directory()
    >> tmpfile := tmpdir ++ ./one.txt
    >> tmpfile.write("Hello world")!
    >> tmpfile.append("!")!
    >> tmpfile.read()
    >> tmpfile.read_bytes()!
    >> tmpdir.files().has(tmpfile)
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

    >> tmpdir.files().has(tmpfile)
    assert not tmpdir.files().has(tmpfile)

    >> tmpdir.remove()!

test "path components"
    >> p := /foo/baz.x/qux.tar.gz
    >> p.base_name()
    >> p.parent()
    >> p.extension()
    >> p.extension(full=no)
    assert p.base_name() == "qux.tar.gz"
    assert p.parent() == /foo/baz.x
    assert p.extension() == "tar.gz"
    assert p.extension(full=no) == "gz"
    assert p.has_extension("gz")
    assert p.has_extension(".gz")
    assert p.has_extension("tar.gz")
    assert not p.has_extension("txt")
    assert not p.has_extension("")
    assert (./foo).has_extension("")
    assert (..).has_extension("")
    assert not (~/.foo).has_extension("foo")
    assert (~/.foo).extension() == ""
    assert (~/foo).extension() == ""
    assert (~/.foo.baz.qux).extension() == "baz.qux"
    assert (~/x/.).parent() == (~)
    assert (~/x).parent() == (~)
    assert (.).parent() == (..)
    assert (..).parent() == (../..)
    assert (../foo).parent() == (..)
    assert (/).parent() == none

test "path concatenation"
    # Concatenation tests:
    assert /foo ++ ./baz == /foo/baz
    assert /foo/bar ++ . == /foo/bar
    assert /foo/ ++ ./baz == /foo/baz
    assert /foo/bar ++ ./baz/ == /foo/bar/baz
    assert /foo/bar ++ ./baz/./qux == /foo/bar/baz/qux
    assert /foo/bar ++ ./baz/qux/../quux == /foo/bar/baz/quux
    assert /foo/bar/baz ++ ../qux == /foo/bar/qux
    assert /foo/bar ++ ././../baz == /foo/baz
    assert /foo ++ ./baz/../qux == /foo/qux
    assert /foo ++ ./baz//qux == /foo/baz/qux
    assert /foo/bar/baz ++ ./.././qux/./../quux == /foo/bar/quux
    >> (./*.tm).glob()

test "a path literal at the end of the file parses without hanging"
	# parse_path's scan loop used to exit at the end-of-file boundary without
	# advancing, producing a zero-width AST that every enclosing parse loop
	# re-parsed forever: `x := 12..round()` hung the compiler. The trailing
	# path below sits against the end of this file to pin the fix.
	p := ..
	assert p == ..
