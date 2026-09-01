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

test "enumeration preserves the form of the path"
    # children(), files(), subdirectories(), each_child(), walk() and glob() used
    # to resolve their argument against the current directory before reading it,
    # so every path they handed back was absolute no matter which form it was
    # asked about. Only the syscalls need a real path now, so `~` is expanded for
    # those and the caller still gets back what they asked in.
    home_dir := (~/.tomo-test-form-XXXXXX).unique_directory()
    (home_dir ++ ./a.txt).write("")!
    (home_dir ++ ./sub).create_directory()!
    >> home_dir.components()[1]
    assert home_dir.components()[1] == "~"
    >> home_dir.children()
    # Child ordering is unspecified, so these compare sorted:
    assert home_dir.children().sorted() == [home_dir ++ ./a.txt, home_dir ++ ./sub].sorted()
    assert home_dir.files() == [home_dir ++ ./a.txt]
    assert home_dir.subdirectories() == [home_dir ++ ./sub]
    assert [c for c in home_dir.each_child()].sorted() == [home_dir ++ ./a.txt, home_dir ++ ./sub].sorted()
    assert [p for p in home_dir.walk()].sorted() == [home_dir, home_dir ++ ./a.txt, home_dir ++ ./sub].sorted()
    assert (home_dir ++ ./*.txt).glob() == [home_dir ++ ./a.txt]
    >> home_dir.remove()!

    rel_dir := (./tomo-test-form-XXXXXX).unique_directory()
    (rel_dir ++ ./a.txt).write("")!
    >> rel_dir.components()[1]
    assert rel_dir.components()[1] == "."
    >> rel_dir.children()
    assert rel_dir.children() == [rel_dir ++ ./a.txt]
    assert [p for p in rel_dir.walk()].sorted() == [rel_dir, rel_dir ++ ./a.txt].sorted()
    assert (rel_dir ++ ./*.txt).glob() == [rel_dir ++ ./a.txt]
    >> rel_dir.remove()!

    abs_dir := (/tmp/tomo-test-form-XXXXXX).unique_directory()
    (abs_dir ++ ./a.txt).write("")!
    assert abs_dir.children() == [abs_dir ++ ./a.txt]
    >> abs_dir.remove()!

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

test "path literals with non-ASCII characters"
    >> ./café.txt
    assert (./café.txt) == ./café.txt
    assert (./café.txt).base_name() == "café.txt"
    >> ~/日本語/naïve.txt
    assert (~/日本語/naïve.txt).base_name() == "naïve.txt"
