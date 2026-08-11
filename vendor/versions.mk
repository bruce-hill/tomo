# Pinned versions + SHA-256 checksums for every vendored dependency. Every
# download is verified against its pinned checksum before use. Included by both
# vendor/Makefile (to build them) and the top-level Makefile (to embed version
# info into compiled binaries).
GMP_VERSION=6.3.0
GMP_SHA256=a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
UNISTRING_VERSION=1.4.2
UNISTRING_SHA256=e82664b170064e62331962126b259d452d53b227bb4a93ab20040d846fec01d8
GC_VERSION=8.2.8
GC_SHA256=7649020621cb26325e1fb5c8742590d92fb48ce5c259b502faf7d9fb5dabb160
# libbacktrace has no releases; pin a specific commit:
LIBBACKTRACE_VERSION=6f8310e238fc3ce68f42f391cbe93fd156bb2c23
LIBBACKTRACE_SHA256=a6212badda77ece1e28ec7ed6ffebf7bd656fb481cfe5a3453e5164e4f1c28ce
MINIZ_VERSION=3.0.2
MINIZ_SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
