/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Turning what boot discovered into the lines a person reads.
 *
 * These functions make no decisions and change no state. They are their own
 * translation unit because the boot transcript is a contract - the Makefile
 * asserts ninety-one of these lines - and a contract is easier to keep when the
 * text satisfying it sits in one place rather than interleaved with the logic
 * that produced it.
 *
 * Nothing here panics. A report describes what was found; deciding whether what
 * was found is acceptable belongs to the caller.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <openrfs/acpi.h>
#include <openrfs/apic.h>
#include <openrfs/apic_timer.h>
#include <openrfs/boot.h>
#include <openrfs/clock.h>
#include <openrfs/console.h>
#include <openrfs/cpu.h>
#include <openrfs/framebuffer.h>
#include <openrfs/heap.h>
#include <openrfs/interrupts.h>
#include <openrfs/logo.h>
#include <openrfs/ioapic.h>
#include <openrfs/memory.h>
#include <openrfs/paging.h>
#include <openrfs/pci.h>
#include <openrfs/pic.h>
#include <openrfs/pit.h>
#include <openrfs/pm_timer.h>
#include <openrfs/self_test.h>
#include <openrfs/test.h>
#include <openrfs/thread.h>
#include <openrfs/timer.h>
#include <openrfs/tsc.h>
#include <openrfs/boot_stages.h>

/*
 * A loader may name itself with an arbitrarily long string. The transcript is a
 * contract, so the name is bounded rather than trusted.
 */
#define MAX_REPORTED_BOOT_LOADER_NAME 64U

void report_boot_information(const struct boot_information *context)
{
    console_write("OpenRFS: boot loader: ");

    if (context->boot_loader_name == NULL) {
        console_write("unnamed");
    } else {
        size_t reported_length = context->boot_loader_name_length;

        if (reported_length > MAX_REPORTED_BOOT_LOADER_NAME) {
            reported_length = MAX_REPORTED_BOOT_LOADER_NAME;
        }

        console_write_n(context->boot_loader_name, reported_length);

        if (reported_length != context->boot_loader_name_length) {
            console_write("...");
        }
    }

    console_putc('\n');

    console_write("OpenRFS: memory map entries: ");
    console_write_u64(context->memory_map_entry_count);
    console_putc('\n');

    console_write("OpenRFS: reported usable bytes: ");
    console_write_u64(context->reported_usable_bytes);
    console_putc('\n');

    console_write("OpenRFS: highest reported address: ");
    console_write_hex(context->highest_reported_address);
    console_putc('\n');
}

void report_allocator(const struct frame_allocator_stats *stats)
{
    console_write("OpenRFS: allocatable frames: ");
    console_write_u64(stats->allocatable_frames);
    console_putc('\n');

    console_write("OpenRFS: free frames: ");
    console_write_u64(stats->free_frames);
    console_putc('\n');

    console_write("OpenRFS: reserved frames: ");
    console_write_u64(stats->reserved_frames);
    console_putc('\n');

    console_write("OpenRFS: highest allocatable address: ");
    console_write_hex(stats->highest_allocatable_address);
    console_putc('\n');
}

void report_acpi_root(const struct acpi_root *root)
{
    console_write("OpenRFS: ACPI ");
    console_write(acpi_root_kind_string(root->kind));
    console_write(" at ");
    console_write_hex(root->physical_address);
    console_write(" OEM ");
    console_write_n(root->oem_id, 6U);
    console_putc('\n');
}

void report_acpi_madt(const struct acpi_madt *madt)
{
    console_write("OpenRFS: ACPI MADT at ");
    console_write_hex(madt->physical_address);
    console_write(" local APIC ");
    console_write_hex(madt->local_apic_address);
    console_write(" flags ");
    console_write_hex(madt->flags);
    console_putc('\n');

    console_write("OpenRFS: ACPI root entries: ");
    console_write_u64(madt->root_entry_count);
    console_write(" MADT OEM ");
    console_write_n(madt->oem_id, 6U);
    console_putc(' ');
    console_write_n(madt->oem_table_id, 8U);
    console_putc('\n');
}

void report_acpi_fadt(const struct acpi_fadt *fadt)
{
    console_write("OpenRFS: ACPI FADT at ");
    console_write_hex(fadt->physical_address);
    console_write(" revision ");
    console_write_u64(fadt->revision);
    console_write(" flags ");
    console_write_hex(fadt->flags);
    console_putc('\n');
}

void report_pm_timer(const struct pm_timer_state *pm_timer)
{
    console_write("OpenRFS: ACPI PM timer port ");
    console_write_hex(pm_timer->port);
    console_write(" width ");
    console_write_u64(pm_timer->counter_bits);
    console_write(" bits address ");
    console_write(pm_timer->extended_address ? "extended" : "fixed");
    console_putc('\n');
}

/*
 * The one firmware table OpenRFS reads whose absence is not a fault. A machine
 * with no PCI Express host bridge publishes no MCFG, and configuration space is
 * still reachable through the I/O ports the PCI specification has always
 * defined, so absence is reported and boot continues.
 */
void report_acpi_mcfg(const struct acpi_mcfg *mcfg, bool present)
{
    if (!present) {
        console_write("OpenRFS: ACPI MCFG absent\n");
        return;
    }

    console_write("OpenRFS: ACPI MCFG at ");
    console_write_hex(mcfg->physical_address);
    console_write(" windows ");
    console_write_u64(mcfg->allocation_count);
    console_putc('\n');

    for (size_t index = 0; index < mcfg->allocation_count; ++index) {
        const struct acpi_ecam_allocation *allocation =
            &mcfg->allocations[index];

        console_write("OpenRFS: ACPI ECAM segment ");
        console_write_u64(allocation->segment);
        console_write(" base ");
        console_write_hex(allocation->base_address);
        console_write(" buses ");
        console_write_u64(allocation->start_bus);
        console_write(" to ");
        console_write_u64(allocation->end_bus);
        console_write(" size ");
        console_write_u64(acpi_ecam_allocation_size(allocation));
        console_putc('\n');
    }
}

void report_acpi_topology(const struct acpi_topology *topology)
{
    console_write("OpenRFS: ACPI local APIC base ");
    console_write_hex(topology->local_apic_address);

    if (topology->local_apic_address_overridden) {
        console_write(" overridden");
    }

    console_putc('\n');

    console_write("OpenRFS: ACPI processors: ");
    console_write_u64(topology->local_apic_count);
    console_write(" enabled ");
    console_write_u64(topology->enabled_processor_count);
    console_write(" NMI entries ");
    console_write_u64(topology->nmi_entry_count);
    console_write(" unmodelled ");
    console_write_u64(topology->ignored_entry_count);
    console_putc('\n');

    for (size_t index = 0; index < topology->io_apic_count; ++index) {
        const struct acpi_io_apic *io_apic = &topology->io_apics[index];

        console_write("OpenRFS: ACPI I/O APIC id ");
        console_write_u64(io_apic->identifier);
        console_write(" at ");
        console_write_hex(io_apic->address);
        console_write(" base GSI ");
        console_write_u64(io_apic->interrupt_base);
        console_putc('\n');
    }

    for (size_t index = 0; index < topology->interrupt_override_count; ++index) {
        const struct acpi_interrupt_override *override =
            &topology->interrupt_overrides[index];

        console_write("OpenRFS: ACPI override ISA IRQ ");
        console_write_u64(override->source);
        console_write(" to GSI ");
        console_write_u64(override->global_system_interrupt);
        console_write(" flags ");
        console_write_hex(override->flags);
        console_putc('\n');
    }
}

void report_apic(const struct apic_state *apic)
{
    console_write("OpenRFS: local APIC id ");
    console_write_u64(apic->id);
    console_write(" version ");
    console_write_hex(apic->version);
    console_write(" LVT entries ");
    console_write_u64((uint64_t)apic->max_lvt_entry + 1U);
    console_write(" at ");
    console_write_hex(apic->base_address);
    console_putc('\n');

    console_write("OpenRFS: local APIC legacy routing ");
    console_write(apic->legacy_interrupts_routed ? "LINT0 ExtINT" : "masked");
    console_putc('\n');

    console_write("OpenRFS: local APIC EOI-broadcast suppression ");
    console_write(
        apic->eoi_broadcast_suppression_supported ? "supported" : "unsupported"
    );
    console_write(" active ");
    console_write(apic->eoi_broadcasts_suppressed ? "yes" : "no");
    console_putc('\n');
}

void report_ioapic(const struct ioapic_state *ioapic)
{
    for (size_t index = 0; index < ioapic->count; ++index) {
        const struct ioapic_unit *unit = &ioapic->units[index];

        console_write("OpenRFS: I/O APIC id ");
        console_write_u64(unit->identifier);
        console_write(" version ");
        console_write_hex(unit->version);
        console_write(" entries ");
        console_write_u64(unit->entry_count);
        console_write(" base GSI ");
        console_write_u64(unit->interrupt_base);
        console_write(" directed EOI ");
        console_write(unit->directed_eoi ? "yes" : "no");
        console_putc('\n');
    }
}
