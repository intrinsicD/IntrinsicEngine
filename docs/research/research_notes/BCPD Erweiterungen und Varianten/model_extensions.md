# Model-level extensions and generalizations of BCPD (non-Hirose authors, up to 2026)

Scope note: BCPD = O. Hirose, "A Bayesian Formulation of Coherent Point Drift", IEEE TPAMI 43 (2021), DOI 10.1109/TPAMI.2020.2971687 — [IEEE Xplore](https://ieeexplore.ieee.org/document/8985307/), [DOI](https://doi.org/10.1109/tpami.2020.2971687). Hirose's own follow-ups (GBCPD, BCPD++, DLD, DET) are context only. Search budget was about 17 tool calls; Google Scholar "cited by" could not be enumerated directly (no Scholar access), so coverage is search-based and certainly incomplete. Several publisher pages (ScienceDirect, Eurographics DL, UCL Discovery landing) returned HTTP 403, so some details come from search snippets only and are marked as such.

## Robustness: heavy-tailed noise, outliers, partial overlap, missing data

### Takeaway
The only confirmed BCPD-model generalization for robustness is AGBCPD (Zhang, Min et al., IEEE T-ASE 2022). It is **rigid only**: it adds anisotropic positional noise and von Mises–Fisher normals, and it targets noise, outliers and partial data in orthopedic CAS. I found no Student-t or heavy-tailed BCPD and no explicit partial-to-full BCPD model by other authors. Student-t mixture registration exists, but it is CPD-like and older than BCPD.

### Cited Findings
- **AGBCPD**: A. Zhang, Z. Min, Z. Zhang, X. Yang, M. Q.-H. Meng, "Anisotropic Generalized Bayesian Coherent Point Drift for Point Set Registration", IEEE Transactions on Automation Science and Engineering, 2022, DOI 10.1109/TASE.2022.3159553 (listed "in press" in the UCL record, so volume and pages were not found) — [UCL Discovery / CORE record](https://api.core.ac.uk/oai/oai:eprints.ucl.ac.uk.OAI2:10150628); [accepted PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
  - What changes: BCPD is generalized to 6D "generalized point sets" (position + normal vector). Positional noise uses an **anisotropic covariance** instead of BCPD's isotropic σ². Normals use a **von Mises–Fisher** distribution with a concentration parameter that is estimated. Inference stays variational Bayes, and the authors claim this preserves BCPD's convergence guarantee. The paper also defines an isotropic variant (IGBCPD) as an ablation — [accepted PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
  - Why: computer-assisted orthopedic surgery (femur, pelvis), where intra-operative data are noisy, anisotropic, outlier-laden and **partial**. The method assumes a prior coarse registration — [accepted PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
  - Scope: **rigid** registration only (rotation, translation); it does not touch the non-rigid motion-coherence prior — [accepted PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
  - Reported results: the authors claim better accuracy, convergence speed and robustness to noise, outliers and partial data than BCPD. I did not extract the exact table numbers — [accepted PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
- **Student-t (CPD-like, not BCPD)**: Zhou et al., "Robust Non-Rigid Point Set Registration Using Student's-t Mixture Model", PLoS ONE 2014. It gives a closed-form update and needs fewer free parameters than CPD — [PMC](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC3950182/). The 2018 follow-up adds Dirichlet-prior modelling of mixing weights for missing correspondences and outliers — [PMC](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC5992220/). Both predate BCPD and are separate lines of work.
- **MO-BCPD** (see Kernels) models segmentation errors in two ways. Inter-organ mislabeling goes through a label-confusion matrix, and over- or under-segmentation goes through the outlier probability ω and outlier density — [arXiv HTML](https://arxiv.org/html/2203.08041)
- **Skewed-distribution CPD**: "Coherent point drift with Skewed Distribution for accurate point cloud registration", Computers & Graphics, 2024 (pii S0097849324001092). I could only see the title, so it is unclear whether it builds on CPD or BCPD — [ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S0097849324001092)
- **Missing data, GP view**: Valdeira, Ferreira, Micheletti, Soares, "Probabilistic Registration for Gaussian Process 3D shape modelling in the presence of extensive missing data", SIAM J. Math. Data Sci. (DOI 10.1137/22M1495494), arXiv 2203.14113. It casts shape fitting as multi-annotator GP regression in parallel with probabilistic registration. The abstract does not mention BCPD — [arXiv](https://arxiv.org/abs/2203.14113)

### Inferences
- A non-rigid Student-t or heavy-tailed BCPD appears to be an open gap; no paper combining the two was found. AGBCPD shows that BCPD's VB machinery extends cleanly to anisotropic covariances and directional likelihoods, which makes a heavy-tailed likelihood (a Student-t written as a Gaussian scale mixture with a Gamma latent per point) a natural VB-compatible extension.
- Partial overlap is handled in practice by BCPD's existing outlier weight ω or by external pipelines, not by a new model.

### Gaps
- No exact AGBCPD error numbers were extracted, and the final T-ASE volume and pages were not found.
- I could not determine whether the 2024 skewed-distribution CPD extends BCPD.
- No primary source was found for a Student-t BCPD, a partial-to-full BCPD, or a missing-data BCPD (non-Hirose).

## Additional cues: normals, color, curvature, labels, landmarks, learned features

### Takeaway
I confirmed two cue extensions of BCPD: **normals** (AGBCPD, vMF likelihood, rigid) and **semantic labels** (MO-BCPD, label-confusion matrix plus per-organ kernels, non-rigid). Color, curvature and learned-descriptor cues exist only for CPD or for non-BCPD methods. No feature-guided or learned-descriptor BCPD was found.

### Cited Findings
- AGBCPD: a normal-vector vMF term inside BCPD's VB framework; the joint posterior covers correspondences, rigid parameters, the anisotropic covariance and the vMF concentration — [accepted PDF](https://discovery.ucl.ac.uk/10150628/1/Min_TASE_AGBCPD_final_submission.pdf)
- MO-BCPD: a label-confusion matrix U[l,l'] gives the probability that a source point with label l' generates a target point with label l. In the experiments U = I because segmentation was accurate — [arXiv HTML](https://arxiv.org/html/2203.08041)
- Color CPD (CPD, not BCPD): "3D non-rigid registration using color: Color Coherent Point Drift", arXiv 1802.01516, motivated by leaf growth — [arXiv](https://arxiv.org/pdf/1802.01516)
- Semantic labels, non-BCPD alternative: SemICP (arXiv 2503.00972) restricts correspondences to anatomically consistent labels, uses a control-point deformation model and linear-elastic regularization — [arXiv](https://arxiv.org/pdf/2503.00972)
- Landmarks and shape priors: Hirose's Dependent Landmark Drift (context, Hirose's own) replaces the coherence prior with a statistical shape model — [arXiv](https://arxiv.org/pdf/1711.06588). GiNGR (Madsen, Aellen, Morel-Forster, Vetter, Lüthi, arXiv 2203.09986, 2022) is a GP-regression framework that explains CPD and ICP as special cases. It supports dense, sparse or partial correspondences and domain-specific GP priors, and it can produce posterior distributions of registrations — [arXiv](https://arxiv.org/abs/2203.09986). The GiNGR paper describes BCPD as a VB reformulation of CPD — [arXiv PDF](https://arxiv.org/pdf/2203.09986)

### Inferences
- In BCPD's model, a cue enters as an additional factor in the per-pair likelihood (vMF for normals, a categorical confusion for labels). The same pattern would fit color or descriptor similarity, but I found no published BCPD instance of it.

### Gaps
- No BCPD with color, curvature or learned descriptors (e.g. FPFH or FCGF in the likelihood) was found. No non-rigid BCPD with normals was found.
- I did not verify whether GiNGR implements BCPD itself or only CPD and ICP inside its framework.

## Kernels and deformation priors: geodesic, anisotropic, articulated or multi-body, physics, local

### Takeaway
The main non-Hirose kernel and prior generalization is **MO-BCPD** (Joutard et al., WBIR 2022). It replaces BCPD's single Gaussian coherence kernel with an organ-block covariance: a per-organ scale and bandwidth and an inter-organ coupling matrix S. This makes BCPD a multi-body or piecewise-coherent model. With S = I, landmark TRE drops from 22 mm to 13 mm on average. Geodesic kernels are Hirose's own (GBCPD). I found no articulated, physics-based or elastic BCPD by other authors.

### Cited Findings
- **MO-BCPD**: S. Joutard, T. Pheiffer, C. Audigier, P. Wohlfahrt, R. Dorent, S. Piat, T. Vercauteren, M. Modat, T. Mansi, "A multi-organ point cloud registration algorithm for abdominal CT registration", WBIR 2022 (Springer LNCS), DOI 10.1007/978-3-031-11203-4_9, arXiv 2203.08041 — [arXiv abs](https://arxiv.org/abs/2203.08041); [Springer](https://link.springer.com/chapter/10.1007/978-3-031-11203-4_9)
  - What changes: the prior covariance G is built per organ pair with organ-specific scale Λ and bandwidth B, so that motion "within and between organs" is decoupled. A symmetric matrix S ∈ [0,1] sets inter-organ coherence: GMC means S = all ones, OMC means S = I. A label-confusion matrix U and outlier terms (ω, p_out) are added, and GP regression interpolates the surface deformation to the whole volume. Experiments used Λ = 10 mm and B = 30 mm for all organs — [arXiv HTML](https://arxiv.org/html/2203.08041)
  - Data: LiTS CT, 104 patients, 10,712 registration pairs; liver, spleen, both kidneys, bladder — [arXiv HTML](https://arxiv.org/html/2203.08041)
  - Results: mean landmark TRE was 22 mm for BCPD and 13 mm for OMC-MO-BCPD. Kidney landmarks improved most, e.g. right kidney center 21 → 5 mm and left kidney center 18 → 6 mm. GMC was about BCPD level, and liver landmarks improved little (liver top 24 → 26 mm) — [arXiv HTML](https://arxiv.org/html/2203.08041)
- GBCPD (context, Hirose): uses a surface coherence matrix that combines a Gaussian kernel with a geodesic exponential kernel, to avoid unnatural deformation where parts are close in Euclidean space — [ResearchGate](https://www.researchgate.net/publication/364361740_Geodesic-Based_Bayesian_Coherent_Point_Drift); [GitHub](https://github.com/ohirose/bcpd)
- Related non-BCPD kernel work: a geodesic squared-exponential kernel for GP-based face registration (arXiv 2112.11853) reports about 2× lower energy than the Euclidean SE kernel — [arXiv](https://arxiv.org/pdf/2112.11853)
- Physics priors exist only in the CPD tracking literature: CPD + FEM for rope or wire tracking (Wang & Yamakawa, Frontiers in Neurorobotics 2022, DOI 10.3389/fnbot.2022.886068) — [Frontiers](https://frontiersin.org/journals/neurorobotics/articles/10.3389/fnbot.2022.886068/text); CDCPD with LLE regularization and constraints — [arXiv 2101.00733](https://arxiv.org/pdf/2101.00733)
- Analytic-CPD (arXiv 2605.00934, 2026 preprint) replaces CPD's Gaussian-kernel M-step with a structured analytic mapping estimator and discusses BCPD as related work. It is CPD-based, not a BCPD extension — [arXiv](https://arxiv.org/html/2605.00934v1)

### Inferences
- MO-BCPD shows that BCPD's Gram matrix G can be replaced by any valid block or structured covariance without changing the VB updates. This is the formal hook for articulated, piecewise-rigid or graph or geodesic priors. GBCPD (geodesic) and MO-BCPD (block-diagonal or coupled) are two instances of it.

### Gaps
- No non-Hirose articulated, piecewise-rigid, elastic or physics, sparse or locally adaptive (spatially varying bandwidth) BCPD was found.
- No BCPD-based deformable-linear-object tracker was confirmed; the DLO literature builds on plain CPD.

## Group-wise, multi-shape, atlas, temporal and shape statistics

### Takeaway
I found no group-wise or atlas BCPD and no 4D-tracking BCPD by other authors. The closest is **Break and Splice** (CGF 2023), which wraps BCPD in a DPGMM clustering and labelling stage to handle topology changes between frames. Group-wise mixture registration exists for generalized CPD, not for BCPD.

### Cited Findings
- **Break and Splice**: "Break and Splice: A Statistical Method for Non-Rigid Point Cloud Registration", Computer Graphics Forum 42(6), 2023, handle/DOI 10.1111/cgf.14788 (Bournemouth eprint 38389). A Dirichlet-process GMM clusters both point sets, source points are labelled by iterative classification, and the labelled source is registered with BCPD. It targets splitting and merging between frames. The authors report about 60% lower error and about 57.8% less time than the state of the art. Details are from a search snippet because the publisher page returned 403, and I did not verify the author names — [Eurographics DL](https://diglib.eg.org/items/fe7f98ff-0e08-49bc-a79c-169a1e7f7071/full); [PDF handle](https://diglib.eg.org/bitstream/handle/10.1111/cgf14788/v42i6_06_14788.pdf); [Bournemouth eprints](https://eprints.bournemouth.ac.uk/38389/)
- Generalised CPD for group-wise diffusion-MRI atlases (Medical Image Analysis 2019) is CPD-based, not BCPD — [ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S1361841519300015)
- Shape statistics on a BCPD-like basis: Hirose's DLD (SSM prior) is context — [arXiv](https://arxiv.org/pdf/1711.06588). GiNGR uses GP morphable models as priors — [arXiv](https://arxiv.org/abs/2203.09986)

### Inferences
- Break and Splice is a "label-then-BCPD" hybrid. The Dirichlet process sits outside BCPD's generative model, which makes it a pipeline extension rather than a nonparametric BCPD.

### Gaps
- No group-wise, atlas, 4D or temporal-prior BCPD and no BCPD-derived SSM by other authors was found. The search was limited, and a Scholar "cited-by" crawl would be needed to be sure.

## Hybrids with deep learning

### Takeaway
No "deep BCPD" was found: no unrolled BCPD, no neural prior inside BCPD, and no learned initialization paper that modifies BCPD's model. Learning-based methods (Neural Deformation Pyramid, SPARE, CPD networks, LCNet) use BCPD as a **baseline** or reformulate **CPD**.

### Cited Findings
- Neural Deformation Pyramid (Li & Harada, NeurIPS 2022) uses BCPD as a baseline and argues for an L1 or robust loss in partial-to-partial registration — [NeurIPS](https://proceedings.neurips.cc/paper_files/paper/2022/file/b2077e6d66da612fcb701589efa9ce88-Paper-Conference.pdf)
- SPARE (arXiv 2405.20188, 2024) is a symmetrized point-to-plane non-rigid method with normal-based adaptive weights, and it compares against BCPD — [arXiv](https://arxiv.org/html/2405.20188v1)
- "Coherent point drift networks" (arXiv 1906.03039) is an unsupervised learned CPD, cited in a reference list but not opened — [arXiv search result listing](https://arxiv.org/pdf/2303.13290)
- LCNet (2025) reformulates CPD into a learning-based model for image-guided liver surgery (search snippet; not opened) — [ResearchGate result page](https://www.researchgate.net/publication/407139894_SPaR-liver_sparse_point-aware_registration_for_intraoperative_liver-deformation_compensation_using_Geometric_Deformation_Graph_Neural_Network)

### Inferences
- Deep or unrolled BCPD looks like an unoccupied niche in the literature found here.

### Gaps
- The search may have missed papers that use BCPD as a refinement stage after learned matching; a cited-by sweep is needed.

## Related "Bayesian CPD"-style formulations

### Takeaway
BCPD's VB formulation already generalizes CPD, and CPD is a special case of it. Related probabilistic frameworks include GiNGR (a GP regression unification), SFGP (GP fitting under missing data), Student-t mixtures with Dirichlet priors on the weights, and Break and Splice (DP clustering + BCPD). None of them is a nonparametric BCPD proper.

### Cited Findings
- BCPD is a Bayesian generalization of CPD; CPD is recovered under specific conditions, and inference is variational Bayes — [IEEE Xplore](https://ieeexplore.ieee.org/document/8985307/)
- GiNGR unifies CPD and ICP as GP-regression registration and gives posterior registrations — [arXiv](https://arxiv.org/abs/2203.09986)
- DSMM: Student-t mixture with Dirichlet prior modelling — [PMC](https://www.ncbi.nlm.nih.gov/pmc/articles/PMC5992220/)
- Break and Splice: DPGMM + BCPD — [Eurographics DL](https://diglib.eg.org/handle/10.1111/cgf14788)
- Context (Hirose): Domain Elastic Transform, a Bayesian function registration for high-dimensional scientific data, arXiv 2603.21235 (2026); the repository README says it appeared in TPAMI — [arXiv](https://arxiv.org/pdf/2603.21235); [GitHub](https://github.com/ohirose/bcpd)
- Application (not a model change): BCPD in two stages (rigid, then non-rigid) for adaptive positioning of film cooling holes, Advanced Engineering Informatics, 2026 (pii S1474034626002806) — [ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S1474034626002806)

### Inferences
- Of the external model extensions found, MO-BCPD (structured prior, labels, outliers, non-rigid) and AGBCPD (anisotropic noise, normals, rigid) are the substantive ones. Break and Splice is a pipeline hybrid. Everything else is CPD-family or BCPD-as-baseline.

### Gaps
- No variational CPD with a Dirichlet-process prior over components, and no nonparametric or infinite-mixture BCPD, was found.
- Bibliographic details for Break and Splice (authors, pages) and the 2024 skewed CPD were not verifiable because of HTTP 403.
