# moddcpp — File Watcher and Process Runner (C++ port of modd)

A zero-dependency C++ port of [modd](https://github.com/cortesi/modd) — watch files and directories, then run commands when they change.

## Why moddcpp?

The original [modd](https://github.com/cortesi/modd) requires the Go toolchain plus dozens of modules. moddcpp compiles with a single `make` using only C++17 and standard Linux headers.

## Quick Start

```bash
make
./moddcpp
```

## Features

- Recursive file watching via inotify
- Pattern-based file matching and exclusion
- Run commands on change (build, test, deploy)
- Pre/post hooks
- Configurable debounce and cooldown periods
- TOML configuration format
- Signal forwarding to child processes

## Build

```bash
make
```
Requires: GCC 10+ or Clang 12+, GNU Make
