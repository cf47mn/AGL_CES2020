#!/bin/sh

cp -f /home/0/wireplumber.conf /var/local/lib/containers/GUEST_IVI/etc/wireplumber/
chsmack -a _ /var/local/lib/containers/GUEST_IVI/etc/wireplumber/wireplumber.conf
