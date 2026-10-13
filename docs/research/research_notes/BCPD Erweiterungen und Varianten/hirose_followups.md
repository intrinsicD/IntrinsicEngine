# Osamu Hirose's follow-up work on Bayesian Coherent Point Drift (BCPD)

Scope: work by O. Hirose (Kanazawa University) after "A Bayesian Formulation of Coherent Point Drift" (IEEE TPAMI 43(7):2269-2286, 2021, DOI 10.1109/TPAMI.2020.2971687), plus how github.com/ohirose/bcpd changed over time. Research date: 2026-10-09.

Method note: the IEEE full-text PDFs of the 2020 acceleration paper and the GBCPD paper could not be downloaded; IEEE returned HTTP 418 or an HTML page to the fetch tools. Statements about those two papers therefore rest on their abstracts (via Semantic Scholar), press releases, the README, the source code (repository cloned and git history read on 2026-10-09), and the 2026 DET paper, which summarises both. Specific numeric results inside those two papers (tables, error values, timings) are **not** verified here.

## Q1. What is BCPD++, and how is BCPD accelerated?

### Takeaway
"BCPD++" is the separate paper "Acceleration of Non-Rigid Point Set Registration With Downsampling and Gaussian Process Regression" (TPAMI 43(8), 2021). It is not the supplementary material of the BCPD paper. The pipeline has three steps: downsample both sets, register the reduced sets with BCPD, then interpolate the displacement vectors of the removed points by GP regression under the same motion-coherence prior. A second, independent layer of acceleration, Nyström approximations of G and P plus a switch to KD-tree search near convergence, already existed in BCPD itself (the `-A/-K/-J/-p` flags). The papers' main claim is scale: more than 10 M points.

### Cited Findings
- Bibliography: O. Hirose, "Acceleration of Non-Rigid Point Set Registration With Downsampling and Gaussian Process Regression," IEEE TPAMI vol. 43, no. 8, pp. 2858-2865, 2021, DOI 10.1109/TPAMI.2020.3043769, IEEE doc 9290402, PubMed 33301401. Single author. Open access (CC BY). — [GitHub README](https://github.com/ohirose/bcpd); [Semantic Scholar API record](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.3043769?fields=title,abstract,year,venue,externalIds)
- Timeline: accepted 2020-12-07; early access 2020-12-14; won Kanazawa University's "Papers of the Month" award for Feb 2021. — [Hirose homepage](https://sites.google.com/view/ohirose/)
- No arXiv version was found. — [Semantic Scholar record](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.3043769?fields=title,abstract,year,venue,externalIds) (externalIds lists only DBLP/MAG/DOI/PubMed)
- Abstract, paraphrased: registration is split into three steps: (i) downsample the point sets; (ii) register the downsampled sets with an algorithm built on the motion coherence prior (BCPD); (iii) interpolate the deformation vectors of the removed points. The same prior yields an interpolation that can be read as Gaussian process regression. Claimed results: registers point sets with over 10 million points, and "reduces computing time more radically than a state-of-the-art acceleration algorithm". — [Semantic Scholar abstract](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2020.3043769?fields=title,abstract,year,venue,externalIds)
- Press release: earlier acceleration methods were efficient only for small sets (under about 100k points). Experiments covered about 100k to over 10M points. Runtimes were "noticeably shorter" than a state-of-the-art approach above 1M points, with accuracy about the same as BCPD. The method is relatively sensitive to disturbances on small data sets, so it suits large sets. Test shapes included a dragon, a monkey and a human body. The release gives no speedup factors or error numbers. — [TechXplore / Kanazawa Univ. press release, Mar 2021](https://techxplore.com/news/2021-03-key-task-vision-graphics-boost.html)
- README claim: "BCPD++ is much faster than BCPD but is slightly less accurate" (Mac Mini 2018 timing figure). The software "non-rigidly registers the shapes containing over 10M points (3M points if geodesic kernel)". — [GitHub README](https://github.com/ohirose/bcpd)
- Downsampling flag `-D [char,int,real]`. The char picks the set: `X/x` target, `Y/y` source, `B/b` both. Uppercase means voxel-grid resampling with voxel width r. Lowercase means ball resampling with radius r. r=0 means random sampling. The int is the number of points after downsampling, and the two sets may use different counts (e.g. `-DB,5000,0.08`). Downsampling automatically triggers interpolation of the deformation vectors. The upsampled result is written with suffix `y.txt`. — [GitHub README](https://github.com/ohirose/bcpd)
- Internal (in-VB) acceleration: `-A` is documented as `-K70 -J300 -p -d7 -e0.15 -f0.2`. `-K` = number of Nyström samples for G. `-J` = Nyström samples for P. `-r` = random seed. `-p` = switch to KD-tree search near convergence. `-d` = scale factor of sigma for the search radius. `-e` = maximum search radius. `-f` = sigma at which KD-tree search starts. The README recommends acceleration above several thousand points and treats it as mandatory at several hundred thousand. — [GitHub README](https://github.com/ohirose/bcpd)
- Rationale given in the DET paper: Nyström gives low-rank approximations of P (rank J) and G (rank K). After the early iterations P is updated by KD-tree search, which prunes low-probability correspondences and approximates P better than Nyström once P becomes sparse. This brings cost to O((M+N) log(M+N)) time and O(M+N) memory. — [DET paper, arXiv 2603.21235, §4.1.4](https://arxiv.org/abs/2603.21235)
- Code discrepancy: in the current source, `-A` sets `J=300, K=100` plus `-p`, while the README still says `-K70`. The value changed from K=70 to K=100 in commit 09c1f32 (2026-01-01, "updated the software"). In v0.87 (2022) the code had `K=70`. — [register/main.c at HEAD](https://github.com/ohirose/bcpd/blob/master/register/main.c); [commit 09c1f32](https://github.com/ohirose/bcpd/commit/09c1f32)
- Interpolation options in the original v0.85 source (2021-02-03): `-L [int]` = number of Nyström samples for accelerating interpolation, and `-1` = 1-NN interpolation instead of GPR. `-L` was gone by v0.87 (2022-10-14). `-1` was gone in the 2025 "major update" (v0.95.0). — [commit bb8c02d](https://github.com/ohirose/bcpd/commit/bb8c02d); [commit 7051900](https://github.com/ohirose/bcpd/commit/7051900); [commit 616b4c5](https://github.com/ohirose/bcpd/commit/616b4c5)

### Inferences
- Two acceleration layers combine: the internal Nyström/KD-tree layer from the original BCPD paper/code, and the external downsample-register-GPR layer that is the "++" contribution. "BCPD++" in the README means BCPD run with `-D`. Its interpolation is the GP posterior mean, roughly v_removed = G_(removed,kept) G_(kept,kept)^-1 v_kept under the same kernel. This is inferred from "interpolation interpreted as Gaussian process regression"; the exact equation was not verified.

### Gaps
- The paper's quantitative tables (speedup factors, registration errors, which "state-of-the-art acceleration" was compared, most likely CPD with FGT/low-rank) could not be verified because the IEEE PDF was not retrievable.
- The exact GPR interpolation formula, and whether the interpolation itself uses Nyström (the old `-L`), are unverified from the paper.

## Q2. "Geodesic-Based Bayesian Coherent Point Drift" (GBCPD / GBCPD++)

### Takeaway
Published in TPAMI 45(5), 2023 (early access Oct 2022). It replaces the Euclidean motion-coherence kernel with a mix of a Gaussian of the geodesic distance (shortest path on a mesh or kNN graph) and the usual Euclidean Gaussian, weighted by τ. The indefinite result is made usable by a low-rank, Nyström-style eigendecomposition with eigenvalue truncation. The aim is to stop CPD/BCPD from dragging spatially close but geodesically distant parts (e.g., legs) together. The accelerated GBCPD++ is claimed to handle several million points; the README says 3M.

### Cited Findings
- Bibliography: O. Hirose, "Geodesic-Based Bayesian Coherent Point Drift," IEEE TPAMI vol. 45, no. 5, pp. 5816-5832, May 2023, DOI 10.1109/TPAMI.2022.3214191, IEEE doc 9918058, PubMed 36227821. Single author. Open access (CC BY). — [GitHub README](https://github.com/ohirose/bcpd); [Semantic Scholar record](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2022.3214191?fields=title,abstract,year,venue,externalIds)
- Timeline: accepted 2022-10-04; early access 2022-10-13; print 2023-04-06. Hirose visited the GLADIA lab (E. Rodolà, Sapienza Rome) from 2022-04-26. — [Hirose homepage](https://sites.google.com/view/ohirose/)
- No arXiv preprint was found. One student report links arXiv:0905.2635, which is Myronenko & Song's CPD paper, not GBCPD. — [Semantic Scholar record](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2022.3214191?fields=title,abstract,year,venue,externalIds); [arXiv 0905.2635](https://arxiv.org/abs/0905.2635)
- Abstract, paraphrased: CPD unnaturally deforms different parts of a shape when they neighbour each other (e.g., human legs) because of the proximity-based motion coherence. GBCPD redefines motion coherence with geodesics on the surface, and an accelerated variant is also proposed. Experiments reportedly show it avoids CPD's drawback and that the accelerated algorithm handles shapes of "several millions of points". — [Semantic Scholar abstract](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/TPAMI.2022.3214191?fields=title,abstract,year,venue,externalIds)
- Kernel definition, as restated by Hirose in the DET paper, which calls it the "surface coherence matrix" [8]: K(y_m, y_m') = τ φ_β(D^geo_mm') + (1−τ) φ_β(D^euc_mm'), with φ_β(c) = exp(−c²/2β²) and τ ∈ [0,1]. Because the geodesic kernel is "generally indefinite", a "fast positive-semidefinite approximation [8]" is applied, and a nonzero τ requires K (the rank) to be set. — [DET paper §4.2.2 and Table 2](https://arxiv.org/abs/2603.21235)
- Code-level construction (base/geokdecomp.c, added in v0.87 on 2022-10-14):
  1. Pick K landmark nodes by downsampling Y.
  2. Run Dijkstra from each landmark over a sparse graph built from the mesh or a kNN graph; unreachable nodes get a geodesic kernel value of 0.
  3. Form the M×K kernel columns as τ·gauss(d_geo, β) + (1−τ)·gauss(‖y_m − y_u‖, β).
  4. Eigendecompose the K×K landmark block with LAPACK dsyev and lift it to approximate eigenvectors Q = sqrt(K/M) · C_MK A Λ^-1 with eigenvalues λ·M/K. This is a Nyström extension.
  5. Keep only components with λ_k/λ_0 > ε, where ε is the `-z` "acceptable condition number of G". This drops the non-positive part and makes G low-rank PSD.

  — [base/geokdecomp.c](https://github.com/ohirose/bcpd/blob/master/base/geokdecomp.c); [commit 7051900 "release: gbcpd/gbcpd++"](https://github.com/ohirose/bcpd/commit/7051900)
- Graph sources (base/sgraph.c): `sgraph_from_mesh` (triangle file) and `sgraph_from_points` (k neighbours, maximum edge length). — [base/sgraph.c](https://github.com/ohirose/bcpd/blob/master/base/sgraph.c)
- CLI: `-G geodesic,τ,triangles.txt` builds geodesics on a supplied mesh and is preferred when a mesh exists. `-G geodesic,τ,k,radius` (e.g. `-G geodesic,0.2,8,0.15`) builds a kNN graph automatically. Related flags: `-b` (Gaussian width β), `-K` (rank constraint on G), `-z` (ε). README: "The geodesic kernel usually outperforms standard kernels when different parts of a source shape are closely located"; GBCPD "works better than CPD and BCPD" on such shapes. — [GitHub README](https://github.com/ohirose/bcpd)
- GBCPD++ means the geodesic kernel combined with `-D` downsampling/interpolation. Demos in the v0.87 release: armadillo, faces, female/male human bodies (`demo/gbcpd++/demoFEMALE01.m` etc.). — [commit 7051900](https://github.com/ohirose/bcpd/commit/7051900)
- The repository's hierarchy demo runs GBCPD at three coarse-to-fine levels, with (β, τ, γ) = (1.2, 0.5, 3) → (0.7, 0, 0.1) → (0.3, 0, 0.1), plus `-J300 -K300 -p -ux -c1e-6 -n500`. Geodesic mixing is used only at the coarsest level. — [demo-python/bcpd/hierarchy.py](https://github.com/ohirose/bcpd/blob/master/demo-python/bcpd/hierarchy.py)
- DET reports that "pure geodesic coherence reduced the topology score" in its ablations, and that its geodesic graph is built in an augmented spatial+functional space (`geodesic+`, DET only). — [DET paper](https://arxiv.org/abs/2603.21235); [GitHub README](https://github.com/ohirose/bcpd)

### Inferences
- In the README the term "geodesic exponential kernel" sits next to code that applies a Gaussian (squared) to the geodesic distance. The DET paper's formula also uses the squared Gaussian φ_β. Treat "exponential" as loose naming, not an exp(−d/β) Laplacian. This is unconfirmed against the GBCPD paper text.
- The PSD fix (truncated Nyström spectrum) means the geodesic prior is always low-rank (rank ≤ K). Accuracy therefore depends on K, which is consistent with "nonzero τ requires K".

### Gaps
- The paper's datasets, competitor list, error metrics and runtime numbers could not be verified (IEEE PDF blocked). It is unknown whether the paper's PSD approximation matches the code exactly, e.g., whether it truncates negative eigenvalues or uses another projection.
- It is also unknown whether the paper uses a different landmark-selection scheme from the code's `downsample(...,-0.05)`.

## Q3. Other Hirose papers related to BCPD (rigid variants, shape transfer, DLD, 2023-2026)

### Takeaway
After GBCPD, the only new peer-reviewed BCPD-lineage method by Hirose is **Domain Elastic Transform (DET)** with E. Rodolà (TPAMI 2026, arXiv 2603.21235). It extends BCPD to function registration, i.e., spatial plus high-dimensional features. Rigid, affine and similarity modes, shape transfer, and "DLD" (Dependent Landmark Drift, an older statistical-shape-model method from 2017) are features of the software, not separate post-2021 BCPD papers. A 2026 ICoICT paper co-authored by Hirose uses ICP, not BCPD. No 2023-2026 Hirose papers on normals, landmark guidance or partial shapes were found.

### Cited Findings
- DET: O. Hirose and E. Rodolà, "Domain Elastic Transform: Bayesian Function Registration for High-Dimensional Scientific Data," IEEE TPAMI 2026, DOI 10.1109/TPAMI.2026.3733393. arXiv 2603.21235: v1 2026-03-22, v2 2026-09-10, v3 2026-09-17. 18 pp. CC BY 4.0. Code public since March 2025. — [arXiv 2603.21235](https://arxiv.org/abs/2603.21235); [GitHub README](https://github.com/ohirose/bcpd)
- DET's changes relative to BCPD, from its Table 6 and §6.1:
  - spatial-functional likelihood, with inputs `-X/-Y` giving function values F (N×D′, M×D′);
  - likelihood balancing ζ = η(D/D′), set with `-j` (η);
  - diagonal or isotropic functional covariance (`-i` = isotropic);
  - an optional adaptive outlier model instead of a uniform one;
  - variance-guided importance sampling (VGIS): p(z) ∝ max(σ_f(z), λ_g σ_g(z)) + ε, as a downsampling alternative;
  - "functional annealing", meaning η is annealed in addition to the coherence parameters λ, β.

  Shared with BCPD: VI, the GP motion prior, Nyström + KD-tree, and GPR interpolation. — [DET paper](https://arxiv.org/abs/2603.21235)
- DET empirical claim: on a 90-case severe-perturbation MERFISH benchmark, DET reached Jaccard 0.881±0.075, topology 0.928±0.047, smoothed PCC 0.736±0.087, label-transfer ARI 0.185±0.088 at 8.58±4.39 s. BCPD reached 0.612 / 0.897 / 0.366 / 0.073 at 2.98 s. PASTE2-DS-1NN had the best ARI (0.194). Removing the D/D′ balancing made all 27 ablation cases fail. — [DET paper, Table 4 and §6.2.1](https://arxiv.org/abs/2603.21235)
- Dependent Landmark Drift: O. Hirose, "Dependent landmark drift: robust point set registration with a Gaussian mixture model and a statistical shape model," arXiv 1711.06588 (2017). It predates BCPD and is not a follow-up. It was integrated into the bcpd binary in v0.95.0 (2025-03-18) via `-C` (shape-variation eigen-pairs `[L';Q]`, with `-y` as the mean shape). — [Semantic Scholar author listing](https://api.semanticscholar.org/graph/v1/author/34897488/papers?fields=title,year,venue,externalIds&limit=100); [GitHub README](https://github.com/ohirose/bcpd); [commit 616b4c5](https://github.com/ohirose/bcpd/commit/616b4c5)
- Semantic Scholar also lists two un-venued Hirose items with no DOI: "Non-rigid regisgtration [sic] with Gaussian Markov Random Field" (2020) and "Fast point set registration using a statistical shape model" (2018). They are probably domestic Japanese talks; their content was not verified. — [Semantic Scholar author listing](https://api.semanticscholar.org/graph/v1/author/34897488/papers?fields=title,year,venue,externalIds&limit=100)
- Erratum: "Erratum to 'A Bayesian Formulation of Coherent Point Drift'," TPAMI 2021, DOI 10.1109/TPAMI.2021.3092384 (IEEE doc 9506964). It fixes the print Fig. 15, which had been replaced by Fig. 3. The README additionally corrects a trace term in Fig. 2 after Proposition 3; that correction is an image only. — [Semantic Scholar listing](https://api.semanticscholar.org/graph/v1/author/34897488/papers?fields=title,year,venue,externalIds&limit=100); [README commit 8c2fe86](https://github.com/ohirose/bcpd/commit/8c2fe86)
- Sho Ogura and O. Hirose, "Scan-Aware Hierarchical Registration for Precise 3D Reconstruction of Mechanical Parts," ICoICT 2026, DOI 10.1109/ICoICT71118.2026.11664824. Multi-view hierarchical integration ordered by the scanner's kinematics, using standard ICP. Best RMSE 0.0269 mm with point-to-plane ICP. Not a BCPD variant. — [Semantic Scholar record](https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/ICoICT71118.2026.11664824?fields=title,abstract,year,venue)
- Invited talk (in Japanese) on generalising and accelerating CPD, ITE, 2020-05-28. — [Hirose homepage](https://sites.google.com/view/ohirose/); [IEICE ken entry](https://ken.ieice.org/ken/paper/20200528C1Y4/eng/)
- Shape transfer is a demo, not a paper: BCPD is used to transfer a deformation/mesh between shapes (MATLAB `demo/.../shapeTransferA.sh` producing `transferV1_y.obj`; Python `demo-python/bcpd/transfer.py`). It was added 2021-02-26 and 2021-03-01. — [GitHub README](https://github.com/ohirose/bcpd); [git history](https://github.com/ohirose/bcpd/commits/master)

### Inferences
- The proposed title "Acceleration of non-rigid point set registration with downsampling and GPR" is the BCPD++ paper itself (TPAMI 43(8), 2021), not an additional work.
- Hirose's post-2021 BCPD research line is therefore: BCPD++ (2021) → GBCPD/GBCPD++ (2022/23) → DET (2026). Rigid and affine handling, DLD and shape transfer are capabilities of the code.

### Gaps
- Google Scholar and IEEE author pages could not be queried directly. Semantic Scholar merges several "O. Hirose" authors, so obscure Japanese-language items (e.g., IEICE technical reports 2021-2025) may be missing.
- No evidence was found of Hirose papers on normals-guided BCPD, landmark-guided BCPD or partial-shape BCPD. This is absence of evidence, not proof of absence.

## Q4. How the reference implementation (github.com/ohirose/bcpd) evolved

### Takeaway
The repository has 88 commits from 2019-03-31 to 2026-09-20, an MIT license since 2023-02-15, and no GitHub releases. The version string went 0.85 → 0.87 → 0.95.0 → 0.97 → 0.98. Major steps:
1. 2021 source release (BCPD and BCPD++).
2. 2022 geodesic kernel (GBCPD/GBCPD++).
3. 2025 "major update": `-T` transformation-model selector, DLD (`-C`), DET (`-X/-Y`).
4. 2026 DET refinements and a performance release.

### Cited Findings
- Version history from `register/version.h`. All entries come from the same `git log` of the clone. — [git history](https://github.com/ohirose/bcpd/commits/master)

  | Version | Date | Commit and contents |
  |---|---|---|
  | — | 2019-03-31 | First commit. Binaries/README only; source not yet public. |
  | 0.85 (built 2020/10/19) | 2021-02-03 | `bb8c02d` "uploaded: source code". Getopt string `D:u:r:w:l:b:k:g:d:e:c:n:N:G:J:K:L:o:x:y:f:s:hpqvaAtW1`. Kernels `-G1..5`: inverse multiquadric, rational quadratic, Laplace, neural-network (Williams 1998), user-defined `mykernel`. Interpolation flags `-L`, `-1`. KD-tree, Nyström and sampling code present. |
  | — | 2021-02-26 to 03-01 | BCPD++ demo scripts and shape-transfer demos added. |
  | 0.87 (2022/10/13) | 2022-10-14 | `7051900` "release: gbcpd/gbcpd++". Added dijkstra.c, heap.c, sgraph.c, geokdecomp.c. New flags: `-G geodesic,...`, `-z` (ε), `-S`, and `-X/-Y` file slots (a function-value file reader already present). `-L` removed. |
  | 0.95.0 (2025/03/17) | 2025-03-18 | `616b4c5` "major update". Getopt `U:T:t:X:Y:C:D:z:...:hipqvaAW`. New: `-T` transformation model (`srn` default, `an`, `a`, `sr`, `r`, `n`); `-C` (DLD); `-U` (function-value normalisation); `-t` (floor added to the functional σ); `-i` (isotropic functional variance). `-1` and `-S` removed. New rigid demos (apartment, chef scans) and a "3D recovery" demo. First DET code distribution (README NEWS: "Mar 18 2025"). |
  | 0.97 | 2026-01-01 | `09c1f32` "updated the software": VGIS sampling code (base/stat.c, sampling.c), DET demos (image registration, shape localisation), `-A` default changed to K=100. |
  | 0.98 | 2026-01-02 | `0567e6c` "performance improvement" (Dijkstra/heap/KD-tree rewrites). Added `-j` (η, DET). |
  | — | 2026-05-28 / 06-01 | External PR #32 (A. Maga) restored comma-separated input parsing. The 2025 reader had silently read CSV as one column. |
  | — | 2026-09-16 / 09-20 | README updated for DET's TPAMI publication; PR #33 added Python demos (bcpd: nonrigid, rigid, plusplus, gbcpd, gbcpd_plusplus, hierarchy, transfer, reconstruction; det: audio, image, merfish, mosta, shape; dld: face, hand). |

- Current flag semantics:
  - Transformation `-T`: `srn` = T(y) = sR(y+v)+t (default); `an` = A(y+v)+t; `a` = affine; `sr` = similarity; `r` = rigid; `n` = nonrigid only. Rigid/affine is selected with `-T`, not `-u`.
  - Parameters: `-w` ω (outlier probability), `-l` λ (expected deformation length sqrt(D/λ)), `-b` β, `-g` γ (2-10 for large rotations; `-g0.1` with `-ux` for roughly pre-aligned shapes), `-k` κ (mixing-coefficient randomness, default ∞), `-j` η (DET).
  - Kernel `-G`: 1/2/3 or geodesic, plus `geodesic+` for DET.
  - Convergence: `-c` tolerance, default 1e-4 (1e-5 to 1e-6 for smooth surfaces); `-n` maximum VB loops; `-N` minimum loops (default 30).
  - Normalisation `-u e|x|y|n`.
  - Output `-s y,x,v,c,e,a,P,T,Y,t,A`; `-h` history mode; `-q` quiet; `-W` no warnings.

  — [GitHub README](https://github.com/ohirose/bcpd)
- In the current code the neural-network and user kernels (`-G4/-G5`) are no longer documented; the README lists only `-G1..3` plus geodesic. — [GitHub README](https://github.com/ohirose/bcpd); [README at bb8c02d](https://github.com/ohirose/bcpd/blob/bb8c02d/README.md)
- There is no normals-, colour- or landmark-correspondence-guided option for BCPD in any version examined. Extra per-point features exist only via DET (`-X/-Y`). "Landmarks" in the README refer to downsampled representative points, and DLD uses a statistical shape model, not user landmarks. — [GitHub README](https://github.com/ohirose/bcpd)

### Inferences
- For a reimplementation, the canonical code reference for the geodesic kernel is `base/geokdecomp.c` + `base/sgraph.c` + `base/dijkstra.c` (v0.87+).
- DET's `-X/-Y` is the only official way to add attributes such as normals or colours as functional values. This is the nearest thing to "normal guidance" in Hirose's code, but it is untested for that purpose.

### Gaps
- The `-S` flag in v0.87 (PW_OPT_NOSIM) and `-a` (PW_OPT_DBIAS) are not explained in the current README, so their exact semantics are not documented here.
- The repository has no release notes, so per-version behavioural changes beyond flags were not audited, e.g., whether the 2026 "performance improvement" changed numerical results.
