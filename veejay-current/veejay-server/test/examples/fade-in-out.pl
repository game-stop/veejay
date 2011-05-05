
my $max_samples = 4;
my $rand_sample = int(rand($max_samples)) + 1;
my $duration = int(rand(250)) + 1;

# create colored stream (black). 
print "242:0 0 0;\n";
# guess it has ID 1
my $black_sample = 1;

# select something and play it (in sample play mode)
print "101:$rand_sample;\n";				

while(1) {
	print "+$duration\n";							# play some frames
	
	# prepare to fade to black		
	print "367:$rand_sample 19 1;\n";		        # mix entry 19 with source type 'stream' 
	print "366:$rand_sample 19 $black_sample;\n";   # mix entry 19 with stream 1

	#start transition
	for ( $i = 0; $i < 255; $i ++ ) {               # fade to black in 255 frames
		my $opacity = $i;                           
		print "361:$rand_sample 19 204 $opacity;\n";# use normal overlay and fade to stream 1
		print "+1\n";								# 1 frame wait
	}									  			
	
	# end transition
	print "+$duration\n";							# screen is black now
	my $previous_sample = $rand_sample;				# save
	# next loop
	$rand_sample = int(rand($max_samples)) + 1;	
	
	# prepare to make transition from black
	print "367:$rand_sample 19 1;\n";				# source type 
	print "366:$rand_sample 19 $black_sample;\n";	# mixing with stream 1
	print "361:$rand_sample 19 204 255;\n";			# mixing stream fully visible
	print "101:$rand_sample;\n";					# play now (black)
	print "355:$previous_sample;\n";				# reset chain of previous sample
	for( $i = 254; $i > 0; $i -- ) {				# fade from black in 255 frames
		my $opacity = $i;
		print "361:$rand_sample 19 204 $opacity;\n";# 
		print "+1\n";
	}
	# end transition
	
	print "355:$rand_sample;\n";					# reset chain
}
