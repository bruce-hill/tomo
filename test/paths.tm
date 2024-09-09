# Tests for file paths
func main():
    >> (/):exists()
    = yes
    >> (~/):exists()
    = yes

    >> tmpdir := (/tmp/tomo-test-path-XXXXXX):unique_directory()
    >> (/tmp):subdirectories():has(tmpdir)
    = yes

    >> tmpfile := (tmpdir++(./one.txt))
    >> tmpfile:write("Hello world")
    >> tmpfile:append("!")
    >> tmpfile:read()
    = "Hello world!"
    >> tmpdir:files():has(tmpfile)
    = yes

    >> tmpfile:remove()

    >> tmpdir:files():has(tmpfile)
    = no

    >> tmpdir:remove()

