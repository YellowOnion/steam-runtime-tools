Container based Steam Runtime
=============================

This experimental container-based release of the 'scout' Steam Runtime is enabled on a per-title basis by forcing it's use in the title's Properties dialog.

Several environment variables can be used for debug and diagnostics purposes:

STEAM_RUNTIME_CONTAINER_ALWAYS=1 will enable the container runtime on all titles.

The scripts in pressure-vessel/bin/ support more environment variables to control the behavior.

In particular, setting PRESSURE_VESSEL_WRAP_GUI will enable a test mode interface with more options.
