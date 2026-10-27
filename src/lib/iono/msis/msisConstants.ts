// SPDX-License-Identifier: GPL-3.0-or-later
// ionosphere-live-3d — NRLMSIS 2.1 model constants (drop 9a, F2b #9).
//
// TypeScript transcription of `msis_constants.F90` from the NRLMSIS 2.1
// reference package (NRL-SOF-014-1, mirror pinned at
// jacobwilliams/NRLMSIS2.1 commit 9124144).
//
// NRL-SOF section 4(b) compliance: this is a verbatim translation of the
// logic of the NRL-SOF; the original copyright notice is reproduced in
// `nrlmsis2.1_license.txt` in this directory and redistribution follows
// the terms stated there. Portions of NRLMSIS Copyright (C) 2021 U.S.
// Naval Research Laboratory; authors D. P. Drob, J. T. Emmert,
// J. M. Picone, R. R. Meier.
//
// Numerical notes (faithfulness):
// - The reference builds with DBLE (real(8)); JS `number` is IEEE-754
//   double, the same width, and every expression keeps the operand
//   order of the original.
// - Fortran 1-based arrays (`specmass(1:10)`) are emulated with arrays
//   whose index 0 holds NaN (out-of-bounds reads fail fast instead of
//   silently shifting). Fortran 0-based arrays keep native indexing.
// - Fortran `reshape` literals are column-major: the flat source order
//   fills the FIRST index fastest. The 2-D tables below preserve the
//   original (i,j) access pattern with 0-based indices.

/** Double-precision kind equivalent (reference builds with DBLE). */
export type Rp = number;

/** Missing-value sentinel (dmissing). */
export const DMISSING = 9.999e-38;

/** Pi (32-digit source literal; parses to the same double as Math.PI). */
export const PI = 3.1415926535897932384626433832795;
export const DEG2RAD = PI / 180.0;
export const DOY2RAD = (2.0 * PI) / 365.0;
export const LST2RAD = PI / 12.0;
export const TANH1 = Math.tanh(1.0);

/** Boltzmann constant, CODATA 2018 (J/K). */
export const KB = 1.380649e-23;
/** Avogadro constant, CODATA 2018 (1/mol). */
export const NA = 6.02214076e23;
/** Reference gravity, CIMO Guide 2014 (m/s^2). */
export const G0 = 9.80665;

// ---------------------------------------------------------------------------
// Species data and derived physical constants (1-based emulation)
// ---------------------------------------------------------------------------

const SPECMASS_RAW: number[] = [
  0.0, // 1 - Mass density (dummy value)
  28.0134, // 2 - N2
  31.9988, // 3 - O2
  31.9988 / 2.0, // 4 - O
  4.0, // 5 - He
  1.0, // 6 - H
  39.948, // 7 - Ar
  28.0134 / 2.0, // 8 - N
  31.9988 / 2.0, // 9 - Anomalous O
  (28.0134 + 31.9988) / 2.0, // 10 - NO
];
/** Species molecular masses in kg/molecule; index 0 is NaN (1-based). */
export const SPECMASS: number[] = [
  Number.NaN,
  ...SPECMASS_RAW.map((m) => m / (1.0e3 * NA)),
];

/** Dry-air mean mass in fully mixed atmosphere, CIPM 2007 (kg/molecule). */
export const MBAR = 28.96546 / (1.0e3 * NA);

const LNVMR_RAW: number[] = [
  1.0, // 1 - Mass density (dummy value)
  0.780848, // 2 - N2
  0.20939, // 3 - O2
  1.0, // 4 - O (dummy value)
  0.0000052, // 5 - He
  1.0, // 6 - H (dummy value)
  0.009332, // 7 - Ar
  1.0, // 8 - N (dummy value)
  1.0, // 9 - Anomalous O (dummy value)
  1.0, // 10 - NO (dummy value)
];
/** Dry-air log volume mixing ratios, CIPM 2007; index 0 is NaN. */
export const LNVMR: number[] = [Number.NaN, ...LNVMR_RAW.map((v) => Math.log(v))];

/** Natural log of global average surface pressure (Pa). */
export const LNP0 = 11.515614;
/** g0/kB factor, K/(kg km). */
export const G0DIVKB = ((G0 / KB) * 1.0e3);
/** Mbar*g0/kB factor used by the pressure integration, K/km. */
export const MBARG0DIVKB = ((MBAR * G0) / KB) * 1.0e3;

// ---------------------------------------------------------------------------
// Vertical profile parameters
// ---------------------------------------------------------------------------
export const NSPEC = 11; // Number of species including temperature
export const ND = 27; // Number of temperature profile nodes
export const P = 4; // Spline order
export const NL = ND - P; // Last temperature profile level index
export const NLS = 9; // Last parameter index for each species (excluding O, NO splines)
export const BWALT = 122.5; // Reference geopotential height for Bates profile
export const ZETAF = 70.0; // Fully mixed below this, uses constant mixing ratios
export const ZETAB = BWALT; // Bates profile above this altitude
export const ZETAA = 85.0; // Default reference height for active minor species
export const ZETAGAMMA = 100.0; // Reference height of tanh taper of chemical/dynamical correction scale height
export const HGAMMA = 1.0 / 30.0; // Inverse scale height of tanh taper
export const IZFMAX = 13; // Fully mixed below this spline index
export const IZFX = 14; // Spline index at zetaF
export const IZAX = 17; // Spline index at zetaA
export const ITEX = NL; // Index of Bates exospheric temperature
export const ITGB0 = NL - 1; // Index of Bates temperature gradient at lower boundary
export const ITB0 = NL - 2; // Index of Bates temperature at lower boundary
// O1 spline parameters
export const NDO1 = 13;
export const NSPLO1 = NDO1 - 5; // Unconstrained spline parameters for O1 (2 additional C1-constrained splines)
export const ZETAREF_O1 = ZETAA; // Joining height for O1 splines, reference height for O1 density
// NO spline parameters
export const NDNO = 13;
export const NSPLNO = NDNO - 5; // Unconstrained spline parameters for NO (2 additional C1-constrained splines)
export const ZETAREF_NO = ZETAB; // Joining height for NO splines, reference height for NO density
// Anomalous oxygen (legacy NRLMSISE-00 profile)
export const ZETAREF_OA = ZETAB;
export const TOA = 4000.0; // Temperature of anomalous oxygen density (K)
export const HOA = ((KB * TOA) / ((16.0 / (1.0e3 * NA)) * G0)) * 1.0e-3; // Hydrostatic scale height (km)

// ---------------------------------------------------------------------------
// Spline nodes
// ---------------------------------------------------------------------------

/** Vertical temperature spline nodes (0..ND+2). */
export const NODES_TN: number[] = [
  -15.0, -10.0, -5.0, 0.0, 5.0, 10.0, 15.0, 20.0, 25.0, 30.0, 35.0, 40.0,
  45.0, 50.0, 55.0, 60.0, 65.0, 70.0, 75.0, 80.0, 85.0, 92.5, 102.5,
  112.5, 122.5, 132.5, 142.5, 152.5, 162.5, 172.5,
];

/** O1 lower-thermosphere nodes, domain 50-85 km (0..NDO1). */
export const NODES_O1: number[] = [
  35.0, 40.0, 45.0, 50.0, 55.0, 60.0, 65.0, 70.0, 75.0, 80.0, 85.0, 92.5,
  102.5, 112.5,
];

/** NO lower-thermosphere nodes, domain 70-122.5 km (0..NDNO). */
export const NODES_NO: number[] = [
  47.5, 55.0, 62.5, 70.0, 77.5, 85.0, 92.5, 100.0, 107.5, 115.0, 122.5,
  130.0, 137.5, 145.0,
];

// ---------------------------------------------------------------------------
// NRLMSIS-2.0 legacy compatibility coefficients
// ---------------------------------------------------------------------------

/**
 * C2 continuity matrix for temperature; last 3 splines constrained.
 * Column-major source: first column [1,-10,33.33], second [1,0,-16.67],
 * third [1,10,33.33]. Access C2TN[i][j] matches c2tn(i+1,j+1).
 */
export const C2TN: number[][] = [
  [1.0, -10.0, 33.333333333333336],
  [1.0, 0.0, -16.666666666666668],
  [1.0, 10.0, 33.333333333333336],
];

/** C1 continuity for O1; last 2 splines constrained. */
export const C1O1: number[][] = [
  [1.75, -2.916666573405061],
  [-1.624999900076852, 21.458332647194382],
];

/** Weights for coefficients on 3rd-to-last O1 spline. */
export const C1O1ADJ: number[] = [0.257142857142857, -0.102857142686844];

/** C1 continuity for NO; last 2 splines constrained. */
export const C1NO: number[][] = [
  [1.5, -3.75],
  [0.0, 15.0],
];

/** Weights for coefficients on 3rd-to-last NO spline. */
export const C1NOADJ: number[] = [0.166666666666667, -0.066666666666667];

// ---------------------------------------------------------------------------
// Horizontal and time-dependent basis function (gfn) parameters
// ---------------------------------------------------------------------------
export const MAXNBF = 512; // Number of basis functions to be allocated
export const MAXN = 6; // Maximum latitude (Legendre) spectral degree
export const MAXL = 3; // Maximum local time (tidal) spectral order
export const MAXM = 2; // Maximum longitude (SPW) order
export const MAXS = 2; // Maximum day of year (intra-annual) Fourier order
export const AMAXN = 6; // Maximum Legendre degree in zonal mean terms
export const AMAXS = 2; // Maximum intra-annual order in zonal mean terms
export const TMAXL = 3; // Maximum tidal order used
export const TMAXN = 6; // Maximum Legendre degree coupled with tides
export const TMAXS = 2; // Maximum intra-annual order coupled with tides
export const PMAXM = 2; // Maximum SPW order used
export const PMAXN = 6; // Maximum Legendre degree coupled with SPW
export const PMAXS = 2; // Maximum intra-annual order coupled with SPW
export const NSFX = 5; // Number of linear solar flux terms
export const NSFXMOD = 5; // Number of nonlinear modulating solar flux terms
export const NMAG = 54; // Number of terms in legacy geomagnetic parameterization
export const NUT = 12; // Number of terms in legacy UT parameterization
export const CTIMEIND = 0;
export const CINTANN = CTIMEIND + (AMAXN + 1);
export const CTIDE = CINTANN + (AMAXN + 1) * 2 * AMAXS;
export const CSPW =
  CTIDE +
  (4 * TMAXS + 2) * (TMAXL * (TMAXN + 1) - (TMAXL * (TMAXL + 1)) / 2);
export const CSFX =
  CSPW + (4 * PMAXS + 2) * (PMAXM * (PMAXN + 1) - (PMAXM * (PMAXM + 1)) / 2);
export const CEXTRA = CSFX + NSFX;
export const MBF = 383; // Last index of linear terms
export const CNONLIN = MBF + 1; // Starting index of nonlinear terms
export const CSFXMOD = CNONLIN; // Starting index of modulating solar flux terms
export const CMAG = CSFXMOD + NSFXMOD; // Starting index of daily geomagnetic terms
export const CUT = CMAG + NMAG; // Starting index of UT terms

// ---------------------------------------------------------------------------
// Integration weights and reference spline values
// ---------------------------------------------------------------------------

/** Weights for log-pressure spline coefficients from temperature (0..3). */
export const GWHT: number[] = [5.0 / 24.0, 55.0 / 24.0, 55.0 / 24.0, 5.0 / 24.0];

/** Weights for 1st spline integration wbeta(0..NL). */
export const WBETA: number[] = Array.from(
  { length: NL + 1 },
  (_, j) => (NODES_TN[4 + j] - NODES_TN[j]) / 4.0,
);
/** Weights for 2nd spline integration wgamma(0..NL). */
export const WGAMMA: number[] = Array.from(
  { length: NL + 1 },
  (_, j) => (NODES_TN[5 + j] - NODES_TN[j]) / 5.0,
);

/** Non-zero bspline values at zetaB, 5th order (0..3). */
export const S5ZETAB: number[] = [
  0.041666666666667, 0.458333333333333, 0.458333333333333,
  0.041666666666667,
];
/** Non-zero bspline values at zetaB, 6th order (0..4). */
export const S6ZETAB: number[] = [
  0.008771929824561, 0.216228070175439, 0.55, 0.216666666666667,
  0.008333333333333,
];
/** Weights for temperature gradient at zetaA (0..2). */
export const WGHTAXDZ: number[] = [-0.102857142857, 0.0495238095238, 0.053333333333];
/** Non-zero bspline values at zetaA, 4th order (0..2). */
export const S4ZETAA: number[] = [0.257142857142857, 0.653968253968254, 0.088888888888889];
/** Non-zero bspline values at zetaA, 5th order (0..3). */
export const S5ZETAA: number[] = [
  0.085714285714286, 0.587590187590188, 0.313020313020313,
  0.013675213675214,
];
/** Non-zero bspline values at zetaA, 6th order (0..4). */
export const S6ZETAA: number[] = [
  0.023376623376623, 0.378732378732379, 0.500743700743701,
  0.095538448479625, 0.001608848667672,
];
/** Non-zero bspline values at zetaF, 4th order (0..2). */
export const S4ZETAF: number[] = [0.166666666666667, 0.666666666666667, 0.166666666666667];
/** Non-zero bspline values at zetaF, 5th order (0..3). */
export const S5ZETAF: number[] = [
  0.041666666666667, 0.458333333333333, 0.458333333333333,
  0.041666666666667,
];
/** Non-zero bspline values at zeta=0, 5th order (0..2). */
export const S5ZETA0: number[] = [0.458333333333333, 0.458333333333333, 0.041666666666667];
