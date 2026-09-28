// SPDX-License-Identifier: GPL-3.0-only
//! Fuzz the same allocation-free ELF admission source compiled into the kernel.

#![no_main]

#[path = "../../elf64.rs"]
#[allow(dead_code)]
mod elf64;
#[path = "../oracle.rs"]
mod oracle;

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    // The parser's admitted profiles are exactly 128 and 256 bytes. Longer
    // inputs still execute its precise length rejection, with no allocation.
    let _ = oracle::check(data);
});
