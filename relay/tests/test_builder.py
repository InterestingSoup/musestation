import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

from relay.builder import (BuildError, Builder, MAX_FILE_BYTES, checked_tree,
                           validate_identity, validate_source)


IDENTITY = {'title_id': 'PPSA99001', 'content_id': 'IV0000-PPSA99001_00-MUSEAPP000000001',
            'name': 'Muse App 0001', 'version': '01.000.000'}


class BuilderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.builder = Builder(self.root, self.root / 'kit', 'compiler', 'packager', namespace=self.root)

    def tearDown(self):
        self.temp.cleanup()

    def test_source_canonical_utf8_digest(self):
        files = {'main.cpp': '// éclair\nint main(){}', 'include/x.hpp': '#pragma once'}
        actual, digest = validate_source({'language': 'cpp', 'files': files})
        self.assertEqual(actual, files)
        expected = hashlib.sha256(json.dumps(files, ensure_ascii=False, sort_keys=True,
                                            separators=(',', ':')).encode()).hexdigest()
        self.assertEqual(digest, expected)

    def test_source_rejects_recipe_and_path_overrides(self):
        for name in ('Makefile', 'run.sh', '../main.cpp', '/main.cpp', 'include/../x.h',
                     'include//x.h', 'include/.hidden.h', 'include/x.cpp', 'include/x.h/else.h',
                     'main.cpp\n--privileged'):
            with self.subTest(name=name), self.assertRaises(BuildError):
                validate_source({'language': 'cpp', 'files': {'main.cpp': 'x', name: 'x'}})

    def test_source_limits_and_encoding(self):
        for files in ({}, {'main.cpp': ''}, {'main.cpp': '\x00'}, {'main.cpp': '\ud800'},
                      {'main.cpp': 'x' * (MAX_FILE_BYTES + 1)},
                      {'main.cpp': 'x', **{f'include/{i}.h': 'x' for i in range(32)}},
                      {'main.cpp': 'x' * MAX_FILE_BYTES, **{f'include/{i}.h': 'x' * MAX_FILE_BYTES for i in range(4)}}):
            with self.assertRaises(BuildError):
                validate_source({'language': 'cpp', 'files': files})

    def test_identity_protects_station_and_version(self):
        valid = dict(IDENTITY)
        self.assertEqual(validate_identity(valid), valid)
        for change in ({'title_id': 'PPSA99576', 'content_id': 'IV0000-PPSA99576_00-MUSEAPP000000001'},
                       {'title_id': 'PPSA99000', 'content_id': 'IV0000-PPSA99000_00-MUSEAPP000000001'},
                       {'title_id': 'PPSA99١٢٣'}, {'version': '02.000.000'},
                       {'content_id': 'IV0000-PPSA99002_00-MUSEAPP0000000001'}, {'name': 'bad\nname'},
                       {'command': 'make'}):
            with self.assertRaises(BuildError):
                validate_identity(dict(valid, **change))

    def test_container_isolation_and_internal_deadline(self):
        command = self.builder._command('test', 'sha256:' + '1' * 64,
                    [(self.root, '/input', True), (self.root / 'out', '/output', False)])
        self.assertEqual(command[command.index('--network') + 1], 'none')
        for item in ('--read-only', '--pull=never', '--cap-drop', '--security-opt', '--pids-limit', '--memory-swap'):
            self.assertIn(item, command)
        self.assertIn('/usr/bin/timeout', command)
        self.assertNotIn('/var/run/docker.sock', ' '.join(command))
        self.assertNotIn('--privileged', command)
        self.assertIn('readonly', command[command.index('--mount') + 1])
        package = self.builder._command('test', 'image', [], package=True)
        self.assertIn('build-generated', package)
        self.assertIn('/usr/bin/timeout', package)
        with self.assertRaises(BuildError):
            self.builder._mount(Path('/bad,readonly=false'), '/input', True)

    def test_output_tree_rejects_symlink_hardlink_and_quota(self):
        output = self.root / 'output'
        output.mkdir()
        (output / 'a').write_bytes(b'abcd')
        self.assertEqual(len(checked_tree(output, 4)), 1)
        with self.assertRaises(BuildError):
            checked_tree(output, 3)
        (output / 'b').symlink_to(output / 'a')
        with self.assertRaises(BuildError):
            checked_tree(output, 100)
        (output / 'b').unlink()
        os.link(output / 'a', output / 'b')
        with self.assertRaises(BuildError):
            checked_tree(output, 100)

    def test_kit_tampering_rejected(self):
        kit = self.builder.kit_dir
        kit.mkdir()
        file = kit / 'hello'
        file.write_bytes(b'public kit')
        file.chmod(0o444)
        manifest = {'schema': 1, 'recipe': 'muse-native-cpp-v1', 'files': [
            {'path': 'hello', 'bytes': 10, 'sha256': hashlib.sha256(b'public kit').hexdigest()}]}
        metadata = kit / 'kit.json'
        metadata.write_text(json.dumps(manifest))
        metadata.chmod(0o444)
        self.builder._kit()
        file.chmod(0o644)
        with self.assertRaises(BuildError):
            self.builder._kit()
        file.write_bytes(b'other data')
        file.chmod(0o444)
        with self.assertRaises(BuildError):
            self.builder._kit()

    def test_existing_build_attempt_not_reused(self):
        valid = dict(IDENTITY)
        job = self.root / 'job'
        (job / 'native-build').mkdir(parents=True)
        with self.assertRaisesRegex(BuildError, 'already attempted'):
            self.builder.build(job, {'language': 'cpp', 'files': {'main.cpp': 'x'}}, valid, job / 'out')

    def test_supervisor_timeout_terminates_child(self):
        # A harmless host test process stands in for Docker, never submitted source.
        self.builder.docker = '/bin/true'
        with self.assertRaisesRegex(BuildError, 'wall-clock'):
            self.builder._run(['/bin/sleep', '10'], 'fake', self.root / 'timeout.log', self.root, .05)

    def test_diagnostic_tail_is_bounded(self):
        self.builder.docker = '/bin/true'
        with self.assertRaises(BuildError) as caught:
            self.builder._run(['/bin/sh', '-c', 'printf "compiler diagnostic\\n"; exit 7'],
                              'fake', self.root / 'failure.log', self.root, 5)
        self.assertIn('compiler diagnostic', str(caught.exception))
        self.assertLess(len(str(caught.exception)), 1000)
        self.assertEqual(caught.exception.log_paths, [str(self.root / 'failure.log')])

    def test_recovery_stops_only_owned_reserved_container(self):
        record = [{'Id': 'a' * 64, 'Config': {'Labels': {
            'org.musestation.relay.builder': '1',
            'org.musestation.relay.namespace': self.builder.namespace_hash}}}]
        with patch('relay.builder.subprocess.run', side_effect=[
            SimpleNamespace(returncode=0, stdout=json.dumps(record).encode(), stderr=b''),
            SimpleNamespace(returncode=0)]) as run:
            self.assertTrue(self.builder.recover_containers())
            self.assertEqual(run.call_args_list[0].args[0][-1], self.builder.container_name)
            self.assertEqual(run.call_args_list[1].args[0], ['docker','rm','-f','a' * 64])
        record[0]['Config']['Labels'] = {'another': 'service'}
        with patch('relay.builder.subprocess.run', return_value=SimpleNamespace(
                returncode=0, stdout=json.dumps(record).encode(), stderr=b'')) as run:
            with self.assertRaisesRegex(BuildError, 'another service'):
                self.builder.recover_containers()
            self.assertEqual(run.call_count, 1)

    def test_two_private_stores_have_disjoint_container_ownership(self):
        fixture = self.root / 'fixture'
        fixture.mkdir()
        other = Builder(self.root, self.root / 'kit', 'compiler', 'packager', namespace=fixture)
        self.assertNotEqual(self.builder.container_name, other.container_name)
        self.assertNotEqual(self.builder.namespace_hash, other.namespace_hash)
        # Even if a container is renamed into the expected slot, ownership differs.
        foreign = [{'Id': 'b' * 64, 'Config': {'Labels': {
            'org.musestation.relay.builder': '1',
            'org.musestation.relay.namespace': other.namespace_hash}}}]
        with patch('relay.builder.subprocess.run', return_value=SimpleNamespace(
                returncode=0, stdout=json.dumps(foreign).encode(), stderr=b'')) as run:
            with self.assertRaisesRegex(BuildError, 'another service'):
                self.builder.recover_containers()
            self.assertEqual(run.call_count, 1)
        command = other._command(other.container_name, 'image', [])
        self.assertIn('org.musestation.relay.namespace=' + other.namespace_hash, command)
        self.assertEqual(command[command.index('--name') + 1], other.container_name)
        same = Builder(self.root, self.root / 'kit', 'compiler', 'packager', namespace=fixture / '..' / 'fixture')
        self.assertEqual(same.container_name, other.container_name)

    def test_namespace_must_be_explicit_existing_absolute_directory(self):
        for namespace in ('relative', self.root / 'missing'):
            with self.assertRaises(BuildError):
                Builder(self.root, self.root / 'kit', 'compiler', 'packager', namespace=namespace)


if __name__ == '__main__':
    unittest.main()
