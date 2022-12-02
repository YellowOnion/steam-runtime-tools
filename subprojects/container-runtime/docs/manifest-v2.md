Steam client - manifest.vdf v2 compat tools
===========================================

manifest.vdf keys:
------------------

  "version" "1":
  --------------

"commandline"
"commandline_getcompatpath"
"commandline_waitforexitandrun"

Still supported for backwards compatibility.

  "version" "2":
  --------------

"commandline"
"require_tool_appid"
"use_sessions"

"commandline" supports the expansion of a "%verb%" keyword.

  examples:
  ---------

Proton 5.13 with soldier SLR:

"manifest"
{
  "version" "2"
  "commandline" "/proton %verb%"
  // Require the soldier SLR tool
  "require_tool_appid" "1391110"
  // Enable session mode
  "use_sessions" "1"
}

soldier SLR (1391110):

"manifest"
{
  "version" "2"
  "commandline" "/run %verb%"
}

Environment:
------------

  Unchanged variables:
  --------------------

STEAM_COMPAT_DATA_PATH
STEAM_COMPAT_CONFIG
STEAM_COMPAT_CLIENT_INSTALL_PATH

  New variables:
  --------------

STEAM_COMPAT_APP_ID

STEAM_COMPAT_SESSION_ID
Only set when session mode is requested. Otherwise execute a single command, no session.

STEAM_COMPAT_APP_LIBRARY_PATH

STEAM_COMPAT_TOOL_PATHS
':'-separated list of compat tool paths and other paths to be mounted in the container

Workflow:
---------

Create a session whenever a new STEAM_COMPAT_SESSION_ID comes through. We do not plan to add an "open session" command.

Close the session after a "waitforexitandrun" verb. We plan to replace this with a "close session" command at some point,
as it will not support running multiple apps in the same session. But it will be adequate for now.

Any run with a compat tool can be forced to use session mode by setting STEAM_COMPAT_FORCE_SESSIONS=1
