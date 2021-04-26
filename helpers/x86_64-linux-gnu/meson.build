# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

shared_module(
  'identify-lib',
  '../identify-lib.c',
  c_args : ['-D_SRT_LIB_VALUE="x86_64-linux-gnu"',],
  install : true,
  install_dir : join_paths(
    pkglibexecdir,
    multiarch,
    'x86_64-linux-gnu',
  )
)
