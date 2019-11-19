all:
	@if [ -e files ]; then \
		echo "Not overwriting existing scout_sdk/files directory"; \
		exit 1; \
	fi
	tar -zxvf ../com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-runtime.tar.gz

clean:
	@if [ "x$(REALLY_DELETE_SDK)" != xyes ]; then \
		echo "Run as: make clean REALLY_DELETE_SDK=yes"; \
		exit 1; \
	fi
	rm -fr files metadata
