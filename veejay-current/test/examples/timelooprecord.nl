#!/usr/bin/perl
use strict;
use warnings;


my $sample=0;
my $recorded_samples=0;
my $current_sample = 0;

sub init_capture()
{
#	print "054:video0;\n";
	print "145:video3;\n";
	print "140:7;\n";
}

sub record_sample()
{
	my $auto_play = shift or die ("no autoswitch given");
	my $record_duration = shift or die ("no duration give");
	# record 75 frames 
	print "152:$record_duration $auto_play;\n";
	# wacht 75 frames
	print "+$record_duration\n";
	# stop recording
	$recorded_samples ++;
}

sub offline_record_sample()
{
	my $record_duration = shift or die ("no duration give");
	# record 75 frames 
	print "150:7 $record_duration 0;\n";
}


sub set_sample()
{
	print "100:$current_sample;\n";
 	print "+1\n";
}



sub play_sample_and_loop_effect()
{
	my $length = shift or die ("no lentgh given");
	my $speed = shift;
	my $bug = $length;
	my $channel = $current_sample - 1;

	print "104:$current_sample $speed;\n";
	print "182:$current_sample 0 4 110;\n";
	print "192:$current_sample 0 0 $channel;\n";
	print "175:$current_sample;\n";
	print "+$length\n";
}


sub play_sample_and_loop_trippler_effect()
{
	my $length = shift or die ("no lentgh given");
	my $speed = shift;
	my $channel = $current_sample - 1;
	my $other_channel = $channel - 1;
	
	print "104:$current_sample $speed;\n";
	print "182:$current_sample 0 4 90;\n";
	print "192:$current_sample 0 0 $channel;\n";
	print "182:$current_sample 1 4 120;\n";
	print "192:$current_sample 1 0 $other_channel;\n";
 	print "182:$current_sample 1 56 190 80 0;\n";
	print "175:$current_sample;\n";
	print "+$length\n";
}


sub play_sample_and_loop()
{
	my $length = shift or die ("no lentgh given");
	my $speed = shift;

	print "104:$current_sample $speed;\n";
	print "+$length\n";
}





my $speed = 3;
my $dur = 4 * 25;
my $no_loops = 1;

#start capture
&init_capture();

print "# CAPTURE STARTED\n";

	# record sample for 100 frames
	&record_sample(1, $dur);
	# one sample ready = 1 now
	$current_sample++;
	print "# first sample is $current_sample\n";
	&set_sample( $current_sample );

	# first sample plays now
	&offline_record_sample( $dur );
	# offline recorder started for dur frames
	# loop sample 1 for dur frames
	&play_sample_and_loop( $dur , $speed );
	# after dur frames there is one sample extra ready
	$recorded_samples++; 

	print "# Recorded $recorded_samples playing $current_sample\n";
	# for 10 times do
	for (my $i = 0; $i < 40 ; $i ++ )
	{
		# play sample
		# start recording immediatly
		&set_sample( $current_sample );
		&offline_record_sample ( $dur );
		if($current_sample < 4)
		{
			print "# normal PLAY\n";
			&play_sample_and_loop($dur,$speed );
		}
		else
		{
			if($current_sample < 3)
			{
				print "# WITH ONE OVERLAY\n";
				&play_sample_and_loop_effect( $dur , $speed  );	
			}
			else
			{
				print "# WITH TWO OVERLAY ONE LIVE FEED\n";
				&play_sample_and_loop_trippler_effect($dur,$speed);
				#&play_sample_and_loop_zoom_effect($dur,$speed);
			}
		}
		#print "+15\n";
		$recorded_samples++;
		$speed ++;
		if($speed == 12) 
		{
			$speed = 2;
		}
		$current_sample++;		
		
	}
#end for loop





