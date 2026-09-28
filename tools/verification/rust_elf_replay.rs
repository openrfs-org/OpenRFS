// SPDX-License-Identifier: GPL-3.0-only
//! Standalone one-input replay of the production ELF admission parser.

#[path = "../../src/rust/elf64.rs"]
#[allow(dead_code)]
mod elf64;
#[path = "../../src/rust/fuzz/oracle.rs"]
mod oracle;

use std::path::Path;

fn main() {
    let input = std::env::args().nth(1).expect("one input path required");
    let data = std::fs::read(Path::new(&input)).expect("input must be readable");
    let (accepted, statuses) = oracle::check(&data);
    println!("status=0 accepted={} proof:{} multiprocess:{}",
             accepted, statuses[0], statuses[1]);
}
