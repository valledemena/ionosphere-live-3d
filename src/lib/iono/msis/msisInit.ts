// SPDX-License-Identifier: GPL-3.0-or-later
// ionosphere-live-3d — NRLMSIS 2.1 initialization (drop 9a, F2b #9).
//
// TypeScript transcription of `msis_init.F90` (NRL-SOF-014-1, mirror
// pinned at jacobwilliams/NRLMSIS2.1 commit 9124144). See the NRL-SOF
// section 4(b) note in msisConstants.ts; the original copyright notice
// is reproduced in `nrlmsis2.1_license.txt` in this directory.
//
// Host-I/O note: the original `msisinit(parmpath,parmfile,iun)` reads
// the parameter file with Fortran stream I/O. On the web there is no
// filesystem, so this port accepts the parameter-space bytes directly
// (`parmBytes`, little-endian real(8) stream, 512 rows column-major).
// The byte layout is identical: `parmin(col*512 + row)`.
//
// Indexing note: Fortran 1-based module arrays (`swleg(1:25)`,
// `specflag(1:10)`, `masswgt(1:10)`) are emulated with length+1 arrays
// whose index 0 is unused (NaN for reals). Fortran 0-based arrays
// (`swg(0:511)`, `beta(0:511,bl:nl)`) keep native 0-based indexing.

import {
  NL,
  NLS,
  NODES_TN,
  NDO1,
  NSPLO1,
  NODES_O1,
  NSPLNO,
  NDNO,
  NODES_NO,
  ZETAGAMMA,
  HGAMMA,
  ZETAREF_O1,
  ZETAREF_NO,
  MAXNBF,
  CTIDE,
  CSPW,
  CSFX,
  NSFX,
  NSFXMOD,
  NMAG,
  NUT,
  CSFXMOD,
  CMAG,
  CUT,
  MBF,
  SPECMASS,
} from "./msisConstants";

/** One vertical-parameter subset (basissubset). */
export interface MsisSubset {
  name: string;
  bl: number;
  nl: number;
  /** beta[512][cols], cols = nl-bl+1. */
  beta: number[][];
  active: boolean[][];
  fitb: number[][];
}

/** Full mutable model state (module-level variables of msis_init). */
export interface MsisState {
  initflag: boolean;
  haveparmspace: boolean;
  zaltflag: boolean;
  /** specflag(1:10); index 0 unused. */
  specflag: boolean[];
  /** massflag(1:10); index 0 unused. */
  massflag: boolean[];
  N2Rflag: boolean;
  /** zsfx/tsfx/psfx(0:MBF). */
  zsfx: boolean[];
  tsfx: boolean[];
  psfx: boolean[];
  /** smod(0:NL). */
  smod: boolean[];
  /** swg(0:MAXNBF-1). */
  swg: boolean[];
  /** masswgt(1:10); index 0 unused (NaN). */
  masswgt: number[];
  /** Legacy switches swleg/swc/sav(1:25); index 0 unused (NaN). */
  swleg: number[];
  swc: number[];
  sav: number[];
  TN: MsisSubset;
  PR: MsisSubset;
  N2: MsisSubset;
  O2: MsisSubset;
  O1: MsisSubset;
  HE: MsisSubset;
  H1: MsisSubset;
  AR: MsisSubset;
  N1: MsisSubset;
  OA: MsisSubset;
  NO: MsisSubset;
  nvertparm: number;
  /** etaTN/etaO1/etaNO rows 0..30; row arrays length 7, k index native (0..1 unused). */
  etaTN: number[][];
  etaO1: number[][];
  etaNO: number[][];
  HRfactO1ref: number;
  dHRfactO1ref: number;
  HRfactNOref: number;
  dHRfactNOref: number;
}

function newSubset(name: string, bl: number, nl: number): MsisSubset {
  const cols = nl - bl + 1;
  return {
    name,
    bl,
    nl,
    beta: Array.from({ length: MAXNBF }, () => new Array<number>(cols).fill(0)),
    active: Array.from({ length: MAXNBF }, () => new Array<boolean>(cols).fill(false)),
    fitb: Array.from({ length: MAXNBF }, () => new Array<number>(cols).fill(0)),
  };
}

function zeroEta(): number[][] {
  return Array.from({ length: 31 }, () => [0, 0, 0, 0, 0, 0, 0]);
}

// ---------------------------------------------------------------------------
// INITPARMSPACE: initialize and allocate the model parameter space
// ---------------------------------------------------------------------------

function initparmspace(state: MsisState): void {
  state.nvertparm = 0;

  state.TN = newSubset("TN", 0, NL);
  state.PR = newSubset("PR", 0, NL);
  state.N2 = newSubset("N2", 0, NLS);
  state.O2 = newSubset("O2", 0, NLS);
  state.O1 = newSubset("O1", 0, NLS + NSPLO1);
  state.HE = newSubset("HE", 0, NLS);
  state.H1 = newSubset("H1", 0, NLS);
  state.AR = newSubset("AR", 0, NLS);
  state.N1 = newSubset("N1", 0, NLS);
  state.OA = newSubset("OA", 0, NLS);
  state.NO = newSubset("NO", 0, NLS + NSPLNO);
  for (const s of [
    state.TN, state.PR, state.N2, state.O2, state.O1, state.HE,
    state.H1, state.AR, state.N1, state.OA, state.NO,
  ]) {
    if (s.name !== "PR") state.nvertparm += s.nl - s.bl + 1;
  }

  // Surface pressure parameter
  state.nvertparm += 1;

  // Solar flux modulation flags
  state.zsfx.fill(false);
  state.tsfx.fill(false);
  state.psfx.fill(false);
  // F1: zonal-mean asymmetric annual terms
  state.zsfx[9] = true; state.zsfx[10] = true; // Pl(1,0) annual terms
  state.zsfx[13] = true; state.zsfx[14] = true; // Pl(3,0) annual terms
  state.zsfx[17] = true; state.zsfx[18] = true; // Pl(5,0) annual terms
  // F2: solar flux modulation of the tides
  for (let n = CTIDE; n <= CSPW - 1; n++) state.tsfx[n] = true;
  // F3: solar flux modulation of stationary planetary wave 1
  for (let n = CSPW; n <= CSPW + 59; n++) state.psfx[n] = true;

  // Reciprocal node difference arrays
  for (let k = 2; k <= 6; k++) {
    for (let j = 0; j <= NL; j++) {
      state.etaTN[j][k] = 1.0 / (NODES_TN[j + k - 1] - NODES_TN[j]);
    }
  }
  for (let k = 2; k <= 4; k++) {
    for (let j = 0; j <= NDO1 - k + 1; j++) {
      state.etaO1[j][k] = 1.0 / (NODES_O1[j + k - 1] - NODES_O1[j]);
    }
    for (let j = 0; j <= NDNO - k + 1; j++) {
      state.etaNO[j][k] = 1.0 / (NODES_NO[j + k - 1] - NODES_NO[j]);
    }
  }

  // C1 constraint terms for O and NO (tapered logistic correction)
  let gammaterm0 = Math.tanh((ZETAREF_O1 - ZETAGAMMA) * HGAMMA);
  state.HRfactO1ref = 0.5 * (1.0 + gammaterm0);
  state.dHRfactO1ref =
    (1.0 - (ZETAREF_O1 - ZETAGAMMA) * (1.0 - gammaterm0) * HGAMMA) /
    state.HRfactO1ref;
  gammaterm0 = Math.tanh((ZETAREF_NO - ZETAGAMMA) * HGAMMA);
  state.HRfactNOref = 0.5 * (1.0 + gammaterm0);
  state.dHRfactNOref =
    (1.0 - (ZETAREF_NO - ZETAGAMMA) * (1.0 - gammaterm0) * HGAMMA) /
    state.HRfactNOref;

  state.haveparmspace = true;
}

/** Allocate a fresh state and initialize the parameter space. */
export function createMsisState(): MsisState {
  const state: MsisState = {
    initflag: false,
    haveparmspace: false,
    zaltflag: true,
    specflag: new Array<boolean>(11).fill(true),
    massflag: new Array<boolean>(11).fill(true),
    N2Rflag: false,
    zsfx: new Array<boolean>(MBF + 1).fill(false),
    tsfx: new Array<boolean>(MBF + 1).fill(false),
    psfx: new Array<boolean>(MBF + 1).fill(false),
    smod: new Array<boolean>(NL + 1).fill(false),
    swg: new Array<boolean>(MAXNBF).fill(true),
    masswgt: new Array<number>(11).fill(0),
    swleg: new Array<number>(26).fill(Number.NaN),
    swc: new Array<number>(26).fill(Number.NaN),
    sav: new Array<number>(26).fill(Number.NaN),
    TN: newSubset("TN", 0, NL),
    PR: newSubset("PR", 0, NL),
    N2: newSubset("N2", 0, NLS),
    O2: newSubset("O2", 0, NLS),
    O1: newSubset("O1", 0, NLS + NSPLO1),
    HE: newSubset("HE", 0, NLS),
    H1: newSubset("H1", 0, NLS),
    AR: newSubset("AR", 0, NLS),
    N1: newSubset("N1", 0, NLS),
    OA: newSubset("OA", 0, NLS),
    NO: newSubset("NO", 0, NLS + NSPLNO),
    nvertparm: 0,
    etaTN: zeroEta(),
    etaO1: zeroEta(),
    etaNO: zeroEta(),
    HRfactO1ref: 0,
    dHRfactO1ref: 0,
    HRfactNOref: 0,
    dHRfactNOref: 0,
  };
  // Declaration defaults: swleg(1:25) = 1.0 (swc/sav start undefined/NaN).
  for (let i = 1; i <= 25; i++) state.swleg[i] = 1.0;
  initparmspace(state);
  return state;
}

// ---------------------------------------------------------------------------
// PRESSPARM: log-pressure spline coefficients from temperature coefficients
// ---------------------------------------------------------------------------

import { GWHT, MBARG0DIVKB, IZFMAX } from "./msisConstants";

function pressparm(state: MsisState): void {
  const mbf = MBF;
  // Integrate pressure on nodes up to the last fully mixed level
  for (let j = 0; j <= mbf; j++) {
    let lnz = 0.0;
    for (let b = 0; b <= 3; b++) {
      lnz = lnz + state.TN.beta[j][b] * GWHT[b] * MBARG0DIVKB;
    }
    state.PR.beta[j][1] = -lnz;
    for (let iz = 1; iz <= IZFMAX; iz++) {
      lnz = 0.0;
      for (let b = 0; b <= 3; b++) {
        lnz = lnz + state.TN.beta[j][iz + b] * GWHT[b] * MBARG0DIVKB;
      }
      state.PR.beta[j][iz + 1] = state.PR.beta[j][iz] - lnz;
    }
  }
}

// ---------------------------------------------------------------------------
// LOADPARMSET: transfer a parameter-space byte stream into the subsets
// ---------------------------------------------------------------------------

/**
 * Load parameters from a little-endian real(8) stream of
 * nvertparm*MAXNBF doubles in column-major order
 * (parmin[col*MAXNBF + row]), exactly as Fortran stream I/O delivers.
 */
export function loadparmset(state: MsisState, bytes: Uint8Array): void {
  const n = bytes.length / 8;
  if (!Number.isInteger(n) || n % MAXNBF !== 0) {
    throw new Error(
      `msis: parameter stream has ${bytes.length} bytes, not a whole 512-row column set`,
    );
  }
  const ncols = n / MAXNBF;
  if (state.nvertparm !== ncols) {
    throw new Error(
      `msis: parameter stream has ${ncols} columns, model expects ${state.nvertparm}`,
    );
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const parmin = (col: number, row: number): number =>
    view.getFloat64((col * MAXNBF + row) * 8, true);

  let i0 = 0;
  let i1 = state.TN.nl - state.TN.bl;
  copyBlock(state.TN, parmin, i0, i1);
  i0 = i1 + 1;
  i1 = i0;
  for (let r = 0; r < MAXNBF; r++) state.PR.beta[r][0] = parmin(i0, r);
  const blocks = [
    state.N2, state.O2, state.O1, state.HE, state.H1,
    state.AR, state.N1, state.OA, state.NO,
  ];
  for (const s of blocks) {
    i0 = i1 + 1;
    i1 = i0 + s.nl - s.bl;
    copyBlock(s, parmin, i0, i1);
  }

  // Solar flux modulation flags from the nonlinear solar-flux rows
  state.smod.fill(false);
  for (let j = 0; j <= NL; j++) {
    if (
      state.TN.beta[CSFXMOD + 0][j] !== 0 ||
      state.TN.beta[CSFXMOD + 1][j] !== 0 ||
      state.TN.beta[CSFXMOD + 2][j] !== 0
    ) {
      state.smod[j] = true;
    }
  }

  pressparm(state);
}

function copyBlock(
  subset: MsisSubset,
  parmin: (col: number, row: number) => number,
  i0: number,
  i1: number,
): void {
  let c = 0;
  for (let col = i0; col <= i1; col++) {
    for (let r = 0; r < MAXNBF; r++) subset.beta[r][c] = parmin(col, r);
    c++;
  }
}

// ---------------------------------------------------------------------------
// TSELEC: legacy switches and mapping to new switches
// ---------------------------------------------------------------------------

function setRange(a: boolean[], lo: number, hi: number, v: boolean): void {
  for (let i = lo; i <= hi; i++) a[i] = v;
}
function setList(a: boolean[], list: number[], v: boolean): void {
  for (const i of list) a[i] = v;
}

/**
 * TSELEC: map 25 legacy switches (0-based input array, element 0 is
 * switch 1) onto the 512 globe switches. Values: 0 off, 1 on,
 * 2 main effects off / cross terms on; 9 accepts -1 (storm-time ap).
 */
export function tselec(state: MsisState, sv: number[]): void {
  if (sv.length !== 25) throw new Error("msis: tselec needs 25 legacy switches");
  const swleg = state.swleg;
  const swc = state.swc;
  const sav = state.sav;
  const swg = state.swg;

  for (let i = 1; i <= 25; i++) {
    sav[i] = sv[i - 1];
    swleg[i] = sv[i - 1] % 2.0;
    if (Math.abs(sv[i - 1]) === 1.0 || Math.abs(sv[i - 1]) === 2.0) {
      swc[i] = 1.0;
    } else {
      swc[i] = 0.0;
    }
  }

  // Main effects
  swg[0] = true; // Global term must be on
  setRange(swg, CSFX, CSFX + NSFX - 1, swleg[1] === 1.0); // Solar flux
  swg[310] = swleg[1] === 1.0; // Solar flux (truncated quadratic F10.7a)
  setRange(swg, 1, 6, swleg[2] === 1.0); // Time independent
  setRange(swg, 304, 305, swleg[2] === 1.0); // Time independent (extra, F10.7a)
  setRange(swg, 311, 312, swleg[2] === 1.0); // Time independent (extra, trunc quad F10.7a)
  setRange(swg, 313, 314, swleg[2] === 1.0); // Time independent (extra, dF10.7)
  setList(swg, [7, 8, 11, 12, 15, 16, 19, 20], swleg[3] === 1.0); // Symmetric annual
  setRange(swg, 306, 307, swleg[3] === 1.0); // Global AO (extra, solar-flux modulated)
  setList(swg, [21, 22, 25, 26, 29, 30, 33, 34], swleg[4] === 1.0); // Symmetric semiannual
  setRange(swg, 308, 309, swleg[4] === 1.0); // Global SAO (extra, solar-flux modulated)
  setList(swg, [9, 10, 13, 14, 17, 18], swleg[5] === 1.0); // Asymmetric annual
  setList(swg, [23, 24, 27, 28, 31, 32], swleg[6] === 1.0); // Asymmetric semiannual
  setRange(swg, 35, 94, swleg[7] === 1.0); // Diurnal
  setRange(swg, 300, 303, swleg[7] === 1.0); // Solar zenith angle
  setRange(swg, 95, 144, swleg[8] === 1.0); // Semidiurnal
  setRange(swg, 145, 184, swleg[14] === 1.0); // Terdiurnal
  setRange(swg, CMAG, CMAG + 1, false); // Geomagnetic activity master switch
  if (swleg[9] > 0 || swleg[13] === 1) setRange(swg, CMAG, CMAG + 1, true); // Daily mode
  if (swleg[9] < 0) {
    swg[CMAG] = false;
    swg[CMAG + 1] = true;
  } // Storm-time mode
  setRange(swg, CMAG + 2, CMAG + 12, swleg[9] === 1.0); // Daily geomagnetic terms
  setRange(swg, CMAG + 28, CMAG + 40, swleg[9] === -1.0); // Storm-time geomagnetic terms
  setRange(swg, CSPW, CSFX - 1, swleg[11] === 1.0 && swleg[10] === 1.0); // Longitudinal
  setRange(swg, CUT, CUT + NUT - 1, swleg[12] === 1.0 && swleg[10] === 1.0); // UT/Lon
  setRange(swg, CMAG + 13, CMAG + 25, swleg[13] === 1.0 && swleg[10] === 1.0); // Mixed (daily)
  setRange(swg, CMAG + 41, CMAG + 53, swleg[13] === 1.0 && swleg[10] === 1.0); // Mixed (storm)

  // Cross terms
  setRange(swg, CSFXMOD, CSFXMOD + NSFXMOD - 1, swc[1] === 1.0); // Solar activity modulation
  if (swc[1] === 0) {
    setRange(swg, 302, 303, false); // Solar zenith angle
    setRange(swg, 304, 305, false); // Time independent
    setRange(swg, 306, 307, false); // Global AO
    setRange(swg, 308, 309, false); // Global SAO
    setRange(swg, 311, 314, false); // Time independent
    swg[447] = false; // UT/Lon
    swg[454] = false; // UT/Lon
  }
  if (swc[2] === 0) {
    // Time independent (latitude terms); in MSISE-00 SWC(2) is unused
    setRange(swg, 9, 20, false); // AO
    setRange(swg, 23, 34, false); // SAO
    setRange(swg, 35, 184, false); // All tides
    setRange(swg, 185, 294, false); // All SPW
    setRange(swg, 392, 414, false); // Daily geomagnetic activity
    setRange(swg, 420, 442, false); // Storm-time geomagnetic activity
    setRange(swg, 449, 453, false); // UT/Lon
  }
  if (swc[3] === 0) {
    // Symmetric annual
    setRange(swg, 201, 204, false); // SPW1 (2,1)
    setRange(swg, 209, 212, false); // SPW1 (4,1)
    setRange(swg, 217, 220, false); // SPW1 (6,1)
    setRange(swg, 255, 258, false); // SPW2 (2,2)
    setRange(swg, 263, 266, false); // SPW2 (4,2)
    setRange(swg, 271, 274, false); // SPW2 (6,2)
    setRange(swg, 306, 307, false); // Global AO solar flux modulation
  }
  if (swc[4] === 0) {
    // Symmetric semiannual
    setRange(swg, 221, 224, false); // SPW1 (1,1)
    setRange(swg, 229, 232, false); // SPW1 (3,1)
    setRange(swg, 237, 240, false); // SPW1 (5,1)
    setRange(swg, 279, 282, false); // SPW2 (3,2)
    setRange(swg, 283, 286, false); // SPW2 (5,2)
    setRange(swg, 287, 290, false); // SPW2 (5,2)
    setRange(swg, 291, 294, false); // SPW2 (6,2)
    setRange(swg, 308, 309, false); // Global SAO solar flux modulation
  }
  if (swc[5] === 0) {
    // Asymmetric annual
    setRange(swg, 47, 50, false); // Diurnal (1,1)
    setRange(swg, 51, 54, false); // Diurnal (2,1)
    setRange(swg, 55, 58, false); // Diurnal (3,1)
    setRange(swg, 59, 62, false); // Diurnal (4,1)
    setRange(swg, 63, 66, false); // Diurnal (5,1)
    setRange(swg, 67, 70, false); // Diurnal (6,1)
    setRange(swg, 105, 108, false); // Semidiurnal (2,2)
    setRange(swg, 109, 112, false); // Semidiurnal (3,2)
    setRange(swg, 113, 116, false); // Semidiurnal (4,2)
    setRange(swg, 117, 120, false); // Semidiurnal (5,2)
    setRange(swg, 121, 124, false); // Semidiurnal (6,2)
    setRange(swg, 153, 156, false); // Terdiurnal (3,3)
    setRange(swg, 157, 160, false); // Terdiurnal (4,3)
    setRange(swg, 161, 164, false); // Terdiurnal (5,3)
    setRange(swg, 165, 168, false); // Terdiurnal (6,3)
    setRange(swg, 197, 200, false); // SPW1 (1,1)
    setRange(swg, 205, 208, false); // SPW1 (3,1)
    setRange(swg, 213, 216, false); // SPW1 (5,1)
    setRange(swg, 259, 262, false); // SPW2 (3,2)
    setRange(swg, 267, 270, false); // SPW2 (5,2)
    setRange(swg, 394, 397, false); // Geomag (daily)
    setRange(swg, 407, 410, false); // Mixed UT/Lon/Geomag (daily)
    setRange(swg, 422, 425, false); // Geomag (storm-time)
    setRange(swg, 435, 438, false); // Mixed UT/Lon/Geomag (storm-time)
    swg[446] = false; // UT/Lon
  }
  if (swc[6] === 0) {
    // Asymmetric semiannual
    setRange(swg, 221, 224, false); // SPW1 (1,1)
    setRange(swg, 229, 232, false); // SPW1 (3,1)
    setRange(swg, 237, 240, false); // SPW1 (5,1)
    setRange(swg, 279, 282, false); // SPW2 (3,2)
    setRange(swg, 287, 290, false); // SPW2 (5,2)
  }
  if (swc[7] === 0) {
    // Diurnal
    setRange(swg, 398, 401, false); // Geomag (daily)
    setRange(swg, 426, 429, false); // Geomag (storm-time)
  }
  if (swc[11] === 0) {
    // Longitude
    setRange(swg, 402, 410, false); // Mixed UT/Lon/Geomag (daily)
    setRange(swg, 430, 438, false); // Mixed UT/Lon/Geomag (storm-time)
    setRange(swg, 452, 453, false); // UT/Lon
  }
  if (swc[12] === 0) {
    // UT/Lon
    setRange(swg, 411, 414, false); // Mixed UT/Lon/Geomag (daily)
    setRange(swg, 439, 440, false); // Mixed UT/Lon/Geomag (storm-time)
  }
}

/** TRETRV: retrieve the stored legacy switch settings (25 elements). */
export function tretrv(state: MsisState): number[] {
  const svv: number[] = [];
  for (let i = 1; i <= 25; i++) svv.push(state.sav[i]);
  return svv;
}

// ---------------------------------------------------------------------------
// MSISINIT: entry point for initializing the model and loading parameters
// ---------------------------------------------------------------------------

export interface MsisInitOptions {
  /** Raw parameter-space bytes (little-endian real(8) stream). Required. */
  parmBytes: Uint8Array;
  /** Direct globe switches switch_gfn (512). Takes precedence over legacy. */
  globalSwitches?: boolean[];
  /** Legacy switch array switch_legacy (25, default all 1.0). */
  legacySwitches?: number[];
  /** lzalt_type: true = geometric altitude input (default true). */
  geometricAltitude?: boolean;
  /** lspec_select: species density flags (10, default all true). */
  specSelect?: boolean[];
  /** lmass_include: mass-density inclusion flags (10). */
  massInclude?: boolean[];
  /** lN2_msis00: NRLMSISE-00 thermospheric N2 variations (default false). */
  n2Msis00?: boolean;
}

export function msisinit(state: MsisState, opts: MsisInitOptions): void {
  if (!state.haveparmspace) initparmspace(state);

  loadparmset(state, opts.parmBytes);

  // Switches
  state.swg.fill(true);
  for (let i = 1; i <= 25; i++) state.swleg[i] = 1.0;
  if (opts.globalSwitches) {
    if (opts.globalSwitches.length !== MAXNBF) {
      throw new Error("msis: globalSwitches needs 512 entries");
    }
    for (let i = 0; i < MAXNBF; i++) state.swg[i] = opts.globalSwitches[i];
  } else if (opts.legacySwitches) {
    if (opts.legacySwitches.length !== 25) {
      throw new Error("msis: legacySwitches needs 25 entries");
    }
    for (let i = 1; i <= 25; i++) state.swleg[i] = opts.legacySwitches[i - 1];
    tselec(state, opts.legacySwitches);
  }

  // Altitude input type
  state.zaltflag = opts.geometricAltitude ?? true;

  // Species flags
  if (opts.specSelect) {
    if (opts.specSelect.length !== 10) {
      throw new Error("msis: specSelect needs 10 entries");
    }
    for (let i = 1; i <= 10; i++) state.specflag[i] = opts.specSelect[i - 1];
  } else {
    state.specflag.fill(true);
  }
  if (state.specflag[1]) {
    if (opts.massInclude) {
      if (opts.massInclude.length !== 10) {
        throw new Error("msis: massInclude needs 10 entries");
      }
      for (let i = 1; i <= 10; i++) state.massflag[i] = opts.massInclude[i - 1];
    } else {
      state.massflag.fill(true);
    }
  } else {
    state.massflag.fill(false);
  }
  for (let i = 1; i <= 10; i++) state.massflag[i] && (state.specflag[i] = true);
  for (let i = 1; i <= 10; i++) state.masswgt[i] = 0;
  for (let i = 1; i <= 10; i++) if (state.massflag[i]) state.masswgt[i] = 1;
  state.masswgt[1] = 0;
  for (let i = 1; i <= 10; i++) state.masswgt[i] = state.masswgt[i] * SPECMASS[i];
  state.masswgt[10] = 0;

  // NRLMSISE-00 thermospheric N2 flag
  state.N2Rflag = opts.n2Msis00 ?? false;

  state.initflag = true;
}
