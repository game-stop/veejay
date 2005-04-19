
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

my $p1_end = 90;
my $p1_start = 0;
my $p2_start = 0;
my $p2_end = 20;
my $p3_start = 0;
my $p3_end = 200;
my $p4_start = 0;
my $p4_end = 40;

my $j=0;
my $k=0;
my $l=0;

print "355:;\n";


for (my $i = 2; $i < 25; $i+=2)
{
print "361:0 3 154 $i;\n";
print "+10;\n";
}

for (my $i = 2; $i < 25; $i+=2)
{
print "361:0 3 154 -$i;\n";
print "+10;\n";
}


print "+200;\n";


print "361:0 3 154 -100;\n";

print "+200;\n";

print "361:0 3 154 40;\n";

print "+200;\n";

for(my $r=0; $r < 5; $r++)
{
for(my $i=$p1_start; $i < $p1_end; $i+=2)
{
	$l++;
	$k++;
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 1 151 $i $j $k $l;\n";
}
for(my $i=$p1_end; $i != $p1_start; $i-=2)
{
	$l--;
	$k--;
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 1 151 $i $j $k $l;\n";
}



}




# smear:
print "361:0 5 152 0 31;\n";


print "+100;\n";

for(my $r=0; $r < 5; $r++)
{
for(my $i=$p1_start; $i < $p1_end; $i+=2)
{
	$l++;
	$k++;
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 1 151 $i $j $k $l;\n";
}
for(my $i=$p1_end; $i != $p1_start; $i-=2)
{
	$l--;
	$k--;
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 1 151 $i $j $k $l;\n";
}



}

