# Subcommand functions (`func main.foo(...)`) define CLI subcommands, but are
# also ordinary functions that can be called directly or used as values.

log : @[Text]

# Add files to the thing
func main.add(files:[Text], force|f:Bool=no)
    log.insert("add $(files[1]!) $force")

# Initialize submodules
func main.submodule.init(paths:[Text])
    log.insert("submodule.init $(paths[1]!)")

test "subcommand functions are directly callable"
    main.add(["a"], force=yes)
    assert log[log.length]! == "add a yes"
    main.add(["b"])
    assert log[log.length]! == "add b no"

test "nested subcommand functions are directly callable"
    main.submodule.init(["p"])
    assert log[log.length]! == "submodule.init p"

test "subcommand functions can be used as function values"
    fn := main.add
    fn(["c"], yes)
    assert log[log.length]! == "add c yes"

test "subcommand functions can't be assigned to"
    main.add = main.add
fails_compile "Subcommand functions are constants"

test "subcommand namespaces aren't values"
    _ := main.submodule
fails_compile "is a group of subcommands, not a value"

# A file can define a plain main() alongside subcommands: it runs when the
# first argument doesn't name a subcommand. It's also directly callable.
func main(word:Text="default")
    log.insert("main $word")

test "a plain main() can coexist with subcommands"
    main("x")
    assert log[log.length]! == "main x"
    main()
    assert log[log.length]! == "main default"
