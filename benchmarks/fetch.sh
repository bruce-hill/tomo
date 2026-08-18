#!/bin/sh
# Download reference implementations from the Computer Language Benchmarks Game.
# Thin wrapper around bench.py; pass benchmark names to limit, e.g. ./fetch.sh nbody
exec python3 "$(dirname "$0")/bench.py" fetch "$@"
