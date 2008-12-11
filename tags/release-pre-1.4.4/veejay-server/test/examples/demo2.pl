
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

for(my $k =0; $k < 2; $k++)
{
	for(my $j = 0; $j < 20; $j ++)
	{
		# 1 frame delay before sending message
		print "+1\n";
		# preset effect 102 (multi mirrors)
		# somewhere on the current playing clip/stream
		# with sequenced parameters 0..3 0..25 
		print "361:0 -1 102 $k $j;\n";
	
	}
}
for(my $k =1; $k != 0; $k--)
{
	for(my $j = 20; $j != 0; $j --)
	{
		# 1 frame delay before sending message
		print "+1\n";
		# preset effect 102 (multi mirrors)
		# somewhere on the current playing clip/stream
		# with sequenced parameters 0..3 0..25 
		print "361:0 -1 102 $k $j;\n";
	
	}
}

print "177:;\n";

