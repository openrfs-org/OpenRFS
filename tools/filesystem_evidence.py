#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fail-closed provenance and complete-sweep receipts for filesystem CI."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


def command(*args):
    return subprocess.check_output(args, text=True).strip()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def scenarios():
    names = command('make', '--no-print-directory', '-s', 'contract-scenarios').splitlines()
    count = int(command('make', '--no-print-directory', '-s', 'contract-counts').split()[0])
    if len(names) != count or len(set(names)) != count:
        raise RuntimeError('Makefile scenario list differs from its declared contract')
    return names


def validate_sweep(names, text, log_directory):
    receipts = re.findall(r'^QEMU scenario (\S+) passed$', text, re.M)
    if sorted(receipts) != sorted(names):
        raise RuntimeError('missing, duplicate or unexpected scenario success receipts')
    if text.count('all deterministic QEMU scenarios passed\n') != 1:
        raise RuntimeError('missing complete-sweep success receipt')
    for name in names:
        path = log_directory / name / 'serial.log'
        if not path.is_file() or path.stat().st_size == 0:
            raise RuntimeError(f'missing or empty serial log: {name}')


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def source(path):
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD'], check=True, stdout=subprocess.DEVNULL)
    event_path = os.environ.get('GITHUB_EVENT_PATH')
    event = json.loads(Path(event_path).read_text()) if event_path else {}
    pr = event.get('pull_request', {})
    head = command('git', 'rev-parse', 'HEAD')
    tree = command('git', 'rev-parse', 'HEAD^{tree}')
    entries = command('git', 'ls-tree', '-r', 'HEAD') + '\n'
    write_json(path, {'checkout_commit': head, 'checkout_tree': tree,
        'pull_request_head': pr.get('head', {}).get('sha'),
        'pull_request_base': pr.get('base', {}).get('sha'),
        'github_sha': os.environ.get('GITHUB_SHA'),
        'run_id': os.environ.get('GITHUB_RUN_ID'),
        'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT'),
        'git_tree_manifest_sha256': hashlib.sha256(entries.encode()).hexdigest(),
        'scenarios': scenarios(), 'sweeps_required': 10})
    path.with_suffix('.tree.txt').write_text(entries)


def main():
    action = sys.argv[1]
    if action == 'source':
        source(Path(sys.argv[2]))
    elif action == 'prepare-sweep':
        for name in scenarios():
            (Path('build/tests') / name / 'serial.log').unlink(missing_ok=True)
    elif action == 'sweep':
        names = scenarios()
        transcript, output = map(Path, sys.argv[2:4])
        validate_sweep(names, transcript.read_text(), Path('build/tests'))
        output.mkdir(parents=True, exist_ok=False)
        for name in names:
            shutil.copyfile(Path('build/tests') / name / 'serial.log', output / (name + '.log'))
        write_json(output / 'receipt.json', {'scenario_count': len(names),
            'transcript_sha256': digest(transcript),
            'logs': {name: digest(output / (name + '.log')) for name in names}})
    elif action == 'filesystem-manifest':
        output = Path(sys.argv[2])
        write_json(output, {str(path): {'bytes': path.stat().st_size, 'sha256': digest(path)}
            for path in sorted(Path('evidence/filesystem').rglob('*'))
            if path != output and path.is_file()})
    elif action == 'manifest':
        output = Path(sys.argv[2])
        paths = set(Path('evidence/ext4').rglob('*'))
        for directory in ['ext4-recovery', 'ext4-powercuts', 'ext4-space',
                          'ext4-inodes', 'ext4-unlink-powercuts']:
            paths.update((Path('build/tests') / directory).rglob('*'))
        paths.update(Path('build').glob('*.e2fsck.txt'))
        paths.update(Path('build').glob('*.sha256.txt'))
        paths.add(Path('build/tests/fat32-data/serial.log'))
        write_json(output, {str(path): {'bytes': path.stat().st_size, 'sha256': digest(path)}
            for path in sorted(paths) if path != output and path.is_file()})
    else:
        raise SystemExit('unknown evidence operation')


if __name__ == '__main__':
    main()
