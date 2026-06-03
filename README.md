# moddcpp — File Watcher and Command Runner (C++ port of modd)

A zero-dependency C++ port of [modd](https://github.com/cortesi/modd) — watch files and directories for changes, then run commands.

## Why moddcpp?

The original [modd](https://github.com/cortesi/modd) requires Go plus dozens of modules. moddcpp compiles with a single `make` using only C++17 and pthreads.

## Quick Start

```bash
make
./moddcpp moddcpp.conf
```

## Features

- inotify-based file watching (Linux only)
- Recursive directory watching with glob/exclude patterns
- Configurable debounce delay per rule
- Shell command execution on file change
- Running child process termination before re-execution
- Graceful shutdown with SIGINT forwarding

## Config Format (moddcpp.conf)

```
prep {
    command: make
}
watch {
    pattern: "src/*.cpp"
    exclude: "*_test.cpp"
    prep: g++ -c $FILE
    run: ./myapp
    delay_ms: 300
}
```

## Build

```bash
make
```
Requires: GCC 10+ or Clang 12+, GNU Make, Linux (inotify)
