#!app://shell.pxe
title --optional "Baseline"
mount --partition 1 --volume bench --read-write data://
mount --optional --read-write host
namespace create
service start text app://textfs.pxe
session app://session.pxe --configure-network --start-remote-services
