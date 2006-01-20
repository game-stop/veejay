
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

for(my $j = 0; $j < 25; $j ++)
{
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 -1 150 $j;\n";
}


for(my $j = 25; $j != 0 ; $j --)
{
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 -1 150 $j;\n";
}
