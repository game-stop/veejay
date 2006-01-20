
my $w = 0;
my $h = 0;
for(my $j = 0; $j != 360 ; $j ++)
{
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# 0 on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 2 155 $j;\n";
	print "+1\n";
	print "361:0 4 142 $w $h 1;\n";
	print "+1\n";
	for(my $k = 2; $k < 8; $k++) {
	print "361:0 6 141 $k 1;\n";
	print "+1\n";
	}
	for(my $k = 8; $k > 2 ; $k--) {
	print "361:0 6 141 $k 1;\n";
	print "+1\n";
	}


}
