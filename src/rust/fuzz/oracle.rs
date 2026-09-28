// SPDX-License-Identifier: GPL-3.0-only
//! Invariants shared by libFuzzer and standalone one-input replay.

use crate::elf64;

pub fn check(data: &[u8]) -> (usize, [i32; 2]) {
    let mut accepted = 0;
    let mut statuses = [-1; 2];
    for (index, profile) in [&elf64::PROOF, &elf64::MULTIPROCESS]
        .into_iter().enumerate()
    {
        match elf64::parse_with(data, profile) {
            Ok(image) => {
                assert_eq!(image.valid, 1);
                assert_eq!(image.segment_count, 1);
                assert_eq!(image.elf_type, 2);
                assert_eq!(image.machine, 62);
                assert_eq!(image.program_flags, 5); // RX, never WX
                assert_eq!(image.file_size, data.len() as u64);
                assert_eq!(image.memory_size, image.file_size);
                assert_eq!(image.alignment, elf64::PAGE_BYTES);
                assert_eq!(image.mapping_end - image.mapping_start, elf64::PAGE_BYTES);
                assert_eq!(image.entry, image.virtual_address + profile.code_offset);
                assert!(image.entry < image.virtual_address + image.file_size);
                assert_eq!(image.code, profile.code[..8]);
                statuses[index] = 0;
                accepted += 1;
            }
            Err(status) => statuses[index] = status as i32,
        }
    }
    assert!(accepted <= 1, "one input cannot match both exact profiles");
    (accepted, statuses)
}
