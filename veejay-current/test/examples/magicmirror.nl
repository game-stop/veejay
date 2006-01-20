
# show multi mirrors, 
# type 0,1 = vertical, 1,2 = horizontal
# count is meaningfull below ~30

my $p1_end = 80;
my $p1_start = 0;
my $p2_start = 0;
my $p2_end = 40;
my $p3_start = 0;
my $p3_end = 200;
my $p4_start = 0;
my $p4_end = 40;

my $j=0;
my $k=0;
my $l=0;
for(my $bla = 0; $bla < 50; $bla ++ )
{

for(my $i=$p1_start; $i < $p1_end; $i++)
{
	#$$l++;
#	$j++;
#	$k++;
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 -1 151 $i $j $i $l;\n";
}
for(my $i=$p1_end; $i != $p1_start; $i--)
{
#	$l--;
#	$j--;
#	$k--;
	# 1 frame delay before sending message
	print "+1\n";
	# preset effect 102 (multi mirrors)
	# somewhere on the current playing clip/stream
	# with sequenced parameters 0..3 0..25 
	print "361:0 -1 151 $i $j $i $l;\n";
}

}
