# Ionosphere Live 3D - Android (third repo) coordination handoff

This note is the local planning seed left after the Rondas 11-16 desktop release.
It is NOT yet part of git (untracked).

## New repo
- Name in GitHub: valledemena/ionosphere-live-3d-android
- URL: https://github.com/valledemena/ionosphere-live-3d-android
- Roles: David (build/publish), GLM (architecture/review/planning), Beam (local tooling/tests/GitHub ops).

## Architecture starting point
- web repo is the discovery first source (`ionosphere-live-3d-web`)
- native desktop repo is the fidelity second source
- MVP likely should start with pure UI shell + cartographic data sources (web API endpoints or static/robust sources), then migrate native layers.
- Offline degradation: each layer has off/degraded/failed and no invented/fabricated data.
- Network layer should mirror validation and cache rules. All keys absent unless explicitly provided; all API calls public.

## Task board (from mobile repo docs/TASKS.md)
[ ] R0 prime baseline permissions and tool
[~] R1 decide Android stack
[~] R2 discover data contract (endpoints) In this front web data list + desktop logic.
[ ] R3 normalized provider adapter
[ ] R4 offline support
[ ] R5 tests & CI

## What is still incomplete in desktop repo (best to revisit when PR #12 scope moves):
- Observatori permission/bounce: send corrected email with RETR as normal path, not directory listing; open GitHub contact mode and adjust text.
- Fixture disposition (temporary `tests/fixtures/ebro` not tracked and package ignored).
- C5 transport seam follow-up (injectable HttpClient already seam, but ensure direct tests).
- C3/licensing and MSIS strategy unresolved until Observatori resolution.
- Branch cleanup retained backup branches (prior GLM backups? GLM left in place).
- Ronda docs plus archived logs.
- Pre-release pins locally.

## Collaboration style / rhythm
- Issues/PRs in new repo.
- Beam proposing scaffolding; GLM reviewers; David builds/run/integration.
- no direct force-push, check run before merge.
- Agent can't directly call GLM (local API model token unavailable means David relays), so exchanges happen through GitHub issues + pasted context.

## Request to GLM in first round
1. Propose an Android baseline split into 5-7 implementation slices and owners.
2. Select rendering + data library.
3. Map desktop algorithms against web 3D scene and design slotted impl.
4. Decide offline caching (Room, encrypted disk, or generated plan).
5. Define test pyramid and CI gates on GitHub Actions.
