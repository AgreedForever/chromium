# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import re

from pylib import constants


_BLACKLIST = [
  re.compile(r'.*OWNERS'),  # Should never be included.
  re.compile(r'.*\.crx'),  # Chrome extension zip files.
  re.compile(r'.*\.so'),  # Libraries packed into .apk.
  re.compile(r'.*Mojo.*manifest\.json'),  # Some source_set()s pull these in.
  re.compile(r'.*\.py'),  # Some test_support targets include python deps.
  re.compile(r'.*\.stamp'),  # Stamp files should never be included.

  # Adding dependency on mojom targets adds data dependency on js bindings.
  # Do not include those files except for JsToCppTest.mojom.js, which is
  # required by webkit_unit_tests during runtime.
  # TODO(yzshen): Remove this rule by generating foo_js that targets can
  # explicitly depend on (crbug.com/603212).
  re.compile(r'.*(?<!JsToCpp)\.mojom\.js'),

  # Chrome external extensions config file.
  re.compile(r'.*external_extensions\.json'),

  # Exists just to test the compile, not to be run.
  re.compile(r'.*jni_generator_tests'),

  # v8's blobs get packaged into APKs.
  re.compile(r'.*natives_blob.*\.bin'),
  re.compile(r'.*snapshot_blob.*\.bin'),
]


def DevicePathComponentsFor(host_path, output_directory):
  """Returns the device path components for a given host path.

  This returns the device path as a list of joinable path components,
  with None as the first element to indicate that the path should be
  rooted at $EXTERNAL_STORAGE.

  e.g., given

    '$CHROMIUM_SRC/foo/bar/baz.txt'

  this would return

    [None, 'foo', 'bar', 'baz.txt']

  This handles a couple classes of paths differently than it otherwise would:
    - All .pak files get mapped to top-level paks/
    - Anything in the output directory gets mapped relative to the output
      directory rather than the source directory.

  e.g. given

    '$CHROMIUM_SRC/out/Release/icu_fake_dir/icudtl.dat'

  this would return

    [None, 'icu_fake_dir', 'icudtl.dat']

  Args:
    host_path: The absolute path to the host file.
  Returns:
    A list of device path components.
  """
  if host_path.startswith(output_directory):
    if os.path.splitext(host_path)[1] == '.pak':
      return [None, 'paks', os.path.basename(host_path)]
    rel_host_path = os.path.relpath(host_path, output_directory)
  else:
    rel_host_path = os.path.relpath(host_path, constants.DIR_SOURCE_ROOT)

  device_path_components = [None]
  p = rel_host_path
  while p:
    p, d = os.path.split(p)
    if d:
      device_path_components.insert(1, d)
  return device_path_components


def GetDataDependencies(runtime_deps_path):
  """Returns a list of device data dependencies.

  Args:
    runtime_deps_path: A str path to the .runtime_deps file.
  Returns:
    A list of (host_path, device_path) tuples.
  """
  if not runtime_deps_path:
    return []

  with open(runtime_deps_path, 'r') as runtime_deps_file:
    rel_host_files = [l.strip() for l in runtime_deps_file if l]

  output_directory = constants.GetOutDirectory()
  abs_host_files = [
      os.path.abspath(os.path.join(output_directory, r))
      for r in rel_host_files]
  filtered_abs_host_files = [
      host_file for host_file in abs_host_files
      if not any(blacklist_re.match(host_file) for blacklist_re in _BLACKLIST)]
  return [(f, DevicePathComponentsFor(f, output_directory))
          for f in filtered_abs_host_files]
