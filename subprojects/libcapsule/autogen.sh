#!/bin/sh
# Copyright © 2017-2018 Collabora Ltd
# SPDX-License-Identifier: LGPL-2.1-or-later

set -e

# Run this to generate all the initial makefiles, etc.

[ -n "$srcdir" ] || srcdir=$(dirname "$0");
[ -n "$srcdir" ] || srcdir=.;

( cd $srcdir

  if [ "$#" = 0 -a "x$NOCONFIGURE" = "x" ];
  then
      (echo "*** WARNING: Running 'configure' with no arguments.";
       echo "*** configure arguments should be passed as:"       ;
       echo "*** $0 ARGS..."                                     ;
       echo ""                                                   ) >&2;
  fi;
  aclocal -I m4 --install || exit $?;
  gtkdocize               || exit $?;
  autoreconf -ivf         || exit $?; );

if [ "$NOCONFIGURE" = "" ];
then
    $srcdir/configure "$@" || exit $?;

    if [ "$1" = "--help" ]; then exit 0; fi;
    echo "Now type 'make' to compile";
else
    echo "Skipping configure process.";
fi
