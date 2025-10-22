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

This is a work-in-progress fork. For build instructions, see CLAUDE.md.

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
