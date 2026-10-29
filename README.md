# SkyrimCoop

**SkyrimCoop** is a peer-to-peer cooperative multiplayer mod for Skyrim Special Edition, forked from [Tilted Online](https://github.com/tiltedphoques/TiltedEvolution) (Skyrim Together Reborn).

## What's Different?

Unlike the original MMO-style client-server architecture, SkyrimCoop implements a **host-based P2P model**:

- **Original (Tilted Online):** Dedicated server + multiple clients (MMO-style)
- **SkyrimCoop:** Host player runs embedded server + clients connect (co-op style, like most modern co-op games)

This simplifies:
- Actor ownership (host always owns NPCs/world state)
- Cell management (host's loaded cells are authoritative)
- Quest progression (syncs to host's state)
- Combat resolution (host adjudicates)

## Getting Started

This is a work-in-progress fork. To play: [docs/install.md](docs/install.md) (the playtest package, built by
`scripts/package.sh`). For build instructions, see CLAUDE.md.

Install and enable the bundled [SkyrimCoopStart.esp](GameFiles/Skyrim/SkyrimCoopStart.esp) in both games along
with the plugin. First-time invites open character creation automatically; confirming the name joins the host.
Returning players use their kept character. External Quick Start is no longer required, and ordinary New Game
behavior stays unchanged. The test runner installs and enables the bundled room automatically.

For Steam to launch the game for an invite, its launcher must use the bundled `SkyrimCoopLauncher.exe` beside
`skse64_loader.exe`. It preserves the invite arguments that SKSE otherwise drops. A manual installation can back
up the original `SkyrimSELauncher.exe` and use a copy of `SkyrimCoopLauncher.exe` under that name; keep the backup
for restoring the vanilla launcher. Launching an already running game needs no launcher substitution.

For the Linux test workflow (two off-screen Skyrim instances, sync scenarios, swapping a
rebuilt plugin into the running games), see the docstring of `scripts/coop_test.py`.

## Acknowledgments

This project is based on [Tilted Online](https://github.com/tiltedphoques/TiltedEvolution) by Tilted Phoques. We are grateful for their excellent work creating the foundation for Skyrim multiplayer.

## Contributing
Have some experience in C++, and want to help advance the project faster? Contribute!
- Check the issues for tasks to work on
- Fork the repository and create pull requests
- Try to keep your code clean, following the code guidelines
- Run clang-format before committing

## Project Structure

* [**client/**](./Code/client): Skyrim client plugin (SKSE)
* [**server/**](./Code/server): Game server implementation (embeddable for P2P hosting)
* [**encoding/**](./Code/encoding): Network message definitions
* [**common/**](./Code/common): Code shared between client and server
* [**skyrim_ui/**](./Code/skyrim_ui): Angular TypeScript UI overlay
* [**tests/**](./Code/tests): Unit tests for encoding and serialization
* [**tp_process/**](./Code/tp_process): CEF (Chromium Embedded Framework) worker process
* [**immersive_launcher/**](./Code/immersive_launcher): Game launcher/updater

See CLAUDE.md for detailed architecture documentation.

## License
[![GNU GPLv3 Image](https://www.gnu.org/graphics/gplv3-127x51.png)](http://www.gnu.org/licenses/gpl-3.0.en.html)

SkyrimCoop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This project is a fork of Tilted Online, which is also licensed under GPLv3.
