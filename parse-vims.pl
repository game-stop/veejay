#!/usr/bin/env perl

# feed this script with vims.h to generate selectors.h

print "// selector.h\n";
print "// generated from vims.h\n";
while (<>){
    if (m/NET_(\S+)\s*=\s*(\d+)\s*,/) {
	$id = $2;        # numeric id
	$tag = lc $1;    # convert to lower case
	$tag =~ s/_/\./g; # pd-ify
	print "SELECTOR(\"" . $tag . "\", " . $id . ");\n";
    }
}
