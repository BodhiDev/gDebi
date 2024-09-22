#!/usr/bin/env python3

import os
import re

# Look/set what version we have
changelog = 'debian/changelog'
if os.path.exists(changelog):
    head = open(changelog).readline()
    match = re.search(r".* \((.*)\) .*", head)
    if match:
        version = match.group(1)
        f=open('GDebi/Version.py', 'w')
        f.write('VERSION="%s"\n' % version)
        f.close()
