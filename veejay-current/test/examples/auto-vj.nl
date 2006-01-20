
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

for(my $k =0; $k < 3; $k++)
{
	for(my $j = 0; $j < 25; $j ++)
	{
		# 1 frame delay before sending message
		print "+1\n";
		# preset effect 102 (multi mirrors)
		# somewhere on the current playing clip/stream
		# with sequenced parameters 0..3 0..25 
		print "361:0 -1 102 $k $j;\n";
	
	}
}

for(my $i = 0 ; $i < 100; $i ++)
{
	$max = int(rand(75));
	$delay = int(rand(25 + max));
	print "+$delay\n";


	$type = int(rand( 3 ));
	$count =int(rand( 10 ));
	print "361:0 -1 102 $type $count;\n"
}
