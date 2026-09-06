# Claude public mathematical review

Context-free MCP invocation; no source or dataset content was supplied.
The response is advisory, not independently verified literature evidence.

**Setting.** Multicut/correlation clustering: choose a partition of V into connected components, minimizing F = Σ_{e cut} c_e (c_e < 0 “wants” cutting). Must‑cut edges are c_e = −∞ (constraint). Since all‑singletons is feasible, the problem is always feasible (unless a must‑cut edge is a self‑loop), but finding the optimum is NP‑hard, so a descent heuristic is reasonable.

**Pipeline validity.**
- *Greedy additive contraction (GAEC)*: repeatedly merge the adjacent pair of components with largest positive aggregated cost; never merge across a must‑cut edge (aggregated cost −∞). Every merge strictly decreases F, so termination is guaranteed.
- *KL vertex/pair moves*: move or swap boundary vertices, accept if F decreases. Forbid moving v into a component containing a must‑cut neighbour. A vertex move can disconnect a component; after the move run a local BFS and split disconnected pieces (no edges between pieces ⇒ F unchanged, feasibility restored).
- *Split move via s‑t cut*: inside a component C, put capacity max(c_e,0) on internal edges, take an internal negative edge (u,v), compute the min u‑v cut (S, C\S). Any bipartition of C yields a valid partition, so the move is feasible; the objective change is
 ΔF = w⁺(δ(S)) − |w⁻(δ(S))| ≤ λ_{uv} − |c_uv|,
 where λ_{uv} is the flow value (only positive edges have capacity, and (u,v) has capacity 0 and is necessarily cut). Thus a sufficient certificate for improvement is λ_{uv} < |c_uv|; the actual signed evaluation may be even better. Accepting only on strict decrease keeps the search monotone and terminating (F takes finitely many values).

**Limitations.**
- Min cut minimizes only the positive penalty, ignoring the reward from other negative edges crossing the cut. The best bipartition of C under signed weights is itself NP‑hard (max‑cut‑like), so this is one heuristic among many possible cuts; it can miss profitable contours whose positive cost is high but whose negative reward is higher.
- On sparse meshes, min cuts are often degenerate: the cheapest u‑v cut frequently isolates u or v alone (small positive degree), giving a useless singleton. Mitigate by using terminal *sets* (u plus neighbours attached by strongly positive edges, likewise for v), or by choosing among multiple min cuts (e.g., the one farthest from s, or after adding a tiny ε per edge to favour short contours).
- No guarantee of local optimality with respect to all splits; result is a local minimum only for the tried moves.

**Must‑cut enforcement.** Maintain the invariant “no must‑cut edge is internal to a component” from the start (GAEC never violates it, KL moves are filtered). If an initial solution violates it, those edges are the first mandatory terminal pairs; since their capacity is 0, the min cut always separates them.

**Connected output.** After every accepted split, compute connected components of S and C\S (BFS restricted to C); replace C by these pieces. Splitting a disconnected part costs nothing and can only increase the count of pieces, so the final partition consists of connected components, as the multicut definition requires.

**Efficient bounded solver.** Run the max‑flow on the induced subgraph of C only (typically much smaller than G). Use Boykov–Kolmogorov or push‑relabel; on mesh‑like (near‑planar, bounded degree) graphs BK is near‑linear in practice, and for genuinely planar meshes planar min‑cut algorithms give O(|C| log|C|). Bound further by restricting to a k‑hop ball around (u,v) with the outside contracted into a super‑sink/super‑source, or by capping flow at |c_uv| (stop as soon as flow ≥ |c_uv|, since no certificate can then arise—though the true ΔF could still be negative, so optionally still evaluate).

**How many terminal pairs.** Candidates are the internal negative edges of C. Rank by |c_uv| (or by |c_uv| relative to positive degree of the endpoints) and try a fixed budget k per component (5–20 typically suffices), always including all must‑cut violations. For small components try all; for large ones, a Gomory–Hu tree (|C|−1 flows) yields min cuts for all pairs simultaneously. After an accepted split, re‑enqueue the two new parts and iterate until no candidate certifies or evaluates a strict decrease.

## Driver assessment

Accepted the original-energy acceptance check, induced sparse subgraphs, bounded terminal trials, and explicit split-search limitations. Hard constraints remain separate booleans, never numeric infinities. Did not adopt unverified complexity claims, flow-value early stopping, or a Gomory-Hu implementation. The suggested terminal sets remain a possible remedy if mesh inspection reveals singleton cuts.
