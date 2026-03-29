# Phase 11: JSON Wire Protocol - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-03-29
**Phase:** 11-json-wire-protocol
**Areas discussed:** JSON library for DLL, Schema design, Migration strategy, Performance budget

---

## JSON Library for DLL

| Option | Description | Selected |
|--------|-------------|----------|
| nlohmann/json | Single-header, header-only C++. ~800KB header, compiles to small code. Well-tested, widely used. Drop-in for MinGW. | ✓ |
| cJSON (C library) | Tiny C library (~1500 LOC). Minimal footprint but verbose API with manual memory management. | |
| Hand-rolled minimal | ~100-line sprintf-based serializer. Zero dependency but limited and error-prone. | |

**User's choice:** nlohmann/json (Recommended)
**Notes:** Clean fit for MinGW DLL — single header drop-in, no build system changes.

---

## Schema Design

| Option | Description | Selected |
|--------|-------------|----------|
| Typed fields | Each opcode gets named fields matching semantics. Self-documenting, debuggable with jq. Slightly more DLL code per opcode. | ✓ |
| Generic args array | Keep args[8] pattern in JSON. Minimal DLL change but loses debuggability benefit. | |
| Hybrid | Common fields named (op, formId), position as sub-objects, remaining as array for uncommon hooks. | |

**User's choice:** Typed fields (Recommended)
**Notes:** Each of ~30 hook opcodes and ~15 command opcodes gets its own named field layout.

---

## Migration Strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Big bang | Replace binary with JSON in one phase. Both DLL and native switch together. Clean break, no compat code. | ✓ |
| Version negotiation | Handshake to agree on protocol version. Supports mixed old/new binaries. More code. | |
| Parallel support | Auto-detect binary vs JSON per message. Gradual per-opcode migration. | |

**User's choice:** Big bang (Recommended)
**Notes:** Both sides built from same repo, always deployed together. Version mismatch impossible.

---

## Performance Budget

| Option | Description | Selected |
|--------|-------------|----------|
| Don't care about JSON overhead | Localhost TCP, nlohmann handles 200K serialize/sec, ~7K msgs/sec needed. <5% CPU. | |
| Cap at 10K msg/sec | Set ceiling, optimize hot paths if exceeded. | |
| Batch high-freq hooks | Batch HOOK_ACTOR_PROCESS into arrays per frame. | |

**User's choice:** None of the above — user clarified the real issue
**Notes:** User pointed out that performance isn't a JSON vs binary question. The real problem is redundant messages (sending unchanged state every frame). Solution: dirty-check cache that deduplicates, included in this phase's scope. Follow-up confirmed: caching is IN SCOPE for Phase 11, not deferred.

---

## Claude's Discretion

- Exact dirty-check thresholds (position epsilon, timing for non-spatial hooks)
- Cache data structure and comparison approach
- Whether to batch dirty events or send individually
- Exact JSON field names per opcode
- HOOK_PROJECTILE special case handling
- Test infrastructure update scope

## Deferred Ideas

None — discussion stayed within phase scope
