<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Bounded writable ext4plus acceptance

Status remains **4.3/5 project-tracked progress** until all gates below pass on
the final integrated tree and the required artifacts are inspected. Historical
receipts at `31af4c3`, `2f3d9ce`, or any other tree are diagnostic context only.
This contract defines filesystem acceptance, not arbitrary ext4 support or an
everyday-use operating-system certification.

## Source and integration boundary

The implementation is ported from writable-ext4plus
`8dcaf7d81d4b0ebdde554402c1f9e48103748891`, relative to its foundation
`804c6ac065d92813e604b971f912383c3addf350`, onto OpenRFS main
`05ff3fe48be0b60d0d47d41f4a9898e4a2386e56`. Main includes the subsequently
squashed foundation fixes and product rename; the port uses three-way content
integration after mapping legacy identifiers to main's OpenRFS identifiers.
The exact file mapping and exclusions are in `EXT4-PORT-MAP.json`.

The filesystem changes include the project-maintained `vendor/ext4plus` fork;
the registry dependency closure is unchanged. VFS, native handle teardown,
SDK, storage-lease ownership, and package publication dependencies are included.
Main's console, desktop, installer, app removals, and branding are preserved.
Legacy Notes/Paint/Media Editor journeys and their UI-only host tests are not
ported: those applications were intentionally removed from main. Filesystem
publication, exhaustion, restart and readback remain required through VFS/SDK
and guest storage probes. Main's current desktop capture remains in `verify`.
No filesystem scenario is replaced by a desktop screenshot.

## Finite 5/5 contract

All five obligations are conjunctive; a passing host test alone does not advance
the milestone. Tests must use real generated fixtures. Missing fixtures, omitted
logs, ignored required tests, timeouts, and uninspected artifacts are failures
or unavailable evidence, never passes.

1. **Admission and reads.** Admit the geometry and feature masks below only.
   Validate checksums, complete block/inode ownership and namespace references,
   journal maps and replayed metadata before exposure or recovery home writes.
   Read/pread/stat/lstat/fstat/seek/readlink and snapshot iteration must preserve
   inode identity, EOF and offsets through supported namespace changes.
2. **Writable public semantics.** Create/open (including exclusive creation,
   dangling final symlinks and inode-bound truncation), writes/append, sparse
   growth/shrink/re-extension, mkdir/rmdir, regular hard links, symlinks, unlink
   with open handles, same/cross-parent rename, no-replace and replacement,
   guarded publication, mode/time and admitted xattr/ACL operations must work
   within the bounds. Unsupported types and combinations must refuse safely.
3. **Durability and retries.** JBD2 reservation, descriptors, payload, revokes,
   checksums, commit, checkpoint, sequence, wrap, recovery markers and flush
   barriers must survive every tested acknowledged/unacknowledged storage
   prefix. Retain uncertain I/O plans byte-for-byte; discard rolled-back
   deterministic refusals. fsync/sync must never promote an incomplete retained
   write to successful durability. Close is not a durability barrier.
4. **Allocation and ownership.** All allocation-bearing mutations and rollback
   must preserve bitmap/group/superblock counts, extent ownership, zero-filled
   sparse/tail bytes and orphan lifetimes. Exhaustion and adaptive splitting
   must permit later fitting requests without leaks or duplicate ownership.
   Handle generations, shared EOF, cursor publication and lease teardown must
   pass contention and refusal tests under the supported single-core model.
5. **Integrated-tree evidence.** The local gates below, full remote filesystem
   workflow, required branch checks and applicable NVMe/process/userspace gates
   must pass. Inspect actual artifacts for head/base/merge/tree identity,
   manifest hashes, complete scenarios, guest exits/markers, zero timeouts,
   fsck and allocation reports, replay and Linux readback. Resolve reviews and
   merge normally under current rules; verify resulting main commit and tree.

## Public operation and gap map

Public `openrfsfs_*` operations in `src/kernel/vfs.c` dispatch through
`include/openrfs/vfs_backend.h` to `src/kernel/ext4_fs.c`. Rust ABI wrappers in
`src/rust/abi.rs` and `lib.rs` enter `src/rust/ext4.rs`. Mutation endpoints use
the shared `JournalMutationStage` and retained JBD2 executor; direct upstream
writes never receive the device writer.

| Public operation | C / Rust route | Required observable proof |
| --- | --- | --- |
| mount / recovery | `ext4_backend_mount` / `mount`, recovery validation reader | hostile feature/geometry/checksum/ownership refusal before writes; replay and remount |
| open / create / exclusive | prepared backend open / `prepare_open`, create transaction | exact retained identity, no mutation on handle exhaustion, dangling/existing symlinks, old/new crash namespace |
| read / pread / seek | backend cookies / inode-based pread | same inode after parent rename/name reuse; pread leaves cursor; failed reads publish zero bytes |
| write / append | leased backend write / `write_inode`, `append_inode` | live EOF under lease, 256 KiB splitting, adaptive rollback, exact retry and short-count publication |
| truncate / ftruncate | path or inode truncation / retained reclaim | sparse zeros, partial-block shrink/re-extension, counts and revokes, unchanged cursor/shared EOF |
| mkdir / rmdir | directory probes | dot/dotdot, parent counts, multiblock empty directories, open snapshot lifetime |
| link / symlink / readlink | namespace probes / literal targets | regular inode link identity; fast/long/dangling/looping links; short readlink buffers |
| unlink / close | guarded unlink / orphan chain and final reclaim | live zero-link inode I/O; last close, sync and repeated recovery cleanup |
| rename / replace / publish | guarded rename / one namespace transaction | parents, cycles, replacement type/emptiness, held target orphans, expected source inode |
| stat / lstat / fstat | path/inode metadata | raw mode/uid/gid/links, signed stored timestamps, zero-link and stale-handle cases |
| directory open/read/close | Rust `DirectorySnapshot` | captured names/metadata survive create/delete/rename; close releases snapshot |
| chmod / set times | inode metadata transaction | ACL mask update, timestamp precision/bounds, retry-stable staged timestamps |
| xattr / ACL inheritance | admitted `user.*` operations / attribute packing | inode/external/shared block hashes, copy-on-write, release, rollback, Linux export |
| fsync / sync | inode-owned or volume retained executor | all storage refusals, unrelated inode Busy, clean idempotence, incomplete request reports FULL |
| unmount / health | prepare, lease close, Rust release | no live references, retryable clean marker, frozen failed teardown, no stale view |
| credentials / modes | native volume capabilities; uid/gid 0 creation | access flags and capabilities enforced; preserve/inherit mode, ACL and setgid metadata |

The former audit's Partial rows are obligations in this table, not permission to
rename a missing semantic as complete. The current native API has no non-root
credential input: metadata/ACL preservation does not claim multiuser ACL
authorization. Adding identities and their permission policy is later OS work.
Backend cursor cookies remain opaque VFS-owned resources; moving the storage of
the cursor into VFS is an architectural refactor, while authenticated ownership
and observable offset semantics are required now.

## Supported bounds and explicit refusals

- 4 KiB blocks, 256-byte inodes, 64-byte group descriptors, first data block 0;
  compat `0x002c`, incompat `0x20c2` plus recovery `0x0004`, ro-compat `0x046b`.
  Unknown bits, readonly states, invalid geometry/checksums/ownership, unsupported
  orphan forms, external journals and unsupported journal features refuse.
- 64 total staged images (data and metadata share that budget), 8,192 revokes,
  at most 8,192 journal slots. Checksum-v3, 64-bit tags; magic escaping and
  sequence overflow refuse. Adaptive writes halve precommit chunks down to one
  touched block, with full allocator rollback before retry. Reclaim also splits.
- 256 KiB backend requests and 64 MiB mutable file size. Native copied writes
  can return shorter 4 KiB chunks. Bounds/overflow refuse before mutation.
- Two volumes, 128 VFS vnodes, 64 file handles, 32 directory iterators; the ext4
  backend shares 64 cookies across file/directory handles. Mount-relative paths
  are shorter than 128 bytes with at most 16 components; on-disk names and
  snapshot entries support 255 bytes. UTF-8 bytes pass through without casefold.
- Namespace census and directory snapshots bound 8,192 entries, pending
  directories 512, ancestry walking 1,024. Census is repeated on view reload.
  Exceeding a bound refuses; no unbounded namespace/performance claim.
- `user.*` explicit xattr mutation, inline plus one external block, names up to
  255 bytes; full packing refuses atomically. External value inodes refuse.
  Valid stored access/default ACLs are preserved and used for inheritance/chmod.
- Writes respect immutable/append-only flags. Supported explicit write times
  span epoch through `0x37fffffff` seconds, nanoseconds below one billion; read
  metadata preserves valid pre-epoch values. Reads use noatime.
- Single-core kernel scheduling plus tested host contention, not SMP certification.
  An unhealthy storage owner freezes new operations until explicit teardown.

## Atomicity, refusals and exact-tree verification

Metadata namespace transactions are atomic at the commit boundary. Existing-file
ordered data can reach home before metadata commit; a power cut may leave a mix
of old/new overwrite bytes. A split request can retain a checkpointed prefix.
Neither sector-level overwrite nor the whole 256 KiB request is all-or-nothing.
For publication, write and fsync a temporary inode, then guarded atomic replace.

A deterministic precommit write refusal rolls back allocations and retires the
logical request. A durable prefix returns a short write whose suffix belongs to
the caller. An uncertain storage refusal retains the exact request/plan. If a
later fsync/sync retries it and hits ENOSPC after a prefix, it reports FULL and
retires the refused suffix; subsequent sync can clean the already durable state.
A failed rollback reload leaves the view absent until reload succeeds and must
not cause sync to apply the rejected write.

Required local targets: clean `make verify`, `make ext4-tests`,
`make ext4-fsync-test`, `make ext4-sparse-truncate-test`, all transaction,
coordinator and revoke-allocation tests including ignored allocation proofs,
with `OPENRFS_EXT4_KERNEL_INTEROP=1` and a generated fixture. Run representative
QEMU recovery, normal VFS, capacity, error, physical-cut and remount scenarios
for changed behavior; inspect bytes, fsck, allocator and journal state.

The full remote workflow adds every ext4 operation's crash/refusal matrix,
admission/geometry, journal wrap, dense 64 MiB allocation/reclaim, exhaustion,
and **ten complete serial TCG sweeps**. Main currently declares **115 scenarios**
and 459 shell assertions: its intentional app removals account for the older
writable branch's 117. This port does not change main's scenario list or expected
counts. Each sweep must match `make contract-scenarios` and `contract-counts`,
contain every success receipt and every fresh nonempty serial log, and stop on
the first failed command. No previous sweep's stale log supplies evidence.

`filesystem_evidence.py` records checkout commit/tree and PR head/base, hashes
the source tree manifest and uploaded artifacts, and validates sweep completeness.
CI runs with two Rust test threads to bound fixture memory without omitting tests.
The host test profile optimizes repeated image validation while explicitly
retaining debug assertions and arithmetic overflow checks.
Linux mount checks use disposable images and release loop devices. Old receipts
remain historical; no check badge substitutes for final-head artifact inspection.
