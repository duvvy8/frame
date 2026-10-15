"""Validate the committed overlay without needing a Chromium checkout."""
import hashlib
import json
from pathlib import Path, PurePosixPath
import re

ROOT = Path(__file__).resolve().parents[1]


def check(root=ROOT):
    manifest = json.loads((root / 'native/manifest.json').read_bytes())
    revision = (root / 'CHROMIUM_REVISION').read_text().strip()
    if not re.fullmatch(r'[0-9a-f]{40}', revision) or manifest['revision'] != revision:
        raise ValueError('Manifest and CHROMIUM_REVISION disagree')
    overlay = root / 'native/overlay'
    seen = set()
    for record in manifest['files']:
        name = record['path']
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or '\\' in name or ':' in name or
                any(part in ('', '.', '..') for part in name.split('/'))):
            raise ValueError(f'Unsafe overlay path: {name}')
        # Windows targets must not alias through case-insensitive paths.
        if name.casefold() in seen:
            raise ValueError(f'Duplicate overlay path: {name}')
        seen.add(name.casefold())
        for key in ('before', 'after'):
            if key == 'before' and record[key] is None:
                continue
            if not re.fullmatch(r'[0-9a-f]{64}', record[key]):
                raise ValueError(f'Invalid {key} hash: {name}')
        target = overlay / path
        if (not target.is_file() or target.is_symlink() or
                not target.resolve().is_relative_to(overlay.resolve())):
            raise ValueError(f'Missing or escaped overlay file: {name}')
        if hashlib.sha256(target.read_bytes()).hexdigest() != record['after']:
            raise ValueError(f'Postimage hash mismatch: {name}')
    actual = {p.relative_to(overlay).as_posix().casefold()
              for p in overlay.rglob('*') if p.is_file()}
    if actual != seen:
        raise ValueError(f'Unlisted overlay files: {sorted(actual - seen)}')
    return len(seen)


if __name__ == '__main__':
    print(f'PASS: {check()} overlay files; revision, safe unique paths and SHA-256 hashes')
