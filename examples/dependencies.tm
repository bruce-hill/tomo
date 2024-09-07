# Show a Tomo dependency graph
use file

_USAGE := "Usage: dependencies <files...>"

_HELP := "
    dependencies: Show a file dependency graph for Tomo source files.
    $_USAGE
"

func get_module_imports_from_file(file:Text, imports:&{Text}, visited_files:&{Text}):
    return if visited_files:has(file)

    reader := when LineReader.from_file(file) is Failure(msg):
        !! Failed: $msg
        return
    is Open(reader): reader

    visited_files:add(file)

    while when reader:next_line() is Success(line):
        if line:matches($/use {..}.tm/):
            local_import := line:replace($/use {..}/, "\1")
            resolved := relative_path(resolve_path(local_import, file))
            if resolved != "":
                local_import = resolved
            get_module_imports_from_file(local_import, imports, visited_files)
        else if line:matches($/use {id}/):
            other_module := line:replace($/use {..}/, "\1")
            imports:add(other_module)

func get_module_imports_from_module(module:Text)->{Text}:
    files_path := resolve_path("~/.local/src/tomo/$module/lib$(module).files")
    if files_path == "":
        !! couldn't resolve: $files_path
        return {:Text}

    when read(files_path) is Failure(msg):
        !! couldn't read: $files_path $msg
        return {:Text}
    is Success(files_content):
        imports := {:Text}
        visited := {:Text}
        for line in files_content:lines():
            line_resolved := resolve_path(line, relative_to="~/.local/src/tomo/$module/")
            skip if line_resolved == ""
            get_module_imports_from_file(line_resolved, &imports, &visited)
        return imports

func build_module_dependency_graph(module:Text, dependencies:&{Text:@{Text}}):
    return if dependencies:has(module)

    module_deps := @{:Text}
    dependencies:set(module, module_deps)

    for dep in get_module_imports_from_module(module):
        module_deps:add(dep)
        build_module_dependency_graph(dep, dependencies)


func build_file_dependency_graph(filename:Text, dependencies:&{Text:@{Text}}):
    return if dependencies:has(filename)

    reader := when LineReader.from_file(filename) is Failure(msg):
        !! Failed: $msg
        return
    is Open(reader): reader

    file_deps := @{:Text}
    dependencies:set(filename, file_deps)

    while when reader:next_line() is Success(line):
        if line:matches($/use {..}.tm/):
            used_file := line:replace($/use {..}/, "\1")
            resolved := relative_path(resolve_path(used_file, filename))
            if resolved != "":
                used_file = resolved

            file_deps:add(used_file)
            build_file_dependency_graph(used_file, dependencies)
        else if line:matches($/use {id}/):
            module := line:replace($/use {..}/, "\1")
            file_deps:add(module)
            build_module_dependency_graph(module, dependencies)


func get_dependency_graph(file:Text)->{Text:{Text}}:
    graph := {:Text:@{Text}}
    resolved := relative_path(file)
    build_file_dependency_graph(resolved, &graph)
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
