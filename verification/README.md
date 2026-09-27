# OpenRFS verification platform

The manifest in `verification/manifest.json` is the executable inventory. Each
target names production files, the real oracle, inputs, tool requirements,
execution context, limits, and what it cannot establish. `run.py` refuses an
unavailable required tool and returns a nonzero result for a timeout, finding,
infrastructure failure, or interruption. Its `verification/runs/<run>/run.json`
is the machine-readable receipt. Run directories are unique and ignored by Git;
CI uploads them even when a job fails. Do not infer a clean HEAD from a passing
run whose `source.dirty_paths` is nonempty.

## Commands

```sh
make verification-list
make verification-manifest
make verify-fast
make verify-extended
make fuzz-smoke
make fuzz-nightly
make fuzz-replay TARGET=package-state-parser INPUT=/absolute/input
make verification-report
python3 tools/verification/run.py run --profile extended --resume verification/runs/<interrupted-run>
```

`verify-extended` runs the existing `make verify` recipe unchanged on a clean
HEAD, followed by static scans, instrumented C fuzz campaigns, a normal QEMU
boot, and the six-scenario production guest matrix. `make verify` itself
cleans `build/`; runner logs live outside that tree. Extended and nightly runs
reject dirty sources. The runner serializes jobs with an advisory lock, limits
individual processes and output files, kills process groups on timeout or
interrupt, and leaves `unfinished` target names in the receipt for an exact
clean-source resume. A fast run may use a dirty development tree, but cannot
be used as exact-head evidence.

The three Clang 18 host fuzzers compile the *production* C translation units.
Saved valid and invalid seeds run through standalone replay binaries before
each campaign. LLVM source profiles measure production functions and regions
reached by the saved seeds; libFuzzer reports campaign edges and executions.
ASan, UBSan, and ASan's leak detector cover only those host binaries. The
ACPI and Multiboot2 shims place test tables in 32-bit-addressable host memory
because the kernel parsers expect identity-mapped early physical addresses.
The Multiboot2 shim makes remaining mapped bytes zero; it does not model
missing physical pages. This is not guest kernel sanitizer coverage. The
transaction target invokes production
`tools/openrfs-transaction.py` with deterministic, versioned operation bytes
and an independent expected generation/version/file/user-data model.
Valgrind Memcheck separately replays all eight committed package-state seeds
against a plain, unsanitized build of the same production C parser.

QEMU receipts require expected exit codes, begin/pass markers, no panic, and
scenario-specific serial checks. `qemu_matrix.py` preserves each serial log and
structured receipt. It checks the copied serial hash against the recipe's hash.
The TCP scenario additionally reports teardown receipts. Machine acceleration
is explicitly TCG. These checks do not provide guest source coverage.

## Current baseline and limits

The starting main was `f78d25d4ac43f05875bce17d80fbba738428d61e`.
This branch was based on main because the separate process/POSIX PR was active
and still changing; stacking that unreviewed work would make the quality branch
hard to attribute. Main has 115 Makefile QEMU scenarios and 459 shell assertions.
The earlier 116/460 inventory described another branch, not this starting SHA.

The initial dirty-tree fast run at 2026-09-27 14:10 UTC passed nine targets on
that base plus uncommitted platform changes. It executed 25,000 inputs in each
C campaign and 200 Hypothesis examples. Saved seed replay reached 711/1180
regions in `package_state.c` (320/882 branches), 139/499 regions in
`acpi_madt.c` (73/312 branches), and 17/28 regions in `acpi_util.c` (6/14
branches). These are *per-target host instrumented source* counts, not an
overall kernel coverage percentage. The package corpus began with 8 committed
seeds and grew to 69 in the isolated campaign copy; ACPI began with 8 and grew
to 49. The added Multiboot2 corpus has 10 committed seeds. Its dirty-tree
runner smoke executed 25,000 inputs in 0.467 seconds and reached 210/281
production regions and 137/228 branches in `multiboot2.c`; all 11 production
functions were reached, including memory-map and framebuffer validation.
Those are saved-seed source coverage counts; the campaign's 116 edge count is
a separate libFuzzer measure. Hypothesis used seed 731 and exercised install,
remove, cancel, injected disk-full, reopen, and tampered-stage recovery.

The first clean-source extended attempt on signed commit
`da44309f002f4efc3608b8c417eaf06586990f08` failed two platform checks:
the isolated Python venv omitted `cryptography`, which the existing package
repository test needs for Ed25519, and the 512 MiB address-space cap was too
small for zizmor. The five QEMU scenarios, normal boot, fuzzers, and focused C
scanners passed in that attempt. The repair pins `cryptography` and its
dependencies in the venv and gives zizmor a 2 GiB address-space limit.
GitHub's first fast job also exposed a profile-validation variable shadowing
bug; a runner regression now simulates an extended-only tool gap while
validating the fast profile. GitHub's clean-HEAD fast and extended jobs both
passed on `3b9ce151d489e3eed16cab3dd990d3195ea9a0a9`, including the
unchanged `make verify` recipe. The subsequent native network finding has its
own failing and fixed exact commits and serial digests in
`verification/findings/native-network-wait-slot.md`.
GitHub's fast and extended jobs also passed on the fixed
`eafe65bae768b38112cd9a47d827894d7c63c477` head, including the new
six-scenario guest matrix.

Gitleaks 8.30.1 now gates the commits after the PR merge-base and retains a
fully redacted finding report. A separate exploratory scan of the repository's
older history timed out after 120 seconds. It had reached 234 commits and
reported 27 candidate matches in fixture, tooling, and vendored files. These
candidates still need triage; the partial run is not
recorded as a clean full-history scan. The committed gate establishes only
the new branch commit range.
A current-tree exploratory scan completed and reported 16 redacted candidate
matches: four committed TLS private-key fixtures and 12 generic-key heuristics
in TLS code, fixture hash tooling, and vendored checksum files. These files
have not been broadly suppressed, and the current-tree scan is not green.

An exploratory Clang Static Analyzer pass across `src/kernel/*.c` found
candidate stack-lifetime and uninitialized-value warnings, and one missing
Monocypher include because that ad hoc pass lacked a per-file vendor include
flag. The raw log and analyzer plists remain in the local ignored
`verification/runs/manual-static-baseline/`. It is **not** a clean broad scan.
The CI gate runs Clang Static Analyzer, clang-tidy, and Cppcheck over the four
production C files listed in the manifest, using the actual common kernel
target flags. Broad findings need separate ownership and precondition review.

Ruff found `NoReturn` missing from the UI font asset generator. Before the fix,
`typing.get_type_hints(fail)` raised `NameError`; after importing `NoReturn` it
resolves. `check_python.py` includes that regression and scans 85 first-party
Python files with correctness-focused rules. The earlier before/after output
is retained under `verification/runs/manual-ruff-finding/`.

Remaining high-risk gaps include guest syscall sequence generation, stale PID
and handle reuse, raw FAT32/ext4 cut-point recovery across actual guest
restarts, encrypted Data and account state on the separate security branch,
network packet parser host fuzzing, TLS handshake fuzzing, and PCI/DMA/NVMe/xHCI
teardown faults. The six-scenario QEMU matrix is a narrow production-path
check, not a claim that all 115 scenarios ran. The existing `make verify`
includes host ext4, package, TLS, and related tests but no full QEMU matrix.

## Tool roster

"Integrated" means a manifest target executes the tool. "Not yet evaluated"
means the project needs a documented suitability and license/version review
before it may be counted as coverage. This inventory intentionally does not
turn installed but unused tools into green checks.

| Project | State | Scope or next decision |
| --- | --- | --- |
| LLVM/Clang sanitizers | Integrated | Clang 18.1.3 ASan/UBSan/LSan on three host-built production C parsers. |
| LLVM libFuzzer | Integrated | Clang 18.1.3, three independent in-process parser targets with replay and corpus coverage. |
| AFL++ | Not yet evaluated | Consider process isolation for parsers with non-resettable global state; no duplicate label for the current libFuzzer targets. |
| Rust cargo-fuzz | Not yet evaluated | Inspect production ext4/image crate callability and nightly sanitizer compatibility. |
| Hypothesis | Integrated | 6.168.1, bounded package transaction operation sequences against production Python. |
| QEMU | Integrated | 8.2.2 TCG normal boot and six separate production scenarios. |
| Clang Static Analyzer | Integrated | 18.1.3, four file gate; broad candidate findings retained for triage. |
| clang-tidy | Integrated | 18.1.3, targeted correctness checks on the same four production files. |
| Cppcheck | Integrated | 2.13.0, independent four file warning/performance/portability gate. |
| Rust Clippy | Integrated | Rust 1.98.1 correctness gate over all four first-party Cargo crates, with an inventory assertion. |
| RustSec cargo-audit | Not yet evaluated | Audit tracked locks with advisory data timestamp and applicability. |
| cargo-deny | Not yet evaluated | Develop researched policy for vendored Rust dependencies. |
| OSV-Scanner | Not yet evaluated | Determine attribution for vendored C and Rust components. |
| Syft | Not yet evaluated | Generate exact-source/build SBOM and identify bundled components. |
| Trivy | Not yet evaluated | Decide whether SBOM cross-check adds independent signal. |
| Gitleaks | Integrated | 8.30.1 scans new branch commits against the merge-base, fails on findings or incomplete scans, and retains a redacted report. Full history remains a separate triage task. |
| ShellCheck | Integrated | 0.9.0, tracked first-party shell scripts and actionlint embedded shell; vendor scripts excluded. |
| actionlint | Integrated | 1.7.12, every workflow, pinned archive digest in installer. |
| zizmor | Integrated | 1.30.1 offline workflow audits, pinned archive digest; online audits omitted. |
| Ruff | Integrated | 0.16.9, 85 first-party Python files and a type-hint regression. |
| Bandit | Not yet evaluated | Assess incremental value for scripts handling paths, downloads, and proof artifacts. |
| Semgrep Community | Not yet evaluated | Develop and test a small repo-specific ownership/evidence rule set. |
| Valgrind | Integrated | 3.22.0 Memcheck and leak check on eight saved package-state seeds in a plain host build; no guest coverage. |
| OSS-Fuzz | Not yet evaluated | Requires local target maturity, disclosure process, maintainers, and external enrollment decision. |

Pin sources: `tools/verification/install_action_scanners.py` verifies archive
SHA-256 for actionlint, zizmor, and Gitleaks; `tools/verification/requirements.txt` pins
Hypothesis, sortedcontainers, and Ruff; CI pins Rust/Clippy 1.98.1 for the
extended gate, Ubuntu apt package versions, and GitHub actions by immutable
commit. The platform's scanner inventory and exact
command lines live in the manifest and each run receipt.

## Findings and repair

On a failed campaign, the runner preserves the first failure, attempts bounded
libFuzzer minimization, replays a retained input twice, and writes
`artifacts/finding.json` with source/tool/input hashes, logs, reproduction
command, status, and empty fields for root-cause/fix/regression triage. A stable
diagnostic-class ID is only a preliminary grouping; a human must deduplicate by
root cause. A failure that does not reproduce twice is marked flaky, never
converted into a pass. For potentially sensitive inputs, retain the local
artifact and report only a concise finding in public CI or PR text.

The repair loop is: choose a reproduced high-impact finding; minimize; inspect
the actual production precondition or ownership failure; add a regression that
fails before the fix; make the smallest source change; rerun replay, focused
host/guest checks, the wider exact-head suite, and CI; then sign and verify a
new commit. No automatic source edits or public disclosure occur without
triage. The runner automates collection and replay, not autonomous code review.
