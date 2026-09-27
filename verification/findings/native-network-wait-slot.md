# Native network wait skipped readiness in a later result slot

- **Affected path:** `openrfs_wait` through `poll_wait_items` in
  `src/kernel/native_process.c`.
- **Failing exact source:** signed commit
  `d29bdb1203ac176333f24496e6ca3c1a0de27d0e`.
- **Fixed exact source:** signed commit
  `eafe65bae768b38112cd9a47d827894d7c63c477`.
- **Reproducer:** `make QEMU_ACCEL=tcg qemu-test-network-native` on each
  commit, using the committed `network-native` guest phase and Python oracle.
- **Before:** QEMU returned nonzero. The serial record contained
  `OPENRFS NETAPP WAIT SLOT failure ready=-110 first=0 second=0 close=0`,
  an app exit of 37, and `ST FAIL network-native`. Serial SHA-256:
  `24ab3d55fc1aee432f132d8544b248ba32e39d18b6b68731b04bcec1b2ed8bfa`.
- **After:** QEMU returned zero. The serial record contained
  `OPENRFS NETAPP PHASE wait-slot-mapping PASS` and `ST PASS network-native`.
  The structured receipt recorded expected and observed exit 245, one success
  receipt, one teardown receipt, and no timeout. Serial SHA-256:
  `1a28122a89a18256f0fa434896e7a96e82f4c370539a95840d36c285923f38f4`.

`network_poll` fills each result at the request's index but returns
`result_count` as the *number of ready entries*. With an idle first request and
a writable second request, the old loop inspected only result slot zero and
eventually timed out. The fix walks all `network_count` positional results.
The guest regression supplies this exact two-handle case and requires the
second slot's writable flag. The local fast profile also passed after the fix.

This finding establishes that the bounded guest case is repaired. It does not
prove arbitrary network scheduling or concurrent handle teardown.
