# AGENTS.md — ionosphere-live-3d

## Shared handoff between OpenCode and Freebuff

This repository is worked on alternately from **OpenCode** and **Freebuff**.
The canonical current state is `HANDOFF_ACTIVE.md` at the repository root.
The append-only history of changes is `HANDOFF_LOG.md`.

The session being continued is:

- Session ID: `ses_ef515a973ffeXydCVmjLVzEjgR`
- Title: `DEEPSEEK V4.1 FLASH`
- Project: `C:\Users\unknown\Desktop\ionosphere-live-3d`

### At the start of every session or turn

Run this first, from the repository root:

```bash
python scripts/handoff.py status
```

It prints:

- the current objective, pending tasks and blockers from `HANDOFF_ACTIVE.md`;
- `git status` and recent commits for the root repo and the Android repo;
- the APK timestamp/size and connected ADB devices;
- the last entries of `HANDOFF_LOG.md`;
- **files modified after the last log entry** — this is how you detect work
  done from the other tool even if it forgot to update the active handoff.

Then:

1. Read `HANDOFF_ACTIVE.md` completely.
2. If `status` reports files changed after the last log entry, inspect
   `git diff` in the affected repo before continuing. Treat the working tree
   as the source of truth; update the handoff if it is stale.
3. Read the last few entries of `HANDOFF_LOG.md` for the narrative of what the
   other tool did and what it verified.
4. Do not repeat work marked as done. Trust only what was actually verified;
   re-run a check if you need certainty.

### Before ending a turn or session

1. Update `HANDOFF_ACTIVE.md` with:
   - current objective;
   - what was done and how it was verified;
   - files changed and their uncommitted/staged state;
   - pending tasks and the next concrete step;
   - blockers (rate limits, device, credentials);
   - a ready-to-paste prompt for the next agent.
2. Append an entry to the log:

```bash
python scripts/handoff.py log --tool <opencode|freebuff> \
  --summary "..." --files "..." --verified "..." --next "..."
```

3. Run `python scripts/handoff.py check` and leave it clean. If it reports
   files, either log them or explain why they are irrelevant.
4. If the turn produced code or evidence for GLM, update `GLM_REVIEW.md` and run
   `python scripts/glm_notify.py` (see the GLM review loop above).

### GLM review loop

GLM is the independent reviewer. **GLM is reached through David (the user):**
David pastes GLM's messages into the chat and saves GLM's files in
`INTERCAMBIO`. Do not post to GitHub unless David explicitly asks.

Whenever a turn changes code or produces evidence GLM must review:

1. Write `GLM_REVIEW.md` with `Status: pending`, `Repo:` and `Issue:`, followed
   by the full message for GLM: context, PR/SHA, **committed vs uncommitted**
   state, changed files, exact commands, results, evidence paths, and the
   specific question.
2. Run `python scripts/glm_notify.py` (relay mode). It writes
   `INTERCAMBIO/work/GLM-MESSAGE-<timestamp>.md`, marks the status `relayed`,
   and prints the message.
3. Include the full message in the final response so David can paste it to GLM.
   Record the relay file path in `HANDOFF_LOG.md`.

One relay per review gate. `python scripts/glm_notify.py --post` is only for
when David explicitly asks to also publish it on issue #15. If the working tree
differs from the PR head, say so explicitly (GLM reviews SHAs).

### Rules

- Never hand-edit old entries in `HANDOFF_LOG.md`; it is append-only.
- Never claim a check passed unless you ran it. Put the exact command and the
  observed result.
- Do not commit or push unless the user asks.
- Keep `HANDOFF_ACTIVE.md` short and factual; the log carries the history.
- If the other tool left uncommitted changes, do not discard them. Build on
  them or ask the user.

## Active OpenCode session

If the OpenRouter free tier is exhausted, switch model/provider or wait for the
reset; the session itself is intact and can be resumed. The session keeps its
full transcript; this repo keeps the canonical cross-tool state.

Resume command:

```
C:\Users\unknown\AppData\Roaming\ai.opencode.desktop\cli\2.2.0-x64\opencode-cli.exe -s ses_ef515a973ffeXydCVmjLVzEjgR
```

## Freebuff

Open this folder as the workspace so this file and `HANDOFF_ACTIVE.md` are
loaded automatically. Start with `python scripts/handoff.py status` and continue
from the pending task. Before ending, update the active handoff and append a
log entry.
