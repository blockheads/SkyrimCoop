# Experience: the skill and level pipeline

How the player gains skill and level experience, and where experience mods hook it (1.6.1170, read in
IDA 2026-10-19). The co-op rule built on it is in `docs/design/party-model.md` ("Experience").

| ID | Name | What |
|---|---|---|
| 40488 | `PlayerCharacter::AddSkillExperience` | The player's skill use (in the vtable). `(this, av, xp, advanceObject, advanceAction)`; for `xp > 0` calls 41561 with `useModifier = 1`. **We hook it** (`HookAddSkillExperience`, `Code/client/Games/Skyrim/PlayerCharacter.cpp`). |
| 41561 | `PlayerSkills::AdvanceSkill` | `(skills, av, xp, advanceObject, advanceAction, useModifier, hideNotification)`. Skills only (av 6-23), skill < 100. With `useModifier`, `xp = useMult * xp + useOffset` (27244). Stores av / object / action on the player (+0xAF0 / +0x9F8 / +0xAF4), runs perk entry point 22 (`ModSkillUse`) on `xp`, clears them. Adds to the skill's xp; while it reaches the threshold: skill +1, Story Manager skill-increase event, threshold `= improveMult * (level+1)^fSkillUseCurve + improveOffset`, level xp `+= fXPPerSkillRank * new level`. Then the "skill increased" message. |
| 41562 | `PlayerSkills::IncrementSkill` (called by `PlayerCharacter::IncrementSkill`) | `(skills, av, count)`: calls 41561 with each level's remaining xp, `useModifier = 0`, keeping the progress ratio. Trainers (Uncapper's name for it); skill books and `Game.IncrementSkill` likely too, not checked. **Does not pass through 40488.** |
| 27244 | ours: `GetSkillUseData` | `(av, &useMult, &useOffset, &improveMult, &improveOffset)` from the `ActorValueInfo`'s skill data (+0x108); false for a non-skill. Hooked as `HookCalculateExperience` for upstream's `ScopedExperienceOverride`. |

Perk entry 22 is where perk-based experience bonuses apply (perk overhauls add many; which vanilla
bonuses use it, against skill-rate actor values, not checked). Its conditions may read the advance object
and action (`EPModSkillUsage_*`), so a caller of 40488 must pass them.

## How experience mods hook it

| Mod | How | Notes |
|---|---|---|
| Skyrim Skill Uncapper AE (TheDreadedAndy/SkyrimAEUncapper) | Jump at the start of 41561: `xp *= mult(skill, skill level, player level)`; patches the 100 cap, the level xp of a skill raise (41561 +0x2D7), perk points (52538), attributes per level. Its 41562 patch calls the original, so books and trainers are not multiplied. | Depends on the player's own skill and level. |
| Experience (Nexus 17751, closed DLL) | Level xp for quests, discoveries and kills (`bEnableKilling`, `fXPKillingMult`), optionally none from skills (`bEnableSkillXP`); caps skills by player level. Uncapper leaves 4 NOPs in the cap code at its author's request. | Reacts to its game's events; which event it uses for kills is unknown (test with a peer's kill). |
| Custom Skills Framework (Exit-9B/CustomSkills) | Its own skills, level and progress in `TESGlobal`s; `Skill::Advance` computes like 41561 and runs entry 22 itself; Papyrus `CustomSkills.AdvanceSkill/IncrementSkill`. | Never passes through 40488 or 41561. |
| Perk overhauls, Requiem-style records | `ModSkillUse` perks; skill `useMult/useOffset` edited in `ActorValueInfo` records. | Applied inside 41561. |

So a gain measured after 41561 already carries the player's perks and every mod's multiplier; adding it to
another player through 40488 applies the receiver's again. Sending the input of 40488 (skill, base amount,
object, action) does not.

## Upstream's shared experience (to be removed)

`HookAddSkillExperience` measures the skill's xp before and after, and for nine combat skills sends the
difference once a second (`SyncExperienceRequest`); each receiver adds it to its last used combat skill
through 40488 with 27244 forced to `useMult 1, useOffset 0`. Faults besides the double perks and mods:

- A skill raise inside the call subtracts the threshold, so the measured gain is negative and cancels other
  gains in that second.
- Amounts cross skills (Destruction's into One-Handed) with different thresholds.
- The hook and `TAddSkillExperience` declare `(this, av, xp)` only: the object and action reach 41561 as
  whatever the hook leaves in r9 and its stack slot. Not yet confirmed in our DLL's disassembly.
