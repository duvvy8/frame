import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('overlay', Path(__file__).with_name('chromium_overlay.py'))
overlay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(overlay)


class OverlayTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.src = self.root / 'chromium/src'
        self.src.mkdir(parents=True)
        overlay.ROOT, overlay.SRC = self.root, self.src
        overlay.OVERLAY = self.root / 'native/overlay'
        self.git('init', '-q')
        self.git('config', 'user.email', 'test@example.invalid')
        self.git('config', 'user.name', 'Overlay tests')
        self.git('config', 'core.autocrlf', 'false')
        (self.src / 'source.cc').write_bytes(b'original\r\n')
        (self.src / 'other.cc').write_bytes(b'original other\n')
        self.git('add', '.')
        self.git('commit', '-qm', 'baseline')
        (self.root / 'CHROMIUM_REVISION').write_bytes(self.git('rev-parse', 'HEAD'))

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.src), *args])

    def test_roundtrip_binary_and_newlines_and_idempotence(self):
        (self.src / 'source.cc').write_bytes(b'changed\r\n')
        (self.src / 'new-font.ttf').write_bytes(bytes(range(256)))
        overlay.export()
        self.git('restore', 'source.cc')
        (self.src / 'new-font.ttf').unlink()
        overlay.apply()
        overlay.apply()
        self.assertEqual((self.src / 'source.cc').read_bytes(), b'changed\r\n')
        self.assertEqual((self.src / 'new-font.ttf').read_bytes(), bytes(range(256)))
        self.assertEqual(self.git('diff', '--cached'), b'')

    def test_conflict_refuses_all_writes(self):
        (self.src / 'source.cc').write_bytes(b'changed')
        (self.src / 'other.cc').write_bytes(b'changed other')
        overlay.export()
        self.git('restore', '.')
        (self.src / 'source.cc').write_bytes(b'user work')
        with self.assertRaisesRegex(RuntimeError, 'Preserved local changes'):
            overlay.apply()
        self.assertEqual((self.src / 'source.cc').read_bytes(), b'user work')
        self.assertEqual((self.src / 'other.cc').read_bytes(), b'original other\n')

    def test_tampered_asset_and_revision_refuse(self):
        (self.src / 'source.cc').write_bytes(b'changed')
        overlay.export()
        (overlay.OVERLAY / 'source.cc').write_bytes(b'corrupted')
        with self.assertRaisesRegex(RuntimeError, 'integrity mismatch'):
            overlay.apply()
        self.git('commit', '--allow-empty', '-qm', 'different revision')
        with self.assertRaisesRegex(RuntimeError, 'revision mismatch'):
            overlay.apply()


if __name__ == '__main__':
    unittest.main()
