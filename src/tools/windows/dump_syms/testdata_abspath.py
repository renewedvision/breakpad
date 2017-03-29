#!/usr/bin/env python

from __future__ import print_function

import os
import sys

# get cwd as a Windows-style path
if sys.platform == 'win32':
  cwd = os.getcwd()
elif sys.platform == 'cygwin':
  cwd = os.popen('cygpath -wa ' + os.getcwd()).read()
else:
  print('Unable to determine absolute path of testdata', file=sys.stderr)
  sys.exit(1)

# escape backslashes in a Windows-style path for use as a C string
cwd = cwd.replace('\\', '\\\\')
print(cwd)
