# Tomo Package Design

In Tomo, the `use` statement serves multiple purposes. The first purpose is for
[local imports](local-imports.md). However, the more complicated case is for
package imports. Packages in Tomo are a bundle of source files that define
their own namespace and symbols that can be used by other code.

When you type

```tomo
use mypackage
```

Tomo will try to figure out where to get the package, download it from there
(with user confirmation), check its authenticity, build the package, and link
your code against it.

## Using Packages

In a particular project, there may be many files that want to use a package.
Each file that uses the package must include `use packagename` or `alias := use
packagename` at the top level of the file.

If a package is used without an alias, all the package's symbols are visible
within the file. This is the equivalent of `from packagename import *` in a
language like Python. If an alias is used, then all the package's symbols must
be accessed through the alias, like `alias.foo()`.

## Installing Packages

When you put `use foo` in a Tomo source file, Tomo needs to know where to
find `foo`. In order to find it, Tomo will search through the following `.ini`
files in order:

1. `filename.tm:packages.ini`: A per-file package config, so `foo.tm` would
   have `foo.tm:packages.ini`. This should only rarely be used.
2. `packages.ini`: A package config that is shared for every file in a
   directory.
3. A system-wide `packages.ini` distributed with the Tomo compiler (default
   location: `~/.local/lib/tomo@version/packages.ini`).

The format of a `packages.ini` file looks like this:

```ini
[packagename]
digest=sha256:2551b5ecc2617d884d856764a7f4f5ba394e97aaf1412110fe63f137b510f546
source=https://example.com/tomo/package-v1.2.3.tar.gz
source-2=https://mirror.example.com/tomo/package-v1.2.3.tar.gz
source-3=https://mirror2.example.com/tomo/package-v1.2.3.tar.gz
```

The important features are:

1. The package name in square brackets. This is used to map `use` statements to
   the corresponding package.
2. A hash digest (optional). This is a hash value used to check package
   integrity when installing a package. See [Package Digests](#Package-Digests)
   for more info.
3. One or more sources. See [Package Sources](#Package-Sources) for more info.

Each key/value pair is separated by an equals sign. Additional fields may be
provided, and are ignored.

## Package Digests

A package is canonically defined by its digest. The digest is a cryptographic
hash digest of the archived source file for the package. The default digest
algorithm used is SHA-256. When you put a digest in your `packages.ini` file,
you define **precisely** what code you are depending on. Tomo does not have a
notion of package versioning or package registries other than the digest.
However, it is easy to play nicely with whichever versioning and distribution
system you prefer to use.

You do not need to manually compute your own digests. When adding a project
dependency, you may list a package with just a source and no digest. Running
the project with `tomo` will cause Tomo to download the package and save its
digest to `packages.ini` so you can be sure future users will get the same
version. You can think of this as a combined package file and lockfile. See
[Package Sources](#Package-Sources) for more information.

The philosophy of package digests is rooted in reproducibility and security.
Because Tomo does not have a centrally managed package repository and does not
plan to build one, we need a way for someone to publish a piece of Tomo code
that relies on a package and have anyone be able to build that code. The code
should build as consistently as possible over time, with no breaking changes or
issues with local version incompatibilities. If the author of a piece of code
knows that the code works with the package whose source code hash is `X`, then
all users can be confident that if they have a package whose source code has is
also `X`, then it can be used without issues, regardless of where they got it.


## The Package Store

Installed packages are content-addressed: each project keeps its own store of
them in `.build/store/<package-digest>/`, next to the project's source files.
The names things call packages by are symlinks: whenever a `use name` resolves,
Tomo records a `packages/name` binding link next to the consumer -- in the
project's `.build/packages/` directory, pointing at `../store/<digest>`, or,
for a package inside the store using another package, in that store entry's own
`packages/` directory, pointing at `../../<digest>`. Every level of the
dependency graph thus resolves the same way (`./packages/name`, one level at a
time), the links are all relative (so a project directory is fully
relocatable), and binding names stay scoped to their consumer: two projects (or
two packages) can bind the same name to different versions without collision.

Because the store is per-project, deleting `.build` removes everything, no
compile ever writes outside your project directory, and upgrading Tomo never
orphans your installed packages.

To avoid re-downloading the same archives for every project, verified source
archives are also cached globally in `$XDG_CACHE_HOME/tomo/<digest>/`
(`~/.cache/tomo/` by default), keyed by digest. Installing a package checks the
project store first, then the cache (re-verifying the digest), and only then
downloads. The cache can be deleted at any time.

After each successful build, unused package state is garbage collected: any
`.build/packages/` binding link or `.build/store/` entry that no `.tm` file in
the directory (transitively) uses anymore is removed, and the corresponding
`packages.ini` entry is annotated with `unused=true` (the marker is removed
again if the package comes back into use). The pins themselves and any
`vendor/` directories are never touched, so re-adding a `use` restores the
package from the download cache without any network access -- and the
`unused=true` markers make it easy to spot pins that could be deleted.

Compiled executables embed a zip of everything needed to rebuild them: the
program's sources, its `packages.ini` pins, license texts, and the full sources
of every package linked in. `tomo --extract-source <program>` unpacks that into
a `<program>-source/` directory with exactly the shape of a working project --
including a pre-seeded `.build/store/` and its binding links -- so an extracted
tree rebuilds as-is, offline, with no edits.

## Package Sources

When you run a piece of Tomo code for the first time, Tomo will look to see if
you have the necessary packages installed in the project's package store. If
not, and no cached archive matches the pinned digest, then Tomo will look for
sources listed in your `packages.ini` files. Sources are enumerated as
`source`, `source-2`, `source-3`, and so on. Each will be checked in order to
find a valid version of the package.

For each source, Tomo will perform the following steps:

1. A source archive is downloaded from the given source:
  a. If the source is a file path (e.g. `./mypackage.tar.gz`), then the archive
     from the local filesystem is used.
  b. If the source is a URI (e.g. `https://example.com/mypackage-v1.2.3.tar.gz`
     or `ftp://example.com/...`), then [cURL](https://curl.se/) is used to
     download the archive file to a temporary directory. Tomo will prompt for
     user confirmation before downloading the file.
2. If a digest is provided in your `packages.ini`, then the archive file's
   digest is computed and compared to the digest in `packages.ini`. If there is
   any mismatch, an error will be raised and the process will exit. If you
   experience a digest mismatch, you should consider removing the source that
   provided it from your sources list, as it may be compromised.
3. If a digest is not provided in your `packages.ini`, Tomo will compute a
   digest for the newly downloaded file and save it to your `packages.ini` file
   so that all future compilations will know what the digest must be.
4. The source archive will be extracted to the project's package store
   (`.build/store/<package-digest>`), compiled with `tomo -p`, and the verified
   archive will be saved to the global download cache.

Package sources are not tied to any single distribution channel by design. You
can host your packages on GitHub, BitBucket, GitLab, your own personal
webserver, a cloud bucket, an FTP server in your closet, a vendored archive in
your repository, a floppy disk, whatever you like. For your end users, the
experience should be seamless, without being tied to a single point of failure
or tech monopoly. If one of your sources becomes unavailable, Tomo will
continue down the line, trying each source until it gets one that can provide
a source archive with the right hash.

### Vendoring Packages

`tomo --vendor <name>` copies a pinned package's digest-verified source
archive from the download cache into your project's `vendor/` directory and
rewrites the package's `./packages.ini` entry to use it as the primary source
(demoting the previous sources to fallbacks, and keeping the digest pin, which
still verifies -- it's the same bytes):

```ini
[foo]
digest=sha256:2551b5...
source=./vendor/foo-v1.2.tar.gz
source-2=https://example.com/foo-v1.2.tar.gz
```

Committing `vendor/` makes the repository self-sufficient: fresh checkouts
build with no network access, still fully digest-verified. If the package
isn't pinned in `./packages.ini` yet, its entry is copied in from the
compiler's default pins.

`tomo --vendor-editable <name>` instead extracts the package's sources into
`vendor/<name>/` and points the entry at that directory, dropping the digest
pin (directory sources aren't digested). Use this when you need to patch a
dependency: edits to the vendored sources are picked up on the next build.

### Local Directory Packages

As a special case, Tomo also permits using local directories as sources. If a
source is listed as a file path to a local directory (instead of an archive
file), Tomo will use the code in that directory for the package without saving
a digest or copying the package into the project's store. This makes it
possible to easily vendor a dependency (e.g. with git submodules).

```ini
[mypackage]
source = ./vendor/mypackage
```

If a digest is provided for a local directory package, Tomo will give you an
error, because digests can only be computed for files, not directories. When
compiling a package, Tomo produces build files which would affect the hash
digest of the directory, making it difficult to compute a correct hash.
Additionally, it is much better as a developer to be able to make local edits
to a vendored package without having to worry about digest mismatches.

## Package Versioning

Tomo's package design is deliberately version-agnostic. If you want to use a
versioning system for distributing a package (a very reasonable goal), then
the recommended practice is to use the source URL to convey versioning:

```ini
[mylib]
source = https://example.com/mypackage/mypackage-v1.2.3.tar.gz
source-2 = https://mirror.example.com/mypackage/mypackage-v1.2.3.tar.gz
```

To upgrade a dependency, simply delete the `digest` from your `packages.ini`,
update the source URL, and rebuild. Tomo will update the digest for you. In
this way, you may use any versioning system you like, not limited to semantic
versioning.

When you upgrade a dependency and rebuild, Tomo will pull in the upgraded
version of the dependency's source code, including its `packages.ini` files,
which will trigger a chain reaction of downloading the correct versions of its
dependencies, and so on.

## Package Coresidence

Because packages are installed to unique locations based on their source code
hashes, it is possible for your project to install different versions of the
same package through transitive dependencies. For example, if you use package
`foo` and package `baz` and `foo` relies on `commonlib-v1.2` while package
`baz` relies on `commonlib-v1.3`, then Tomo will dutifully install and link
both versions of `commonlib`, without any problems. Left unchecked, this can
result in a bloated binary with multiple versions of many libraries. If this is
a problem that you are experiencing, you may wish to consider synchronizing
your dependencies so they depend on the same common library (e.g. upgrading
`foo` in this example) or considering pruning your dependency tree to reduce
the number of transitive dependencies in your project.

## Creating a Package Archive

A Tomo package source archive may be any of the following formats:

- `.tar.gz` or `.tgz`
- `.tar.xz` or `.txz`
- `.tar`
- `.zip`

The contents of the archive should be either the files in your package, or a
single folder containing the files in your package. Tomo will handle both.

Tomo package archives can be created any way you like, but here are some easy
options:

- If you are using git, then `git archive --format=tar.gz -o mypackage.tar.gz`
- If you are using GitHub, then when you create a tag and push to GitHub, a
  source archive will be created automatically at
  `https://github.com/<org>/<repo>/archive/refs/tags/<tagname>.tar.gz`
- Run `tar -czf mypackage.tar.gz --exclude='*/.*' /path/to/project` 
