# Synthetic protected-custody fixture

This result-free fixture exercises protected review, authorization, digest
invalidation, and one-shot attempt consumption without private or held-out
evidence. Regression tests copy the files into a temporary Git repository,
replace only task/source/path placeholders, freeze the protocol, and never
contact a network or external dataset.
