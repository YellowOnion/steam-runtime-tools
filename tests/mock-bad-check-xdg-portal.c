/*
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>

int
main (int argc,
      char **argv)
{
  // Give bad output
  fprintf (stderr, "The 'version' property is not available for 'org.freedesktop.portal.OpenURI', either there isn't a working xdg-desktop-portal or it is a very old version\n");

  printf ("{\n"
          "\t\"interfaces\" : {\n"
          "\t\t\"org.freedesktop.portal.OpenURI\" : {\n"
          "\t\t\t\"available\" : false\n"
          "\t\t},\n"
          "\t\t\"org.freedesktop.portal.Email\" : {\n"
          "\t\t\t\"available\" : true,\n"
          "\t\t\t\"version\" : 3\n"
          "\t\t}\n"
          "\t},\n"
          "\t\"backends\" : {\n"
          "\t\t\"org.freedesktop.impl.portal.desktop.gtk\" : {\n"
          "\t\t\t\"available\" : true\n"
          "\t\t},\n"
          "\t\t\"org.freedesktop.impl.portal.desktop.kde\" : {\n"
          "\t\t\t\"available\" : false\n"
          "\t\t}\n"
          "\t}\n"
          "}\n");

  return 1;
}

