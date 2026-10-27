// SPDX-License-Identifier: GPL-3.0-or-later
// ionosphere-live-3d — NRLMSIS 2.1 math utilities (drop 9a, F2b #9).
//
// TypeScript transcription of `msis_utils.F90` (NRL-SOF-014-1, mirror
// pinned at jacobwilliams/NRLMSIS2.1 commit 9124144). See the NRL-SOF
// section 4(b) note in msisConstants.ts; the original copyright notice
// is reproduced in `nrlmsis2.1_license.txt` in this directory.
//
// All routines are real(8) in the original; JS `number` (IEEE-754
// double) is the same width and every expression keeps the operand
// order of the original.

import { PI } from "./msisConstants";

// ---------------------------------------------------------------------------
// ALT2GPH: geodetic altitude to geopotential height (WGS84 closed form)
// References: DMA TR8350.2 (1987); Featherstone & Claessens (2008);
// Jekeli (2009); NIMA TR8350.2 (2000).
// ---------------------------------------------------------------------------

// WGS84 defining parameters
const WGS_A = 6378.137 * 1e3; // Semi-major axis of reference ellipsoid (m)
const WGS_FINV = 298.257223563; // 1/f = reciprocal of flattening
const WGS_W = 7292115e-11; // Angular velocity of Earth rotation (rad/s)
const WGS_GM = 398600.4418 * 1e9; // Gravitational constant x Earth mass (m^3/s^2)
// WGS84 derived parameters
const WGS_ASQ = WGS_A * WGS_A;
const WGS_WSQ = WGS_W * WGS_W;
const WGS_F = 1.0 / WGS_FINV;
const WGS_ESQ = 2 * WGS_F - WGS_F * WGS_F;
const WGS_E = Math.sqrt(WGS_ESQ); // Ellipsoid eccentricity
const WGS_ELIN = WGS_A * WGS_E; // Linear eccentricity of ellipsoid
const WGS_ELINSQ = WGS_ELIN * WGS_ELIN;
const WGS_EPR = WGS_E / (1 - WGS_F); // Second eccentricity
const WGS_Q0 =
  ((1.0 + (3.0 / (WGS_EPR * WGS_EPR))) * Math.atan(WGS_EPR) -
    3.0 / WGS_EPR) /
  2.0; // DMA TR8350.2, Eq. 3-25
const WGS_U0 =
  ((-WGS_GM * Math.atan(WGS_EPR)) / WGS_ELIN - (WGS_WSQ * WGS_ASQ) / 3.0); // Theoretical potential of reference ellipsoid (m^2/s^2)
const WGS_G0 = 9.80665; // Standard gravity (m/s^2), CGPM 1901; WMO
const WGS_GMDIVELIN = WGS_GM / WGS_ELIN;
// Centrifugal potential taper
const WGS_X0SQ = (2e7) ** 2; // Axial distance squared at which tapering begins (m^2)
const WGS_HSQ = (1.2e7) ** 2; // Relaxation scale length of taper (m^2)

const ALT2GPH_DEG2RAD = 0.017453292519943295;

/**
 * ALT2GPH: geodetic altitude (km) at geodetic latitude (deg) to
 * geopotential height (km).
 */
export function alt2gph(lat: number, alt: number): number {
  // Cartesian and ellipsoidal coordinates (Featherstone eqs. 1, 3, 4, 19, 21)
  const altm = alt * 1000.0;
  const sinsqlat = Math.sin(lat * ALT2GPH_DEG2RAD) ** 2;
  const v = WGS_A / Math.sqrt(1 - WGS_ESQ * sinsqlat);
  const xsq = (v + altm) ** 2 * (1 - sinsqlat);
  const zsq = (v * (1 - WGS_ESQ) + altm) ** 2 * sinsqlat;
  const rsqminElinsq = xsq + zsq - WGS_ELINSQ;
  const usq =
    rsqminElinsq / 2.0 +
    Math.sqrt((rsqminElinsq ** 2) / 4.0 + WGS_ELINSQ * zsq);
  const cossqdelta = zsq / usq;

  // Gravitational potential (Jekeli eqs. 113-114)
  const epru = WGS_ELIN / Math.sqrt(usq);
  const atanepru = Math.atan(epru);
  const q =
    ((1 + (3.0 / (epru * epru))) * atanepru - 3.0 / epru) / 2.0;
  let U =
    -WGS_GMDIVELIN * atanepru -
    ((WGS_WSQ * (WGS_ASQ * q * (cossqdelta - 1 / 3.0)) ) / WGS_Q0) / 2.0;

  // Centrifugal potential (tapered) and total potential
  let Vc: number;
  if (xsq <= WGS_X0SQ) {
    Vc = (WGS_WSQ / 2.0) * xsq;
  } else {
    Vc = (WGS_WSQ / 2.0) * (WGS_HSQ * Math.tanh((xsq - WGS_X0SQ) / WGS_HSQ) + WGS_X0SQ);
  }
  U = U - Vc;

  return (U - WGS_U0) / WGS_G0 / 1000.0;
}

/**
 * GPH2ALT: geopotential height (km) to geodetic altitude (km) at
 * colatitude-ish angle theta, Newton iteration with numerical
 * derivative (epsilon 0.0005, at most 10 steps; first-test loop).
 */
export function gph2alt(theta: number, gph: number): number {
  const epsilon = 0.0005;
  let x = gph;
  let n = 0;
  let dx = epsilon + epsilon;
  while (Math.abs(dx) > epsilon && n < 10) {
    const y = alt2gph(theta, x);
    const dydz = (alt2gph(theta, x + dx) - y) / dx;
    dx = (gph - y) / dydz;
    x = x + dx;
    n = n + 1;
  }
  return x;
}

// ---------------------------------------------------------------------------
// BSPLINE: nonzero b-spline values for all orders up to kmax (max 6)
// ---------------------------------------------------------------------------

export interface BsplineResult {
  /** Index of last nonzero b-spline (-1 below, nd above the nodes). */
  i: number;
  /**
   * Spline values S[l+5][k]: spline index relative to i (l in -5..0),
   * spline order k in 2..6 (positions 0..1 unused, zero).
   */
  S: number[][];
}

/**
 * BSPLINE: evaluate b-splines at x on nodes(0..nd) up to order kmax.
 * eta[row][k] holds the precomputed reciprocal node differences
 * (k index native, positions 0..1 unused).
 */
export function bspline(
  x: number,
  nodes: number[],
  nd: number,
  kmax: number,
  eta: number[][],
): BsplineResult {
  const S: number[][] = Array.from({ length: 6 }, () => [0, 0, 0, 0, 0, 0, 0]);
  let i: number;

  // Index of last (rightmost) nonzero spline
  if (x >= nodes[nd]) {
    i = nd;
    return { i, S };
  }
  if (x <= nodes[0]) {
    i = -1;
    return { i, S };
  }
  let low = 0;
  let high = nd;
  i = Math.floor((low + high) / 2);
  while (x < nodes[i] || x >= nodes[i + 1]) {
    if (x < nodes[i]) {
      high = i;
    } else {
      low = i;
    }
    i = Math.floor((low + high) / 2);
  }

  // Linear splines
  S[0 + 5][2] = (x - nodes[i]) * eta[i][2];
  if (i > 0) S[-1 + 5][2] = 1 - S[0 + 5][2];
  if (i >= nd - 1) S[0 + 5][2] = 0.0; // Reset out-of-bounds spline to zero

  // k = 3 (quadratic splines)
  const w: number[] = [0, 0, 0, 0, 0]; // w(-4..0)
  w[0 + 4] = (x - nodes[i]) * eta[i][3];
  if (i !== 0) w[-1 + 4] = (x - nodes[i - 1]) * eta[i - 1][3];
  if (i < nd - 2) S[0 + 5][3] = w[0 + 4] * S[0 + 5][2];
  if (i - 1 >= 0 && i - 1 < nd - 2)
    S[-1 + 5][3] = w[-1 + 4] * S[-1 + 5][2] + (1.0 - w[0 + 4]) * S[0 + 5][2];
  if (i - 2 >= 0) S[-2 + 5][3] = (1.0 - w[-1 + 4]) * S[-1 + 5][2];

  // k = 4 (cubic splines)
  for (let l = 0; l >= -2; l--) {
    const j = i + l;
    if (j < 0) break; // Skip out-of-bounds splines
    w[l + 4] = (x - nodes[j]) * eta[j][4];
  }
  if (i < nd - 3) S[0 + 5][4] = w[0 + 4] * S[0 + 5][3];
  for (let l = -1; l >= -2; l--) {
    if (i + l >= 0 && i + l < nd - 3)
      S[l + 5][4] = w[l + 4] * S[l + 5][3] + (1.0 - w[l + 1 + 4]) * S[l + 1 + 5][3];
  }
  if (i - 3 >= 0) S[-3 + 5][4] = (1.0 - w[-2 + 4]) * S[-2 + 5][3];

  // k = 5
  for (let l = 0; l >= -3; l--) {
    const j = i + l;
    if (j < 0) break; // Skip out-of-bounds splines
    w[l + 4] = (x - nodes[j]) * eta[j][5];
  }
  if (i < nd - 4) S[0 + 5][5] = w[0 + 4] * S[0 + 5][4];
  for (let l = -1; l >= -3; l--) {
    if (i + l >= 0 && i + l < nd - 4)
      S[l + 5][5] = w[l + 4] * S[l + 5][4] + (1.0 - w[l + 1 + 4]) * S[l + 1 + 5][4];
  }
  if (i - 4 >= 0) S[-4 + 5][5] = (1.0 - w[-3 + 4]) * S[-3 + 5][4];
  if (kmax === 5) return { i, S }; // Exit if only 5th order spline is needed

  // k = 6
  for (let l = 0; l >= -4; l--) {
    const j = i + l;
    if (j < 0) break; // Skip out-of-bounds splines
    w[l + 4] = (x - nodes[j]) * eta[j][6];
  }
  if (i < nd - 5) S[0 + 5][6] = w[0 + 4] * S[0 + 5][5];
  for (let l = -1; l >= -4; l--) {
    if (i + l >= 0 && i + l < nd - 5)
      S[l + 5][6] = w[l + 4] * S[l + 5][5] + (1.0 - w[l + 1 + 4]) * S[l + 1 + 5][5];
  }
  if (i - 5 >= 0) S[-5 + 5][6] = (1.0 - w[-4 + 4]) * S[-4 + 5][5];

  return { i, S };
}

// ---------------------------------------------------------------------------
// DILOG: dilogarithm on [0,1), expansion to order 3 (relative error < 1e-5)
// Reference: Ginsberg & Zaborowski (1975), Commun. ACM 18.
// ---------------------------------------------------------------------------

const PI2_6 = (PI * PI) / 6.0;

export function dilog(x0: number): number {
  let x = x0;
  if (x > 0.5) {
    const lnx = Math.log(x);
    x = 1.0 - x; // Reflect argument into [0,0.5] range
    const xx = x * x;
    const x4 = 4.0 * x;
    return (
      PI2_6 -
      lnx * Math.log(x) -
      (4.0 * xx * (23.0 / 16.0 + x / 36.0 + xx / 576.0 + (xx * x) / 3600.0) +
        x4 +
        3.0 * (1.0 - xx) * lnx) /
        (1.0 + x4 + xx)
    );
  } else {
    const xx = x * x;
    const x4 = 4.0 * x;
    return (
      (4.0 * xx * (23.0 / 16.0 + x / 36.0 + xx / 576.0 + (xx * x) / 3600.0) +
        x4 +
        3.0 * (1.0 - xx) * Math.log(1.0 - x)) /
      (1.0 + x4 + xx)
    );
  }
}
