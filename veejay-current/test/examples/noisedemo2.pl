
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

my $len = 50;

print "177:;\n";

print "182:0 1 207 9 60;\n";

print "+$len\n";

print "182:0 2 106 255;\n";

print "+$len\n";

print "182:0 3 205 255 0 100 352 0;\n";

print "+$len\n";

print "182:0 4 134 0 1568;\n";

print "+$len\n";

print "193:0 3;\n";
print "193:0 4;\n";

for(my $i=1; $i < 50; $i++)
{
	print "+2\n";
	print "182:0 3 141 $len 1 0;\n";
}

print "+1\n";

