# Playtesting: reporting bugs

When something goes wrong, press **Enter** and type:

```
/bug what happened
```

Every player's game saves its part of the report: the last minutes of its log and network messages, and what it
shows of every character. The host's part also keeps its newest save. Keep your own notes of what you were
doing; the report has the rest. If it looks wrong, press F12 (Steam's screenshot) and send that too.

At the end of the session, press **Enter** and type:

```
/export
```

The chat shows where the zip went, and the path is copied to your clipboard. Send that zip (each player sends
their own).

**After a crash or a freeze:** start the game again. A zip with the crash is made at start in
`Documents/My Games/Skyrim Special Edition/SkyrimCoop/`; send the newest `SkyrimCoop-*.zip` there.

On Linux (Proton) that folder is in the game's prefix:
`~/.steam/steam/steamapps/compatdata/489830/pfx/drive_c/users/steamuser/Documents/My Games/Skyrim Special Edition/SkyrimCoop/`.
