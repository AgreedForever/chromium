# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import motopho_thread as mt
import robot_arm as ra

import json
import glob
import logging
import numpy
import os
import re
import subprocess
import sys
import time


MOTOPHO_THREAD_TIMEOUT = 15
DEFAULT_URLS = [
    # TODO(bsheedy): See about having versioned copies of the flicker app
    # instead of using personal github.
    # Purely a flicker app - no additional CPU/GPU load
    'https://weableandbob.github.io/Motopho/'
    'flicker_apps/webvr/webvr-flicker-app-klaus.html?'
    'polyfill=0\&canvasClickPresents=1',
    # URLs that render 3D scenes in addition to the Motopho patch
    # Heavy CPU load, moderate GPU load
    'https://webvr.info/samples/test-slow-render.html?'
    'latencyPatch=1\&canvasClickPresents=1\&'
    'heavyGpu=1\&workTime=20\&cubeCount=8\&cubeScale=0.4',
    # Moderate CPU load, light GPU load
    'https://webvr.info/samples/test-slow-render.html?'
    'latencyPatch=1\&canvasClickPresents=1\&'
    'heavyGpu=1\&workTime=12\&cubeCount=8\&cubeScale=0.3',
    # Light CPU load, moderate GPU load
    'https://webvr.info/samples/test-slow-render.html?'
    'latencyPatch=1\&canvasClickPresents=1\&'
    'heavyGpu=1\&workTime=5\&cubeCount=8\&cubeScale=0.4',
    # Heavy CPU load, very light GPU load
    'https://webvr.info/samples/test-slow-render.html?'
    'latencyPatch=1\&canvasClickPresents=1\&'
    'workTime=20',
    # No additional CPU load, very light GPU load
    'https://webvr.info/samples/test-slow-render.html?'
    'latencyPatch=1\&canvasClickPresents=1',
]


def GetTtyDevices(tty_pattern, vendor_ids):
  """Finds all devices connected to tty that match a pattern and device id.

  If a serial device is connected to the computer via USB, this function
  will check all tty devices that match tty_pattern, and return the ones
  that have vendor identification number in the list vendor_ids.

  Args:
    tty_pattern: The search pattern, such as r'ttyACM\d+'.
    vendor_ids: The list of 16-bit USB vendor ids, such as [0x2a03].

  Returns:
    A list of strings of tty devices, for example ['ttyACM0'].
  """
  product_string = 'PRODUCT='
  sys_class_dir = '/sys/class/tty/'

  tty_devices = glob.glob(sys_class_dir + '*')

  matcher = re.compile('.*' + tty_pattern)
  tty_matches = [x for x in tty_devices if matcher.search(x)]
  tty_matches = [x[len(sys_class_dir):] for x in tty_matches]

  found_devices = []
  for match in tty_matches:
    class_filename = sys_class_dir + match + '/device/uevent'
    with open(class_filename, 'r') as uevent_file:
      # Look for the desired product id in the uevent text.
      for line in uevent_file:
        if product_string in line:
          ids = line[len(product_string):].split('/')
          ids = [int(x, 16) for x in ids]

          for desired_id in vendor_ids:
            if desired_id in ids:
              found_devices.append(match)

  return found_devices


class WebVrLatencyTest(object):
  """Base class for all WebVR latency tests.

  This is meant to be subclassed for each platform the test is run on. While
  the latency test itself is cross-platform, the setup and teardown for
  tests is platform-dependent.
  """
  def __init__(self, args):
    self.args = args
    self._num_samples = args.num_samples
    self._test_urls = args.urls or DEFAULT_URLS
    assert (self._num_samples > 0),'Number of samples must be greater than 0'
    self._device_name = 'generic_device'
    self._test_results = {}

    # Connect to the Arduino that drives the servos
    devices = GetTtyDevices(r'ttyACM\d+', [0x2a03, 0x2341])
    assert (len(devices) == 1),'Found %d devices, expected 1' % len(devices)
    self.robot_arm = ra.RobotArm(devices[0])

  def RunTests(self):
    """Runs latency tests on all the URLs provided to the test on creation.

    Repeatedly runs the steps to start Chrome, measure/store latency, and
    clean up before storing all results to a single file for dashboard
    uploading.
    """
    try:
      self._OneTimeSetup()
      for url in self._test_urls:
        self._Setup(url)
        self._Run(url)
        self._Teardown()
      self._SaveResultsToFile()
    finally:
      self._OneTimeTeardown()

  def _OneTimeSetup(self):
    """Performs any platform-specific setup once before any tests."""
    raise NotImplementedError(
        'Platform-specific setup must be implemented in subclass')

  def _Setup(self, url):
    """Performs any platform-specific setup before each test."""
    raise NotImplementedError(
        'Platform-specific setup must be implemented in subclass')

  def _Run(self, url):
    """Run the latency test.

    Handles the actual latency measurement, which is identical across
    different platforms, as well as result storing.
    """
    # Motopho scripts use relative paths, so switch to the Motopho directory
    os.chdir(self.args.motopho_path)

    # Set up the thread that runs the Motopho script
    motopho_thread = mt.MotophoThread(self._num_samples)
    motopho_thread.start()

    # Run multiple times so we can get an average and standard deviation
    for _ in xrange(self._num_samples):
      self.robot_arm.ResetPosition()
      # Start the Motopho script
      motopho_thread.StartIteration()
      # Let the Motopho be stationary so the script can calculate the bias
      time.sleep(3)
      motopho_thread.BlockNextIteration()
      # Move so we can measure latency
      self.robot_arm.StartMotophoMovement()
      if not motopho_thread.WaitForIterationEnd(MOTOPHO_THREAD_TIMEOUT):
        # TODO(bsheedy): Look into ways to prevent Motopho from not sending any
        # data until unplugged and replugged into the machine after a reboot.
        logging.error('Motopho thread timeout, '
                      'Motopho may need to be replugged.')
      self.robot_arm.StopAllMovement()
      time.sleep(1)
    self._StoreResults(motopho_thread.latencies, motopho_thread.correlations,
                       url)

  def _Teardown(self):
    """Performs any platform-specific teardown after each test."""
    raise NotImplementedError(
        'Platform-specific teardown must be implemented in subclass')

  def _OneTimeTeardown(self):
    """Performs any platform-specific teardown after all tests."""
    raise NotImplementedError(
        'Platform-specific teardown must be implemented in sublcass')

  def _RunCommand(self, cmd):
    """Runs the given cmd list and returns its output.

    Prints the command's output and exits if any error occurs.

    Returns:
      A string containing the stdout and stderr of the command.
    """
    try:
      return subprocess.check_output(cmd, stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
      logging.error('Failed command output: %s', e.output)
      raise e

  def _SetChromeCommandLineFlags(self, flags):
    raise NotImplementedError(
        'Command-line flag setting must be implemented in subclass')

  def _StoreResults(self, latencies, correlations, url):
    """Temporarily stores the results of a test.

    Stores the given results in memory to be later retrieved and written to
    a file in _SaveResultsToFile once all tests are done. Also logs the raw
    data and its average/standard deviation.
    """
    avg_latency = sum(latencies) / len(latencies)
    std_latency = numpy.std(latencies)
    avg_correlation = sum(correlations) / len(correlations)
    std_correlation = numpy.std(correlations)
    logging.info('\nURL: %s\n'
                 'Raw latencies: %s\nRaw correlations: %s\n'
                 'Avg latency: %f +/- %f\nAvg correlation: %f +/- %f',
                 url, str(latencies), str(correlations), avg_latency,
                 std_latency, avg_correlation, std_correlation)

    self._test_results[url] = {
        'correlations': correlations,
        'std_correlation': std_correlation,
        'latencies': latencies,
        'std_latency': std_latency,
    }

  def _SaveResultsToFile(self):
    if not (self.args.output_dir and os.path.isdir(self.args.output_dir)):
      logging.warning('No output directory set, not saving results to file')
      return

    correlation_string = self._device_name + '_correlation'
    latency_string = self._device_name + '_latency'
    charts = {
        correlation_string: {
            'summary': {
                'improvement_direction': 'up',
                'name': correlation_string,
                'std': 0.0,
                'type': 'list_of_scalar_values',
                'units': '',
                'values': [],
            }
        },
        latency_string: {
            'summary': {
                'improvement_direction': 'down',
                'name': latency_string,
                'std': 0.0,
                'type': 'list_of_scalar_values',
                'units': 'ms',
                'values': [],
            }
        }
    }
    for url, results in self._test_results.iteritems():
      charts[correlation_string][url] = {
          'improvement_direction': 'up',
          'name': correlation_string,
          'std': results['std_correlation'],
          'type': 'list_of_scalar_values',
          'units': '',
          'values': results['correlations'],
      }

      charts[correlation_string]['summary']['values'].extend(
          results['correlations'])

      charts[latency_string][url] = {
          'improvement_direction': 'down',
          'name': latency_string,
          'std': results['std_latency'],
          'type': 'list_of_scalar_values',
          'units': 'ms',
          'values': results['latencies'],
      }

      charts[latency_string]['summary']['values'].extend(results['latencies'])

    results = {
      'format_version': '1.0',
      'benchmark_name': 'webvr_latency',
      'benchmark_description': 'Measures the motion-to-photon latency of WebVR',
      'charts': charts,
    }

    with file(os.path.join(self.args.output_dir,
                           self.args.results_file), 'w') as outfile:
      json.dump(results, outfile)
