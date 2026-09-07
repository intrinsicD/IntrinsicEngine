**Correction accepted.** The 90° source-antipodal cutoff matches Shapira; the 45° figure I used earlier was the sphere-geometry consequence, not your parameter.

**Owner / cancellation / disconnected-root artifact.** Face-birth ownership assigns each face to the peak whose superlevel component first swallowed it. Cancelling a peak hands its faces to the parent that absorbed it at the saddle. Retaining a *grandchild* while cancelling its parent then carves a hole out of the grandparent's drainage. What remains of the grandparent is whatever was not claimed by retained descendants, which need not touch the grandparent's own peak. Hence a root born at a noisy pole, with a tiny exclusive basin, keeps the pole cap plus scattered fragments the retained lobe peaks never reached. Persistence bounds a peak's prominence, and core area bounds the component at death, but neither bounds residual exclusive area after descendants are decided. Your diagnosis is right: exact plateaus are orthogonal to this.

**Must the mandatory root represent a part?** No. Infinite persistence is the elder rule's bookkeeping convention, not a segmentation claim. Nothing in the method says the global maximum must own a region. It only says the root is never *paired*. Gating it on exclusive area like any other peak is consistent with the model.

**Is Round 3 defensible and smallest?** Defensible, but not smallest. The log-thickness variance merge and boundary-length tie-break are a second segmentation criterion layered on top of the watershed. A narrower change lives inside the mechanism you already have:

1. Process peaks in increasing persistence, so descendants are decided before ancestors. Elder rule guarantees parent persistence ≥ child persistence.
2. Define the area gate on *exclusive residual area*: component area at death minus area already awarded to retained descendants. Apply it to the root as well.
3. When a region fails, or a connected fragment of a region falls under threshold, hand it to its tree neighbour: the parent for ordinary peaks, and for root fragments the retained child sharing the longest boundary. Only fall back to ID order on ties.

This keeps the same two thresholds, no new metric, and merges follow the saddle structure rather than a value statistic. It should give two neck regions and one decorative region for the same reason yours would, but with one fewer knob to defend.

**Caveats.** This is repair after the fact. The pole peak is a ray-sampling tail, and higher persistence will not remove a maximum that is genuinely the global one. Pole-tail behaviour must be examined on the frog before Round 3 is fixed. Agreed on evaluating frog Round 2 first.

**Round 4 level sets.** Yes, genuinely different. Watershed on maxima only sees parts containing a maximum. A tapering limb has none, so it drains into the body. Shapira's GMM partitions by thickness *value* class, and iso-thickness contours cut across a limb, giving a seam without a limb maximum. It is not a substitute: on the neck it would isolate the waist band as its own class. Reserve it as a complementary cue, not a replacement.
