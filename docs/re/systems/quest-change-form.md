# A quest's change form in a save (QUST, 1.6.1170)

What `TESQuest::WriteToSaveGame` (25029, vfunc 0x0E) writes, in order, for each change flag set.
Parsed by `scripts/re/save_diff.py` (`parse_quest`). Verified 2026-10-19: all ~7,400 QUST change forms
in 15 saves parse to their last byte, and for ~1,000 quests on two running games the parsed flags,
stages, objectives and reference alias fills equal the live engine's (harness `quest` command).

UESP's "Save File Format/QUST Changeform" has the same outline with most fields unnamed; FallrimTools
(a ReSaver fork on GitHub) parses only the top level. The field names here come from the writer and
CommonLibSSE-NG's `TESQuest` offsets.

| Change flag | Bit | Written |
|---|---|---|
| `Flags` (TESForm) | 0 | u32 form flags (0x10), u16 in-game form flags (0x18) |
| `QuestFlags` | 1 | u16 `data.flags` (0xDC) |
| `ScriptDelay` | 2 | f32 `data.questDelayTime` (0xD8) |
| `Stages` | 31 | vsval n; n × {s16 stage, u8 done (stage flags & 1)}: the executed stages list at 0xE8 |
| `Objectives` | 29 | vsval n; n × {u32 index, u32 state}: objectives (0xF8) with a state other than 0 |
| `RuntimeData` | 28 | under `aliasAccessLock`: u8 promote task pending (0x248); u32 n; n × reference alias fill (below); u32 n; n × {u32 alias id, RefID location}: location alias fills (0xA0); u8 has start event; the start event (below) |
| `InstanceData` | 27 | u32 `currentInstanceID` (0x50); vsval n; n × instance (below) |
| `AlreadyRun` | 26 | u8 `alreadyRun` (0x22A) |
| `Script` | 30 | nothing here: the quest script's state is in the Papyrus section |

- **Reference alias fill** (`refAliasMap`, 0x70; writer 0x1403D57C0): u32 alias id, u8 temporary (the
  reference's form flag 0x4000), RefID reference; for a temporary reference also RefIDs of its base
  (0x40), two extra-data forms and its cell (TESObjectREFR 20248).
- **Start event** (`startEventData`, 0x240; writer 0x14052AD50): u32 header, u32 event type (a
  four-character code from `BGSStoryEventManager`, for example `SCPT`), u32 n; n × {u32 kind, value}:
  kind 0 a reference, 1, 2 and 4 a form (RefID), 3 a u32.
- **Instance** (0x38 array; writer 0x140391E50): u32 id; vsval n × {u32, RefID}; vsval n × {RefID,
  u32}; u16; u8. The instance's text replacement data; it follows the alias fills.
- Counts marked vsval use the save's variable-size count (`BGSSaveGameBuffer` 36051/36052): the low two
  bits of the first byte give its size (1, 2 or 4 bytes), the value is the rest shifted right by 2.
  RefIDs are the save's 3-byte references (36047/36048).
- Quest flag 0x80 (`StageWait`) is set while a quest waits for its start-up stage: a save taken while
  start-game quests are still starting shows it.
