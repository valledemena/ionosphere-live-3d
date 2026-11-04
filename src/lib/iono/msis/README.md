# NRLMSIS 2.1 port — boundary notice (drop 9a, F2b #9)

This directory contains a TypeScript translation of the NRLMSIS 2.1
reference package (NRL-SOF-014-1), for **research, academic, and
non-profit purposes only**. Any commercial use is prohibited.

- Reference mirror pinned at `jacobwilliams/NRLMSIS2.1` commit `9124144`.
- The translated files (`msisConstants.ts`, `msisUtils.ts`, `msisInit.ts`)
  are DERIVATIVE WORKS of the NRL-SOF (a translation to another programming
  language is a derivative work per section 4(b)): they carry the NRL-SOF
  terms, are NOT GPL, and cannot be relicensed under the GPL-3 of the
  repository root. The fence applies to the whole directory.
- The NRL-SOF originals (`msis21.parm`, `nrlmsis2.1_license.txt`) are
  fenced here, byte-exact. Any genuinely new, non-derivative glue code
  added to this directory may be GPL-3.0-or-later.
- HWM14 is out of scope for v1 (no port, no call path).

NRL-SOF section 4(c) verbatim statement:

> "This software incorporates the MSIS® empirical atmospheric model
> software designed and provided by NRL. Use is governed by the Open
> Source Academic Research License Agreement contained in the file
> nrlmsis2.1_license.txt."

Upstream: https://github.com/jacobwilliams/NRLMSIS2.1

Section 4(b) obligations acknowledged for this derivative work:
prominent change notices are carried in each translated file header;
delivery of the modified portion to NRL Code 7630 and the open-source
availability of the derivative are tracked by the project; the modified
software is NOT identified as "MSIS" (it is called
"ionosphere-live-3d NRLMSIS 2.1 port"), while the MSIS acronym is kept
in the individual files to document provenance.

Files:

| File | Source | Status (9a) |
| --- | --- | --- |
| `msisConstants.ts` | `msis_constants.F90` (196 lines) | translated |
| `msisUtils.ts` | `msis_utils.F90` (282 lines) | translated |
| `msisInit.ts` | `msis_init.F90` (635 lines) | translated |
| `msis21.parm` | parameter data (536576 B) | byte-exact blob |
| `nrlmsis2.1_license.txt` | license text (4444 B) | byte-exact blob |

Numerics: the reference builds with DBLE (`real(8)`); JS `number`
is IEEE-754 double, the same width. Full bit-exact validation of the
evaluation chain against the 200 reference vectors lands in 9c.
