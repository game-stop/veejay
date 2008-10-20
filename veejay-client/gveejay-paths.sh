#!/bin/sh -e

prefix="/usr/local"
datadir="${prefix}/share"

DATADIRNAME="@DATADIRNAME@"

gveejay_datadir="${datarootdir}/gveejay"

cat << EOF
/*
	This file has been automatically generated. Do not edit 

*/
#ifndef GVEEJAY_PATHS_H
#define GVEEJAY_PATHS_H

#define GVEEJAY_DATADIR "$gveejay_datadir"

#endif /* GVEEJAY_PATHS_H */
EOF

