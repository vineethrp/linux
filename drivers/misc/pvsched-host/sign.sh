#!/bin/bash

KERN_SRC="$HOME/WS/linux/linux-kvm"
MODULE=pvsched-host.ko

$KERN_SRC/out/scripts/sign-file sha512 $KERN_SRC/out/certs/signing_key.pem $KERN_SRC/out/certs/signing_key.x509 $MODULE
