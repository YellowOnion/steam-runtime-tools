all: scout

scout:
	./populate-depot.py --ssh --depot=depot scout='{"version": "latest-steam-client-main-branch"}'

soldier:
	./populate-depot.py --ssh --depot=depot scout='{"version": "latest-steam-client-main-branch"}' soldier

clean:
	rm -fr depot/pressure-vessel depot/com.valvesoftware.SteamRuntime.*

check:
	prove -v tests/*.sh
