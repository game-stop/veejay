
for(my $j = 0; $j != 360 ; $j ++)
{
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# 0 on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "182:0 0 155 $j;\n";
}
