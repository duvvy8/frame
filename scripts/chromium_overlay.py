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


def apply():
    import hashlib
    manifest = json.loads((OVERLAY.parent / 'manifest.json').read_text())
    if git('rev-parse', 'HEAD').decode().strip() != manifest['revision']:
        raise RuntimeError('Chromium revision mismatch')
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
        if current != record['before']:
            raise RuntimeError(f'Preserved local changes; export or reconcile: {record["path"]}')
        writes.append((target, data))
    for target, data in writes:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    print(f'Applied {len(writes)} files; all other overlay files already current')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('mode', choices=['export', 'apply'])
    args = parser.parse_args()
    globals()[args.mode]()
