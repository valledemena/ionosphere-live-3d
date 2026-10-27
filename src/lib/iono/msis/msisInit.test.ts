// ionosphere-live-3d — drop 9a TU: NRLMSIS constants + init + parm load.
// Pins are independent values computed with a Python struct reader of
// msis21.parm (LE doubles, column-major) and closed-form evaluation of
// the documented constant expressions — not round-trips of the TS code.

import { describe, expect, it } from "vitest";
import { readFileSync } from "node:fs";
import {
  MBAR,
  G0DIVKB,
  MBARG0DIVKB,
  HOA,
  NODES_TN,
  NODES_O1,
  NODES_NO,
  WBETA,
  WGAMMA,
  SPECMASS,
  C2TN,
  GWHT,
  PI,
  NL,
  MAXNBF,
} from "./msisConstants";
import {
  createMsisState,
  msisinit,
  loadparmset,
  tretrv,
} from "./msisInit";
import { dilog, bspline, alt2gph, gph2alt } from "./msisUtils";

function parmBytes(): Uint8Array {
  const url = new URL("./msis21.parm", import.meta.url);
  const buf = readFileSync(url);
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
}

describe("9a constants (independent pins)", () => {
  it("physical constants match closed-form values", () => {
    expect(MBAR).toBe(4.8098277928661373e-26);
    expect(G0DIVKB).toBe(7.102927681112287e26);
    expect(MBARG0DIVKB).toBe(34.163858971332104);
    expect(HOA).toBe(211.95980834824437);
  });
  it("nodes and weights match the reference tables", () => {
    expect(NODES_TN).toHaveLength(30);
    expect(NODES_TN[0]).toBe(-15);
    expect(NODES_TN[1]).toBe(-10);
    expect(NODES_TN[29]).toBe(172.5);
    expect(NODES_O1).toHaveLength(14);
    expect(NODES_O1[0]).toBe(35);
    expect(NODES_O1[13]).toBe(112.5);
    expect(NODES_NO).toHaveLength(14);
    expect(NODES_NO[0]).toBe(47.5);
    expect(NODES_NO[13]).toBe(145);
    expect(WBETA[0]).toBe(5);
    expect(WGAMMA[0]).toBe(5);
    expect(WBETA).toHaveLength(NL + 1);
    expect(SPECMASS[2]).toBe(28.0134 / (1e3 * 6.02214076e23));
    expect(C2TN[0][2]).toBe(33.333333333333336);
    expect(GWHT[0]).toBe(5 / 24);
    expect(PI).toBe(Math.PI);
  });
});

describe("9a initparmspace", () => {
  it("allocates 131 vertical parameters and fills eta/HR tables", () => {
    const s = createMsisState();
    expect(s.nvertparm).toBe(131);
    expect(s.haveparmspace).toBe(true);
    expect(s.initflag).toBe(false);
    expect(s.TN.beta).toHaveLength(MAXNBF);
    expect(s.TN.beta[0]).toHaveLength(24);
    expect(s.O1.beta[0]).toHaveLength(18);
    expect(s.NO.beta[0]).toHaveLength(18);
    // etaTN(0,2) = 1/(nodesTN(1)-nodesTN(0)) = 1/5
    expect(s.etaTN[0][2]).toBe(0.2);
    expect(s.etaTN[0][3]).toBe(0.1);
    expect(s.etaO1[0][2]).toBe(0.2);
    expect(s.etaO1[0][3]).toBe(0.1);
    // etaNO(0,2) = 1/(55-47.5) = 1/7.5
    expect(s.etaNO[0][2]).toBe(1 / 7.5);
    // Untouched rows stay zero (etaTN rows 24..30, etaO1 k=5..6)
    expect(s.etaTN[24][2]).toBe(0);
    expect(s.etaO1[0][5]).toBe(0);
    // Tapered-logistic C1 constraint factors
    expect(s.HRfactO1ref).toBe(0.2689414213699951);
    expect(s.dHRfactO1ref).toBe(6.43656365691809);
    expect(s.HRfactNOref).toBe(0.8175744761936437);
    expect(s.dHRfactNOref).toBe(0.888434919925785);
  });
});

describe("9a loadparmset (real msis21.parm, 536576 B)", () => {
  it("transfers columns bit-exactly (independent Python pins)", () => {
    const s = createMsisState();
    loadparmset(s, parmBytes());
    // TN beta column 0, rows 0..1 (Python struct LE pins)
    expect(s.TN.beta[0][0]).toBe(0.003239199179340925);
    expect(s.TN.beta[1][0]).toBe(5.0026442080241173e-05);
    expect(s.TN.beta[0][1]).toBe(0.0034594526207017357);
    // TN column-0 absolute sum pin
    let sum = 0;
    for (let r = 0; r < MAXNBF; r++) sum += Math.abs(s.TN.beta[r][0]);
    expect(sum).toBe(0.007525866823450319);
    // NO last column head pins
    expect(s.NO.beta[0][17]).toBe(31.519174105979232);
    expect(s.NO.beta[1][17]).toBe(0.04337829255390184);
    // N2 first column is all zeros in the file
    expect(s.N2.beta.every((row) => row[0] === 0)).toBe(true);
    // PR file column (surface pressure slot) is all zeros; pressparm fills cols 1..14
    expect(s.PR.beta.every((row) => row[0] === 0)).toBe(true);
    expect(s.PR.beta[0][1]).not.toBe(0);
    expect(Number.isFinite(s.PR.beta[0][14])).toBe(true);
    // Total nonzero count pin (67072 doubles)
    let nz = 0;
    for (const sub of [s.TN, s.PR, s.N2, s.O2, s.O1, s.HE, s.H1, s.AR, s.N1, s.OA, s.NO]) {
      for (const row of sub.beta) for (const v of row) if (v !== 0) nz++;
    }
    // File has 3605 nonzero; pressparm adds PR cols 1..14 (384 rows x 14, minus zeros)
    expect(nz).toBeGreaterThan(3605);
    // smod flags turn on from the nonlinear solar-flux rows
    expect(s.smod.some((f) => f)).toBe(true);
  });
  it("rejects a truncated stream", () => {
    const s = createMsisState();
    expect(() => loadparmset(s, new Uint8Array(100))).toThrow();
  });
});

describe("9a msisinit switches and flags", () => {
  it("defaults to all-on with geometric altitude", () => {
    const s = createMsisState();
    msisinit(s, { parmBytes: parmBytes() });
    expect(s.initflag).toBe(true);
    expect(s.zaltflag).toBe(true);
    expect(s.N2Rflag).toBe(false);
    expect(s.swg.every((v) => v)).toBe(true);
    expect(s.swleg.slice(1)).toEqual(new Array(25).fill(1));
    expect(tretrv(s).every((v) => Number.isNaN(v))).toBe(true);
    // Default species/mass flags and weights
    expect(s.specflag.slice(1).every((v) => v)).toBe(true);
    expect(s.masswgt[1]).toBe(0);
    expect(s.masswgt[10]).toBe(0);
    expect(s.masswgt[2]).toBe(SPECMASS[2]);
  });
  it("legacy switch 7 off kills diurnal and solar-zenith terms, keeps the global term", () => {
    const s = createMsisState();
    const legacy = new Array(25).fill(1);
    legacy[6] = 0; // switch 7 (diurnal) off
    msisinit(s, { parmBytes: parmBytes(), legacySwitches: legacy });
    expect(s.swg[0]).toBe(true);
    for (let i = 35; i <= 94; i++) expect(s.swg[i]).toBe(false);
    for (let i = 300; i <= 303; i++) expect(s.swg[i]).toBe(false);
    // Untouched bands stay on (semidiurnal)
    for (let i = 95; i <= 144; i++) expect(s.swg[i]).toBe(true);
    expect(tretrv(s)).toEqual(legacy);
  });
});

describe("9a utils smoke", () => {
  it("dilog(0) is exactly 0", () => {
    expect(dilog(0)).toBe(0);
  });
  it("bspline partitions unity at an interior node span", () => {
    const s = createMsisState();
    const { i, S } = bspline(100, NODES_TN, 27, 6, s.etaTN);
    expect(i).toBe(21);
    let sum = 0;
    for (let l = -5; l <= 0; l++) sum += S[l + 5][6];
    expect(sum).toBeCloseTo(1, 9);
  });
  it("gph2alt inverts alt2gph", () => {
    const z = alt2gph(45, 200);
    expect(gph2alt(45, z)).toBeCloseTo(200, 2);
  });
});
