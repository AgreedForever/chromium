# Copyright (c) 2009, Google Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import logging
import platform
import re
import sys
import webbrowser

from webkitpy.common.system.executive import Executive
from webkitpy.common.system.filesystem import FileSystem
from webkitpy.common.system.platform_info import PlatformInfo

_log = logging.getLogger(__name__)


class User(object):
    DEFAULT_NO = 'n'
    DEFAULT_YES = 'y'

    def __init__(self, platform_info=None):
        # We cannot get the PlatformInfo object from a SystemHost because
        # User is part of SystemHost itself.
        self._platform_info = platform_info or PlatformInfo(sys, platform, FileSystem(), Executive())

    # FIXME: These are @classmethods because bugzilla.py doesn't have a Tool object (thus no User instance).
    @classmethod
    def prompt(cls, message, repeat=1, raw_input=raw_input):
        response = None
        while repeat and not response:
            repeat -= 1
            response = raw_input(message)
        return response

    @classmethod
    def _wait_on_list_response(cls, list_items, can_choose_multiple, raw_input):
        while True:
            if can_choose_multiple:
                response = cls.prompt(
                    'Enter one or more numbers (comma-separated) or ranges (e.g. 3-7), or \'all\': ', raw_input=raw_input)
                if not response.strip() or response == 'all':
                    return list_items

                try:
                    indices = []
                    for value in re.split(r"\s*,\s*", response):
                        parts = value.split('-')
                        if len(parts) == 2:
                            indices += range(int(parts[0]) - 1, int(parts[1]))
                        else:
                            indices.append(int(value) - 1)
                except ValueError:
                    continue

                return [list_items[i] for i in indices]
            else:
                try:
                    result = int(cls.prompt('Enter a number: ', raw_input=raw_input)) - 1
                except ValueError:
                    continue
                return list_items[result]

    @classmethod
    def prompt_with_list(cls, list_title, list_items, can_choose_multiple=False, raw_input=raw_input):
        print list_title
        i = 0
        for item in list_items:
            i += 1
            print '%2d. %s' % (i, item)
        return cls._wait_on_list_response(list_items, can_choose_multiple, raw_input)

    def confirm(self, message=None, default=DEFAULT_YES, raw_input=raw_input):
        if not message:
            message = 'Continue?'
        choice = {'y': 'Y/n', 'n': 'y/N'}[default]
        response = raw_input('%s [%s]: ' % (message, choice))
        response = response.strip().lower()
        if not response:
            response = default
        return response and response[0] == 'y'

    def can_open_url(self):
        try:
            webbrowser.get()
            return True
        except webbrowser.Error:
            return False

    def open_url(self, url):
        if not self.can_open_url():
            _log.warning('Failed to open %s', url)
        webbrowser.open(url)
