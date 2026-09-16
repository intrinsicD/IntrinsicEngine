# Final results review resolutions
- Claude accepts the bounded result and independently reproduces all medians,
  ranges and percentages from the ten supplied sample rows. No release blocker.
- Keep ABBAABBAAB, explicitly defining A=before and B=after. The review inverted
  the letter convention; the actual manifest/order/attempts all agree.
- State the 1.77 ms higher after no-op median and overlapping ranges. Do not adopt
  the suggestion that its maximum drives the median or call it proven noise; the
  cause is not isolated. Keep all samples; no no-op speedup/regression claim.
- Specify that every after *rebuild* sample beats every before rebuild sample;
  that separation does not apply to the no-op scenario.
- Retain maximum single-process RSS without a memory improvement claim.
- Claude's final audit is fixed-packet arithmetic review, not archive inspection.
  Root independently validated all ten canonical files, freeze hashes, source
  identity, raw invocations and archive membership; runner assertions check every
  sample's commands, modmap, prerequisite BMIs and package identity.
