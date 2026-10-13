# BCPD: computational extensions by authors other than Hirose (acceleration, scalability, implementations)

Scope note: the baseline is Hirose, "A Bayesian Formulation of Coherent Point Drift", IEEE TPAMI 43(7), 2021, DOI 10.1109/TPAMI.2020.2971687 ([IEEE](https://ieeexplore.ieee.org/document/8985307/), [DOI](https://doi.org/10.1109/tpami.2020.2971687)). Hirose's own acceleration (Nyström + KD-tree inside VB; BCPD++ downsampling + GP interpolation; GBCPD; DET) is listed only as context. Method: web search, the Semantic Scholar "cited by" list for the TPAMI paper, and reading the source or README of the third-party code. The main result is negative: **I found no peer-reviewed paper by another group whose main contribution is speeding up or scaling BCPD itself.** Third-party work on BCPD is in model extensions (generalized/anisotropic, multi-organ) and in re-implementations.

## 1. GPU implementations and libraries that include BCPD, and how they differ from the paper

### Takeaway
I found no GPU (CUDA/PyTorch/JAX) implementation of BCPD. GPU ports exist only for classic CPD. Two third-party BCPD codes exist and neither is peer-reviewed. probreg (Python, neka-nat) has a dense, experimental BCPD that differs markedly from the paper. rbcpd (R, BIMSB) bundles a pinned copy of the official C code with BCPD++ acceleration.

### Cited Findings
- **Reference code (context, Hirose):** ohirose/bcpd is C (OpenMP and OpenBLAS are build prerequisites). Its Python and MATLAB demos are wrappers that call the binary. The README does not mention a GPU. Acceleration flags: `-A` (preset Nyström + KD-tree), `-K`/`-J` (Nyström sample counts for G and P), `-p` (KD-tree search with `-d7 -e0.15 -f0.2`), and `-D` (voxel, ball or random downsampling with interpolation back to full resolution = "++"). The README warns that Nyström alone will probably not converge without the KD-tree. It recommends one acceleration method for several thousand points and both for several hundred thousand, because otherwise memory allocation fails. — [ohirose/bcpd README](https://github.com/ohirose/bcpd)
- **probreg (neka-nat, Python, code-only):** the README labels BCPD "experimental". Only the combined model (rigid + scale + nonrigid) is implemented, with no rigid-only BCPD. cupy/CUDA acceleration is documented only for rigid/affine CPD, and the timing section leaves out BCPD. — [probreg README](https://github.com/neka-nat/probreg); [PyPI](https://pypi.org/project/probreg)
- **Deviations in probreg `bcpd.py` from the paper (source read):**
  - The m×m Gram matrix is dense and inverted explicitly with `np.linalg.inv`, and every M-step inverts another dense m×m matrix. There is no Nyström.
  - P is computed dense over all M×N pairs. The KD-tree is used only for the RMSE stopping criterion.
  - The default kernel is inverse multiquadric (`imq`), with `rbf` as an option.
  - Defaults are `lmd=2.0`, `k=1e20` (so α≈1/M is effectively fixed), `gamma=1.0`, `beta=1.0`, and `w=0.0` (outlier term off). `maxiter=50`, `tol=1e-3` on the change in RMSE.
  - σ² is floored with `max(abs(sigma2), eps)` and ν with `1e-20`. Zero denominators are replaced by float32 eps.
  - — [probreg/bcpd.py](https://raw.githubusercontent.com/neka-nat/probreg/master/probreg/bcpd.py)
- **rbcpd (Artür Manukyan, BIMSB Berlin, R, v0.1.0, code-only):** "native R bindings to a bundled, pinned implementation" of BCPD. It runs the BCPD++ workflow in memory: representative-point downsampling, Nyström low-rank approximation of G and P, KD-tree search near convergence, and interpolation back to all points. The page does not name the upstream commit. — [rbcpd on r-universe](https://bimsbbioinfo.r-universe.dev/rbcpd); repo [BIMSBbioinfo/rbcpd](https://github.com/BIMSBbioinfo/rbcpd)
- **GPU work in the vicinity (CPD only, not BCPD):** cpd-pytorch / CPD-Pytorch (CPU/CUDA/MPS, batched, block-wise E-step, 100k points in under 1 GB), torchcpd (pycpd port, CUDA), cmsaliba/coherent_point_drift_cuda (CUDA C++), and nestormh/GPUCPD (discontinued). — [YuliangXiaoYLX/CPD-Pytorch](https://github.com/YuliangXiaoYLX/CPD-Pytorch); [ramtingh/torchcpd](https://github.com/ramtingh/torchcpd); [cmsaliba/coherent_point_drift_cuda](https://github.com/cmsaliba/coherent_point_drift_cuda); [nestormh/GPUCPD](https://github.com/nestormh/GPUCPD)
- **ECPD (Golyanik et al.)** implements the Fast Gauss Transform on the GPU for Extended CPD. This is a CPD variant, not BCPD. — [ECPD 2016 PDF](https://people.mpi-inf.mpg.de/~golyanik/04_DRAFTS/ECPD2016.pdf)
- A 2026 arXiv paper (Structured Analytic CPD) evaluated BCPD with the official Windows executable. Comparison studies therefore use the reference binary rather than ports. — [arXiv 2605.00934](https://arxiv.org/pdf/2605.00934)

### Inferences
- The only BCPD with a GPU is effectively the one you write yourself. The E-step (M×N responsibilities) and the M×M kernel products map onto exactly the block-wise E-step patterns that cpd-pytorch already uses for CPD.
- probreg's BCPD is not a valid paper reference. It has O(M²) memory plus O(M³) inversion per iteration, no outliers by default, and fixed mixture weights. Benchmarks using "BCPD via probreg" are therefore not comparable with the paper.
- rbcpd is a binding of the original rather than an independent implementation, so its numbers should match the reference.

### Gaps
- I did not check pycpd, Open3D plugins or MATLAB File Exchange explicitly. My searches found no BCPD in pycpd, and Open3D does not ship BCPD. I found no MATLAB toolbox of BCPD by a third party.
- I found no JAX implementation.
- I did not search GitHub code search systematically (no API access). Smaller ports may exist.

## 2. Low-rank / Nyström / random features / FGT / KD-tree / multiscale applied to BCPD

### Takeaway
All known acceleration techniques for BCPD come from Hirose himself: Nyström + KD-tree in TPAMI 2021, BCPD++ downsampling + GP interpolation, the Nyström + parallel-Dijkstra acceleration in GBCPD (TPAMI 2022), and DET (TPAMI 2026). I found no third-party paper with random features, FGT, IFGT or a multiscale BCPD.

### Cited Findings
- (Context, Hirose) The bottlenecks are G at O(M²), P at O(MN) and Σ at O(M³). Nyström + KD-tree reduces them to at most O((M+N) log(M+N)). — [BCPD, ResearchGate](https://www.researchgate.net/publication/339084651_A_Bayesian_Formulation_of_Coherent_Point_Drift)
- (Context, Hirose) GBCPD: the geodesic kernel conversion is accelerated with Nyström and parallel Dijkstra runs and is applicable to several million points. IEEE TPAMI 2022, DOI 10.1109/TPAMI.2022.3214191. — [GBCPD, ResearchGate](https://www.researchgate.net/publication/364361740_Geodesic-Based_Bayesian_Coherent_Point_Drift)
- (Context, Hirose) "++" = downsampling + displacement-field interpolation. DET was published in IEEE TPAMI (Sep 2026; arXiv Mar 2026; code from Mar 2025). — [ohirose/bcpd README](https://github.com/ohirose/bcpd)
- **Third-party reuse of the "++" scheme:** rbcpd exposes downsampling + Nyström + KD-tree as an R API (see section 1). — [rbcpd](https://bimsbbioinfo.r-universe.dev/rbcpd)
- In the Semantic Scholar citations of BCPD, no title contains "GPU", "real-time", "stochastic" or "low-rank". "Accelerated" appears only in Yao et al., "Fast and Robust Non-Rigid Registration Using Accelerated Majorization-Minimization" (TPAMI 2023, DOI 10.1109/TPAMI.2023.3247603, arXiv 2206.03410). That is an independent ICP-like method that cites BCPD as a baseline, not a BCPD acceleration. — [Semantic Scholar API, citations of DOI 10.1109/TPAMI.2020.2971687](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.2971687/citations?fields=title,year,venue,externalIds&limit=1000)

### Inferences
- An FGT/IFGT or KeOps-style kernel reduction for the BCPD E-step would be new as a third-party contribution. The structure is the same as in CPD, plus the additional per-point terms from the posterior covariance (σ²·tr(Σ_m)) in the Gaussian exponent.
- Hirose's README warns that Nyström alone does not converge, which marks a real stability hazard for low-rank approximations of P. Any new low-rank method must address it.

### Gaps
- The Semantic Scholar list was matched on titles only. A paper that accelerates BCPD as a side contribution in its body (for example a medical-imaging pipeline) may be missed.
- I found no evidence of multiscale or coarse-to-fine BCPD (σ/β annealing across pyramid levels) by other authors.

## 3. Stochastic/mini-batch VI, online/streaming BCPD, real-time tracking

### Takeaway
I found no published stochastic, mini-batch or online BCPD. Real-time deformable object tracking in robotics (DLO/cloth) is built on CPD variants (CDCPD/CDCPD2, SPR, TrackDLO), not on BCPD. In those works BCPD appears at most as a baseline.

### Cited Findings
- CDCPD / CDCPD2 (Chi & Berenson, IROS 2019; Wang, McConachie, Berenson): constrained CPD for occlusion-robust rope and cloth tracking without physics simulation. Code: UM-ARM-Lab/cdcpd. Not BCPD. — [arXiv 2101.00733](https://arxiv.org/pdf/2101.00733); [UM-ARM-Lab/cdcpd](https://github.com/UM-ARM-Lab/cdcpd)
- TrackDLO (Xiang et al., IEEE RA-L 2023): real-time DLO tracking under occlusion using motion coherence theory. Not BCPD. — [TrackDLO PDF](http://brian.coltin.org/pub/xiang2023trackdlo.pdf)
- Tang, Wang, Tomizuka (RA-L 2018): a CPD-based state-estimation framework for rope manipulation. — [rope_framework.pdf](https://changhaowang.github.io/files/rope_framework.pdf)
- MotionDLO (arXiv 2608.22398, preprint): event + frame-based CPD tracking with 12 ms updates and ≤0.43 mm point-to-curve error. CPD, not BCPD, and not peer-reviewed. — [arXiv 2608.22398](https://arxiv.org/html/2608.22398)
- The BCPD citation list contains "Overlap-Aware Online-Adaptive Non-Rigid Registration of Intraoperative Tissue in Minimally Invasive Surgery" (IEEE TMI 2025, DOI 10.1109/TMI.2025.3620746). Whether it builds on BCPD and is "online" in the streaming sense could not be checked. — [Semantic Scholar citations](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.2971687/citations?fields=title,year,venue,externalIds&limit=1000)

### Inferences
- The likely reason BCPD is absent from robot tracking is that the official code runs as a CLI binary with file I/O and has no warm-start or streaming API. Tracking pipelines need per-frame warm starts from the previous state. Probably the most practical "online BCPD" path is to warm-start σ², the mixture weights and v from the previous frame. That is unpublished, so this is an inference only.

### Gaps
- I found no stochastic or mini-batch VI for BCPD, and no sources at all. I could not check the full text of the TMI 2025 paper.

## 4. Numerical stability (scale collapse, σ², initialization)

### Takeaway
Third-party stability work comes mainly from Min, Meng et al. (CUHK/UCL). They generalized BCPD to normals (6D) and anisotropic noise for robustness against noise and outliers in surgical navigation. I found no third-party paper that analyses scale collapse in BCPD specifically. Stability knowledge lives in Hirose's README flags and in implementation guards (probreg).

### Cited Findings
- Zhang, Min, Pan, Meng, "Robust and Accurate Point Set Registration with Generalized Bayesian Coherent Point Drift", IEEE/RSJ IROS 2021, pp. 516–523, DOI 10.1109/IROS51168.2021.9635908. It targets robustness to noise and outliers in surgical navigation. — [UCL Discovery](https://discovery.ucl.ac.uk/view/people/ZMINX16.html)
- Min et al., "Anisotropic Generalized Bayesian Coherent Point Drift for Point Set Registration", IEEE T-ASE 2023, DOI 10.1109/TASE.2022.3159553. It generalizes BCPD to 6D (position + normal) with anisotropic positional noise. — [UCL PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
- "Generalized 3D Rigid Point Set Registration with Anisotropic Positional Error Based on Bayesian Coherent Point Drift", IEEE ICRA 2022, DOI 10.1109/icra46639.2022.9812395. — [Semantic Scholar citations](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.2971687/citations?fields=title,year,venue,externalIds&limit=1000)
- Joutard et al., "A multi-organ point cloud registration algorithm for abdominal CT registration" (MO-BCPD), WBIR 2022, DOI 10.1007/978-3-031-11203-4_9, arXiv 2203.08041. It registers several organs jointly with per-organ elasticity, inter-organ motion coherence and segmentation tolerance. TRE is about half that of BCPD (13 mm vs 22 mm according to a search snippet). No acceleration and no code. — [arXiv 2203.08041](https://arxiv.org/abs/2203.08041)
- (Context, Hirose README) Practical stability guards:
  - `-k` infinity, or very large values, can cause errors.
  - A minimum of 30 VB loops can cause errors on small data (`-N1`).
  - `-g` (γ, scaling of the initial σ²) of 2–10 for large rotations, and `-g0.1 -ux` for pre-aligned inputs.
  - `-un` is discouraged because β and λ lose their meaning.
  - — [ohirose/bcpd](https://github.com/ohirose/bcpd)
- probreg guards:
  - `sigma2 = max(abs(sigma2), float64 eps)` and `nu ≥ 1e-20`.
  - Zero denominators are replaced by float32 eps.
  - Bounding-box ranges are floored at 1e-10.
  - The optional `use_debias` adds s²·σ²_m to σ².
  - — [probreg/bcpd.py](https://raw.githubusercontent.com/neka-nat/probreg/master/probreg/bcpd.py)

### Inferences
- The `abs()` in probreg's σ² update suggests that negative σ² values occur in practice (cancellation in s1 − 2s2 + s3). That is a known float issue which the paper's formula does not show. Computing it directly as a sum of squared residuals would be more robust.

### Gaps
- I found no third-party analysis of the scale estimate s → 0 (scale collapse) in BCPD, and no sources at all.

## 5. Bibliographic overview (third-party BCPD extensions and implementations)

### Takeaway
List of the third-party works found, separating peer-reviewed papers from code. None of them is primarily an acceleration of BCPD.

### Cited Findings
- **Peer-reviewed:**
  - Zhang/Min/Pan/Meng, IROS 2021, DOI 10.1109/IROS51168.2021.9635908 (G-BCPD, robustness). — [UCL](https://discovery.ucl.ac.uk/view/people/ZMINX16.html)
  - Min et al., IEEE T-ASE 2023, DOI 10.1109/TASE.2022.3159553 (AGBCPD). — [UCL PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
  - ICRA 2022, DOI 10.1109/icra46639.2022.9812395 (anisotropic rigid BCPD). — [S2](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.2971687/citations?fields=title,year,venue,externalIds&limit=1000)
  - Joutard et al., WBIR 2022, DOI 10.1007/978-3-031-11203-4_9 (MO-BCPD). — [arXiv](https://arxiv.org/abs/2203.08041)
  - Application: "A Novel UAV Recognition Method Based on BCPD with Full Polarization Information", PIERS 2021, DOI 10.1109/PIERS53385.2021.9694720. — [S2](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.2971687/citations?fields=title,year,venue,externalIds&limit=1000)
  - Application: "Adaptive positioning for film cooling holes based on Bayesian coherent point drift", Advanced Engineering Informatics 2026, DOI 10.1016/j.aei.2026.104588. — [S2](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.2971687/citations?fields=title,year,venue,externalIds&limit=1000)
- **Code only:**
  - probreg (neka-nat, Python, experimental, dense). — [GitHub](https://github.com/neka-nat/probreg)
  - rbcpd (BIMSBbioinfo, R, binding of the pinned original with BCPD++). — [r-universe](https://bimsbbioinfo.r-universe.dev/rbcpd)
- **Context (Hirose, excluded):**
  - BCPD, TPAMI 2021, DOI 10.1109/TPAMI.2020.2971687.
  - GBCPD, TPAMI 2022, DOI 10.1109/TPAMI.2022.3214191.
  - DET, TPAMI 2026.
  - — [ohirose/bcpd](https://github.com/ohirose/bcpd)

### Inferences
- The "acceleration of BCPD by third parties" niche looks empty as of 2026-10. A GPU or kernel-reduction BCPD (for example with block-wise E-step and FGT/KeOps) would be a real gap and not a reimplementation of existing work.

### Gaps
- Not checked: a Google Scholar "cited by" pass (no access) and IEEE Xplore full text. The S2 list may be incomplete (limit 1000, title matching only).
- The cross-atlas paper on bioRxiv ("Bridging Biomedical Atlas Ecosystem ... Scalable Tissue Specimen Registration", 2026, DOI 10.64898/2026.08.13.744704) cites BCPD. Whether it scales BCPD itself was not checked. It possibly relates to the BIMSB rbcpd package (unconfirmed).
