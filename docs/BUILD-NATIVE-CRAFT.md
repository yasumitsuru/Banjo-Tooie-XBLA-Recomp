# Native build through Craft Anything

This project remains a Windows C++/CMake ReXGlue port. The root `package.json`
is a dependency-free build adapter for Craft Anything's supported `npm run build`
check. It does not add a JavaScript application, change engines, or install packages.

From `app/`, run:

```cmd
npm run build
```

That command runs `tools\build_native.cmd`, configures the existing
`banjotooie-recomp/CMakeLists.txt` with the project's local VS 2022/Clang toolchain,
and builds the real `banjotooie` target with CMake and Ninja. Configuration and
compilation errors propagate as nonzero exit codes to npm and the checker.

The game executable is `out\native\banjotooie.exe`. The SDK also writes
`rexglue-sdk\out\win-amd64\rexruntime.dll`, as in the original native build.
The adapter uses generated sources already present in
`banjotooie-recomp\generated`; initial SDK/code generation remains available through
`tools\build_port.cmd all`. No network operation is needed by the adapter.

The build also creates `Build\banjotooie.exe`, the small Windows launcher Craft
discovers for Play and screenshot checks. It runs the actual native game, forwards
all command-line arguments, waits for it, and returns its exit code. The game itself
produces screenshots. Original game assets remain outside `app/`, in the existing
sibling folder `Banjo Tooie BR`; the launcher supplies that default `--game_data_root`.
Checker user data and cache are isolated in `out\craft-runtime`. Explicit
`--game_data_root`, `--user_data_root`, and `--cache_path` arguments override those
defaults. The launcher does not copy or bundle the game's assets.

Build success proves compilation and linking. It does not prove gameplay, 4K/60 FPS,
input/save behavior, or full translation coverage; those require their own tests.
