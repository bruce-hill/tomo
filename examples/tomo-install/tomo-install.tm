use shell

_USAGE := "
    tomo-install file.tm...
"

_HELP := "
    tomo-install: a tool for installing libraries for the Tomo language
    Usage: $_USAGE
"

func find_urls(path:Path -> List(Text)):
    urls := @[:Text]
    if path:is_directory():
        for f in path:children():
            urls:insert_all(find_urls(f))
    else if path:is_file() and path:extension() == ".tm":
        for line in path:by_line()!:
            if m := line:matches($/use{space}{url}/) or line:matches($/{id}{space}:={space}use{space}{url}/):
                urls:insert(m[-1])
    return urls

func main(paths:List(Path)):
    if paths.length == 0:
        paths = [(./)]

    urls := (++: find_urls(p) for p in paths) or [:Text]

    github_token := (~/.config/tomo/github-token):read()

    (~/.local/share/tomo/installed):create_directory()
    (~/.local/share/tomo/lib):create_directory()

    for url in urls:
        original_url := url
        url_without_protocol := url:trim($|http{0-1 s}://|, trim_right=no)
        hash := $Shell@(echo -n @url_without_protocol | sha256sum):get_output()!:slice(to=32)
        if (~/.local/share/tomo/installed/$hash):is_directory():
            say("Already installed: $url")
            skip

        alias := none:Text
        curl_flags := ["-L"]
        if github := url_without_protocol:matches($|github.com/{!/}/{!/}#{..}|):
            user := github[1]
            repo := github[2]
            tag := github[3]
            url = "https://api.github.com/repos/$user/$repo/tarball/$tag"
            alias = "$(repo:trim($/tomo-/, trim_right=no)).$(tag).$(user)"
            if github_token:
                curl_flags ++= ["-H", "Authorization: Bearer $github_token"]
            curl_flags ++= [
                "-H", "Accept: application/vnd.github+json",
                "-H", "X-GitHub-Api-Version: 2022-11-28",
            ]

        (~/.local/share/tomo/downloads/$hash):create_directory()
        say($Shell@`
            set -euo pipefail
            cd ~/.local/share/tomo/downloads/@hash
            curl @curl_flags @url | tar xz -C ~/.local/share/tomo/installed --strip-components=1 --one-top-level=@hash
            echo @original_url > ~/.local/share/tomo/installed/@hash/source.url
            tomo -L ~/.local/share/tomo/installed/@hash
            ln -f -s ../installed/@hash/lib@hash.so ~/.local/share/tomo/lib/lib@hash.so
        `:get_output()!)

        if alias:
            say($Shell(
                set -exuo pipefail
                ln -f -s @hash ~/.local/share/tomo/installed/@alias
                ln -f -s lib@hash.so ~/.local/share/tomo/lib/lib@alias.so
            ):get_output()!)
        
        say("$\[1]Installed $url!$\[]")
                
