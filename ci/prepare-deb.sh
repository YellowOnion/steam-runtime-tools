#!/bin/sh
# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

echo "#!/bin/sh" > /usr/sbin/policy-rc.d
echo "exit 101" >> /usr/sbin/policy-rc.d
chmod +x /usr/sbin/policy-rc.d
echo "Acquire::Languages \"none\";" > /etc/apt/apt.conf.d/90nolanguages
echo 'force-unsafe-io' > /etc/dpkg/dpkg.cfg.d/autopkgtest

case "$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")" in
    (heavy)
        if [ -n "${HEAVY_APT_SOURCES_FILE-}" ]; then
            cp "${HEAVY_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        ;;

    (soldier)
        if [ -n "${SOLDIER_APT_SOURCES_FILE-}" ]; then
            cp "${SOLDIER_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        ;;

    (sniper)
        if [ -n "${SNIPER_APT_SOURCES_FILE-}" ]; then
            cp "${SNIPER_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        ;;

    (medic)
        if [ -n "${MEDIC_APT_SOURCES_FILE-}" ]; then
            cp "${MEDIC_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi
        ;;

    (*scout*)
        if [ -n "${SCOUT_APT_SOURCES_FILE-}" ]; then
            cp "${SCOUT_APT_SOURCES_FILE}" /etc/apt/sources.list
        fi

        # Go back to Ubuntu 12.04's default gcc/g++/cpp
        update-alternatives --set g++ /usr/bin/g++-4.6
        update-alternatives --set gcc /usr/bin/gcc-4.6
        update-alternatives --set cpp-bin /usr/bin/cpp-4.6
        ;;
esac

apt-get -y update

apt-get install -y --no-install-recommends \
    ca-certificates \
    git \
    "$@"
# Optional
apt-get install -y --no-install-recommends eatmydata || :
dbus-uuidgen --ensure || :
