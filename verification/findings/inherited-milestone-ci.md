# Inherited milestone CI failures outside the verification gate

The new verification workflow's fast and extended jobs passed on signed
`29d248fcc34e59669b2c3679c1f4dab06a01a210` (run `36328965706`) and
`ce0ca0cd65ec91a8589970ea06301197dd38abab` (run `36395893912`). Other
milestone workflows on the PR still need their own triage. They are not counted
as a pass, and their assertions have not been relaxed.

* Process milestone run `36328965580` failed in `Assemble release evidence`.
  `.github/workflows/process-milestone.yml` expects FAT16 fixture SHA-256
  `3DD00932416E7CBF816C187DBEFD8467BA90B35ACB064F916BE3297BBA066FD7`.
  The unchanged `tools/make-process-fixture.py` builds and independently
  verifies `9BDC1FE33DF03F28CA07605F85A28D8C7CDFF240C9304F39007E7D7C1B979ED3`.
  The workflow log prints that latter digest immediately before exit 1. A
  separate in-memory rebuild on 2026-09-28 reproduced it. Neither the fixture
  generator nor the workflow's expected digest changed from starting main
  `f78d25d4ac43f05875bce17d80fbba738428d61e` on this branch.
* Linux uname milestone run `36328965781` failed in its release assembly. Its
  workflow expects FAT16 fixture digest
  `4A1A3DFA5A649FD7CBFF700CB51693AEDE274147CD39794AD2158CFC937736E9`;
  the unchanged generator declares
  `FC92FE49F976F42BC2DBDEA2692A220E3F7C46981F269D886A6967AB09445715`,
  which the run log prints before exit 1. The independently pinned BusyBox
  binary is needed to regenerate that full image, so this is a mismatch
  observation, not a completed repair or claim about the right release digest.
* Filesystem compatibility run `36328965600` failed after roughly 3.5 hours
  while `Exercise ordinary ext4 held-file mutation power cuts` was still in
  progress. Its setup, artifact, and earlier ext4 checks succeeded; the log
  did not provide a confirmed kernel diagnostic for that final step. Treat this
  as an unresolved, separate long-running gate failure.

The next repair should compare each frozen release contract with its intended
tag and independently generated fixture before changing an expected digest.
The filesystem job needs its last serial and power-cut evidence inspected
before assigning a product root cause. This branch does not alter those
historical milestone assertions or include the active process/POSIX PR.
