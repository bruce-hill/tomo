# Show dependency graph
use file

func build_dependency_graph(filename:Text, dependencies:&{Text:@{Text}}):
    reader := when LineReader.from_file(filename) is Failure(msg):
        return
    is Open(reader): reader

    while when reader:next_line() is Success(line):
        if line:matches($/{start}use {..}.tm/):
            import := line:replace($/{start}use {..}/, "\2")
            resolved := relative_path(resolve_path(import, filename))
            if resolved != "":
                import = resolved

            if not dependencies:has(filename):
                dependencies:set(filename, @{:Text})

            dependencies:get(filename):add(import)
            if not dependencies:has(import):
                build_dependency_graph(import, dependencies)

func get_dependency_graph(file:Text)->{Text:{Text}}:
    graph := {:Text:@{Text}}
    resolved := relative_path(file)
    build_dependency_graph(resolved, &graph)
    return {f:deps[] for f,deps in graph}

func draw_tree(file:Text, dependencies:{Text:{Text}}, already_printed:&{Text}, prefix="", is_last=yes):
    color_file := if resolve_path(file): file else: "$\x1b[31;1m$file$\x1b[m"


    if already_printed:has(file):
        say(prefix ++ (if is_last: "└── " else: "├── ") ++ color_file ++ " $\x1b[2m(recursive)$\x1b[m")
        return

    say(prefix ++ (if is_last: "└── " else: "├── ") ++ color_file)
    already_printed:add(file)
    
    child_prefix := prefix ++ (if is_last: "    " else: "│   ")
    
    children := dependencies:get(file, {:Text})
    for i,child in children.items:
        is_child_last := (i == children.length)
        draw_tree(child, dependencies, already_printed, child_prefix, is_child_last)

func main(files:[Text]):
    for f,file in files:
        if not file:matches($/{..}.tm/):
            say("$\x1b[2mSkipping $file$\x1b[m")
            skip

        printed := {:Text}
        resolved := relative_path(file)
        if resolved != "":
            file = resolved

        deps := get_dependency_graph(file)

        say(file)
        printed:add(file)
        children := deps:get(file, {:Text})
        for i,child in children.items:
            is_child_last := (i == children.length)
            draw_tree(child, deps, already_printed=&printed, is_last=is_child_last)
