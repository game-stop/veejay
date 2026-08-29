#!/bin/sh -e
reloaded_datadir="${prefix}/share/reloaded"

cat << EOF
/*
	This file has been automatically generated. Do not edit 

*/
#ifndef GVEEJAY_PATHS_H
#define GVEEJAY_PATHS_H

#define RELOADED_DATADIR "$reloaded_datadir"

#endif /* GVEEJAY_PATHS_H */
EOF

