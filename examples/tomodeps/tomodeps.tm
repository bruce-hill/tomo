# Show a Tomo dependency graph

use patterns

_USAGE := "Usage: tomodeps <files...>"

_HELP := "
    tomodeps: Show a file dependency graph for Tomo source files.
    $_USAGE
"

enum Dependency(File(path:Path), Module(name:Text))

func _get_file_dependencies(file:Path -> |Dependency|)
    if not file.is_file()
        say("Could not read file: $file")
        return ||

    deps : @|Dependency|
    if lines := file.by_line()
        for line in lines
            if line.matches_pattern($Pat/use {..}.tm/)
                file_import := Path.from_text(line.replace_pattern($Pat/use {..}/, "@1")).resolved(relative_to=file)
                deps.add(Dependency.File(file_import))
            else if line.matches_pattern($Pat/use {id}/)
                module_name := line.replace_pattern($Pat/use {..}/, "@1")
                deps.add(Dependency.Module(module_name))
    return deps[]

func _build_dependency_graph(dep:Dependency, dependencies:@{Dependency=|Dependency|})
    return if dependencies.has(dep)

    dependencies[dep] = || # Placeholder

    dep_deps := when dep is File(path)
        _get_file_dependencies(path)
    is Module(module)
        dir := (~/.local/share/tomo/installed/$module)
        module_deps : @|Dependency|
        visited : @|Path|
        unvisited := @|f.resolved() for f in dir.files() if f.extension() == ".tm"|
        while unvisited.length > 0
            file := unvisited.items[-1]
            unvisited.remove(file)
            visited.add(file)

            for file_dep in _get_file_dependencies(file)
                when file_dep is File(f)
                    if not visited.has(f)
                        unvisited.add(f)
                is Module(m)
                    module_deps.add(file_dep)
        module_deps[]

    dependencies[dep] = dep_deps

    for dep2 in dep_deps
        _build_dependency_graph(dep2, dependencies)

func get_dependency_graph(dep:Dependency -> {Dependency=|Dependency|})
    graph : @{Dependency=|Dependency|}
    _build_dependency_graph(dep, graph)
    return graph

func _printable_name(dep:Dependency -> Text)
    when dep is Module(module)
        return "\[34;1]$module\[]"
    is File(f)
        f = f.relative_to((.))
        if f.exists()
            return Text(f)
        else
            return "\[31;1]$f (not found)\[]"

func _draw_tree(dep:Dependency, dependencies:{Dependency=|Dependency|}, already_printed:@|Dependency|, prefix="", is_last=yes)
    if already_printed.has(dep)
        say(prefix ++ (if is_last then "└── " else "├── ") ++ _printable_name(dep) ++ " \[2](recursive)\[]")
        return

    say(prefix ++ (if is_last then "└── " else "├── ") ++ _printable_name(dep))
    already_printed.add(dep)
    
    child_prefix := prefix ++ (if is_last then "    " else "│   ")
    
    children := dependencies[dep] or ||
    for i,child in children.items
        is_child_last := (i == children.length)
        _draw_tree(child, dependencies, already_printed, child_prefix, is_child_last)

func draw_tree(dep:Dependency, dependencies:{Dependency=|Dependency|})
    printed : @|Dependency|
    say(_printable_name(dep))
    printed.add(dep)
    deps := dependencies[dep] or ||
    for i,child in deps.items
        is_child_last := (i == deps.length)
        _draw_tree(child, dependencies, already_printed=printed, is_last=is_child_last)

func main(files:[Text])
    if files.length == 0
        exit("
            Please provide at least one file!
            $_USAGE
        ")

    for arg in files
        if arg.matches_pattern($Pat/{..}.tm/)
            path := Path.from_text(arg).resolved()
            dependencies := get_dependency_graph(File(path))
            draw_tree(File(path), dependencies)
        else if arg.matches_pattern($Pat/{id}/)
            dependencies := get_dependency_graph(Module(arg))
            draw_tree(Module(arg), dependencies)
        else
            say("\[2]Skipping $arg\[]")
            skip

