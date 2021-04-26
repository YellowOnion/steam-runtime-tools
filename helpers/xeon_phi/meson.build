# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

shared_module(
  'identify-platform',
  '../identify-platform.c',
  c_args : ['-D_SRT_PLATFORM_VALUE="xeon_phi"',],
  install : true,
  install_dir : join_paths(
    pkglibexecdir,
    multiarch,
    'xeon_phi',
  )
)
