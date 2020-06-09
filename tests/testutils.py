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

    def setUp(self) -> None:
        self.G_TEST_SRCDIR = os.getenv(
            'G_TEST_SRCDIR',
            os.path.abspath(os.path.dirname(__file__)),
        )
        self.top_srcdir = os.path.dirname(self.G_TEST_SRCDIR)
        self.G_TEST_BUILDDIR = os.getenv(
            'G_TEST_BUILDDIR',
            os.path.abspath(
                os.path.join(os.path.dirname(__file__), '..', '_build'),
            ),
        )
        self.top_builddir = os.path.dirname(self.G_TEST_BUILDDIR)

        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)

        artifacts = os.getenv('AUTOPKGTEST_ARTIFACTS')

        if artifacts is not None:
            self.artifacts = os.path.abspath(artifacts)
        else:
            self.artifacts = self.tmpdir.name

    def tearDown(self) -> None:
        pass


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
