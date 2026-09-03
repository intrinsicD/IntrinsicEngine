# Framework24 current-default curvature differential — 2026-09-03

## Disposition

This is a local-development candidate observation for BUG-163, not sealed or
claim-eligible evidence. It replaces the current-product interpretation of the
historical two-ring comparison; it does not erase that earlier result. BENCH-001
still owns the clean, frozen, independently audited Framework24 comparison.

## Bound inputs

- Framework24 source: clean commit
  `6dd50a8289c64b5054bc9601beb5647f459d7969`.
- Framework24 invocation:
  `CurvatureTaubin(mesh, 0, false, Policy::Sequential)`.
- Intrinsic source: BUG-163 implementation worktree based on
  `ce2379fe5f6702b50dc1c4cf0d916a187e782749`; the final implementation is
  commit `62d5544dbf549d096a6b907b5846d1827e4f34e5`.
- Intrinsic toolchain: CMake `ci` preset, Clang 23.0.0.
- Shared input: `tests/data/sculpt.obj`, SHA-256
  `210dc85f20cdec11cb0b50bec255b86126fb6b47f9f938369049570de3f88a25`.
- Input handling: both probes received the identical OBJ coordinates. This
  sculpt asset already has unit maximum extent, so Framework24's separate
  centering step does not change curvature.

The actual Framework24 probe emitted SHA-256
`a247c1f89eb3ce02177eaa2d04ff5acc0b4ca9541b24db603046c112e326fd49`;
the Intrinsic `ICURV002` payload emitted SHA-256
`18f05b0dc64863c88be7f2af7b7d9500e0e415c441d0803e19993ea6dc061c51`.
Those raw files were local `/tmp` diagnostics and are not durable proof; the
checked-in regression freezes the whole-field hash and readable anchors.

## Independent recalculation

An independent parser decoded the Intrinsic binary header and all samples,
joined them to the Framework24 text rows by stable vertex index, and recomputed
absolute scalar error plus sign-invariant direction error. It observed 3,669
vertices, 11,013 edges, 7,342 faces, zero boundary vertices, zero rejected
triangles, and 3,669 supported/nonzero curvature vertices.

| Quantity | Recomputed result |
| --- | ---: |
| Maximum position absolute difference | `0` |
| Maximum `kmin` absolute difference | `5.440092820663267e-14` |
| Maximum `kmax` absolute difference | `8.237854842718662e-14` |
| Maximum sign-invariant `dir1` component difference | `2.979848701123444e-8` |
| Maximum sign-invariant `dir2` component difference | `2.980150226594702e-8` |
| FNV-1a hash of both scalar fields after `1e-5` quantization | `0xfc090818c136a6e2` |

Direction error includes Intrinsic's deliberate conversion to public
`glm::vec3` storage. Direction signs are geometrically equivalent and therefore
compared modulo sign. No timing result was interpreted.

## Readable scalar anchors

| Vertex | `kmin` | `kmax` |
| ---: | ---: | ---: |
| 0 | `-1.1309506518419563` | `47.629353972839404` |
| 4 | `-1.2745543828717363` | `75.265942565283126` |
| 511 | `1.9095566040154699` | `2.0814124836958148` |
| 1000 | `1.9913375118628998` | `1.9971206015746612` |
| 2048 | `-4.0273646171654249` | `0.080819169051565512` |
| 2901 | `-3.3026408414958044` | `-3.0920399983311264` |
| 3668 | `-4.0106676790063247` | `0.025921025178834916` |

## Scope

This run supports only current-revision compatibility on identical finite sculpt
coordinates plus the checked-in analytic/acute/obtuse/open controls. It does
not establish universal geometric accuracy, performance, loader equivalence,
direction identity at repeated eigenspaces, METHOD-039 semantic-part quality,
or product-level Framework24 convergence.
