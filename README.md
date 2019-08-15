Update workflow:

- Download latest pressure-vessel from http://jenkins.internal.steamos.cloud/job/scout/job/pressure-vessel/
- Extract the pressure-vessel archive
- Create a symlink pressure-vessel -> pressure-vessel-<version>
- Download latest staging beta runtime:
  https://images.internal.steamos.cloud/steamrt-scout/snapshots/latest-staging-beta/com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz
- Download latest staging beta sdk:
  https://images.internal.steamos.cloud/steamrt-scout/snapshots/latest-staging-beta/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-runtime.tar.gz
- Create a scout directory
- Extract com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz into it
- Run steamcmd
- Login to an account with access to SteamLinuxRuntime (appid 1070560)
- run_app_build <abspath>/steampipe/app_build_1070560.vdf
- deploy build on Steamworks partner site
