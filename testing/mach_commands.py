# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

import json
import os
import sys
import tempfile
import subprocess
import shutil
from collections import defaultdict

from mach.decorators import (
    CommandArgument,
    CommandProvider,
    Command,
)

from mozbuild.base import MachCommandBase
from mozbuild.base import MachCommandConditions as conditions
import mozpack.path as mozpath
from argparse import ArgumentParser

UNKNOWN_TEST = '''
I was unable to find tests from the given argument(s).

You should specify a test directory, filename, test suite name, or
abbreviation. If no arguments are given, there must be local file
changes and corresponding IMPACTED_TESTS annotations in moz.build
files relevant to those files.

It's possible my little brain doesn't know about the type of test you are
trying to execute. If you suspect this, please request support by filing
a bug at
https://bugzilla.mozilla.org/enter_bug.cgi?product=Testing&component=General.
'''.strip()

UNKNOWN_FLAVOR = '''
I know you are trying to run a %s test. Unfortunately, I can't run those
tests yet. Sorry!
'''.strip()

CONFIG_ENVIRONMENT_NOT_FOUND = '''
No config environment detected. This means we are unable to properly
detect test files in the specified paths or tags. Please run:

    $ mach configure

and try again.
'''.lstrip()

MOCHITEST_CHUNK_BY_DIR = 4
MOCHITEST_TOTAL_CHUNKS = 5

TEST_SUITES = {
    'cppunittest': {
        'aliases': ('Cpp', 'cpp'),
        'mach_command': 'cppunittest',
        'kwargs': {'test_file': None},
    },
    'crashtest': {
        'aliases': ('C', 'Rc', 'RC', 'rc'),
        'mach_command': 'crashtest',
        'kwargs': {'test_file': None},
    },
    'firefox-ui-functional': {
        'aliases': ('Fxfn',),
        'mach_command': 'firefox-ui-functional',
        'kwargs': {},
    },
    'firefox-ui-update': {
        'aliases': ('Fxup',),
        'mach_command': 'firefox-ui-update',
        'kwargs': {},
    },
    'jetpack': {
        'aliases': ('J',),
        'mach_command': 'jetpack-test',
        'kwargs': {},
    },
    'check-spidermonkey': {
        'aliases': ('Sm', 'sm'),
        'mach_command': 'check-spidermonkey',
        'kwargs': {'valgrind': False},
    },
    'mochitest-a11y': {
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'a11y', 'test_paths': None},
    },
    'mochitest-browser': {
        'aliases': ('bc', 'BC', 'Bc'),
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'browser-chrome', 'test_paths': None},
    },
    'mochitest-chrome': {
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'chrome', 'test_paths': None},
    },
    'mochitest-devtools': {
        'aliases': ('dt', 'DT', 'Dt'),
        'mach_command': 'mochitest',
        'kwargs': {'subsuite': 'devtools', 'test_paths': None},
    },
    'mochitest-plain': {
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'plain', 'test_paths': None},
    },
    'python': {
        'mach_command': 'python-test',
        'kwargs': {'tests': None},
    },
    'reftest': {
        'aliases': ('RR', 'rr', 'Rr'),
        'mach_command': 'reftest',
        'kwargs': {'tests': None},
    },
    'web-platform-tests': {
        'aliases': ('wpt',),
        'mach_command': 'web-platform-tests',
        'kwargs': {}
    },
    'valgrind': {
        'aliases': ('V', 'v'),
        'mach_command': 'valgrind-test',
        'kwargs': {},
    },
    'xpcshell': {
        'aliases': ('X', 'x'),
        'mach_command': 'xpcshell-test',
        'kwargs': {'test_file': 'all'},
    },
}

# Maps test flavors to metadata on how to run that test.
TEST_FLAVORS = {
    'a11y': {
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'a11y', 'test_paths': []},
    },
    'browser-chrome': {
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'browser-chrome', 'test_paths': []},
    },
    'crashtest': {},
    'chrome': {
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'chrome', 'test_paths': []},
    },
    'firefox-ui-functional': {
        'mach_command': 'firefox-ui-functional',
        'kwargs': {'tests': []},
    },
    'firefox-ui-update': {
        'mach_command': 'firefox-ui-update',
        'kwargs': {'tests': []},
    },
    'marionette': {
        'mach_command': 'marionette-test',
        'kwargs': {'tests': []},
    },
    'mochitest': {
        'mach_command': 'mochitest',
        'kwargs': {'flavor': 'mochitest', 'test_paths': []},
    },
    'python': {
        'mach_command': 'python-test',
        'kwargs': {},
    },
    'reftest': {
        'mach_command': 'reftest',
        'kwargs': {'tests': []}
    },
    'steeplechase': {},
    'web-platform-tests': {
        'mach_command': 'web-platform-tests',
        'kwargs': {'include': []}
    },
    'xpcshell': {
        'mach_command': 'xpcshell-test',
        'kwargs': {'test_paths': []},
    },
}

for i in range(1, MOCHITEST_TOTAL_CHUNKS + 1):
    TEST_SUITES['mochitest-%d' %i] = {
        'aliases': ('M%d' % i, 'm%d' % i),
        'mach_command': 'mochitest',
        'kwargs': {
            'flavor': 'mochitest',
            'subsuite': 'default',
            'chunk_by_dir': MOCHITEST_CHUNK_BY_DIR,
            'total_chunks': MOCHITEST_TOTAL_CHUNKS,
            'this_chunk': i,
            'test_paths': None,
        },
    }

TEST_HELP = '''
Test or tests to run. Tests can be specified by filename, directory, suite
name or suite alias.

The following test suites and aliases are supported: %s
''' % ', '.join(sorted(TEST_SUITES))
TEST_HELP = TEST_HELP.strip()


@CommandProvider
class Test(MachCommandBase):
    @Command('test', category='testing', description='Run tests (detects the kind of test and runs it).')
    @CommandArgument('what', default=None, nargs='*', help=TEST_HELP)
    def test(self, what):
        """Run tests from names or paths.

        mach test accepts arguments specifying which tests to run. Each argument
        can be:

        * The path to a test file
        * A directory containing tests
        * A test suite name
        * An alias to a test suite name (codes used on TreeHerder)

        If no input is provided, tests will be run based on files changed in
        the local tree. Relevant tests, tags, or flavors are determined by
        IMPACTED_TESTS annotations in moz.build files relevant to the
        changed files.

        When paths or directories are given, they are first resolved to test
        files known to the build system.

        If resolved tests belong to more than one test type/flavor/harness,
        the harness for each relevant type/flavor will be invoked. e.g. if
        you specify a directory with xpcshell and browser chrome mochitests,
        both harnesses will be invoked.
        """
        from mozbuild.testing import TestResolver

        # Parse arguments and assemble a test "plan."
        run_suites = set()
        run_tests = []
        resolver = self._spawn(TestResolver)

        for entry in what:
            # If the path matches the name or alias of an entire suite, run
            # the entire suite.
            if entry in TEST_SUITES:
                run_suites.add(entry)
                continue
            suitefound = False
            for suite, v in TEST_SUITES.items():
                if entry in v.get('aliases', []):
                    run_suites.add(suite)
                    suitefound = True
            if suitefound:
                continue

            # Now look for file/directory matches in the TestResolver.
            relpath = self._wrap_path_argument(entry).relpath()
            tests = list(resolver.resolve_tests(paths=[relpath]))
            run_tests.extend(tests)

            if not tests:
                print('UNKNOWN TEST: %s' % entry, file=sys.stderr)

        if not what:
            # TODO: This isn't really related to try, and should be
            # extracted to a common library for vcs interactions when it is
            # introduced in bug 1185599.
            from autotry import AutoTry
            at = AutoTry(self.topsrcdir, resolver, self._mach_context)
            changed_files = at.find_changed_files()
            if changed_files:
                print("Tests will be run based on modifications to the "
                      "following files:\n\t%s" % "\n\t".join(changed_files))

            from mozbuild.frontend.reader import (
                BuildReader,
                EmptyConfig,
            )
            config = EmptyConfig(self.topsrcdir)
            reader = BuildReader(config)
            files_info = reader.files_info(changed_files)

            paths, tags, flavors = set(), set(), set()
            for info in files_info.values():
                paths |= info.test_files
                tags |= info.test_tags
                flavors |= info.test_flavors

            # This requires multiple calls to resolve_tests, because the test
            # resolver returns tests that match every condition, while we want
            # tests that match any condition. Bug 1210213 tracks implementing
            # more flexible querying.
            if tags:
                run_tests = list(resolver.resolve_tests(tags=tags))
            if paths:
                run_tests += [t for t in resolver.resolve_tests(paths=paths)
                              if not (tags & set(t.get('tags', '').split()))]
            if flavors:
                run_tests = [t for t in run_tests if t['flavor'] not in flavors]
                for flavor in flavors:
                    run_tests += list(resolver.resolve_tests(flavor=flavor))

        if not run_suites and not run_tests:
            print(UNKNOWN_TEST)
            return 1

        status = None
        for suite_name in run_suites:
            suite = TEST_SUITES[suite_name]

            if 'mach_command' in suite:
                res = self._mach_context.commands.dispatch(
                    suite['mach_command'], self._mach_context,
                    **suite['kwargs'])
                if res:
                    status = res

        buckets = {}
        for test in run_tests:
            key = (test['flavor'], test.get('subsuite', ''))
            buckets.setdefault(key, []).append(test)

        for (flavor, subsuite), tests in sorted(buckets.items()):
            if flavor not in TEST_FLAVORS:
                print(UNKNOWN_FLAVOR % flavor)
                status = 1
                continue

            m = TEST_FLAVORS[flavor]
            if 'mach_command' not in m:
                print(UNKNOWN_FLAVOR % flavor)
                status = 1
                continue

            kwargs = dict(m['kwargs'])
            kwargs['subsuite'] = subsuite

            res = self._mach_context.commands.dispatch(
                    m['mach_command'], self._mach_context,
                    test_objects=tests, **kwargs)
            if res:
                status = res

        return status


@CommandProvider
class MachCommands(MachCommandBase):
    @Command('cppunittest', category='testing',
        description='Run cpp unit tests (C++ tests).')
    @CommandArgument('test_files', nargs='*', metavar='N',
        help='Test to run. Can be specified as one or more files or ' \
            'directories, or omitted. If omitted, the entire test suite is ' \
            'executed.')

    def run_cppunit_test(self, **params):
        import mozinfo
        from mozlog import commandline
        log = commandline.setup_logging("cppunittest",
                                        {},
                                        {"tbpl": sys.stdout})

        # See if we have crash symbols
        symbols_path = os.path.join(self.distdir, 'crashreporter-symbols')
        if not os.path.isdir(symbols_path):
            symbols_path = None

        # If no tests specified, run all tests in main manifest
        tests = params['test_files']
        if len(tests) == 0:
            tests = [os.path.join(self.distdir, 'cppunittests')]
            manifest_path = os.path.join(self.topsrcdir, 'testing', 'cppunittest.ini')
        else:
            manifest_path = None

        if conditions.is_android(self):
            from mozrunner.devices.android_device import verify_android_device
            verify_android_device(self, install=False)
            return self.run_android_test(tests, symbols_path, manifest_path, log)

        return self.run_desktop_test(tests, symbols_path, manifest_path, log)

    def run_desktop_test(self, tests, symbols_path, manifest_path, log):
        import runcppunittests as cppunittests
        from mozlog import commandline

        parser = cppunittests.CPPUnittestOptions()
        commandline.add_logging_group(parser)
        options, args = parser.parse_args()

        options.symbols_path = symbols_path
        options.manifest_path = manifest_path
        options.xre_path = self.bindir

        try:
            result = cppunittests.run_test_harness(options, tests)
        except Exception as e:
            log.error("Caught exception running cpp unit tests: %s" % str(e))
            result = False
            raise

        return 0 if result else 1

    def run_android_test(self, tests, symbols_path, manifest_path, log):
        import remotecppunittests as remotecppunittests
        from mozlog import commandline

        parser = remotecppunittests.RemoteCPPUnittestOptions()
        commandline.add_logging_group(parser)
        options, args = parser.parse_args()

        options.symbols_path = symbols_path
        options.manifest_path = manifest_path
        options.xre_path = self.bindir
        options.dm_trans = "adb"
        options.local_lib = self.bindir.replace('bin', 'fennec')
        for file in os.listdir(os.path.join(self.topobjdir, "dist")):
            if file.endswith(".apk") and file.startswith("fennec"):
                options.local_apk = os.path.join(self.topobjdir, "dist", file)
                log.info("using APK: " + options.local_apk)
                break

        try:
            result = remotecppunittests.run_test_harness(options, tests)
        except Exception as e:
            log.error("Caught exception running cpp unit tests: %s" % str(e))
            result = False
            raise

        return 0 if result else 1

def executable_name(name):
    return name + '.exe' if sys.platform.startswith('win') else name

@CommandProvider
class CheckSpiderMonkeyCommand(MachCommandBase):
    @Command('check-spidermonkey', category='testing', description='Run SpiderMonkey tests (JavaScript engine).')
    @CommandArgument('--valgrind', action='store_true', help='Run jit-test suite with valgrind flag')

    def run_checkspidermonkey(self, **params):
        import subprocess
        import sys

        js = os.path.join(self.bindir, executable_name('js'))

        print('Running jit-tests')
        jittest_cmd = [os.path.join(self.topsrcdir, 'js', 'src', 'jit-test', 'jit_test.py'),
              js, '--no-slow', '--jitflags=all']
        if params['valgrind']:
            jittest_cmd.append('--valgrind')

        jittest_result = subprocess.call(jittest_cmd)

        print('running jstests')
        jstest_cmd = [os.path.join(self.topsrcdir, 'js', 'src', 'tests', 'jstests.py'),
              js, '--jitflags=all']
        jstest_result = subprocess.call(jstest_cmd)

        print('running jsapi-tests')
        jsapi_tests_cmd = [os.path.join(self.bindir, executable_name('jsapi-tests'))]
        jsapi_tests_result = subprocess.call(jsapi_tests_cmd)

        print('running check-style')
        check_style_cmd = [sys.executable, os.path.join(self.topsrcdir, 'config', 'check_spidermonkey_style.py')]
        check_style_result = subprocess.call(check_style_cmd, cwd=os.path.join(self.topsrcdir, 'js', 'src'))

        print('running check-masm')
        check_masm_cmd = [sys.executable, os.path.join(self.topsrcdir, 'config', 'check_macroassembler_style.py')]
        check_masm_result = subprocess.call(check_masm_cmd, cwd=os.path.join(self.topsrcdir, 'js', 'src'))

        print('running check-js-msg-encoding')
        check_js_msg_cmd = [sys.executable, os.path.join(self.topsrcdir, 'config', 'check_js_msg_encoding.py')]
        check_js_msg_result = subprocess.call(check_js_msg_cmd, cwd=self.topsrcdir)

        all_passed = jittest_result and jstest_result and jsapi_tests_result and check_style_result and check_masm_result and check_js_msg_result

        return all_passed

@CommandProvider
class JsapiTestsCommand(MachCommandBase):
    @Command('jsapi-tests', category='testing', description='Run jsapi tests (JavaScript engine).')
    @CommandArgument('test_name', nargs='?', metavar='N',
        help='Test to run. Can be a prefix or omitted. If omitted, the entire ' \
             'test suite is executed.')

    def run_jsapitests(self, **params):
        import subprocess

        bin_suffix = ''
        if sys.platform.startswith('win'):
            bin_suffix = '.exe'

        print('running jsapi-tests')
        jsapi_tests_cmd = [os.path.join(self.bindir, executable_name('jsapi-tests'))]
        if params['test_name']:
            jsapi_tests_cmd.append(params['test_name'])

        jsapi_tests_result = subprocess.call(jsapi_tests_cmd)

        return jsapi_tests_result

def autotry_parser():
    from autotry import arg_parser
    parser = arg_parser()
    # The --no-artifact flag is only interpreted locally by |mach try|; it's not
    # like the --artifact flag, which is interpreted remotely by the try server.
    #
    # We need a tri-state where set is different than the default value, so we
    # use a different variable than --artifact.
    parser.add_argument('--no-artifact',
                        dest='no_artifact',
                        action='store_true',
                        help='Force compiled (non-artifact) builds even when '
                             '--enable-artifact-builds is set.')
    return parser

@CommandProvider
class PushToTry(MachCommandBase):
    def normalise_list(self, items, allow_subitems=False):
        from autotry import parse_arg

        rv = defaultdict(list)
        for item in items:
            parsed = parse_arg(item)
            for key, values in parsed.iteritems():
                rv[key].extend(values)

        if not allow_subitems:
            if not all(item == [] for item in rv.itervalues()):
                raise ValueError("Unexpected subitems in argument")
            return rv.keys()
        else:
            return rv

    def validate_args(self, **kwargs):
        from autotry import AutoTry
        if not kwargs["paths"] and not kwargs["tests"] and not kwargs["tags"]:
            print("Paths, tags, or tests must be specified as an argument to autotry.")
            sys.exit(1)

        if kwargs["platforms"] is None:
            if 'AUTOTRY_PLATFORM_HINT' in os.environ:
                kwargs["platforms"] = [os.environ['AUTOTRY_PLATFORM_HINT']]
            else:
                print("Platforms must be specified as an argument to autotry.")
                sys.exit(1)

        try:
            platforms = self.normalise_list(kwargs["platforms"])
        except ValueError as e:
            print("Error parsing -p argument:\n%s" % e.message)
            sys.exit(1)

        try:
            tests = (self.normalise_list(kwargs["tests"], allow_subitems=True)
                     if kwargs["tests"] else {})
        except ValueError as e:
            print("Error parsing -u argument (%s):\n%s" % (kwargs["tests"], e.message))
            sys.exit(1)

        try:
            talos = (self.normalise_list(kwargs["talos"], allow_subitems=True)
                     if kwargs["talos"] else [])
        except ValueError as e:
            print("Error parsing -t argument:\n%s" % e.message)
            sys.exit(1)

        paths = []
        for p in kwargs["paths"]:
            p = mozpath.normpath(os.path.abspath(p))
            if not (os.path.isdir(p) and p.startswith(self.topsrcdir)):
                print('Specified path "%s" is not a directory under the srcdir,'
                      ' unable to specify tests outside of the srcdir' % p)
                sys.exit(1)
            if len(p) <= len(self.topsrcdir):
                print('Specified path "%s" is at the top of the srcdir and would'
                      ' select all tests.' % p)
                sys.exit(1)
            paths.append(os.path.relpath(p, self.topsrcdir))

        try:
            tags = self.normalise_list(kwargs["tags"]) if kwargs["tags"] else []
        except ValueError as e:
            print("Error parsing --tags argument:\n%s" % e.message)
            sys.exit(1)

        extra_values = {k['dest'] for k in AutoTry.pass_through_arguments.values()}
        extra_args = {k: v for k, v in kwargs.items()
                      if k in extra_values and v}

        return kwargs["builds"], platforms, tests, talos, paths, tags, extra_args


    @Command('try',
             category='testing',
             description='Push selected tests to the try server',
             parser=autotry_parser)

    def autotry(self, **kwargs):
        """Autotry is in beta, please file bugs blocking 1149670.

        Push the current tree to try, with the specified syntax.

        Build options, platforms and regression tests may be selected
        using the usual try options (-b, -p and -u respectively). In
        addition, tests in a given directory may be automatically
        selected by passing that directory as a positional argument to the
        command. For example:

        mach try -b d -p linux64 dom testing/web-platform/tests/dom

        would schedule a try run for linux64 debug consisting of all
        tests under dom/ and testing/web-platform/tests/dom.

        Test selection using positional arguments is available for
        mochitests, reftests, xpcshell tests and web-platform-tests.

        Tests may be also filtered by passing --tag to the command,
        which will run only tests marked as having the specified
        tags e.g.

        mach try -b d -p win64 --tag media

        would run all tests tagged 'media' on Windows 64.

        If both positional arguments or tags and -u are supplied, the
        suites in -u will be run in full. Where tests are selected by
        positional argument they will be run in a single chunk.

        If no build option is selected, both debug and opt will be
        scheduled. If no platform is selected a default is taken from
        the AUTOTRY_PLATFORM_HINT environment variable, if set.

        The command requires either its own mercurial extension ("push-to-try",
        installable from mach mercurial-setup) or a git repo using git-cinnabar
        (available at https://github.com/glandium/git-cinnabar).

        """

        from mozbuild.testing import TestResolver
        from autotry import AutoTry

        print("mach try is under development, please file bugs blocking 1149670.")

        resolver_func = lambda: self._spawn(TestResolver)
        at = AutoTry(self.topsrcdir, resolver_func, self._mach_context)

        if kwargs["list"]:
            at.list_presets()
            sys.exit()

        if kwargs["load"] is not None:
            defaults = at.load_config(kwargs["load"])

            if defaults is None:
                print("No saved configuration called %s found in autotry.ini" % kwargs["load"],
                      file=sys.stderr)

            for key, value in kwargs.iteritems():
                if value in (None, []) and key in defaults:
                    kwargs[key] = defaults[key]

        if kwargs["push"] and at.find_uncommited_changes():
            print('ERROR please commit changes before continuing')
            sys.exit(1)

        if not any(kwargs[item] for item in ("paths", "tests", "tags")):
            kwargs["paths"], kwargs["tags"] = at.find_paths_and_tags(kwargs["verbose"])

        builds, platforms, tests, talos, paths, tags, extra = self.validate_args(**kwargs)

        if paths or tags:
            if not os.path.exists(os.path.join(self.topobjdir, 'config.status')):
                print(CONFIG_ENVIRONMENT_NOT_FOUND)
                sys.exit(1)

            paths = [os.path.relpath(os.path.normpath(os.path.abspath(item)), self.topsrcdir)
                     for item in paths]
            paths_by_flavor = at.paths_by_flavor(paths=paths, tags=tags)

            if not paths_by_flavor and not tests:
                print("No tests were found when attempting to resolve paths:\n\n\t%s" %
                      paths)
                sys.exit(1)

            if not kwargs["intersection"]:
                paths_by_flavor = at.remove_duplicates(paths_by_flavor, tests)
        else:
            paths_by_flavor = {}

        # Add --artifact if --enable-artifact-builds is set ...
        if self.substs.get("MOZ_ARTIFACT_BUILDS"):
            extra["artifact"] = True
        # ... unless --no-artifact is explicitly given.
        if kwargs["no_artifact"]:
            if "artifact" in extra:
                del extra["artifact"]

        try:
            msg = at.calc_try_syntax(platforms, tests, talos, builds, paths_by_flavor, tags,
                                     extra, kwargs["intersection"])
        except ValueError as e:
            print(e.message)
            sys.exit(1)

        if kwargs["verbose"]:
            if self.substs.get("MOZ_ARTIFACT_BUILDS"):
                if kwargs["no_artifact"]:
                    print('mozconfig has --enable-artifact-builds but '
                          '--no-artifact specified, not including --artifact '
                          'flag in try syntax')
                else:
                    print('mozconfig has --enable-artifact-builds; including '
                          '--artifact flag in try syntax (use --no-artifact '
                          'to override)')

        if kwargs["verbose"] and paths_by_flavor:
            print('The following tests will be selected: ')
            for flavor, paths in paths_by_flavor.iteritems():
                print("%s: %s" % (flavor, ",".join(paths)))

        if kwargs["verbose"] or not kwargs["push"]:
            print('The following try syntax was calculated:\n%s' % msg)

        if kwargs["push"]:
            at.push_to_try(msg, kwargs["verbose"])

        if kwargs["save"] is not None:
            at.save_config(kwargs["save"], msg)


def get_parser(argv=None):
    parser = ArgumentParser()
    parser.add_argument(dest="suite_name",
                        nargs=1,
                        choices=['mochitest'],
                        type=str,
                        help="The test for which chunk should be found. It corresponds "
                             "to the mach test invoked (only 'mochitest' currently).")

    parser.add_argument(dest="test_path",
                        nargs=1,
                        type=str,
                        help="The test (any mochitest) for which chunk should be found.")

    parser.add_argument('--total-chunks',
                        type=int,
                        dest='total_chunks',
                        required=True,
                        help='Total number of chunks to split tests into.',
                        default=None)

    parser.add_argument('--chunk-by-runtime',
                        action='store_true',
                        dest='chunk_by_runtime',
                        help='Group tests such that each chunk has roughly the same runtime.',
                        default=False)

    parser.add_argument('--chunk-by-dir',
                        type=int,
                        dest='chunk_by_dir',
                        help='Group tests together in the same chunk that are in the same top '
                             'chunkByDir directories.',
                        default=None)

    parser.add_argument('--disable-e10s',
                        action='store_false',
                        dest='e10s',
                        help='Find test on chunk with electrolysis preferences disabled.',
                        default=True)

    parser.add_argument('-p', '--platform',
                        choices=['linux', 'linux64', 'mac', 'macosx64', 'win32', 'win64'],
                        dest='platform',
                        help="Platform for the chunk to find the test.",
                        default=None)

    parser.add_argument('--debug',
                        action='store_true',
                        dest='debug',
                        help="Find the test on chunk in a debug build.",
                        default=False)

    return parser


def download_mozinfo(platform=None, debug_build=False):
    temp_dir = tempfile.mkdtemp()
    temp_path = os.path.join(temp_dir, "mozinfo.json")
    args = [
        'mozdownload',
        '-t', 'tinderbox',
        '--ext', 'mozinfo.json',
        '-d', temp_path,
    ]
    if platform:
        if platform == 'macosx64':
            platform = 'mac64'
        args.extend(['-p', platform])
    if debug_build:
        args.extend(['--debug-build'])

    subprocess.call(args)
    return temp_dir, temp_path

@CommandProvider
class ChunkFinder(MachCommandBase):
    @Command('find-test-chunk', category='testing',
             description='Find which chunk a test belongs to (works for mochitest).',
             parser=get_parser)
    def chunk_finder(self, **kwargs):
        total_chunks = kwargs['total_chunks']
        test_path = kwargs['test_path'][0]
        suite_name = kwargs['suite_name'][0]
        _, dump_tests = tempfile.mkstemp()

        from mozbuild.testing import TestResolver
        resolver = self._spawn(TestResolver)
        relpath = self._wrap_path_argument(test_path).relpath()
        tests = list(resolver.resolve_tests(paths=[relpath]))
        if len(tests) != 1:
            print('No test found for test_path: %s' % test_path)
            sys.exit(1)

        flavor = tests[0]['flavor']
        subsuite = tests[0]['subsuite']
        args = {
            'totalChunks': total_chunks,
            'dump_tests': dump_tests,
            'chunkByDir': kwargs['chunk_by_dir'],
            'chunkByRuntime': kwargs['chunk_by_runtime'],
            'e10s': kwargs['e10s'],
            'subsuite': subsuite,
        }

        temp_dir = None
        if kwargs['platform'] or kwargs['debug']:
            self._activate_virtualenv()
            self.virtualenv_manager.install_pip_package('mozdownload==1.17')
            temp_dir, temp_path = download_mozinfo(kwargs['platform'], kwargs['debug'])
            args['extra_mozinfo_json'] = temp_path

        found = False
        for this_chunk in range(1, total_chunks+1):
            args['thisChunk'] = this_chunk
            try:
                self._mach_context.commands.dispatch(suite_name, self._mach_context, flavor=flavor, resolve_tests=False, **args)
            except SystemExit:
                pass
            except KeyboardInterrupt:
                break

            fp = open(os.path.expanduser(args['dump_tests']), 'r')
            tests = json.loads(fp.read())['active_tests']
            for test in tests:
                if test_path == test['path']:
                    if 'disabled' in test:
                        print('The test %s for flavor %s is disabled on the given platform' % (test_path, flavor))
                    else:
                        print('The test %s for flavor %s is present in chunk number: %d' % (test_path, flavor, this_chunk))
                    found = True
                    break

            if found:
                break

        if not found:
            raise Exception("Test %s not found." % test_path)
        # Clean up the file
        os.remove(dump_tests)
        if temp_dir:
            shutil.rmtree(temp_dir)


@CommandProvider
class TestInfoCommand(MachCommandBase):
    from datetime import date, timedelta
    @Command('test-info', category='testing',
        description='Display historical test result summary.')
    @CommandArgument('test_name', nargs='?', metavar='N',
        help='Test of interest.')
    @CommandArgument('--branches',
        default='mozilla-central,mozilla-inbound,autoland',
        help='Report for named branches (default: mozilla-central,mozilla-inbound,autoland)')
    @CommandArgument('--start',
        default=(date.today() - timedelta(7)).strftime("%Y-%m-%d"),
        help='Start date (YYYY-MM-DD)')
    @CommandArgument('--end',
        default=date.today().strftime("%Y-%m-%d"),
        help='End date (YYYY-MM-DD)')

    def test_info(self, **params):

        import which
        from mozbuild.base import MozbuildObject

        self.test_name = params['test_name']
        self.branches = params['branches']
        self.start = params['start']
        self.end = params['end']

        if len(self.test_name) < 6:
            print("'%s' is too short for a test name!" % self.test_name)
            return

        here = os.path.abspath(os.path.dirname(__file__))
        build_obj = MozbuildObject.from_environment(cwd=here)

        self._hg = None
        if conditions.is_hg(build_obj):
            if self._is_windows():
                self._hg = which.which('hg.exe')
            else:
                self._hg = which.which('hg')

        self._git = None
        if conditions.is_git(build_obj):
            if self._is_windows():
                self._git = which.which('git.exe')
            else:
                self._git = which.which('git')

        self.set_test_name()
        self.report_test_results()
        self.report_test_durations()
        self.report_bugs()

    def find_in_hg_or_git(self, test_name):
        if self._hg:
            cmd = [self._hg, 'files', '-I', test_name]
        elif self._git:
            cmd = [self._git, 'ls-files', test_name]
        else:
            return None
        try:
            out = subprocess.check_output(cmd).splitlines()
        except subprocess.CalledProcessError:
            out = None
        return out

    def set_test_name(self):
        # Generating a unified report for a specific test is complicated
        # by differences in the test name used in various data sources.
        # Consider:
        #   - It is often convenient to request a report based only on
        #     a short file name, rather than the full path;
        #   - Bugs may be filed in bugzilla against a simple, short test
        #     name or the full path to the test;
        #   - In ActiveData, the full path is usually used, but sometimes
        #     also includes additional path components outside of the
        #     mercurial repo (common for reftests).
        # This function attempts to find appropriate names for different
        # queries based on the specified test name.

        import re

        # full_test_name is full path to file in hg (or git)
        self.full_test_name = None
        out = self.find_in_hg_or_git(self.test_name)
        if out and len(out) == 1:
            self.full_test_name = out[0]
        elif out and len(out) > 1:
            print("Ambiguous test name specified. Found:")
            for line in out:
                print(line)
        else:
            out = self.find_in_hg_or_git('**/%s*' % self.test_name)
            if out and len(out) == 1:
                self.full_test_name = out[0]
            elif out and len(out) > 1:
                print("Ambiguous test name. Found:")
                for line in out:
                    print(line)
        if self.full_test_name:
            print("Found %s in source control." % self.full_test_name)
        else:
            print("Unable to validate test name '%s'!" % self.test_name)
            self.full_test_name = self.test_name

        # short_name is full_test_name without path
        self.short_name = None
        name_idx = self.full_test_name.rfind('/')
        if name_idx > 0:
            self.short_name = self.full_test_name[name_idx+1:]

        # robo_name is short_name without ".java" - for robocop
        self.robo_name = None
        if self.short_name:
            robo_idx = self.short_name.rfind('.java')
            if robo_idx > 0:
                self.robo_name = self.short_name[:robo_idx]
            if self.short_name == self.test_name:
                self.short_name = None

        # activedata_test_name is name in ActiveData
        self.activedata_test_name = None
        simple_names = [
            self.full_test_name,
            self.test_name,
            self.short_name,
            self.robo_name
        ]
        simple_names = [x for x in simple_names if x]
        searches = [
            {"in": {"result.test": simple_names}},
        ]
        regex_names = [".*%s.*" % re.escape(x) for x in simple_names if x]
        for r in regex_names:
            searches.append({"regexp": {"result.test": r}})
        query = {
            "from": "unittest",
            "format": "list",
            "limit": 10,
            "groupby": ["result.test"],
            "where": {"and": [
                {"or": searches},
                {"in": {"build.branch": self.branches.split(',')}},
                {"gt": {"run.timestamp": {"date": self.start}}},
                {"lt": {"run.timestamp": {"date": self.end}}}
            ]}
        }
        print("Querying ActiveData...") # Following query can take a long time
        data = self.submit(query)
        if data and len(data) > 0:
            self.activedata_test_name = [
                d['result']['test']
                for p in simple_names + regex_names
                for d in data
                if re.match(p+"$", d['result']['test'])
            ][0]  # first match is best match
        if self.activedata_test_name:
            print("Found records matching '%s' in ActiveData." %
                self.activedata_test_name)
        else:
            print("Unable to find matching records in ActiveData; using %s!" %
                self.test_name)
            self.activedata_test_name = self.test_name

    def get_platform(self, record):
        platform = record['build']['platform']
        type = record['build']['type']
        e10s = "-%s" % record['run']['type'] if 'run' in record else ""
        return "%s/%s%s:" % (platform, type, e10s)

    def submit(self, query):
        import requests
        response = requests.post("http://activedata.allizom.org/query",
                                 data=json.dumps(query),
                                 stream=True)
        response.raise_for_status()
        data = response.json()["data"]
        return data

    def report_test_results(self):
        # Report test pass/fail summary from ActiveData
        query = {
            "from": "unittest",
            "format": "list",
            "limit": 100,
            "groupby": ["build.platform", "build.type", "run.type"],
            "select": [
                {"aggregate": "count"},
                {
                    "name": "failures",
                    "value": {"case": [
                        {"when": {"eq": {"result.ok": "F"}}, "then": 1}
                    ]},
                    "aggregate": "sum",
                    "default": 0
                }
            ],
            "where": {"and": [
                {"eq": {"result.test": self.activedata_test_name}},
                {"in": {"build.branch": self.branches.split(',')}},
                {"gt": {"run.timestamp": {"date": self.start}}},
                {"lt": {"run.timestamp": {"date": self.end}}}
            ]}
        }
        print("\nTest results for %s on %s between %s and %s" %
            (self.activedata_test_name, self.branches, self.start, self.end))
        data = self.submit(query)
        if data and len(data) > 0:
            data.sort(key=self.get_platform)
            for record in data:
                platform = self.get_platform(record)
                runs = record['count']
                failures = record['failures']
                print("%-30s %6d failures in %6d runs" % (
                    platform, failures, runs))
        else:
            print("No test result data found.")

    def report_test_durations(self):
        # Report test durations summary from ActiveData
        query = {
	    "from": "unittest",
            "format": "list",
	    "limit": 100,
	    "groupby": ["build.platform","build.type","run.type"],
	    "select": [
		{"value":"result.duration","aggregate":"average","name":"average"},
		{"value":"result.duration","aggregate":"min","name":"min"},
		{"value":"result.duration","aggregate":"max","name":"max"},
		{"aggregate":"count"}
	    ],
	    "where": {"and": [
                {"eq": {"result.ok": "T"}},
		{"eq": {"result.test": self.activedata_test_name}},
                {"in": {"build.branch": self.branches.split(',')}},
                {"gt": {"run.timestamp": {"date": self.start}}},
                {"lt": {"run.timestamp": {"date": self.end}}}
	    ]}
        }
        data = self.submit(query)
        print("\nTest durations for %s on %s between %s and %s" %
            (self.activedata_test_name, self.branches, self.start, self.end))
        if data and len(data) > 0:
            data.sort(key=self.get_platform)
            for record in data:
                platform = self.get_platform(record)
                print("%-30s %6.2f s (%.2f s - %.2f s over %d runs)" % (
                    platform, record['average'], record['min'],
                    record['max'], record['count']))
        else:
            print("No test durations found.")

    def report_bugs(self):
        # Report open bugs matching test name
        import requests
        search = self.full_test_name
        if self.test_name:
            search = '%s,%s' % (search, self.test_name)
        if self.short_name:
            search = '%s,%s' % (search, self.short_name)
        if self.robo_name:
            search = '%s,%s' % (search, self.robo_name)
        payload = {'quicksearch': search,
                   'include_fields':'id,summary'}
        response = requests.get('https://bugzilla.mozilla.org/rest/bug',
                                payload)
        response.raise_for_status()
        json_response = response.json()
        print("\nBugzilla quick search for '%s':" % search)
        if 'bugs' in json_response:
            for bug in json_response['bugs']:
                print("Bug %s: %s" % (bug['id'], bug['summary']))
        else:
            print("No bugs found.")
