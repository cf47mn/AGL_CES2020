#!/bin/bash

watch_file=/var/local/lib/containers/GUEST_IVI/var/local/lib/afm/applications/bomb-ic/0.1/runxdg.toml

while inotifywait -e OPEN $watch_file; do
	sync
	/usr/bin/lxc-attach -n GUEST_IC -- halt -f
done
