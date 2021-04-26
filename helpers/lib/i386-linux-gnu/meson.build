# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

shared_module(
  'identify-lib',
  '../../identify-lib.c',
  c_args : ['-D_SRT_LIB_VALUE="lib/i386-linux-gnu"',],
  install : true,
  install_dir : join_paths(
    pkglibexecdir,
    multiarch,
    'lib/i386-linux-gnu',
  )
)
