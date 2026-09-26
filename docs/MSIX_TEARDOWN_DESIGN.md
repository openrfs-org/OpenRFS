# MSI-X teardown and queued delivery

The production paths are `virtio_net_shutdown`/`reset`, NVMe and xHCI
teardown, and the VirtIO RNG device proof. Each stops its device and calls
`msix_unbind` with interrupts disabled. The PCI function and its MSI-X table
are device-controlled inputs; the local APIC and the kernel's vector allocator
are the only interrupt ownership roots. The handler context, PCI claim, BAR
mapping, and vector must remain owned until queued delivery is serviced.

The old unbind path unregistered the handler and freed the vector before
reenabling interrupts. A queued fixed interrupt in the local APIC IRR then
reached an unowned vector and caused the observed fatal vector 144. Reusing
that vector could instead deliver the stale interrupt to a different device.

Teardown now masks the function and entry, checks the PCI config readback,
disables MSI-X, and checks that readback before changing the table. It replaces
the live device handler with a no-device-access drain handler while interrupts
are disabled. It then services local APIC IRR requests for the vector with
bounded interrupt windows before unregistering and recycling the vector. If
the requests do not quiesce, teardown fails and retains the handler, vector,
claim, and mapping. A QEMU proof queues a self-IPI on the allocated vector
with interrupts disabled and requires the drain handler to observe it.

The bounded quiet period is functional evidence for the emulated single-CPU
path, not a hardware guarantee against a device that ignores MSI-X masking or
sends arbitrarily late messages. Without an IOMMU and platform interrupt
remapping, a malicious PCI device remains outside this boundary. Physical
platform qualification must include interrupt-remapping and teardown tests.
