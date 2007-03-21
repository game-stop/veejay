
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30


print "177:;\n";

for ($k=60; $k < 255; $k+=4)
{
print "361:0 4 146 $k 60 60;\n";
print "+1\n";
}

for ($k=60; $k < 255; $k+=4)
{
print "361:0 4 146 60 $k 60;\n";
print "+1\n";
}


for ($k=60; $k < 255; $k+=4)
{
print "361:0 4 146 60 60 $k;\n";
print "+1\n";
}

