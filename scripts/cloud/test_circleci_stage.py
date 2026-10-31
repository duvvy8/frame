"""Exercise checkpoint correctness and path boundaries; never run a build."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('stage', Path(__file__).with_name('circleci-stage.py'))
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


class CheckpointTests(unittest.TestCase):
    def test_delta_chain_preserves_edits_deletions_and_timestamps(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            old = stage.STATE, stage.CHECKPOINTS
            try:
                stage.STATE, stage.CHECKPOINTS = root/'state', root/'checkpoints'
                stage.STATE.mkdir()
                (stage.STATE/'remove.txt').write_text('remove')
                (stage.STATE/'keep.txt').write_text('old')
                first = stage.inventory(stage.STATE)
                stage.checkpoint(1,{})
                (stage.STATE/'remove.txt').unlink()
                (stage.STATE/'keep.txt').write_text('changed bytes')
                (stage.STATE/'new.txt').write_text('new')
                expected = stage.inventory(stage.STATE)
                stage.checkpoint(2,first)
                import shutil
                shutil.rmtree(stage.STATE)
                self.assertEqual(stage.restore(),2)
                self.assertEqual(stage.inventory(stage.STATE),expected)
                self.assertEqual((stage.STATE/'keep.txt').read_text(),'changed bytes')
                self.assertFalse((stage.STATE/'remove.txt').exists())
            finally:
                stage.STATE, stage.CHECKPOINTS = old

    def test_checkpoint_cannot_escape_its_root(self):
        root = Path(tempfile.gettempdir())/'cloud-checkpoint-root'
        for name in ('../outside', 'C:/outside', '/outside', 'x\\..\\outside'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                stage.safe_member(root,name)

    def test_local_build_execution_is_refused(self):
        from unittest.mock import patch
        with patch.dict(stage.os.environ, {'CIRCLECI':'false'}):
            with self.assertRaisesRegex(RuntimeError,'local compilation is forbidden'):
                stage.run(1)


if __name__=='__main__':
    unittest.main(verbosity=2)
