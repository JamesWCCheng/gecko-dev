# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
"""
This file contains a whitelist of gecko.v2 index route job names.  The intent
of this whitelist is to raise an alarm when new jobs are added.  If those jobs
already run in Buildbot, then it's important that the generated index routes
match (and that only one of Buildbot and TaskCluster be tier-1 at any time).
If the jobs are new and never ran in Buildbot, then their job name can be added
here without any further fuss.

Once all jobs have been ported from Buildbot, this file can be removed.
"""

from __future__ import absolute_import, print_function, unicode_literals

# please keep me in lexical order
JOB_NAME_WHITELIST = set([
    'android-api-15-debug',
    'android-api-15-gradle-dependencies-opt',
    'android-api-15-gradle-opt',
    'android-api-15-opt',
    'android-x86-opt',
    'browser-haz-debug',
    'linux-debug',
    'linux-opt',
    'linux-pgo',
    'linux-qr-debug',
    'linux-qr-opt',
    'linux64-add-on-devel',
    'linux64-artifact-opt',
    'linux64-asan-debug',
    'linux64-asan-opt',
    'linux64-ccov-opt',
    'linux64-clang-tidy',
    'linux64-debug',
    'linux64-jsdcov-opt',
    'linux64-opt',
    'linux64-pgo',
    'linux64-qr-debug',
    'linux64-qr-opt',
    'linux64-st-an-opt',
    'linux64-stylo-debug',
    'linux64-stylo-opt',
    'linux64-valgrind-opt',
    'macosx64-add-on-devel',
    'macosx64-clang-tidy',
    'macosx64-debug',
    'macosx64-opt',
    'macosx64-qr-debug',
    'macosx64-qr-opt',
    'macosx64-st-an-opt',
    'shell-haz-debug',
    'sm-arm-sim-debug',
    'sm-arm64-sim-debug',
    'sm-asan-opt',
    'sm-compacting-debug',
    'sm-mozjs-sys-debug',
    'sm-msan-opt',
    'sm-nonunified-debug',
    'sm-package-opt',
    'sm-plain-opt',
    'sm-plaindebug-debug',
    'sm-rootanalysis-debug',
    'sm-tsan-opt',
    'win32-add-on-devel',
    'win32-clang-tidy',
    'win32-debug',
    'win32-opt',
    'win32-pgo',
    'win32-qr-debug',
    'win32-qr-opt',
    'win32-st-an-debug',
    'win32-st-an-opt',
    'win64-add-on-devel',
    'win64-clang-tidy',
    'win64-debug',
    'win64-opt',
    'win64-pgo',
    'win64-st-an-debug',
    'win64-st-an-opt',
    'win64-qr-debug',
    'win64-qr-opt',
])

JOB_NAME_WHITELIST_ERROR = """\
The gecko-v2 job name {} is not in the whitelist in gecko_v2_whitelist.py.
If this job runs on Buildbot, please ensure that the job names match between
Buildbot and TaskCluster, then add the job name to the whitelist.  If this is a
new job, there is nothing to check -- just add the job to the whitelist.
"""
