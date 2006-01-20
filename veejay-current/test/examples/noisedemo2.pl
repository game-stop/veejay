
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

my $len = 50;

print "355:;\n";

print "361:0 1 207 9 60;\n";

print "+$len\n";

print "361:0 2 106 255;\n";

print "+$len\n";

print "361:0 3 205 255 0 100 352 0;\n";

print "+$len\n";

print "361:0 4 134 0 1568;\n";

print "+$len\n";

for(my $i=1; $i < 50; $i++)
{
	print "+2\n";
	print "361:0 3 141 $len 1 0;\n";
}

print "+1\n";

