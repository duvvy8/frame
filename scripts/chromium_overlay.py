"""Preserve and replay Frame's native overlay without editing Git's index.

Export after deliberate Chromium edits. Apply checks every file before writing;
unexpected local changes are reported instead of being overwritten.
"""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'chromium' / 'src'
OVERLAY = ROOT / 'native' / 'overlay'


def git(*args):
    return subprocess.check_output(['git', '-C', str(SRC), *args])


def export():
    revision = git('rev-parse', 'HEAD').decode().strip()
    if revision != (ROOT / 'CHROMIUM_REVISION').read_text().strip():
        raise RuntimeError('Cannot export a different Chromium revision')
    names = set(git('diff', '--name-only', '-z').decode().split('\0'))
    names.update(git('ls-files', '--others', '--exclude-standard', '-z').decode().split('\0'))
    records = []
    for name in sorted(names - {''}):
        path = SRC / name
        if not path.is_file():
            raise RuntimeError(f'Deletions require explicit review: {name}')
        baseline = subprocess.run(['git', '-C', str(SRC), 'show', f'HEAD:{name}'],
                                  capture_output=True)
        before = baseline.stdout if baseline.returncode == 0 else None
        after = path.read_bytes()
        import hashlib
        digest = lambda data: hashlib.sha256(data).hexdigest()
        target = OVERLAY / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(after)
        records.append({'path': name, 'before': digest(before) if before is not None else None,
                        'after': digest(after)})
    (OVERLAY.parent / 'manifest.json').write_text(
        json.dumps({'revision': revision, 'files': records}, indent=2) + '\n')
    print(f'Exported {len(records)} native overlay files')


def apply(previous_manifest=None):
    import hashlib
    manifest = json.loads((OVERLAY.parent / 'manifest.json').read_text())
    if git('rev-parse', 'HEAD').decode().strip() != manifest['revision']:
        raise RuntimeError('Chromium revision mismatch')
    if manifest['revision'] != (ROOT / 'CHROMIUM_REVISION').read_text().strip():
        raise RuntimeError('Manifest revision mismatch')
    previous = {}
    if previous_manifest is not None:
        if previous_manifest['revision'] != manifest['revision']:
            raise RuntimeError('Previous overlay revision mismatch')
        previous = {record['path']: record for record in previous_manifest['files']}
        if set(previous) - {record['path'] for record in manifest['files']}:
            raise RuntimeError('Overlay removals require explicit reconciliation')
    writes = []
    for record in manifest['files']:
        target = SRC / record['path']
        data = (OVERLAY / record['path']).read_bytes()
        digest = lambda value: hashlib.sha256(value).hexdigest()
        if digest(data) != record['after']:
            raise RuntimeError(f'Overlay integrity mismatch: {record["path"]}')
        current = digest(target.read_bytes()) if target.exists() else None
        if current == record['after']:
            continue
        prior = previous.get(record['path'])
        if prior and prior['before'] != record['before']:
            raise RuntimeError(f'Baseline changed: {record["path"]}')
        if current != record['before'] and not (prior and current == prior['after']):
            raise RuntimeError(f'Preserved local changes; export or reconcile: {record["path"]}')
        writes.append((target, data))
    for target, data in writes:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    print(f'Applied {len(writes)} files; all other overlay files already current')


def upgrade(from_ref):
    """Replace only a verified earlier overlay, preserving unknown local work."""
    # Resolve to a commit first, then use the hash as the show argument. A ref
    # is never interpolated into shell text or treated as a Git option.
    commit = subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', '--verify', '--end-of-options',
         f'{from_ref}^{{commit}}']).decode().strip()
    previous = json.loads(subprocess.check_output(
        ['git', '-C', str(ROOT), 'show', f'{commit}:native/manifest.json']))
    apply(previous)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['export', 'apply', 'upgrade'])
    parser.add_argument('--from-ref', help='Previous Frame overlay commit (upgrade only)')
    args = parser.parse_args()
    if args.mode == 'upgrade':
        if not args.from_ref:
            parser.error('upgrade requires --from-ref')
        upgrade(args.from_ref)
    else:
        if args.from_ref:
            parser.error('--from-ref is only valid with upgrade')
        globals()[args.mode]()
