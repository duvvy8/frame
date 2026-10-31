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

    def test_internal_links_survive_delta_restore(self):
        import shutil
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            old = stage.STATE, stage.CHECKPOINTS
            try:
                stage.STATE, stage.CHECKPOINTS = root/'state', root/'checkpoints'
                stage.STATE.mkdir()
                (stage.STATE/'package').mkdir()
                (stage.STATE/'package/tool.js').write_text('tool')
                try:
                    (stage.STATE/'tool').symlink_to('package/tool.js')
                    (stage.STATE/'directory').symlink_to('package', target_is_directory=True)
                except OSError as error:
                    self.skipTest('Symlink creation unavailable: ' + str(error))
                first = stage.inventory(stage.STATE)
                stage.checkpoint(1, {})
                (stage.STATE/'tool').unlink()
                (stage.STATE/'tool').write_text('regular replacement')
                (stage.STATE/'directory').unlink()
                (stage.STATE/'new-link').symlink_to('package/tool.js')
                expected = stage.inventory(stage.STATE)
                stage.checkpoint(2, first)
                shutil.rmtree(stage.STATE)
                self.assertEqual(stage.restore(), 2)
                self.assertEqual(stage.inventory(stage.STATE), expected)
                self.assertFalse((stage.STATE/'tool').is_symlink())
                self.assertTrue((stage.STATE/'new-link').is_symlink())
                self.assertEqual((stage.STATE/'new-link').read_text(), 'tool')
            finally:
                stage.STATE, stage.CHECKPOINTS = old

    def test_external_link_target_is_refused(self):
        root = Path(tempfile.gettempdir())/'cloud-checkpoint-root'
        with self.assertRaises(ValueError):
            stage.safe_link(root, root/'link', '../outside')

    def test_link_metadata_is_archived_without_dereferencing(self):
        import json
        import zipfile
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            state, checkpoints = root/'state', root/'checkpoints'
            state.mkdir()
            info = {'symlink': 'missing-target', 'directory': False}
            with patch.object(stage, 'STATE', state), patch.object(stage, 'CHECKPOINTS', checkpoints):
                with patch.object(stage, 'inventory', return_value={'link': info}):
                    stage.checkpoint(1, {})
                with zipfile.ZipFile(checkpoints/'stage-01.zip') as archive:
                    self.assertEqual(archive.namelist(), ['_checkpoint.json'])
                    self.assertEqual(json.loads(archive.read('_checkpoint.json'))['changed']['link'], info)
                with patch.object(Path, 'symlink_to', autospec=True) as create_link:
                    self.assertEqual(stage.restore(), 1)
                    create_link.assert_called_once_with(state/'link', 'missing-target', target_is_directory=False)


if __name__=='__main__':
    unittest.main(verbosity=2)
