Container based Steam Runtime
=============================

This experimental container-based release of the 'scout' Steam Runtime is enabled on a per-title basis by forcing it's use in the title's Properties dialog.

The Steam Client will understand the following environment variables:

STEAM_RUNTIME_CONTAINER_TEST=1 will spawn a 'test mode' dialog allowing you to select a different runtime and set various options.
STEAM_RUNTIME_CONTAINER_ALL=1 will force the container runtime on all titles.
