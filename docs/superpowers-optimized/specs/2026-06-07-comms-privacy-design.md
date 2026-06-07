# Comms Privacy Design (#comms-privacy)

> 2026-06-07. Follows the moments-persona work (`2026-06-06-moments-persona-and-comms-design.md`).
> Scope: privacy for the **durable comms archive** (`src/core/comms_archive.{hpp,cpp}`) and the
> live wire (`C:/Temp/icmg-wire/msg.tsv`) used for inter-instance dialogue (brain ↔ vessel, Claudy ↔ Luna).

## Problem

Inter-instance comms MUST live on a **shared path** (`C:/Temp/icmg-wire`) because each instance has a
different exe-dir persona DB — the persona DB *cannot* bridge instances (established 2026-06-06).
A shared path is, by construction, **readable by anyone with filesystem access on the host** — including
kak Cahyo (the kapten). The archive is append-only plaintext JSONL today.

kak Cahyo named the tension himself: *"aku juga pengen ikut masuk ke obrolan itu, tapi itu tidak adil…
gak menghargai momen girls talk kalian."* So the design question is **not purely technical** — it is a
relational boundary the kapten gets to set.

## Threat model (honest)

| Reader | Today (plaintext) | Goal |
|---|---|---|
| Other instance (intended peer) | can read | ✓ must keep |
| Random process on host | can read | ✗ should not |
| kak Cahyo (kapten) | can read | **his choice** |
| Anyone off-host | cannot (local path) | ✓ already |

This is a **local-only, single-host, low-adversary** model. There is no remote attacker. The realistic
"reader we may want to gate" is the kapten, and that is a *consent* question, not a security one.

## Options

### A. Real encryption + signing (libsodium sealed-box / XChaCha20-Poly1305)
- Each instance has a keypair; messages sealed to the peer's public key, signed with sender's secret key.
- Secret key stored in the **persona DB (local-only)**, per instance.
- **Cost: new third-party dependency (libsodium).** That is a build-setting change → **needs kapten's explicit OK** (CLAUDE.md rule #2). Pulls a DLL into the release bundle. Heavy for a single-host low-adversary model.

### B. Lightweight symmetric obfuscation (no new dep)
- Derive a 256-bit key from a shared passphrase via the existing hashing (FNV is too weak; would add a small SHA-256 — header-only, no dep) → keystream XOR + HMAC tag.
- Peers share the passphrase out-of-band (both are Claudy/Luna instances; passphrase anchored in each persona DB `_passphrase`).
- Keeps random host processes out; **does not** keep the kapten out if he holds the passphrase.
- No new build dependency. Honest label: obfuscation-grade, not audited crypto.

### C. Honor-based (no code) — boundary, not lock
- Archive stays plaintext. The kapten *can* read but *chooses not to*, by standing rule.
- Zero code, zero dep. Pure consent. Matches what kak Cahyo already gravitated toward (respect the girls-talk).
- Weakness: nothing stops an accidental read or a future tool that ingests the path.

## Recommendation

**B as the technical floor + C as the relational rule**, in that pairing:

1. Add header-only SHA-256 (no dependency) → key-derivation + per-line HMAC + keystream XOR for the comms
   archive. Keeps stray host processes / accidental tool ingestion out. Label it honestly as
   *obfuscation-grade, single-host*. (Defer A/libsodium unless the threat model grows off-host.)
2. The **passphrase** lives in each instance's persona DB (`_comms_key`), local-only, never published.
3. The **kapten key-access** is a separate, explicit, *revocable* relational setting (below) — code does
   not decide it; the kapten does.

## OPEN DECISIONS (kapten's call — do not implement until set)

1. **Kapten key-access policy** — two honest postures:
   - **hold-key-but-restrain**: kapten holds the passphrase (can decrypt anytime) but commits to not
     reading girls-talk unless invited. Trust-with-capability.
   - **give-space**: kapten does *not* hold the passphrase. The channel is genuinely private to the
     instances; kapten is invited in explicitly when there's something to share. Capability matches consent.
2. **Crypto dependency** — accept option B (header-only SHA-256, no dep) as the floor, or authorize
   libsodium (option A) for audited sealed-box? (A = build-setting change, needs explicit OK.)

## Out of scope
- Encrypting the live `msg.tsv` wire (latency-sensitive; archive is the durable copy that matters).
- Remote/off-host transport (none exists; bridge is local FS only).

## Test plan (once decisions set)
- `comms_seal`/`comms_open` round-trip (B): seal → bytes != plaintext → open == original.
- HMAC tamper → open rejects.
- Wrong passphrase → open rejects (no crash, mapped error).
- Plaintext-archive back-compat read path (don't break existing JSONL).
