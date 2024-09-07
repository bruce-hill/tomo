# Show a Tomo dependency graph
use file

USAGE := "Usage: dependencies <files...>"

HELP := "
    dependencies: Show a file dependency graph for Tomo source files.
    $USAGE
"

func build_dependency_graph(filename:Text, dependencies:&{Text:@{Text}}, module=""):
    reader := when LineReader.from_file(filename) is Failure(msg):
        !! Failed: $msg
        return
    is Open(reader): reader

    key := if module: module else: filename
    if not dependencies:has(key):
        dependencies:set(key, @{:Text})

    while when reader:next_line() is Success(line):
        if line:matches($/use {..}.tm/):
            import := line:replace($/use {..}/, "\1")
            resolved := relative_path(resolve_path(import, filename))
            if resolved != "":
                import = resolved

            dependencies:get(key):add(import)

            if not dependencies:has(import):
                build_dependency_graph(import, dependencies)
        else if line:matches($/use {id}/):
            import := line:replace($/use {..}/, "\1")

            dependencies:get(key):add(import)
            files_path := resolve_path("~/.local/src/tomo/$import/lib$(import).files")
            if files_path == "":
                !! couldn't resolve: $files_path
            skip if files_path == ""
            when read(files_path) is Failure(msg):
                !! couldn't read: $files_path $msg
                skip
            is Success(files_content):
                for line in files_content:lines():
                    line_resolved := resolve_path(line, relative_to="~/.local/src/tomo/$import/")
                    skip if line_resolved == ""
                    build_dependency_graph(line_resolved, dependencies, module=import)

func get_dependency_graph(file:Text)->{Text:{Text}}:
    graph := {:Text:@{Text}}
    resolved := relative_path(file)
    build_dependency_graph(resolved, &graph)
    return {f:deps[] for f,deps in graph}

func draw_tree(file:Text, dependencies:{Text:{Text}}, already_printed:&{Text}, prefix="", is_last=yes):
    color_file := if file:matches($/{id}/):
        "$\x1b[34;1m$file$\x1b[m"
    else if resolve_path(file) != "":
        file
    else:
        "$\x1b[31;1m$file (could not resolve)$\x1b[m"

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
