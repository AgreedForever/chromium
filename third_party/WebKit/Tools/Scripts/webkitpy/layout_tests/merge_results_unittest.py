# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# pylint: disable=invalid-name
# pylint complains about the assertXXX methods and the usage of short variables
# m/a/b/d in the tests.

import types
import unittest

import cStringIO as StringIO

from collections import OrderedDict

from webkitpy.common.system.filesystem_mock import FileSystemTestCase, MockFileSystem
from webkitpy.layout_tests import merge_results


class JSONMergerTests(unittest.TestCase):

    def test_type_match(self):
        self.assertTrue(
            merge_results.TypeMatch(types.DictType)(dict()))
        self.assertFalse(
            merge_results.TypeMatch(types.ListType, types.TupleType)(dict()))
        self.assertTrue(
            merge_results.TypeMatch(types.ListType, types.TupleType)(list()))
        self.assertTrue(
            merge_results.TypeMatch(types.ListType, types.TupleType)(tuple()))

    def test_merge_listlike(self):
        m = merge_results.JSONMerger()

        tests = [
            # expected, (inputa, inputb)
            ([1, 2], ([1], [2])),
            ([2, 1], ([2], [1])),
            ([1, 2, 3], ([], [1, 2, 3])),
            ([1, 2, 3], ([1], [2, 3])),
            ([1, 2, 3], ([1, 2], [3])),
            ([1, 2, 3], ([1, 2, 3], [])),
        ]

        for expected, (inputa, inputb) in tests:
            self.assertListEqual(
                expected, m.merge_listlike([inputa, inputb]))
            self.assertListEqual(
                expected, m.merge([inputa, inputb]))
            self.assertSequenceEqual(
                expected,
                m.merge_listlike([tuple(inputa), tuple(inputb)]),
                types.TupleType)
            self.assertSequenceEqual(
                expected,
                m.merge([tuple(inputa), tuple(inputb)]),
                types.TupleType)

    def test_merge_simple_dict(self):
        m = merge_results.JSONMerger()
        m.fallback_matcher = m.merge_equal

        tests = [
            # expected, (inputa, inputb)
            ({'a': 1}, ({'a': 1}, {'a': 1})),

            ({'a': 1, 'b': 2}, ({'a': 1, 'b': 2}, {})),
            ({'a': 1, 'b': 2}, ({}, {'a': 1, 'b': 2})),
            ({'a': 1, 'b': 2}, ({'a': 1}, {'b': 2})),

            ({'a': 1, 'b': 2, 'c': 3}, ({'a': 1, 'b': 2, 'c': 3}, {})),
            ({'a': 1, 'b': 2, 'c': 3}, ({'a': 1, 'b': 2}, {'c': 3})),
            ({'a': 1, 'b': 2, 'c': 3}, ({'a': 1}, {'b': 2, 'c': 3})),
            ({'a': 1, 'b': 2, 'c': 3}, ({}, {'a': 1, 'b': 2, 'c': 3})),
        ]

        for expected, (inputa, inputb) in tests:
            self.assertDictEqual(expected, m.merge_dictlike([inputa, inputb]))

        with self.assertRaises(merge_results.MergeFailure):
            m.merge_dictlike([{'a': 1}, {'a': 2}])

    def test_merge_compound_dict(self):
        m = merge_results.JSONMerger()

        tests = [
            # expected, (inputa, inputb)
            ({'a': [1, 2]}, ({'a': [1]}, {'a': [2]})),
            ({'a': [1, 'c', 3]}, ({'a': [1]}, {'a': ['c', 3]})),
            ({'a': [1], 'b': [2]}, ({'a': [1]}, {'b': [2]})),
            ({'a': {'b': 1, 'c': 2}}, ({'a': {'b': 1}}, {'a': {'c': 2}})),
        ]
        for expected, (inputa, inputb) in tests:
            self.assertDictEqual(expected, m.merge_dictlike([inputa, inputb]))

    def test_merge(self):
        m = merge_results.JSONMerger()
        m.fallback_matcher = m.merge_equal

        tests = [
            # expected, (inputa, inputb)
            (None, (None, None)),
            ({'a': 1}, ({'a': 1}, None)),
            ({'b': 2}, (None, {'b': 2})),

            ({'a': 1}, ({'a': 1}, {'a': 1})),

            # "Left side" value is None
            ({'a': None, 'b': 2}, ({'a': None, 'b': 2}, {})),
            ({'a': None, 'b': 2}, ({}, {'a': None, 'b': 2})),
            ({'a': None, 'b': 2}, ({'a': None}, {'b': 2})),

            ({'a': None, 'b': 2, 'c': 3}, ({'a': None, 'b': 2, 'c': 3}, {})),
            ({'a': None, 'b': 2, 'c': 3}, ({'a': None, 'b': 2}, {'c': 3})),
            ({'a': None, 'b': 2, 'c': 3}, ({'a': None}, {'b': 2, 'c': 3})),
            ({'a': None, 'b': 2, 'c': 3}, ({}, {'a': None, 'b': 2, 'c': 3})),

            # "Right side" value is None
            ({'a': 1, 'b': None}, ({'a': 1, 'b': None}, {})),
            ({'a': 1, 'b': None}, ({}, {'a': 1, 'b': None})),
            ({'a': 1, 'b': None}, ({'a': 1}, {'b': None})),

            ({'a': 1, 'b': None, 'c': 3}, ({'a': 1, 'b': None, 'c': 3}, {})),
            ({'a': 1, 'b': None, 'c': 3}, ({'a': 1, 'b': None}, {'c': 3})),
            ({'a': 1, 'b': None, 'c': 3}, ({'a': 1}, {'b': None, 'c': 3})),
            ({'a': 1, 'b': None, 'c': 3}, ({}, {'a': 1, 'b': None, 'c': 3})),

            # Both values non-None
            ({'a': 1, 'b': 2}, ({'a': 1, 'b': 2}, {})),
            ({'a': 1, 'b': 2}, ({}, {'a': 1, 'b': 2})),
            ({'a': 1, 'b': 2}, ({'a': 1}, {'b': 2})),

            ({'a': 1, 'b': 2, 'c': 3}, ({'a': 1, 'b': 2, 'c': 3}, {})),
            ({'a': 1, 'b': 2, 'c': 3}, ({'a': 1, 'b': 2}, {'c': 3})),
            ({'a': 1, 'b': 2, 'c': 3}, ({'a': 1}, {'b': 2, 'c': 3})),
            ({'a': 1, 'b': 2, 'c': 3}, ({}, {'a': 1, 'b': 2, 'c': 3})),

            # Complex values
            ({'a': [1, 2]}, ({'a': [1]}, {'a': [2]})),
            ({'a': [1, 'c', 3]}, ({'a': [1]}, {'a': ['c', 3]})),
            ({'a': [1], 'b': [2]}, ({'a': [1]}, {'b': [2]})),
            ({'a': {'b': 1, 'c': 2}}, ({'a': {'b': 1}}, {'a': {'c': 2}})),
        ]

        for expected, (inputa, inputb) in tests:
            self.assertEqual(expected, m.merge([inputa, inputb]))

        with self.assertRaises(merge_results.MergeFailure):
            m.merge([{'a': 1}, {'a': 2}])

        # Ordered values
        a = OrderedDict({'a': 1})
        b = OrderedDict({'b': 2})

        a_before_b = OrderedDict()
        a_before_b['a'] = 1
        a_before_b['b'] = 2

        b_before_a = OrderedDict()
        b_before_a['b'] = 2
        b_before_a['a'] = 1

        r1 = m.merge([a, b])
        self.assertSequenceEqual(a_before_b.items(), r1.items())
        self.assertIsInstance(r1, OrderedDict)

        r2 = m.merge([b, a])
        self.assertSequenceEqual(b_before_a.items(), r2.items())
        self.assertIsInstance(r2, OrderedDict)

    def test_custom_match_on_name(self):
        m = merge_results.JSONMerger()
        m.add_helper(
            merge_results.NameRegexMatch('a'),
            lambda o, name=None: sum(o))

        self.assertDictEqual({'a': 3}, m.merge([{'a': 1}, {'a': 2}]))
        with self.assertRaises(merge_results.MergeFailure):
            m.merge([{'b': 1}, {'b': 2}])

        # Test that helpers that are added later have precedence.
        m.add_helper(
            merge_results.NameRegexMatch('b'),
            lambda o, name=None: sum(o))
        m.add_helper(
            merge_results.NameRegexMatch('b'),
            lambda o, name=None: o[0] - o[1])
        self.assertDictEqual({'b': -1}, m.merge([{'b': 1}, {'b': 2}]))

    def test_custom_match_on_obj_type(self):
        m = merge_results.JSONMerger()
        m.add_helper(
            merge_results.TypeMatch(int),
            lambda o, name=None: sum(o))
        self.assertDictEqual({'a': 3}, m.merge([{'a': 1}, {'a': 2}]))
        self.assertDictEqual({'b': 3}, m.merge([{'b': 1}, {'b': 2}]))

    def test_custom_match_on_obj_value(self):
        m = merge_results.JSONMerger()
        m.add_helper(
            merge_results.ValueMatch(3),
            lambda o, name=None: sum(o))
        self.assertDictEqual({'a': 6}, m.merge([{'a': 3}, {'a': 3}]))
        self.assertDictEqual({'a': 5}, m.merge([{'a': 2}, {'a': 3}]))
        self.assertDictEqual({'a': 7}, m.merge([{'a': 3}, {'a': 4}]))
        with self.assertRaises(merge_results.MergeFailure):
            m.merge([{'a': 1}, {'a': 2}])


class MergeFilesOneTests(FileSystemTestCase):
    def test(self):
        mock_filesystem = MockFileSystem({'/s/file1': '1', '/s/file2': '2'}, dirs=['/output'])

        merger = merge_results.MergeFilesOne(mock_filesystem)

        with self.assertFilesAdded(mock_filesystem, {'/output/file1': '1'}):
            merger('/output/file1', ['/s/file1'])

        with self.assertRaises(AssertionError):
            merger('/output/file1', ['/s/file1', '/s/file2'])


class MergeFilesMatchingContentsTests(FileSystemTestCase):
    def test(self):
        mock_filesystem = MockFileSystem({'/s/file1': '1', '/s/file2': '2', '/s/file3': '1'}, dirs=['/output'])

        merger = merge_results.MergeFilesMatchingContents(mock_filesystem)

        with self.assertFilesAdded(mock_filesystem, {'/output/out1': '1'}):
            merger('/output/out1', ['/s/file1'])

        with self.assertFilesAdded(mock_filesystem, {'/output/out2': '2'}):
            merger('/output/out2', ['/s/file2'])

        with self.assertRaises(merge_results.MergeFailure):
            merger('/output/out3', ['/s/file1', '/s/file2'])

        with self.assertFilesAdded(mock_filesystem, {'/output/out4': '1'}):
            merger('/output/out4', ['/s/file1', '/s/file3'])


class MergeFilesLinesSortedTests(FileSystemTestCase):
    def test(self):
        mock_filesystem = MockFileSystem({'/s/file1': 'A\nC\n', '/s/file2': 'B\n', '/s/file3': 'A\nB\n'}, dirs=['/output'])

        merger = merge_results.MergeFilesLinesSorted(mock_filesystem)

        with self.assertFilesAdded(mock_filesystem, {'/output/out1': 'A\nC\n'}):
            merger('/output/out1', ['/s/file1'])

        with self.assertFilesAdded(mock_filesystem, {'/output/out2': 'B\n'}):
            merger('/output/out2', ['/s/file2'])

        with self.assertFilesAdded(mock_filesystem, {'/output/out3': 'A\nB\nC\n'}):
            merger('/output/out3', ['/s/file1', '/s/file2'])

        with self.assertFilesAdded(mock_filesystem, {'/output/out4': 'A\nB\nB\n'}):
            merger('/output/out4', ['/s/file2', '/s/file3'])


class MergeFilesKeepFilesTests(FileSystemTestCase):

    def test(self):
        mock_filesystem = MockFileSystem({'/s1/file1': 'a', '/s2/file1': 'b', '/s3/file1': 'c'}, dirs=['/output'])

        merger = merge_results.MergeFilesKeepFiles(mock_filesystem)

        with self.assertFilesAdded(mock_filesystem, {'/output/out_0': 'a', '/output/out_1': 'b', '/output/out_2': 'c'}):
            merger('/output/out', ['/s1/file1', '/s2/file1', '/s3/file1'])


class DirMergerTests(FileSystemTestCase):

    def test_success_no_overlapping_files(self):
        mock_filesystem = MockFileSystem({'/shard0/file1': '1', '/shard1/file2': '2'})
        d = merge_results.DirMerger(mock_filesystem)
        with self.assertFilesAdded(mock_filesystem, {'/output/file1': '1', '/output/file2': '2'}):
            d.merge('/output', ['/shard0', '/shard1'])

    def test_success_no_overlapping_files_but_matching_contents(self):
        mock_filesystem = MockFileSystem({'/shard0/file1': '1', '/shard1/file2': '1'})
        d = merge_results.DirMerger(mock_filesystem)
        with self.assertFilesAdded(mock_filesystem, {'/output/file1': '1', '/output/file2': '1'}):
            d.merge('/output', ['/shard0', '/shard1'])

    def test_success_same_file_but_matching_contents(self):
        mock_filesystem = MockFileSystem({'/shard0/file1': '1', '/shard1/file1': '1'})
        d = merge_results.DirMerger(mock_filesystem)
        with self.assertFilesAdded(mock_filesystem, {'/output/file1': '1'}):
            d.merge('/output', ['/shard0', '/shard1'])

    def test_failure_same_file_but_contents_differ(self):
        mock_filesystem = MockFileSystem({'/shard0/file1': '1', '/shard1/file1': '2'})
        d = merge_results.DirMerger(mock_filesystem)
        with self.assertRaises(merge_results.MergeFailure):
            d.merge('/output', ['/shard0', '/shard1'])


class MergeFilesJSONPTests(FileSystemTestCase):

    def assertLoad(self, fd, expected_before, expected_json, expected_after):
        before, json_data, after = merge_results.MergeFilesJSONP.load_jsonp(fd)
        self.assertEqual(expected_before, before)
        self.assertDictEqual(expected_json, json_data)
        self.assertEqual(expected_after, after)

    def assertDump(self, before, json, after, expected_data):
        fd = StringIO.StringIO()
        merge_results.MergeFilesJSONP.dump_jsonp(fd, before, json, after)
        self.assertMultiLineEqual(fd.getvalue(), expected_data)

    def test_load(self):
        fdcls = StringIO.StringIO
        self.assertLoad(fdcls('{"a": 1}'), '', {'a': 1}, '')
        self.assertLoad(fdcls('f({"a": 1});'), 'f(', {'a': 1}, ');')
        self.assertLoad(fdcls('var o = {"a": 1}'), 'var o = ', {'a': 1}, '')
        self.assertLoad(fdcls('while(1); // {"a": 1}'), 'while(1); // ', {'a': 1}, '')
        self.assertLoad(fdcls('/* {"a": 1} */'), '/* ', {'a': 1}, ' */')

    def test_dump(self):
        self.assertDump('', {}, '', '{}')
        self.assertDump('f(', {}, ');', 'f({});')
        self.assertDump('var o = ', {}, '', 'var o = {}')
        self.assertDump('while(1); // ', {}, '', 'while(1); // {}')
        self.assertDump('/* ', {}, ' */', '/* {} */')

        self.assertDump('', {'a': 1}, '', """\
{
  "a": 1
}""")
        self.assertDump('', {'a': [1, 'c', 3], 'b': 2}, '', """\
{
  "a": [
    1,
    "c",
    3
  ],
  "b": 2
}""")

    def assertMergeResults(self, mock_filesystem_contents, inputargs, filesystem_contains, json_data_merger=None):
        mock_filesystem = MockFileSystem(mock_filesystem_contents, dirs=['/output'])

        file_merger = merge_results.MergeFilesJSONP(mock_filesystem, json_data_merger)
        with self.assertFilesAdded(mock_filesystem, filesystem_contains):
            file_merger(*inputargs)

    def assertMergeRaises(self, mock_filesystem_contents, inputargs):
        mock_filesystem = MockFileSystem(mock_filesystem_contents, dirs=['/output'])

        file_merger = merge_results.MergeFilesJSONP(mock_filesystem)
        with self.assertRaises(merge_results.MergeFailure):
            file_merger(*inputargs)

    def test_single_file(self):
        self.assertMergeResults(
            {'/s/filea': '{"a": 1}'},
            ('/output/out1', ['/s/filea']),
            {'/output/out1': """\
{
  "a": 1
}"""})

        self.assertMergeResults(
            {'/s/filef1a': 'f1({"a": 1})'},
            ('/output/outf1', ['/s/filef1a']),
            {'/output/outf1': """\
f1({
  "a": 1
})"""})

        self.assertMergeResults(
            {'/s/fileb1': '{"b": 2}'},
            ('/output/out2', ['/s/fileb1']),
            {'/output/out2': """\
{
  "b": 2
}"""})

        self.assertMergeResults(
            {'/s/filef1b1': 'f1({"b": 2})'},
            ('/output/outf2', ['/s/filef1b1']),
            {'/output/outf2': """\
f1({
  "b": 2
})"""})

    def test_two_files_nonconflicting_values(self):
        self.assertMergeResults(
            {
                '/s/filea': '{"a": 1}',
                '/s/fileb1': '{"b": 2}',
            },
            ('/output/out3', ['/s/filea', '/s/fileb1']),
            {
                '/output/out3': """\
{
  "a": 1,
  "b": 2
}"""})

        self.assertMergeResults(
            {
                '/s/filef1a': 'f1({"a": 1})',
                '/s/filef1b1': 'f1({"b": 2})',
            },
            ('/output/outf3', ['/s/filef1a', '/s/filef1b1']),
            {
                '/output/outf3': """\
f1({
  "a": 1,
  "b": 2
})"""})

    def test_two_files_identical_values_fails_by_default(self):
        self.assertMergeRaises(
            {
                '/s/fileb1': '{"b": 2}',
                '/s/fileb2': '{"b": 2}',
            },
            ('/output/out4', ['/s/fileb1', '/s/fileb2']))

        self.assertMergeRaises(
            {
                '/s/filef1b1': 'f1({"b": 2})',
                '/s/filef1b2': 'f1({"b": 2})',
            },
            ('/output/outf4', ['/s/filef1b1', '/s/filef1b2']))

    def test_two_files_identical_values_works_with_custom_merger(self):
        json_data_merger = merge_results.JSONMerger()
        json_data_merger.fallback_matcher = json_data_merger.merge_equal

        self.assertMergeResults(
            {
                '/s/fileb1': '{"b": 2}',
                '/s/fileb2': '{"b": 2}',
            },
            ('/output/out4', ['/s/fileb1', '/s/fileb2']),
            {
                '/output/out4': """\
{
  "b": 2
}"""},
            json_data_merger=json_data_merger)

        self.assertMergeResults(
            {
                '/s/filef1b1': 'f1({"b": 2})',
                '/s/filef1b2': 'f1({"b": 2})',
            },
            ('/output/outf4', ['/s/filef1b1', '/s/filef1b2']),
            {
                '/output/outf4': """\
f1({
  "b": 2
})"""},
            json_data_merger=json_data_merger)

    def test_two_files_conflicting_values(self):
        self.assertMergeRaises(
            {
                '/s/fileb1': '{"b": 2}',
                '/s/fileb3': '{"b": 3}',
            },
            ('/output/outff1', ['/s/fileb1', '/s/fileb3']))
        self.assertMergeRaises(
            {
                '/s/filef1b1': 'f1({"b": 2})',
                '/s/filef1b3': 'f1({"b": 3})',
            },
            ('/output/outff2', ['/s/filef1b1', '/s/filef1b3']))

    def test_two_files_conflicting_function_names(self):
        self.assertMergeRaises(
            {
                '/s/filef1a': 'f1({"a": 1})',
                '/s/filef2a': 'f2({"a": 1})',
            },
            ('/output/outff3', ['/s/filef1a', '/s/filef2a']))

    def test_two_files_mixed_json_and_jsonp(self):
        self.assertMergeRaises(
            {
                '/s/filea': '{"a": 1}',
                '/s/filef1a': 'f1({"a": 1})',
            },
            ('/output/outff4', ['/s/filea', '/s/filef1a']))


class JSONTestResultsMerger(unittest.TestCase):

    def test_allow_unknown_if_matching(self):
        merger = merge_results.JSONTestResultsMerger(allow_unknown_if_matching=False)
        self.assertEqual(
            {'version': 3.0},
            merger.merge([{'version': 3.0}, {'version': 3.0}]))

        with self.assertRaises(merge_results.MergeFailure):
            merger.merge([{'random': 'hello'}, {'random': 'hello'}])

        merger = merge_results.JSONTestResultsMerger(allow_unknown_if_matching=True)
        self.assertEqual(
            {'random': 'hello'},
            merger.merge([{'random': 'hello'}, {'random': 'hello'}]))

    def test_summable(self):
        merger = merge_results.JSONTestResultsMerger()
        self.assertEqual(
            {'fixable': 5},
            merger.merge([{'fixable': 2}, {'fixable': 3}]))
        self.assertEqual(
            {'num_failures_by_type': {'A': 4, 'B': 3, 'C': 2}},
            merger.merge([
                {'num_failures_by_type': {'A': 3, 'B': 1}},
                {'num_failures_by_type': {'A': 1, 'B': 2, 'C': 2}},
            ]))

    def test_interrupted(self):
        merger = merge_results.JSONTestResultsMerger()
        self.assertEqual(
            {'interrupted': False},
            merger.merge([{'interrupted': False}, {'interrupted': False}]))
        self.assertEqual(
            {'interrupted': True},
            merger.merge([{'interrupted': True}, {'interrupted': False}]))
        self.assertEqual(
            {'interrupted': True},
            merger.merge([{'interrupted': False}, {'interrupted': True}]))

    def test_seconds_since_epoch(self):
        merger = merge_results.JSONTestResultsMerger()
        self.assertEqual(
            {'seconds_since_epoch': 2},
            merger.merge([{'seconds_since_epoch': 3}, {'seconds_since_epoch': 2}]))
        self.assertEqual(
            {'seconds_since_epoch': 2},
            merger.merge([{'seconds_since_epoch': 2}, {'seconds_since_epoch': 3}]))
        self.assertEqual(
            {'seconds_since_epoch': 12},
            merger.merge([{'seconds_since_epoch': 12}, {}]))


class LayoutTestDirMergerTests(unittest.TestCase):

    maxDiff = None

    # Common HTML files every shard has.
    dashboard_html = '<html><body>Dashboard would be here.</body></html>'
    dashboard_css = '.css { value = would be here }'

    # JSON files for shard 1
    # Shard1 has the following tests;
    #   testdir1/test1.html
    #   testdir1/test2.html
    #   testdir2/testdir2.1/test3.html
    shard0_output_json = """\
{
  "build_number": "DUMMY_BUILD_NUMBER",
  "builder_name": "abc",
  "chromium_revision": "123",
  "fixable": 1,
  "interrupted": false,
  "layout_tests_dir": "/b/s/w/irJ1McdS/third_party/WebKit/LayoutTests",
  "num_failures_by_type": {
    "AUDIO": 2,
    "CRASH": 3
  },
  "num_flaky": 4,
  "num_passes": 5,
  "num_regressions": 6,
  "path_delimiter": "/",
  "pixel_tests_enabled": true,
  "random_order_seed": 4,
  "seconds_since_epoch": 1488435717,
  "skipped": 8,
  "tests": {
    "testdir1": {
      "test1.html": {
        "actual": "PASS",
        "expected": "PASS",
        "has_stderr": false,
        "time": 0.3
      },
      "test2.html": {
        "actual": "PASS",
        "expected": "PASS",
        "has_stderr": false,
        "time": 0.3
      }
    },
    "testdir2": {
      "testdir2.1": {
        "test3.html": {
          "actual": "PASS",
          "expected": "PASS",
          "has_stderr": false,
          "time": 0.3
        }
      }
    }
  },
  "version": 3
}"""

    shard0_archived_results_json = """\
ADD_RESULTS({
  "result_links": [
    "results.html"
  ],
  "tests": {
    "testdir1": {
      "test1.html": {
        "archived_results": [
          "PASS"
        ]
      },
      "test2.html": {
        "archived_results": [
          "PASS"
        ]
      }
    },
    "testdir2": {
      "testdir2.1": {
        "test3.html": {
          "archived_results": [
            "PASS"
          ]
        }
      }
    }
  }
});"""

    shard0_stats_json = """\
{
  "testdir1": {
    "test1.html": {
      "results": [1, 2, 3, 4, 5]
    },
    "test2.html": {
      "results": [6, 7, 8, 9, 10]
    }
  },
  "testdir2": {
    "testdir2.1": {
      "test3.html": {
        "results": [11, 12, 13, 14, 15]
      }
    }
  }
}
"""
    shard0_times_ms_json = """{
  "testdir1": {
    "test1.html": 263,
    "test2.html": 32
  },
  "testdir2": {
    "testdir2.1": {
      "test3.html": 77
    }
  }
}"""

    # Logs for shard 1
    shard0_access_log = """\
127.0.0.1 - - [01/Mar/2017:22:20:10 -0800] "GET /testdir1/test1.html HTTP/1.1" 200 594
127.0.0.1 - - [01/Mar/2017:22:20:10 -0800] "GET /testdir1/test2.html HTTP/1.1" 200 251
"""
    shard0_error_log = """\
[Wed Mar 01 22:20:07.392108 2017] [ssl:warn] [pid 15009] AH01909: RSA certificate configured for 127.0.0.1:443 does NOT include an ID which matches the server name
"""

    # JSON files for shard 2
    # Shard1 has the following tests;
    #   testdir2/testdir2.1/test4.html
    #   testdir3/testt.html
    shard1_output_json = """\
{
  "build_number": "DUMMY_BUILD_NUMBER",
  "builder_name": "abc",
  "chromium_revision": "123",
  "fixable": 9,
  "interrupted": false,
  "layout_tests_dir": "/b/s/w/sadfa124/third_party/WebKit/LayoutTests",
  "num_failures_by_type": {
    "AUDIO": 10,
    "CRASH": 11
  },
  "num_flaky": 12,
  "num_passes": 13,
  "num_regressions": 14,
  "path_delimiter": "/",
  "pixel_tests_enabled": true,
  "random_order_seed": 4,
  "seconds_since_epoch": 1488435717,
  "skipped": 15,
  "tests": {
    "testdir2": {
      "testdir2.1": {
        "test4.html": {
          "actual": "FAIL",
          "expected": "PASS",
          "has_stderr": true,
          "time": 0.3
        }
      }
    },
    "testdir3": {
      "test5.html": {
        "actual": "PASS",
        "expected": "PASS",
        "has_stderr": true,
        "time": 0.3
      }
    }
  },
  "version": 3
}"""

    shard1_archived_results_json = """\
ADD_RESULTS({
  "result_links": [
    "results.html"
  ],
  "tests": {
    "testdir2": {
      "testdir2.1": {
        "test4.html": {
          "archived_results": [
            "FAIL"
          ]
        }
      }
    },
    "testdir3": {
      "test5.html": {
        "archived_results": [
          "PASS"
        ]
      }
    }
  }
});"""

    shard1_stats_json = """\
{
  "testdir2": {
    "testdir2.1": {
      "test4.html": {
        "results": [16, 17, 18, 19, 20]
      }
    }
  },
  "testdir3": {
    "test5.html": {
      "results": [21, 22, 23, 24, 25]
    }
  }
}
"""
    shard1_times_ms_json = """{
  "testdir2": {
    "testdir2.1": {
      "test4.html": 99
    }
  },
  "testdir3": {
    "test5.html": 11
  }
}"""

    # Logs for shard 2
    shard1_access_log = """\
127.0.0.1 - - [01/Mar/2017:22:20:10 -0800] "GET /resource.html HTTP/1.1" 200 594
"""
    shard1_error_log = """\
[Wed Mar 01 22:20:07.400802 2017] [ssl:warn] [pid 15010] AH01909: RSA certificate configured for 127.0.0.1:443 does NOT include an ID which matches the server name
"""

    layout_test_filesystem = {
        # Files for shard0
        '/shards/0/layout-test-results/access_log.txt': shard0_access_log,
        '/shards/0/layout-test-results/archived_results.json': shard0_archived_results_json,
        '/shards/0/layout-test-results/dashboard.css': dashboard_css,
        '/shards/0/layout-test-results/dashboard.html': dashboard_html,
        '/shards/0/layout-test-results/error_log.txt': shard0_error_log,
        '/shards/0/layout-test-results/failing_results.json': "ADD_RESULTS(" + shard0_output_json + ");",
        '/shards/0/layout-test-results/full_results.json': shard0_output_json,
        '/shards/0/layout-test-results/stats.json': shard0_stats_json,
        '/shards/0/layout-test-results/testdir1/test1-actual.png': '1ap',
        '/shards/0/layout-test-results/testdir1/test1-diff.png': '1dp',
        '/shards/0/layout-test-results/testdir1/test1-diffs.html': '1dh',
        '/shards/0/layout-test-results/testdir1/test1-expected-stderr.txt': '1est',
        '/shards/0/layout-test-results/testdir1/test1-expected.png': '1ep',
        '/shards/0/layout-test-results/testdir1/test2-actual.png': '2ap',
        '/shards/0/layout-test-results/testdir1/test2-diff.png': '2dp',
        '/shards/0/layout-test-results/testdir1/test2-diffs.html': '2dh',
        '/shards/0/layout-test-results/testdir1/test2-expected-stderr.txt': '2est',
        '/shards/0/layout-test-results/testdir1/test2-expected.png': '2ep',
        '/shards/0/layout-test-results/testdir2/testdir2.1/test3-actual.png': '3ap',
        '/shards/0/layout-test-results/testdir2/testdir2.1/test3-diff.png': '3dp',
        '/shards/0/layout-test-results/testdir2/testdir2.1/test3-diffs.html': '3dh',
        '/shards/0/layout-test-results/testdir2/testdir2.1/test3-expected-stderr.txt': '3est',
        '/shards/0/layout-test-results/testdir2/testdir2.1/test3-expected.png': '3ep',
        '/shards/0/layout-test-results/times_ms.json': shard0_times_ms_json,
        '/shards/0/output.json': shard0_output_json,
        # Files for shard1
        '/shards/1/layout-test-results/access_log.txt': shard1_access_log,
        '/shards/1/layout-test-results/archived_results.json': shard1_archived_results_json,
        '/shards/1/layout-test-results/dashboard.css': dashboard_css,
        '/shards/1/layout-test-results/dashboard.html': dashboard_html,
        '/shards/1/layout-test-results/error_log.txt': shard1_error_log,
        '/shards/1/layout-test-results/failing_results.json': "ADD_RESULTS(" + shard1_output_json + ");",
        '/shards/1/layout-test-results/full_results.json': shard1_output_json,
        '/shards/1/layout-test-results/stats.json': shard1_stats_json,
        '/shards/1/layout-test-results/testdir2/testdir2.1/test4-actual.png': '4ap',
        '/shards/1/layout-test-results/testdir2/testdir2.1/test4-diff.png': '4dp',
        '/shards/1/layout-test-results/testdir2/testdir2.1/test4-diffs.html': '4dh',
        '/shards/1/layout-test-results/testdir2/testdir2.1/test4-expected-stderr.txt': '4est',
        '/shards/1/layout-test-results/testdir2/testdir2.1/test4-expected.png': '4ep',
        '/shards/1/layout-test-results/testdir3/test5-actual.png': '5ap',
        '/shards/1/layout-test-results/testdir3/test5-diff.png': '5dp',
        '/shards/1/layout-test-results/testdir3/test5-diffs.html': '5dh',
        '/shards/1/layout-test-results/testdir3/test5-expected-stderr.txt': '5est',
        '/shards/1/layout-test-results/testdir3/test5-expected.png': '5ep',
        '/shards/1/layout-test-results/times_ms.json': shard1_times_ms_json,
        '/shards/1/output.json': shard1_output_json,
    }

    # Combined JSON files
    output_output_json = """\
{
  "build_number": "DUMMY_BUILD_NUMBER",
  "builder_name": "abc",
  "chromium_revision": "123",
  "fixable": 10,
  "interrupted": false,
  "layout_tests_dir": "src",
  "num_failures_by_type": {
    "AUDIO": 12,
    "CRASH": 14
  },
  "num_flaky": 16,
  "num_passes": 18,
  "num_regressions": 20,
  "path_delimiter": "/",
  "pixel_tests_enabled": true,
  "random_order_seed": 4,
  "seconds_since_epoch": 1488435717,
  "skipped": 23,
  "tests": {
    "testdir1": {
      "test1.html": {
        "actual": "PASS",
        "expected": "PASS",
        "has_stderr": false,
        "time": 0.3
      },
      "test2.html": {
        "actual": "PASS",
        "expected": "PASS",
        "has_stderr": false,
        "time": 0.3
      }
    },
    "testdir2": {
      "testdir2.1": {
        "test3.html": {
          "actual": "PASS",
          "expected": "PASS",
          "has_stderr": false,
          "time": 0.3
        },
        "test4.html": {
          "actual": "FAIL",
          "expected": "PASS",
          "has_stderr": true,
          "time": 0.3
        }
      }
    },
    "testdir3": {
      "test5.html": {
        "actual": "PASS",
        "expected": "PASS",
        "has_stderr": true,
        "time": 0.3
      }
    }
  },
  "version": 3
}"""

    output_archived_results_json = """\
ADD_RESULTS({
  "result_links": [
    "results.html",
    "results.html"
  ],
  "tests": {
    "testdir1": {
      "test1.html": {
        "archived_results": [
          "PASS"
        ]
      },
      "test2.html": {
        "archived_results": [
          "PASS"
        ]
      }
    },
    "testdir2": {
      "testdir2.1": {
        "test3.html": {
          "archived_results": [
            "PASS"
          ]
        },
        "test4.html": {
          "archived_results": [
            "FAIL"
          ]
        }
      }
    },
    "testdir3": {
      "test5.html": {
        "archived_results": [
          "PASS"
        ]
      }
    }
  }
});"""

    output_stats_json = """\
{
  "testdir1": {
    "test1.html": {
      "results": [
        1,
        2,
        3,
        4,
        5
      ]
    },
    "test2.html": {
      "results": [
        6,
        7,
        8,
        9,
        10
      ]
    }
  },
  "testdir2": {
    "testdir2.1": {
      "test3.html": {
        "results": [
          11,
          12,
          13,
          14,
          15
        ]
      },
      "test4.html": {
        "results": [
          16,
          17,
          18,
          19,
          20
        ]
      }
    }
  },
  "testdir3": {
    "test5.html": {
      "results": [
        21,
        22,
        23,
        24,
        25
      ]
    }
  }
}
"""
    output_times_ms_json = """{
  "testdir1": {
    "test1.html": 263,
    "test2.html": 32
  },
  "testdir2": {
    "testdir2.1": {
      "test3.html": 77,
      "test4.html": 99
    }
  },
  "testdir3": {
    "test5.html": 11
  }
}"""

    # Combined Logs
    output_access_log = """\
127.0.0.1 - - [01/Mar/2017:22:20:10 -0800] "GET /resource.html HTTP/1.1" 200 594
127.0.0.1 - - [01/Mar/2017:22:20:10 -0800] "GET /testdir1/test1.html HTTP/1.1" 200 594
127.0.0.1 - - [01/Mar/2017:22:20:10 -0800] "GET /testdir1/test2.html HTTP/1.1" 200 251
"""
    output_error_log = """\
[Wed Mar 01 22:20:07.392108 2017] [ssl:warn] [pid 15009] AH01909: RSA certificate configured for 127.0.0.1:443 does NOT include an ID which matches the server name
[Wed Mar 01 22:20:07.400802 2017] [ssl:warn] [pid 15010] AH01909: RSA certificate configured for 127.0.0.1:443 does NOT include an ID which matches the server name
"""

    layout_test_output_filesystem = {
        '/out/layout-test-results/access_log.txt': output_access_log,
        '/out/layout-test-results/archived_results.json': output_archived_results_json,
        '/out/layout-test-results/dashboard.css': dashboard_css,
        '/out/layout-test-results/dashboard.html': dashboard_html,
        '/out/layout-test-results/error_log.txt': output_error_log,
        '/out/layout-test-results/failing_results.json': "ADD_RESULTS(" + output_output_json + ");",
        '/out/layout-test-results/full_results.json': output_output_json,
        '/out/layout-test-results/stats.json': output_stats_json,
        '/out/layout-test-results/testdir1/test1-actual.png': '1ap',
        '/out/layout-test-results/testdir1/test1-diff.png': '1dp',
        '/out/layout-test-results/testdir1/test1-diffs.html': '1dh',
        '/out/layout-test-results/testdir1/test1-expected-stderr.txt': '1est',
        '/out/layout-test-results/testdir1/test1-expected.png': '1ep',
        '/out/layout-test-results/testdir2/testdir2.1/test3-actual.png': '3ap',
        '/out/layout-test-results/testdir2/testdir2.1/test3-diff.png': '3dp',
        '/out/layout-test-results/testdir2/testdir2.1/test3-diffs.html': '3dh',
        '/out/layout-test-results/testdir2/testdir2.1/test3-expected-stderr.txt': '3est',
        '/out/layout-test-results/testdir2/testdir2.1/test3-expected.png': '3ep',
        '/out/layout-test-results/testdir2/testdir2.1/test4-actual.png': '4ap',
        '/out/layout-test-results/testdir2/testdir2.1/test4-diff.png': '4dp',
        '/out/layout-test-results/testdir2/testdir2.1/test4-diffs.html': '4dh',
        '/out/layout-test-results/testdir2/testdir2.1/test4-expected-stderr.txt': '4est',
        '/out/layout-test-results/testdir2/testdir2.1/test4-expected.png': '4ep',
        '/out/layout-test-results/testdir3/test5-actual.png': '5ap',
        '/out/layout-test-results/testdir3/test5-diff.png': '5dp',
        '/out/layout-test-results/testdir3/test5-diffs.html': '5dh',
        '/out/layout-test-results/testdir3/test5-expected-stderr.txt': '5est',
        '/out/layout-test-results/testdir3/test5-expected.png': '5ep',
        '/out/layout-test-results/times_ms.json': output_times_ms_json,
        '/out/output.json': output_output_json,
    }

    def test(self):
        fs = MockFileSystem(self.layout_test_filesystem)

        merger = merge_results.LayoutTestDirMerger(fs, results_json_value_overrides={'layout_tests_dir': 'src'})
        merger.merge('/out', ['/shards/0', '/shards/1'])

        for fname, contents in self.layout_test_output_filesystem.items():
            self.assertIn(fname, fs.files)
            self.assertMultiLineEqual(contents, fs.files[fname])
