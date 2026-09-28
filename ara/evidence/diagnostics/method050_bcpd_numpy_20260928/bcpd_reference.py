#!/usr/bin/env python3
"""Independent dense NumPy implementation of Hirose's BCPD (TPAMI 2021, Algorithm 1) used to
check METHOD-050's C++ update (tests/unit/geometry/Test.CoherentPointDriftBayesian.cpp).

omega = 0, kappa = infinity, gamma = 1, no input normalization. The fixture (60 points, seed 3)
is rounded to float32 so the C++ solver (float input) sees the same values.

usage: bcpd_reference.py <out dir> <lambda> <beta> <iterations> <estimate scale 0|1> <variance terms 0|1>
Writes source.txt/target.txt and prints "<iteration> <sigma^2> <scale>" per iteration.
"""
import sys
import numpy as np

out, lam, beta, iters = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4])
estimate_scale, variances = sys.argv[5] == '1', sys.argv[6] == '1'
rng = np.random.default_rng(3)
M = 60
Y = rng.uniform(-1, 1, (M, 3)); Y[:, 2] *= 0.6
X = Y + np.c_[0.08 * np.sin(2 * Y[:, 1]), np.zeros(M), 0.08 * np.cos(1.5 * Y[:, 0])]
Y = Y.astype(np.float32).astype(np.float64); X = X.astype(np.float32).astype(np.float64)
np.savetxt(f'{out}/source.txt', Y, fmt='%.9g'); np.savetxt(f'{out}/target.txt', X, fmt='%.9g')
N, D = len(X), 3
G = np.exp(-((Y[:, None, :] - Y[None, :, :]) ** 2).sum(-1) / (2 * beta ** 2))
sig2 = ((X[:, None, :] - Y[None, :, :]) ** 2).sum() / (N * M * D)
v = np.zeros((M, 3)); var = np.zeros(M); alpha = np.full(M, 1.0 / M)
s, R, t = 1.0, np.eye(3), np.zeros(3)
for k in range(iters):
    yhat = s * (Y + v) @ R.T + t
    d2 = ((X[None, :, :] - yhat[:, None, :]) ** 2).sum(-1)                      # M x N
    logp = -d2 / (2 * sig2) + (np.log(alpha) - s * s * D * var / (2 * sig2))[:, None]
    logp -= logp.max(0, keepdims=True)
    P = np.exp(logp); P /= P.sum(0, keepdims=True)
    nu = P.sum(1); Nh = nu.sum(); xhat = (P @ X) / nu[:, None]
    Tinv = (xhat - t) @ R / s
    Sigma = np.linalg.inv(lam * np.linalg.inv(G) + np.diag(s * s / sig2 * nu))
    v = s * s / sig2 * Sigma @ (nu[:, None] * (Tinv - Y))
    var = np.diag(Sigma).copy() if variances else np.zeros(M)
    u = Y + v
    xbar = (nu[:, None] * xhat).sum(0) / Nh; ubar = (nu[:, None] * u).sum(0) / Nh; vbar = (nu * var).sum() / Nh
    Sxu = ((nu[:, None] * (xhat - xbar)).T @ (u - ubar)) / Nh
    Suu = ((nu[:, None] * (u - ubar)).T @ (u - ubar)) / Nh + vbar * np.eye(3)
    U, _, Vt = np.linalg.svd(Sxu); C = np.eye(3); C[2, 2] = np.sign(np.linalg.det(U @ Vt))
    R = U @ C @ Vt
    s = np.trace(R.T @ Sxu) / np.trace(Suu) if estimate_scale else 1.0
    t = xbar - s * R @ ubar
    yhat = s * u @ R.T + t
    sig2 = (P * ((X[None, :, :] - yhat[:, None, :]) ** 2).sum(-1)).sum() / (Nh * D) + s * s * vbar
    print(f"{k} {sig2:.17g} {s:.17g}")
