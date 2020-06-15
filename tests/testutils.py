# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import logging
import os
import subprocess
import sys
import tempfile
import unittest

try:
    import typing
except ImportError:
    pass
else:
    typing      # placate pyflakes


class MyCompletedProcess:
    """
    A minimal reimplementation of subprocess.CompletedProcess from
    Python 3.5+, so that some tests can be run on the Python 3.4
    interpreter in Debian 8 'jessie', SteamOS 2 'brewmaster' and
    Ubuntu 14.04 'trusty'.
    """

    def __init__(
        self,
        args='',                # type: typing.Union[typing.List[str], str]
        returncode=-1,          # type: int
        stdout=None,            # type: typing.Optional[bytes]
        stderr=None             # type: typing.Optional[bytes]
    ) -> None:
        self.args = args
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def check_returncode(self) -> None:
        if self.returncode != 0:
            raise subprocess.CalledProcessError(
                self.returncode,
                str(self.args),
                output=self.stdout,
            )


def run_subprocess(
    args,           # type: typing.Union[typing.List[str], str]
    check=False,
    input=None,     # type: typing.Optional[bytes]
    timeout=None,   # type: typing.Optional[int]
    **kwargs        # type: typing.Any
):
    """
    This is basically a reimplementation of subprocess.run()
    from Python 3.5+, so that some tests can be run on the Python
    3.4 interpreter in Debian 8 'jessie', SteamOS 2 'brewmaster'
    and Ubuntu 14.04 'trusty'.
    """

    popen = subprocess.Popen(args, **kwargs)    # type: ignore
    out, err = popen.communicate(input=input, timeout=timeout)
    completed = MyCompletedProcess(
        args=args,
        returncode=popen.returncode,
        stdout=out,
        stderr=err,
    )

    if check:
        completed.check_returncode()

    return completed


class BaseTest(unittest.TestCase):
    """
    Base class with some useful test setup.
    """

    G_TEST_BUILDDIR = ''
    G_TEST_SRCDIR = ''
    artifacts = ''
    tmpdir = None       # type: tempfile.TemporaryDirectory
    top_builddir = ''
    top_srcdir = ''

    @classmethod
    def setUpClass(cls) -> None:
        cls.G_TEST_SRCDIR = os.getenv(
            'G_TEST_SRCDIR',
            os.path.abspath(os.path.dirname(__file__)),
        )
        cls.top_srcdir = os.path.dirname(cls.G_TEST_SRCDIR)
        cls.G_TEST_BUILDDIR = os.getenv(
            'G_TEST_BUILDDIR',
            os.path.abspath(
                os.path.join(os.path.dirname(__file__), '..', '_build'),
            ),
        )
        cls.top_builddir = os.path.dirname(cls.G_TEST_BUILDDIR)

        cls.tmpdir = tempfile.TemporaryDirectory()

        artifacts = os.getenv('AUTOPKGTEST_ARTIFACTS')

        if artifacts is not None:
            cls.artifacts = os.path.abspath(artifacts)
        else:
            cls.artifacts = cls.tmpdir.name

    def setUp(self) -> None:
        cls = self.__class__
        self.G_TEST_BUILDDIR = cls.G_TEST_BUILDDIR
        self.G_TEST_SRCDIR = cls.G_TEST_SRCDIR
        self.artifacts = cls.artifacts
        self.top_builddir = cls.top_builddir
        self.top_srcdir = cls.top_srcdir

        # Class and each test get separate temp directories
        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)

    def tearDown(self) -> None:
        pass

    @classmethod
    def tearDownClass(cls) -> None:
        cls.tmpdir.cleanup()


def tee_file_and_stderr(path: str) -> subprocess.Popen:
    """
    Return a context manager with a stdin attribute.
    Anything written to its stdin will be written to `path`
    and also to stderr.
    """
    return subprocess.Popen(
        ['tee', '--', path],
        stdin=subprocess.PIPE,
        stdout=2,
        stderr=2,
    )


def test_main():
    logging.basicConfig(level=logging.DEBUG)

    try:
        from tap.runner import TAPTestRunner
    except ImportError:
        TAPTestRunner = None    # type: ignore

    if TAPTestRunner is not None:
        runner = TAPTestRunner()
        runner.set_stream(True)
        unittest.main(testRunner=runner)
    else:
        print('1..1')
        program = unittest.main(exit=False)
        if program.result.wasSuccessful():
            print(
                'ok 1 - %r (tap module not available)'
                % program.result
            )
        else:
            print(
                'not ok 1 - %r (tap module not available)'
                % program.result
            )
            sys.exit(1)

# vi: set sw=4 sts=4 et:
