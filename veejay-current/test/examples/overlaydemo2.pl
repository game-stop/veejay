
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

my $len = 50;

print "355:;\n";


for(my $mode =0; $mode < 31; $mode++)
{
	print "361:0 1 201 $mode;\n";
	print "+$len\n";
}


for(my $mode=0; $mode < 23; $mode++)
{
for(my $val = 0; $val < 255; $val+=8)
{
print "361:0 1 207 $mode $val;\n";
}
for(my $val = 255; $val > 1; $val-=8)
{
print "361:0 1 207 $mode $val;\n";
}
}


for(my $mode=0; $mode < 31; $mode++)
{
for(my $val = 0; $val < 100; $val+=4)
{
print "361:0 1 202 $mode $val;\n";
}
for(my $val = 100; $val > 1; $val-=4)
{
print "361:0 1 202 $mode $val;\n";
}
}


