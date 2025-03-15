# Show a Tomo dependency graph

_USAGE := "Usage: tomodeps <files...>"

_HELP := "
    tomodeps: Show a file dependency graph for Tomo source files.
    $_USAGE
"

enum Dependency(File(path:Path), Module(name:Text))

func _get_file_dependencies(file:Path -> {Dependency}):
    if not file:is_file():
        !! Could not read file: $file
        return {:Dependency}

    deps := @{:Dependency}
    if lines := file:by_line():
        for line in lines:
            if line:matches($/use {..}.tm/):
                file_import := Path.from_text(line:replace($/use {..}/, "\1")):resolved(relative_to=file)
                deps:add(Dependency.File(file_import))
            else if line:matches($/use {id}/):
                module_name := line:replace($/use {..}/, "\1")
                deps:add(Dependency.Module(module_name))
    return deps[]

func _build_dependency_graph(dep:Dependency, dependencies:@{Dependency,{Dependency}}):
    return if dependencies:has(dep)

    dependencies[dep] = {:Dependency} # Placeholder

    dep_deps := when dep is File(path):
        _get_file_dependencies(path)
    is Module(module):
        dir := (~/.local/share/tomo/installed/$module)
        module_deps := @{:Dependency}
        visited := @{:Path}
        unvisited := @{f:resolved() for f in dir:files() if f:extension() == ".tm"}
        while unvisited.length > 0:
            file := unvisited.items[-1]
            unvisited:remove(file)
            visited:add(file)

            for file_dep in _get_file_dependencies(file):
                when file_dep is File(f):
                    if not visited:has(f):
                        unvisited:add(f)
                is Module(m):
                    module_deps:add(file_dep)
        module_deps[]

    dependencies[dep] = dep_deps

    for dep2 in dep_deps:
        _build_dependency_graph(dep2, dependencies)

func get_dependency_graph(dep:Dependency -> {Dependency,{Dependency}}):
    graph := @{:Dependency,{Dependency}}
    _build_dependency_graph(dep, graph)
    return graph

func _printable_name(dep:Dependency -> Text):
    when dep is Module(module):
        return "$(\x1b)[34;1m$module$(\x1b)[m"
    is File(f):
        f = f:relative()
        if f:exists():
            return Text(f)
        else:
            return "$(\x1b)[31;1m$(f) (not found)$(\x1b)[m"

func _draw_tree(dep:Dependency, dependencies:{Dependency,{Dependency}}, already_printed:@{Dependency}, prefix="", is_last=yes):
    if already_printed:has(dep):
        say(prefix ++ (if is_last: "└── " else: "├── ") ++ _printable_name(dep) ++ " $\x1b[2m(recursive)$\x1b[m")
        return

    say(prefix ++ (if is_last: "└── " else: "├── ") ++ _printable_name(dep))
    already_printed:add(dep)
    
    child_prefix := prefix ++ (if is_last: "    " else: "│   ")
    
    children := dependencies[dep] or {:Dependency}
    for i,child in children.items:
        is_child_last := (i == children.length)
        _draw_tree(child, dependencies, already_printed, child_prefix, is_child_last)

func draw_tree(dep:Dependency, dependencies:{Dependency,{Dependency}}):
    printed := @{:Dependency}
    say(_printable_name(dep))
    printed:add(dep)
    deps := dependencies[dep] or {:Dependency}
    for i,child in deps.items:
        is_child_last := (i == deps.length)
        _draw_tree(child, dependencies, already_printed=printed, is_last=is_child_last)

func main(files:[Text]):
    if files.length == 0:
        exit("
            Please provide at least one file!
            $_USAGE
        ")

    for arg in files:
        if arg:matches($/{..}.tm/):
            path := Path.from_text(arg):resolved()
            dependencies := get_dependency_graph(File(path))
            draw_tree(File(path), dependencies)
        else if arg:matches($/{id}/):
            dependencies := get_dependency_graph(Module(arg))
            draw_tree(Module(arg), dependencies)
        else:
            say("$\x1b[2mSkipping $arg$\x1b[m")
            skip

