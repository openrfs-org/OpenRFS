/* SPDX-License-Identifier: GPL-3.0-only */
#include <rsd/config.h>
#include <rsd/term.h>

/*
 * Every option here corresponds to something the system either has or
 * has a document about. There is no option for a feature that does not
 * exist: a config screen offering IPv6 on a kernel with no IPv6 in it
 * would be a menu that lies.
 */
static const char *const GROUPS[RSD_CONF_GROUPS] = {
    "kernel", "drivers", "filesystems", "network", "userland", "ports"
};

#define K 0U
#define D 1U
#define F 2U
#define N 3U
#define U 4U
#define P 5U

static struct rsd_opt defaults[RSD_OPT_MAX] = {
    { "WXORX", "No page both writable and executable", K,
      "Verified by make verify. Not a switch.", true, true },
    { "NOFPU", "No floating point or SIMD in the kernel", K,
      "Verified by make verify. Not a switch.", true, true },
    { "LEDGER", "Record and check the boot stages", K,
      "Fourteen declared stages, checked in dependency order.", true,
      false },
    { "SMP", "Symmetric multiprocessing", K,
      "Not implemented. The scheduler runs one core.", false, false },
    { "KDEBUG", "Keep kernel debug symbols", K,
      "Bigger image, readable backtraces.", false, false },

    { "NVME", "NVMe storage", D,
      "Root is on nvme0. Removing this removes the root disk.", true,
      true },
    { "VIRTIO_NET", "virtio-net", D,
      "The only network card with a driver.", true, false },
    { "HDA", "High Definition Audio", D,
      "Output only. No capture path yet.", true, false },
    { "USB", "USB host and HID", D,
      "Keyboard and mass storage.", true, false },
    { "NVIDIA", "NVIDIA register probes", D,
      "Reads registers and reports. It does not drive anything.",
      false, false },
    { "IOMMU", "IOMMU", D,
      "Absent on this machine. Bus mastering stays off without it.",
      false, false },

    { "FAT32", "FAT32, read and write", F,
      "Root is FAT32. Removing this removes the root filesystem.",
      true, true },
    { "EXT4", "ext4 and JBD2, boundary only", F,
      "Reads the structures and stops at the journal.", true, false },

    { "INET", "IPv4", N, "The only address family implemented.", true,
      false },
    { "INET6", "IPv6", N, "Not implemented.", false, false },
    { "TCP", "TCP", N, "Needed by DNS and TLS.", true, false },
    { "DNS", "DNS resolver", N, "Encodes and parses queries itself.",
      true, false },
    { "TLS", "TLS 1.2 through BearSSL", N,
      "Two cipher suites, both ECDHE. No TLS 1.3.", true, false },

    { "NATIVE_ABI", "Native ABI v1", U,
      "4 processes, 8 threads, 128 handles, 256 MiB.", true, true },
    { "LINUX_ABI", "Linux syscall profiles", U,
      "A subset, for ports that expect Linux.", false, false },
    { "PKG", "Signed package manager", U,
      "Ed25519 against the trust roots the system holds.", true,
      false },
    { "DESKTOP", "RSD desktop", U,
      "Started by starty after login, not at boot.", true, false },

    { "LUA", "Lua 5.4.7", P, "Signed by root:9f2a.", true, false },
    { "SQLITE", "SQLite 3.46.0", P, "Signed by root:9f2a.", true,
      false },
    { "SDL2", "SDL 2.30.5", P, "Signed by root:c417.", true, false },
    { "ZLIB", "zlib 1.3.1", P, "Signed by root:9f2a.", true, false },
};

static struct rsd_opt live[RSD_OPT_MAX];
static bool loaded;

static void ensure(void)
{
    if (loaded) {
        return;
    }
    for (uint32_t i = 0U; i < RSD_OPT_MAX; ++i) {
        live[i] = defaults[i];
    }
    loaded = true;
}

const char *rsd_conf_group(uint32_t group)
{
    return group < RSD_CONF_GROUPS ? GROUPS[group] : "";
}

uint32_t rsd_conf_count(void)
{
    return RSD_OPT_MAX;
}

struct rsd_opt *rsd_conf_at(uint32_t index)
{
    ensure();
    return index < RSD_OPT_MAX ? &live[index] : (struct rsd_opt *)0;
}

struct rsd_opt *rsd_conf_in(uint32_t group, uint32_t nth)
{
    uint32_t seen = 0U;

    ensure();
    for (uint32_t i = 0U; i < RSD_OPT_MAX; ++i) {
        if (live[i].group != group) {
            continue;
        }
        if (seen == nth) {
            return &live[i];
        }
        ++seen;
    }
    return (struct rsd_opt *)0;
}

uint32_t rsd_conf_group_count(uint32_t group)
{
    uint32_t n = 0U;

    ensure();
    for (uint32_t i = 0U; i < RSD_OPT_MAX; ++i) {
        n += live[i].group == group ? 1U : 0U;
    }
    return n;
}

uint32_t rsd_conf_group_on(uint32_t group)
{
    uint32_t n = 0U;

    ensure();
    for (uint32_t i = 0U; i < RSD_OPT_MAX; ++i) {
        n += (live[i].group == group && live[i].on) ? 1U : 0U;
    }
    return n;
}

void rsd_conf_reset(void)
{
    loaded = false;
    ensure();
}

struct rsd_opt *rsd_conf_find(const char *name)
{
    ensure();
    for (uint32_t i = 0U; i < RSD_OPT_MAX; ++i) {
        if (rsd_streq(live[i].name, name)) {
            return &live[i];
        }
    }
    return (struct rsd_opt *)0;
}

bool rsd_conf_set(const char *name, bool on)
{
    struct rsd_opt *opt = rsd_conf_find(name);

    if (opt == (struct rsd_opt *)0 || opt->locked) {
        return false;
    }
    opt->on = on;
    return true;
}

bool rsd_conf_get(const char *name)
{
    struct rsd_opt *opt = rsd_conf_find(name);

    return opt != (struct rsd_opt *)0 && opt->on;
}

uint32_t rsd_conf_changed(void)
{
    uint32_t n = 0U;

    ensure();
    for (uint32_t i = 0U; i < RSD_OPT_MAX; ++i) {
        n += live[i].on != defaults[i].on ? 1U : 0U;
    }
    return n;
}

/* Saving makes the current state the one "changed" is measured from. */
void rsd_conf_commit(void)
{
    ensure();
    for (uint32_t i = 0U; i < RSD_OPT_MAX; ++i) {
        defaults[i].on = live[i].on;
    }
}
