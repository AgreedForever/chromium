#!/usr/bin/python
# Copyright (c) 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""The intermediate has a policies extension marked as critical, which contains
an unknown qualifer (1.2.3.4)."""

import sys
sys.path += ['..']

import common

# Self-signed root certificate (used as trust anchor).
root = common.create_self_signed_root_certificate('Root')

# Intermediate that has a critical policies extension containing an unknown
# policy qualifer.
intermediate = common.create_intermediate_certificate('Intermediate', root)
intermediate.get_extensions().add_property(
    '2.5.29.32', ('critical,DER:30:13:30:11:06:02:2a:03:30:0b:30:09:06:03:'
                  '2a:03:04:0c:02:68:69'))

# Target certificate.
target = common.create_end_entity_certificate('Target', intermediate)

chain = [target, intermediate, root]
common.write_chain(__doc__, chain, 'chain.pem')
