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
    >> tmpdir := (/tmp/tomo-test-path-XXXXXX).unique_directory()!
    >> (/tmp).subdirectories()!.has(tmpdir)
    assert (/tmp).subdirectories()!.has(tmpdir)

test "optional path"
    >> optional_path : Path? = ./foo
    >> optional_path
    assert optional_path == ./foo

test "reading and writing files"
    >> tmpdir := (/tmp/tomo-test-path-XXXXXX).unique_directory()!
    >> tmpfile := tmpdir ++ ./one.txt
    >> tmpfile.write("Hello world")!
    >> tmpfile.append("!")!
    >> tmpfile.read()
    >> tmpfile.read_bytes()!
    >> tmpdir.files()!.has(tmpfile)
    assert tmpfile.read() == "Hello world!"
    assert tmpfile.read_bytes()! == [0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64, 0x21]
    assert tmpdir.files()!.has(tmpfile)

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

    >> tmpdir.files()!.has(tmpfile)
    assert not tmpdir.files()!.has(tmpfile)

    >> tmpdir.remove()!

test "enumeration preserves the form of the path"
    # children(), files(), subdirectories(), each_child(), walk() and glob() used
    # to resolve their argument against the current directory before reading it,
    # so every path they handed back was absolute no matter which form it was
    # asked about. Only the syscalls need a real path now, so `~` is expanded for
    # those and the caller still gets back what they asked in.
    home_dir := (~/.tomo-test-form-XXXXXX).unique_directory()!
    (home_dir ++ ./a.txt).write("")!
    (home_dir ++ ./sub).create_directory()!
    >> home_dir.components()![1]
    assert home_dir.components()![1] == "~"
    >> home_dir.children()
    # Child ordering is unspecified, so these compare sorted:
    assert home_dir.children()!.sorted() == [home_dir ++ ./a.txt, home_dir ++ ./sub].sorted()
    assert home_dir.files()! == [home_dir ++ ./a.txt]
    assert home_dir.subdirectories()! == [home_dir ++ ./sub]
    assert [c for c in home_dir.each_child()!].sorted() == [home_dir ++ ./a.txt, home_dir ++ ./sub].sorted()
    assert [p for p in home_dir.walk()!].sorted() == [home_dir, home_dir ++ ./a.txt, home_dir ++ ./sub].sorted()
    assert home_dir.glob("*.txt")! == [home_dir ++ ./a.txt]
    >> home_dir.remove()!

    rel_dir := (./tomo-test-form-XXXXXX).unique_directory()!
    (rel_dir ++ ./a.txt).write("")!
    >> rel_dir.components()![1]
    assert rel_dir.components()![1] == "."
    >> rel_dir.children()
    assert rel_dir.children()! == [rel_dir ++ ./a.txt]
    assert [p for p in rel_dir.walk()!].sorted() == [rel_dir, rel_dir ++ ./a.txt].sorted()
    assert rel_dir.glob("*.txt")! == [rel_dir ++ ./a.txt]
    >> rel_dir.remove()!

    abs_dir := (/tmp/tomo-test-form-XXXXXX).unique_directory()!
    (abs_dir ++ ./a.txt).write("")!
    assert abs_dir.children()! == [abs_dir ++ ./a.txt]
    >> abs_dir.remove()!

test "enumerating a directory that isn't there gives none"
    # These were typed as non-optional while the C returned a none sentinel:
    # children() surfaced as an empty list, indistinguishable from a real empty
    # directory, and each_child() surfaced as a null closure that segfaulted the
    # moment the loop called it.
    missing := (./tomo-no-such-directory)
    assert not missing.exists()
    assert missing.children() == none
    assert missing.files() == none
    assert missing.subdirectories() == none
    assert missing.each_child() == none

    # ...but a directory that exists and is empty is still an empty list:
    empty_dir := (/tmp/tomo-test-empty-XXXXXX).unique_directory()!
    >> empty_dir.children()
    assert empty_dir.children()! == []
    assert empty_dir.files()! == []
    assert empty_dir.subdirectories()! == []
    assert [c for c in empty_dir.each_child()!] == []
    >> empty_dir.remove()!

test "move refuses to overwrite unless asked, and understands ~"
    # Path$move called rename(2) directly, which replaces the destination
    # without complaint, so `overwrite=no` -- the default -- did not prevent
    # anything: moving onto an existing file destroyed it and reported Success.
    # It also passed the path to rename(2) unexpanded, so a "~" path failed
    # with ENOENT even though .exists() said otherwise.
    dir := (/tmp/tomo-test-move-XXXXXX).unique_directory()!
    (dir ++ ./src.txt).write("SOURCE")!
    (dir ++ ./dest.txt).write("DESTINATION")!

    >> (dir ++ ./src.txt).move(dir ++ ./dest.txt)
    assert (dir ++ ./dest.txt).read()! == "DESTINATION", "the destination was clobbered"
    assert (dir ++ ./src.txt).exists(), "the source was consumed by a move that failed"

    assert (dir ++ ./src.txt).move(dir ++ ./dest.txt, overwrite=yes).Success
    assert (dir ++ ./dest.txt).read()! == "SOURCE"

    # Home-based paths reach rename(2) expanded now:
    home_src := (~/.tomo-test-move-XXXXXX).unique_directory()!
    home_dest := home_src.sibling("tomo-test-move-destination")!
    assert home_src.move(home_dest).Success
    assert home_dest.is_directory()
    assert not home_src.exists()
    >> home_dest.remove()!
    >> dir.remove()!

test "set_owner leaves out what it is not given"
    # The none checks were inverted: passing an owner skipped the lookup and
    # left the uid as -1, while passing none looked up the empty user name, so
    # every call failed with "Not a valid user: ".
    dir := (/tmp/tomo-test-owner-XXXXXX).unique_directory()!
    file := dir ++ ./f.txt
    file.write("")!
    assert file.set_owner().Success
    if group := file.group()
        assert file.set_owner(group=group).Success
    assert not file.set_owner(owner="tomo-no-such-user").Success
    >> dir.remove()!

test "unique paths do not alias each other"
    # unique_directory() and write_unique() built their name in a static
    # buffer and handed it to Path$from_str(), which does not copy, so every
    # result pointed at the same storage: making a second temporary directory
    # silently rewrote the path of the first.
    a := (/tmp/tomo-test-alias-a-XXXXXX).unique_directory()!
    b := (/tmp/tomo-test-alias-b-XXXXXX).unique_directory()!
    >> a
    >> b
    assert a != b, "the first temporary directory was overwritten by the second"
    assert a.is_directory() and b.is_directory()
    >> a.remove()!
    >> b.remove()!

    f := (/tmp/tomo-test-alias-f-XXXXXX.txt).write_unique("first")!
    g := (/tmp/tomo-test-alias-g-XXXXXX.txt).write_unique("second")!
    assert f != g, "the first unique file was overwritten by the second"
    assert f.read()! == "first"
    assert g.read()! == "second"
    >> f.remove()!
    >> g.remove()!

test "the root has no parent and so no siblings"
    # sibling() appended "/../name" and normalized, so (/).sibling("x") gave
    # (/x) while (/).parent() gave none: the root had a sibling but nothing to
    # be a sibling of. It is written as the .parent().child(name) it is
    # documented to be now, so the two agree.
    assert (/).parent() == none
    assert (/).sibling("x") == none
    assert (/foo/baz).sibling("doop") == (/foo/doop)
    assert (/foo).sibling("bar") == (/bar)
    # It really is parent-then-child, for every form:
    for p in [(/foo/baz), (./foo/baz), (~/foo/baz), (../baz)]
        assert p.sibling("doop") == p.parent()!.child("doop")

    # "/.." is "/", so climbing past the root stays there:
    assert ((/) ++ (../..)) == (/)
    assert ((/foo/bar) ++ (../../..)) == (/)
    # ...while a relative path keeps climbing, having no root to stop at:
    assert ((..) ++ (..)) == (../..)

test "relative_to and resolved agree about the current directory"
    # relative_to() returned an empty path when the two sides matched, because
    # normalize_inplace()'s "nothing was written" fallback compared a pointer it
    # had already incremented and so never fired. An empty path is one the type
    # cannot otherwise hold: Path$from_str() maps "" to (/).
    assert (/tmp).relative_to((/tmp)) == (.)
    assert (/).relative_to((/)) == (.)

    # resolved() concatenated onto its base without making the base absolute,
    # so with the default base of (./) it returned a relative path despite its
    # name and its documented return value.
    assert (./foo).resolved() == Path.current_dir().child("foo")
    assert (.).resolved() == Path.current_dir()
    assert (./a/b).resolved(relative_to=(/tmp)) == (/tmp/a/b)

test "walking something that isn't there gives none"
    # walk() handed back an iterator over the path itself even when nothing was
    # at that path, so an empty tree and a missing one looked the same. It is
    # optional now, like each_child().
    assert (./tomo-no-such-path).walk() == none
    assert (.).walk() != none

    # A path that exists but is not a directory still walks over just itself:
    dir := (/tmp/tomo-test-walk-XXXXXX).unique_directory()!
    file := dir ++ ./only.txt
    file.write("")!
    assert [p for p in file.walk()!] == [file]
    assert [p for p in dir.walk()!].sorted() == [dir, file].sorted()
    >> dir.remove()!

test "filesystem operations do not race another process"
    # These used to look before they acted, which is a window another process
    # can change the answer in. move() now asks the kernel to rename only if
    # the destination does not exist, in one operation, and the rest either
    # attempt the act and read the error, or confirm what they found.
    dir := (/tmp/tomo-test-race-XXXXXX).unique_directory()!
    (dir ++ ./src.txt).write("SOURCE")!
    (dir ++ ./dest.txt).write("DESTINATION")!

    # move() and copy_to() both decline, and neither touches the destination:
    assert not (dir ++ ./src.txt).move(dir ++ ./dest.txt).Success
    assert not (dir ++ ./src.txt).copy_to(dir ++ ./dest.txt).Success
    assert (dir ++ ./dest.txt).read()! == "DESTINATION"
    assert (dir ++ ./src.txt).read()! == "SOURCE"

    # ...and overwrite=yes still replaces a file or a non-empty directory:
    assert (dir ++ ./src.txt).copy_to(dir ++ ./dest.txt, overwrite=yes).Success
    assert (dir ++ ./dest.txt).read()! == "SOURCE"
    (dir ++ ./d1).create_directory()!
    (dir ++ ./d2).create_directory()!
    (dir ++ ./d2/inner.txt).write("x")!
    assert (dir ++ ./d1).move(dir ++ ./d2, overwrite=yes).Success

    # create_directory() treats EEXIST as success, which is the race-free way
    # to create one, but only a directory counts:
    (dir ++ ./afile).write("x")!
    assert not (dir ++ ./afile).create_directory().Success
    assert (dir ++ ./realdir).create_directory().Success
    assert (dir ++ ./realdir).create_directory().Success # Already there is fine

    # remove() attempts the removal rather than asking what is there first:
    assert not (dir ++ ./gone).remove().Success
    assert (dir ++ ./gone).remove(ignore_missing=yes).Success
    >> dir.remove()!

test "a recursive remove reports failure instead of aborting"
    # nftw(3) passes no user data to its callback, so the callback used to
    # fail(), which exits the process: one unremovable file partway through a
    # recursive delete killed the program instead of returning Failure. It
    # leaves the reason in a _Thread_local for Path$remove() to collect now.
    dir := (/tmp/tomo-test-rm-XXXXXX).unique_directory()!
    (dir ++ ./sub/deeper).create_directory()!
    (dir ++ ./top.txt).write("")!
    (dir ++ ./sub/mid.txt).write("")!
    (dir ++ ./sub/deeper/leaf.txt).write("")!
    assert dir.remove().Success
    assert not dir.exists()

    # A missing path is a Failure the caller can act on, not an exit:
    gone := (/tmp/tomo-test-rm-not-there)
    assert not gone.remove().Success
    assert gone.remove(ignore_missing=yes).Success
    say("still running after a failed remove")

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
    # No "." in the name at all means no extension, which is none, not "":
    assert (~/.foo).extension() == none
    assert (~/foo).extension() == none
    assert (./foo.).extension() == none
    assert (..).extension() == none
    assert (~/.foo.baz.qux).extension() == "baz.qux"
    assert (~/x/.).parent() == (~)
    assert (~/x).parent() == (~)
    assert (.).parent() == (..)
    assert (..).parent() == (../..)
    assert (../foo).parent() == (..)
    assert (/).parent() == none

test "extension() and has_extension(\"\") agree"
    # For any name that is valid UTF-8, "the extension is none" and "it has no
    # extension" have to be the same question. The root used to answer no to
    # has_extension("") while reporting no extension.
    for p in [(./foo), (./foo.txt), (./.git), (./foo.), (./a.tar.gz), (..), (.), (/), (~), (~/x.y)]
        no_ext := p.has_extension("")
        assert (p.extension() == none) == no_ext,
            "$p: extension() is $(p.extension()) but has_extension of an empty text is $no_ext"

test "matches_glob matches path components from the back"
    # This used to be a single fnmatch() against the path's whole text, so
    # "*.txt" did not match (./file.txt) -- the leading "./" had to be written
    # out -- and the same file matched differently depending on whether the
    # path was written relative, absolute, or home-based. The pattern is now
    # split on "/" and matched against a trailing run of components.
    assert (./file.txt).matches_glob("*.txt")
    assert (/tmp/dir/file.txt).matches_glob("*.txt")
    assert (~/dir/file.txt).matches_glob("*.txt")
    assert not (./file.txt).matches_glob("*.jpg")

    # More components match a longer suffix, and depth is exact:
    assert (./dir/file.txt).matches_glob("dir/*.txt")
    assert (./dir/file.txt).matches_glob("./dir/*.txt")
    assert not (./dir/sub/file.txt).matches_glob("dir/*.txt")

    # A leading "/" can only line up at the front, so it anchors:
    assert (/tmp/dir/file.txt).matches_glob("/tmp/dir/*.txt")
    assert not (/other/dir/file.txt).matches_glob("/tmp/dir/*.txt")
    assert not (./dir/file.txt).matches_glob("/dir/*.txt")

    # "**" is zero or more components:
    assert (./a/b/c/file.txt).matches_glob("**/*.txt")
    assert (./file.txt).matches_glob("**/*.txt")
    assert not (./a/file.jpg).matches_glob("**/*.txt")
    assert (./a/b/file.txt).matches_glob("a/**/*.txt")
    assert (./a/file.txt).matches_glob("a/**/*.txt")
    assert (./anything/at/all).matches_glob("**")
    assert (/tmp/x/y).matches_glob("/tmp/**")
    assert not (/other/y).matches_glob("/tmp/**")

    # Wildcards stay inside one component, and a leading "." is literal:
    assert not (./dir/.hidden).matches_glob("*")
    assert (./dir/.hidden).matches_glob(".*")
    assert (./src/file.c).matches_glob("*.[ch]")
    assert not (./src/file.o).matches_glob("*.[ch]")

    # Still no brace alternation, and an empty pattern matches nothing:
    assert not (./src/file.c).matches_glob("*.{c,h}")
    assert not (./src/file.c).matches_glob("")

test "everything glob() returns matches the pattern it came from"
    # The two used to disagree: glob() would hand back (./dir/a.txt) for the
    # pattern (./dir/*.txt), which matches_glob then said did not match.
    dir := (/tmp/tomo-test-glob-XXXXXX).unique_directory()!
    (dir ++ ./a.txt).write("")!
    (dir ++ ./b.txt).write("")!
    (dir ++ ./c.jpg).write("")!
    (dir ++ ./sub).create_directory()!
    (dir ++ ./sub/d.txt).write("")!

    for pattern in ["*.txt", "*", "*.[tj]*", "sub/*.txt"]
        matches := dir.glob(pattern)!
        for f in matches
            assert f.matches_glob(pattern),
                "glob() returned $f for $pattern but matches_glob disagrees"

    # And the bare name matches wherever the file sits:
    for f in dir.glob("sub/*.txt")!
        assert f.matches_glob("*.txt")
        assert f.matches_glob("**/*.txt")
        assert f.matches_glob("sub/*.txt")

    # "**" is answered by walking rather than by glob(3), which cannot express
    # it, so the two branches have to agree with the matcher equally:
    deep := dir.glob("**/*.txt")!
    assert deep.length == 3, "expected a.txt, b.txt and sub/d.txt, got $deep"
    for f in deep
        assert f.matches_glob("**/*.txt"), "$f does not match the pattern it came from"
    assert dir.glob("**/*.jpg")! == [dir ++ ./c.jpg]

    # A directory that cannot be read is none, not an empty list:
    assert (./tomo-no-such-directory).glob("*") == none
    # ...while a pattern that matches nothing is an empty list:
    assert dir.glob("*.nothing")! == []

    >> dir.remove()!

test "paths are normalized however they are built"
    # Path literals were left exactly as written, so (./path/to/) and
    # (./path/to) were different values naming the same directory, and
    # (/foo/../bar) stayed unresolved. A literal has no interpolation, so its
    # text is entirely known while compiling: it is normalized there, and the
    # generated code carries the result rather than redoing it on every run.
    assert (/foo/../bar) == (/bar)
    assert (./path/to/) == (./path/to)
    assert (./a/./b) == (./a/b)
    assert (/a//b) == (/a/b)
    assert (~/x/../y) == (~/y)

    # The forms that mean themselves are untouched:
    assert "$(.)" == "."
    assert "$(..)" == ".."
    assert "$(/)" == "/"
    assert "$(~)" == "~"
    # ...and "~" is a stand-in for an unknown directory, so nothing pops it:
    assert "$((~).parent()!)" == "~/.."

    # Text is not known until it is run, so that normalizes when it is built:
    assert Path.from_text("./a/../b") == (./b)
    assert (.).child("a/../b") == (./b)
    assert ((/tmp) ++ (./a/../b)) == (/tmp/b)

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
    >> (.).glob("*.tm")

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
