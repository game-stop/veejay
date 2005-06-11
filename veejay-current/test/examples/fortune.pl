
print "310:/usr/local/lib/libvj_drawtext_plugin.so;\n";
print "+10\n";
for ( my $m = 0; $m < 200 ; $m ++ )
{

	my @text = `/usr/games/fortune|tr -d [:cntrl:]`;
	my $lines = join "\n", @text;
	my $size = 12 + int(rand(15));	
	my $y    =  10 + int(rand(400));
	$lines =~ s/\n/\" \"/g;
	$lines =~ s/":"//;
	$lines =~ s/"="//;
	$lines =~ s/";"//;

	print "312:DrawText::text=$lines:size=$size:x=2:y=$y:rand=41;\n";

	

	print "+200\n";

}
