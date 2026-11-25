# Installing SkyrimCoop (playtest)

You need Skyrim Special Edition (or Anniversary Edition) on Steam, game version **1.7.104**, on Windows or on Linux
through Proton, and **SkyrimModSync** (`sms`), the tool that shares the host's mod setup
([design](design/launcher.md)): download it from its
[latest release](https://github.com/blockheads/SkyrimModSync/releases/latest) (`sms-windows-x86_64.exe` on Windows,
`sms-linux-x86_64` on Linux; `chmod +x` it there). Everyone plays the host's exact setup: SkyrimCoop, SKSE, Address Library, the
Unofficial Skyrim Special Edition Patch and Alternate Start - Live Another Life, plus any mods the host adds.

## The host

1. Install the mods in Mod Organizer 2, as usual: the SkyrimCoop zip (`SkyrimCoop-<version>-skyrim-1.7.104.zip`,
   from this repo's [releases](https://github.com/blockheads/SkyrimCoop/releases)) is an ordinary mod archive. SKSE **2.3.1** as an MO2 mod (its loader and DLL in the mod's `Root/` folder), Address Library, USSEP
   and Alternate Start from Nexus. Enable the plugins; the load order is MO2's.
   Anything to share must be an MO2 mod: from the game folder itself only SKSE's root files are published.
2. Publish the profile:

   ```
   sms host init --mo2 <MO2 folder> --profile <profile>
   sms host status        # every mod, where friends get it, what you'd send
   sms host source SkyrimCoop github blockheads/SkyrimCoop   # friends download it from our releases
   sms host publish       # prints the code for friends
   sms host serve         # keep running while friends sync (restart it after publishing again)
   ```

3. Play: `sms setup` once, then `sms play` (no code: your own pack, from your own MO2 files; nothing downloads).
   Or play through MO2 as usual. Load a save (one used only for multiplayer) and invite friends from the Steam
   overlay (Shift+Tab). `sms unload` after playing; publishing an update waits for it.

## A friend

```
sms setup              # finds Skyrim, asks for a store folder (same drive as Skyrim: no copies)
sms nexus-login        # optional: a Premium Nexus key downloads Nexus mods from Nexus, sparing the host
sms play <code>        # syncs the mods, loads them into the game, starts Skyrim through Steam
```

Accept the host's invite from the main menu (Shift+Tab). Someone the host's save has not met makes a character
first; naming it joins. Only the host saves.

After playing, `sms unload` puts your game back exactly as it was (your own mods, settings and Bethesda's launcher).
While the pack is loaded, Steam's Play starts SKSE; after an update from the host, `sms play` again (the code is remembered).

Without a Premium key the host sends every mod (the tool never automates Nexus's website).

## Playing

- New Game skips the Helgen intro (Alternate Start). You wake in a prison cell, make your character, then pick how
  your life begins at the statue of Mara (escaping Helgen is one of the choices).
- Bugs and crashes: see `playtest.md` (in the mod's `SKSE/Plugins/SkyrimCoop` folder): `/bug`, `/export` in the chat.
