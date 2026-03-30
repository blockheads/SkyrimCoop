# Phase 1: ABI Safety and Source Cleanup - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-30
**Phase:** 01-abi-safety-and-source-cleanup
**Areas discussed:** -flayout-compat=ms strategy, try/catch cleanup scope, relay code deletion

---

## `-flayout-compat=ms` Strategy

### Q1: How to handle the GCC flag uncertainty?

| Option | Description | Selected |
|--------|-------------|----------|
| A) Test and enable | Test flag empirically, if supported add to xmake.lua and re-enable SKYRIM_STRUCT_ASSERT | ✓ |
| B) Test and document only | Test flag, document result, keep SKYRIM_STRUCT_ASSERT disabled | |
| C) Skip entirely | Delete stale TODO, rely on -mms-bitfields | |

**User's choice:** A — Test and enable for full ABI confidence
**Notes:** None

### Q2: CI check for GCC version?

| Option | Description | Selected |
|--------|-------------|----------|
| Add CI check | Fail build if GCC doesn't support the flag | |
| Let it error naturally | Trust xmake.lua flag, GCC will error if unsupported | ✓ |

**User's choice:** Let it error naturally
**Notes:** None

---

## try/catch Cleanup Scope

### Q3: Which try/catch sites to replace?

| Option | Description | Selected |
|--------|-------------|----------|
| A) Replace all 3 | Consistent no-exceptions policy across client codebase | ✓ |
| B) Only HostService | Per roadmap — leave CrashHandler and TESObjectREFR alone | |
| C) HostService + TESObjectREFR | Leave CrashHandler as reasonable safety net | |

**User's choice:** A — Replace all 3 for consistency
**Notes:** None

### Q4: Error-code replacement pattern?

| Option | Description | Selected |
|--------|-------------|----------|
| Individual checks | Wrap each failable call individually | |
| Single bool chain | `if (!step1() \|\| !step2()) { cleanup(); return false; }` | |
| Follow existing patterns | Match whatever code patterns are in place | ✓ |

**User's choice:** Neutral — follow existing code patterns
**Notes:** User has no strong preference, deferred to Claude's discretion based on surrounding code

---

## Relay Code Deletion

### Q5: Preservation approach?

| Option | Description | Selected |
|--------|-------------|----------|
| A) Clean delete | Git history is enough | |
| B) Commit message note | One-line note about what was removed | |
| C) Archive to .planning/ | Copy files before deleting | |
| D) Mixed | Delete relay_dll entirely, keep native_client as dead code | ✓ |

**User's choice:** Delete relay_dll entirely. Keep native_client in source tree as dead code (remove from xmake build only) since it has useful source for Phase 3 porting.
**Notes:** native_client/transport/ has ServerMessageRouter and other transport code that Phase 3 will port for LoopbackTransport

### Q6: Flag native_client transport code for Phase 3?

| Option | Description | Selected |
|--------|-------------|----------|
| Flag in Phase 3 context | Note "deleted here, port from git history" | |
| Existing roadmap covers it | Phase 3 already references the port | |
| Keep as dead code | Don't delete yet, Phase 3 references directly | ✓ |

**User's choice:** Keep native_client as dead code — Phase 3 can reference directly instead of digging through git history
**Notes:** Defer full deletion of native_client/ to Phase 3 after transport code ported

---

## Claude's Discretion

- Error-code replacement pattern at each try/catch site — match surrounding code style

## Deferred Ideas

- Delete Code/native_client/ directory — deferred to Phase 3 after transport code ported
