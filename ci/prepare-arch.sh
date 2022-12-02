#!/bin/bash
# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

# Enable multilib repository
echo -e "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist" >> /etc/pacman.conf

pacman -Syu --needed --noconfirm --noprogressbar \
base-devel \
git \
lib32-glibc \
lib32-mesa \
python \
python-chardet \
python-six \
python-tappy \
sudo \
"$@"
