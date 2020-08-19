# Copyright © 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

pandoc = find_program('pandoc', required : true)

if run_command(pandoc, [
  '-f', 'markdown-smart',
  '-t', 'man',
  '/dev/null',
]).returncode() == 0
  pandoc_markdown_nosmart = 'markdown-smart'
else
  pandoc_markdown_nosmart = 'markdown'
endif

foreach tool : [
  'adverb',
  'launch',
  'launcher',
  'locale-gen',
  'test-ui',
  'try-setlocale',
  'unruntime',
  'wrap',
]
  custom_target(
    tool + '.1',
    build_by_default : true,
    command : [
      pandoc,
      '-s',
      '-o', '@OUTPUT@',
      '-f', pandoc_markdown_nosmart,
      '-t', 'man',
      '@INPUT@',
    ],
    input : tool + '.1.md',
    output : 'pressure-vessel-' + tool + '.1',
    install : true,
    install_dir : join_paths(
      get_option('prefix'),
      get_option('mandir'),
      'man1',
    ),
  )
endforeach

# vim:set sw=2 sts=2 et:
