# SkyrimCoop

Co-op multiplayer for Skyrim Special Edition. One player hosts, friends join through a Steam invite, no server.
A fork of [Tilted Online](https://github.com/tiltedphoques/TiltedEvolution) (Skyrim Together Reborn).

Work in progress.

## Play

See [docs/install.md](docs/install.md).

## Build

Development is Linux only for now; Windows is not supported yet. You need xmake, clang 18 and libstdc++ 13.

```bash
scripts/xmake-linux.sh f                 # configure the native tests (once)
scripts/xmake-msvc.sh f -m dev           # configure the plugin; downloads LLVM and the Windows SDK (once)
scripts/xmake-msvc.sh build -j8 SkyrimCoopPlugin
scripts/check.sh                         # builds and runs the tests; run it before a pull request
```

Paths to your Steam library and game go in environment variables or `~/.config/skyrimcoop/paths.env`
([docs/design/testing.md](docs/design/testing.md)). Without them the scripts use Steam's default folder.

## Docs

- [docs/progress.md](docs/progress.md): where things stand
- [docs/design/](docs/design/): how it works, starting with [the glossary](docs/design/glossary.md)
- [docs/design/testing.md](docs/design/testing.md): the tests, and running two games side by side

## License

GPLv3, see [LICENSE](LICENSE) and [NOTICE](NOTICE).
