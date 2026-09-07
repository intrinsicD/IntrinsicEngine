Accepted: β·(K_new−K_old), symmetric sign, and "missed competitor = search limitation, logged not failed". Here is the smallest comparator.

**Energy (named `NeckEnergy`)**

For a partition P of triangles, with per-edge dihedral term κ_e = dot(n_j−n_i, c_j−c_i)/|c_j−c_i|², smoothed to κ_r,e by averaging over edges within geodesic radius r:

```
E(P) = Σ_{e ∈ ∂P} len_e · (1 + λ · r · κ_r,e)  +  β · K(P)
ΔE(split) = boundary_cost(new ∂) + β·(K_new − K_old)
accept split iff ΔE < 0
```

κ_r,e > 0 convex, < 0 concave, 0 flat. r is physical (fraction of bbox diagonal), λ dimensionless.

Fail-closed property is structural, not threshold-based: on any convex mesh every edge has κ ≥ 0, so every boundary edge costs at least len_e, and every split has ΔE > β ≥ 0. Sphere, cylinder of any aspect, ellipsoid of any aspect, and a convex ridge are all rejected regardless of λ, r, β. No waist-ratio heuristic is needed or used.

Gain exists only where a closed cut runs mostly through edges with λ·r·κ_r,e < −1. That is the only tunable content, and it is calibrated once on the synthetic family below.

**Candidate generation**

Harmonic fields φ between farthest-point seed pairs supply order only. For each field, sweep level t and compute the geometric normalized cut:

```
N(t) = len(C_t) / sqrt(min(A(φ<t), A(φ>t)))
```

Keep strict local minima of N(t) whose prominence against the same field's profile within ±Δt exceeds a value fixed on the synthetic family. Each surviving C_t becomes one candidate split. Rank candidates by ΔE. Accept greedily while ΔE < 0, recomputing on the current partition. Region count is an outcome, never an input.

**Frog protocol**

1. Compute E_old on the raw local039 labels before any cleanup. Store value, label hash, and per-region boundary costs.
2. For each plane singly and for all seven subsets jointly: relabel only the dominant region by half-space, keep the four tiny regions fixed, compute ΔE on raw labels. No cleanup on either side.
3. Every plane result is a speculative competitor. Report ΔE sign and magnitude, not a verdict.
4. Restore labels, recompute E, assert exact equality with E_old and hash match. Any mismatch aborts the report.

**Tests on synthetic family (all before frog)**

- Sphere, cylinder aspect 1 to 8, ellipsoid aspect 1 to 8: zero accepted splits, and min ΔE over all candidates > 0.
- Box with rounded convex ridge: zero accepted splits.
- Two spheres joined by a neck with radius ratio ρ in {0.3, 0.5, 0.7, 0.9}: for ρ below the calibrated ρ*, exactly one accepted split, cut within one edge length of the true waist. Above ρ*, none. Record ρ* as the declared sensitivity, not a quality claim.
- Three-lobe chain: two accepted splits, three regions, without setting a count.
- Invariance: uniform scale, rotation, and one subdivision level preserve every accept and reject decision.
- Baseline recovery: E recomputed on restored labels equals stored E_old exactly.

Deliverable after tests pass: λ, r, Δt, prominence, and ρ* frozen in one config file, then the frog run.
