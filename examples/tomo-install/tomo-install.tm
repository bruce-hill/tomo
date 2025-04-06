use shell
use patterns

_USAGE := "
    tomo-install file.tm...
"

_HELP := "
    tomo-install: a tool for installing libraries for the Tomo language
    Usage: $_USAGE
"

func find_urls(path:Path -> [Text]):
    urls : @[Text] = @[]
    if path.is_directory():
        for f in path.children():
            urls.insert_all(find_urls(f))
    else if path.is_file() and path.extension() == ".tm":
        for line in path.by_line()!:
            if captures := line.pattern_captures($Pat/use{space}{url}/) or line.pattern_captures($Pat/{id}{space}:={space}use{space}{url}/):
                urls.insert(captures[-1])
    return urls

func main(paths:[Path]):
    if paths.length == 0:
        paths = [(./)]

    urls := (++: find_urls(p) for p in paths) or []

    github_token := (~/.config/tomo/github-token).read()

    (~/.local/share/tomo/installed).create_directory()
    (~/.local/share/tomo/lib).create_directory()

    for url in urls:
        original_url := url
        url_without_protocol := url.trim_pattern($Pat"http{0-1 s}://", right=no)
        hash := $Shell@(echo -n @url_without_protocol | sha256sum).get_output()!.slice(to=32)
        if (~/.local/share/tomo/installed/$hash).is_directory():
            say("Already installed: $url")
            skip

        alias : Text? = none
        curl_flags := ["-L"]
        if github := url_without_protocol.pattern_captures($Pat"github.com/{!/}/{!/}#{..}"):
            user := github[1]
            repo := github[2]
            tag := github[3]
            url = "https://api.github.com/repos/$user/$repo/tarball/$tag"
            alias = "$(repo.without_prefix("tomo-")).$(tag).$(user)"
            if github_token:
                curl_flags ++= ["-H", "Authorization: Bearer $github_token"]
            curl_flags ++= [
                "-H", "Accept: application/vnd.github+json",
                "-H", "X-GitHub-Api-Version: 2022-11-28",
            ]

        (~/.local/share/tomo/downloads/$hash).create_directory()
        say($Shell@`
            set -euo pipefail
            cd ~/.local/share/tomo/downloads/@hash
            curl @curl_flags @url | tar xz -C ~/.local/share/tomo/installed --strip-components=1 --one-top-level=@hash
            echo @original_url > ~/.local/share/tomo/installed/@hash/source.url
            tomo -L ~/.local/share/tomo/installed/@hash
            if [ "`uname -s`" = "Darwin" ]; then
                ln -f -s ../installed/@hash/lib@hash.dylib ~/.local/share/tomo/lib/lib@hash.dylib
            else
                ln -f -s ../installed/@hash/lib@hash.so ~/.local/share/tomo/lib/lib@hash.so
            fi
        `.get_output()!)

        if alias:
            say($Shell(
                set -exuo pipefail
                ln -f -s @hash ~/.local/share/tomo/installed/@alias
                if [ "`uname -s`" = "Darwin" ]; then
                    ln -f -s lib@hash.dylib ~/.local/share/tomo/lib/lib@alias.dylib
                else
                    ln -f -s lib@hash.so ~/.local/share/tomo/lib/lib@alias.so
                fi
            ).get_output()!)
        
        say("$\[1]Installed $url!$\[]")
                
