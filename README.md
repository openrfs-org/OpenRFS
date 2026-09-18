OpenRFS writable ext4plus development branch
============================================

OpenRFS is a small, privacy-focused, Unix-like operating system built from
scratch for x86_64.

This branch preserves the in-progress writable ext4plus work. It forked before
the repository-wide OpenRFS rename, so many source paths, ABI identifiers,
fixtures, and historical commits still contain the former `phipia` name. Those
names describe the branch's ancestry and current forward-porting work; they are
not the current product identity. The authoritative OpenRFS product tree and
project README are on `main`.

Branch status
-------------

* Current product identity: OpenRFS.
* Current branch purpose: preserve and complete the writable ext4plus work.
* Divergence at this update: 355 branch-only commits and 8 `main`-only commits.
* Implementation: extensive Stage 2 filesystem, VFS, recovery, durability, and
  fault-injection work exists on this branch.
* Verified milestone gates: 1 of 5. Stage 1 has recorded host/e2fsprogs evidence
  at `771ab6c`; the complete Stage 2 gate is not yet established.
* Exact-head CI: the previous branch tip had no GitHub check runs. A green claim
  requires fresh checks on the final forward-ported head.

The `1/5` value is a verification count, not an implementation-progress count.
It must advance only when the corresponding acceptance gate has complete,
inspected evidence on the exact branch head.

Current technical boundary
--------------------------

The detailed implementation and evidence boundary is documented in
`docs/EXT4.md`. The historical audit and gate definitions are in
`docs/EXT4-WRITABLE-AUDIT.md`.

Before this work can merge, it must be forward-ported onto current `main`, the
remaining legacy product identity must be reconciled without breaking ABI and
fixture provenance, deleted applications must stay deleted, and every required
filesystem, NVMe, VFS, package, networking, and release check must pass on the
resulting exact head.

This branch is development evidence. It is not an everyday-use certification,
an anonymity claim, or a release-ready tree.
