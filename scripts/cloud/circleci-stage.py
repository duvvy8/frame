"""Bounded, resumable Windows cloud build; never invokes tools off CircleCI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import zipfile

STATE = Path('C:/frame-ci')
CHECKPOINTS = Path('C:/frame-checkpoints')
DIAGNOSTICS = Path('C:/frame-diagnostics')
REPO = Path(__file__).resolve().parents[2]


def safe_member(root, name):
    path = root / name
    if not path.resolve().is_relative_to(root.resolve()) or ':' in name or '\\' in name:
        raise ValueError('Unsafe checkpoint member: ' + name)
    return path


def restore():
    STATE.mkdir(exist_ok=True)
    previous = 0
    for archive in sorted(CHECKPOINTS.glob('stage-*.zip')):
        number = int(archive.stem.split('-')[1])
        if number != previous + 1:
            raise RuntimeError('Missing checkpoint; refusing an incomplete build state')
        with zipfile.ZipFile(archive) as z:
            metadata = json.loads(z.read('_checkpoint.json'))
            for name in metadata['deleted']:
                path = safe_member(STATE, name)
                if path.is_file():
                    path.unlink()
            for item in z.infolist():
                if item.filename == '_checkpoint.json':
                    continue
                target = safe_member(STATE, item.filename)
                target.parent.mkdir(parents=True, exist_ok=True)
                with z.open(item) as src, target.open('wb') as dst:
                    shutil.copyfileobj(src, dst, 4 * 1024 * 1024)
                stamp = metadata['changed'][item.filename][1]
                os.utime(target, ns=(stamp, stamp))
        previous = number
        # Archives are disposable copies downloaded into this cloud job.
        archive.unlink()
    return previous


def inventory(root):
    result = {}
    for folder, dirs, names in os.walk(root, followlinks=False):
        dirs[:] = [d for d in dirs if not (Path(folder) / d).is_symlink()]
        for name in names:
            path = Path(folder) / name
            if path.is_symlink():
                raise RuntimeError('Checkpoint cannot preserve a symlink safely: ' + str(path))
            stat = path.stat()
            result[path.relative_to(root).as_posix()] = [stat.st_size, stat.st_mtime_ns]
    return result


def checkpoint(number, before):
    current = inventory(STATE)
    changed = {name: info for name, info in current.items() if before.get(name) != info}
    CHECKPOINTS.mkdir(exist_ok=True)
    archive = CHECKPOINTS / f'stage-{number:02d}.zip'
    with zipfile.ZipFile(archive, 'x', zipfile.ZIP_DEFLATED, compresslevel=1, allowZip64=True) as z:
        for name in sorted(changed):
            # Git packs, archives and images are already compressed.
            method = zipfile.ZIP_STORED if Path(name).suffix in ('.pack', '.zip', '.png', '.jpg', '.woff2') else zipfile.ZIP_DEFLATED
            z.write(STATE / name, name, compress_type=method, compresslevel=1)
        z.writestr('_checkpoint.json', json.dumps({'changed': changed,
                       'deleted': sorted(set(before) - set(current))}))
    return {'changedFiles': len(changed), 'checkpointBytes': archive.stat().st_size}


def command(args, cwd, deadline, env):
    remaining = deadline - time.monotonic()
    if remaining < 15:
        return None
    print('Cloud step: ' + Path(str(args[0])).name, flush=True)
    with (DIAGNOSTICS / 'commands.log').open('ab') as log:
        p = subprocess.Popen(args, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                             creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            return p.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            # Only this stage's owned cloud command tree is stopped.
            subprocess.run(['taskkill.exe', '/PID', str(p.pid), '/T', '/F'],
                           capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
            p.wait()
            return None


def run(number):
    if os.environ.get('CIRCLECI') != 'true' or os.name != 'nt':
        raise RuntimeError('Cloud-only entry point; local compilation is forbidden')
    if os.environ.get('FRAME_FREE_PLAN_CONFIRMED') != '1':
        raise RuntimeError('Free-plan verification is required; no paid fallback')
    DIAGNOSTICS.mkdir(exist_ok=True)
    previous = restore()
    if previous != number - 1:
        raise RuntimeError('Stage order does not match the restored checkpoint')
    before = inventory(STATE)
    # Interrupted Git commands may leave locks in a checkpoint. This is a new
    # cloud VM, before any Git/build command starts; only these owned stale
    # lock files are removed. Their deletion is recorded in the next delta.
    for folder, _, names in os.walk(STATE):
        if '.git' not in Path(folder).parts:
            continue
        for name in names:
            if name in ('index.lock', 'shallow.lock', 'packed-refs.lock', 'config.lock'):
                (Path(folder) / name).unlink()
    marker = STATE / 'progress.json'
    progress = json.loads(marker.read_text()) if marker.exists() else {}
    env = os.environ.copy()
    env.update({'DEPOT_TOOLS_WIN_TOOLCHAIN': '0', 'DEPOT_TOOLS_UPDATE': '0',
                'GIT_TERMINAL_PROMPT': '0', 'FRAME_CLOUD_BUILD': '1',
                'VPYTHON_VIRTUALENV_ROOT': str(STATE / 'vpython'),
                'GCLIENT_PY3': '1'})
    # Twenty minutes of work leaves room for restore, checkpoint and upload
    # within CircleCI's one-hour Free-plan job limit. Runtime remains measured.
    deadline = time.monotonic() + 20 * 60
    depot = STATE / 'depot_tools'
    source = STATE / 'chromium/src'
    revision = json.loads((REPO / 'native/manifest.json').read_text())['revision']
    outcome = None
    try:
        if not depot.exists():
            outcome = command(['git', 'clone', '--depth=1', 'https://chromium.googlesource.com/chromium/tools/depot_tools.git', str(depot)], STATE, deadline, env)
            if outcome is None:
                return
            if outcome:
                raise RuntimeError('Cloud depot_tools download failed')
        env['PATH'] = str(depot) + os.pathsep + env['PATH']
        chromium = source.parent
        chromium.mkdir(exist_ok=True)
        config = chromium / '.gclient'
        if not config.exists():
            config.write_text("solutions = [{'name':'src','url':'https://chromium.googlesource.com/chromium/src.git','managed':False,'custom_deps':{},'custom_vars':{'checkout_android':False,'checkout_ios':False}}]\ntarget_os = ['win']\n")
        if not (source / '.git').exists():
            source.mkdir(exist_ok=True)
            for args in (['git','init',str(source)], ['git','-C',str(source),'config','core.longpaths','true'],
                         ['git','-C',str(source),'config','core.autocrlf','false'],
                         ['git','-C',str(source),'remote','add','origin','https://chromium.googlesource.com/chromium/src.git']):
                if command(args, STATE, deadline, env) != 0:
                    raise RuntimeError('Cloud source initialization failed')
        if not progress.get('checkout'):
            for args in (['git','-C',str(source),'fetch','--depth=1','origin',revision],
                         ['git','-C',str(source),'checkout','--detach',revision]):
                outcome = command(args, STATE, deadline, env)
                if outcome is None:
                    return
                if outcome:
                    raise RuntimeError('Pinned Chromium checkout failed')
            progress['checkout'] = True
        if not progress.get('sync'):
            outcome = command(['cmd.exe','/d','/c',str(depot/'gclient.bat'),'sync','--no-history','--nohooks','--jobs=4'], chromium, deadline, env)
            if outcome is None:
                return
            if outcome:
                raise RuntimeError('Cloud dependency sync failed; see command log')
            progress['sync'] = True
        if not progress.get('hooks'):
            outcome = command(['cmd.exe','/d','/c',str(depot/'gclient.bat'),'runhooks'], chromium, deadline, env)
            if outcome is None:
                return
            if outcome:
                raise RuntimeError('Toolchain/hooks failed; no compiler fallback')
            progress['hooks'] = True
        if not progress.get('complete'):
            outcome = command(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',
                               str(REPO/'scripts/cloud/build-frame.ps1'),'-ChromiumSource',str(source),
                               '-DepotTools',str(depot),'-Jobs','3'], REPO, deadline, env)
            if outcome == 0:
                progress['complete'] = True
            elif outcome is not None:
                raise RuntimeError('Frame cloud build failed; see command log')
        if progress.get('complete') and number == 12:
            result = DIAGNOSTICS / 'Frame-Widevine-cloud.zip'
            shutil.make_archive(str(result.with_suffix('')), 'zip', source/'FrameCloudPackage')
        if number == 12 and not progress.get('complete'):
            raise RuntimeError('Free-plan stage limit reached before completion; progress is checkpointed, no paid retry')
    finally:
        progress['lastStage'] = number
        marker.write_text(json.dumps(progress, indent=2) + '\n')
        details = checkpoint(number, before)
        (DIAGNOSTICS / 'progress.json').write_text(json.dumps({**progress, **details}, indent=2) + '\n')
        print(json.dumps({**progress, **details}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=int, choices=range(1, 13), required=True)
    run(parser.parse_args().stage)
