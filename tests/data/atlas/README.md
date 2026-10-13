# Atlas diagnostic fixtures

Maintained inputs for `tools/diagnostics/atlas/{check_property_atlas,trace_atlas,
run_boundary_refinement,inspect_boundary_refinement}.py`. These fixed geometry,
region labels and expected-stage inputs are required by those consumers; they
are not a retained campaign archive. Ownership: the atlas diagnostic tools and
their regression tests. Add/update fixtures only with the consuming checks.

Promoted byte-for-byte from `ara/evidence/diagnostics/method045/` at
`36a7b5062d9be3239d667efd9e41c4d4028ba249`. The relative suffixes are unchanged.
No solver was run during promotion.

| Fixture | SHA-256 |
| --- | --- |
| `baselines/dolphin.geometry.json.gz` | `dfdcf20eaf48523695051a1456ff84ffb2682fc9716818315fd79a1dc222876e` |
| `baselines/dolphin.labels` | `be06515fd74c6ebc08d47ca5e05720c3bffb5aed71cf519c8ce307651018dd26` |
| `baselines/fandisk.geometry.json.gz` | `127644edc8298357e65408a47599d1e0a8518fbee2cfb4ee89daf927fd2b578d` |
| `baselines/fandisk.labels` | `d66b3d215a241914f7362584840d111f416b76a047c208ec589d9efa3dd14ed7` |
| `baselines/frog.geometry.json.gz` | `5e28e544b727c693b221269bd2f7924ab220078e809872120e472913aa3133e3` |
| `baselines/frog.labels` | `23817553e6515f245e5f22e515a03acc1aebdafce28ead039b2a0bff265e8399` |
| `baselines/sculpt.geometry.json.gz` | `7afeffec4018cdad9032b0611b9d54710514fdd3dd7826c2913c56acae6cc8f2` |
| `baselines/sculpt.labels` | `c2b1ea735efb706b16c29dd40fa2ac69da60624b0a30566cd9928aff4575daaf` |
| `examples/frog/result.npz` | `af5f0adf0f0d2138529a2f14bc6a0d92a8dc020db6457822348d17a514059ecb` |
| `examples/sculpt/result.npz` | `94d1fa6dbf8a33c1b0207883a7dd93b47f2e14e877930f015c1f66412abf3159` |
| `stages/frog.edges.gz` | `419d145d1d041df92e179c3bacf74823d8032a7497b15e12509f2e91492cc57a` |
| `stages/frog.extrema.json.gz` | `2d1e2921203f7fe116b6be7ce2c07261d8e6d7a7961e571baca37cc8774d0715` |
| `stages/references/frog-feature-labels.npy` | `a5880b62f9065096abc81be72305c4c714bb23ea8b82ac91d0f1614a7572e414` |
| `stages/references/frog-protected-labels.npy` | `ffebc67d5fbe56b899839b80e577ba2fde8930ff9a544ddc80fc563e29e70d1c` |
| `stages/references/sculpt-feature-labels.npy` | `38f452df034292aa0b2870148c955bf9b329f43ae21e7b3d9b18e289dcd8e9f3` |
| `stages/references/sculpt-protected-labels.npy` | `154ed13ad32570c1ff54acb6aaa0a3f929dc349cc2f7eadaad39deadc24e5421` |
| `stages/sculpt.edges.gz` | `9a510986d1a54f9c76c7f33e448fcb1060c64630bdf19d60be34d3b2a0b49a83` |
| `stages/sculpt.extrema.json.gz` | `a7032bb3e7d9e82ec2f39b470c2f0ae468bd7b4f4b394707189871fea1a3fea5` |
| `stages/stages.json.gz` | `947e88541ed73b1843bfb0855b230f3dcb575b0cac963d7e30cedf08fc5bc27c` |
