
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

my $len = 100;

print "177:0;\n";
print "176:0;\n";
print "182:0 1 142 0 62 1;\n";
print "178:0 100;\n";
print "175:0;\n";
print "179:0 100;\n";
print "+200\n";

print "177:0;\n";
print "175:0;\n";

for(my $i=0; $i < 255; $i++)
{
print "182:0 1 204 $i;\n";
print "+1\n";
}

