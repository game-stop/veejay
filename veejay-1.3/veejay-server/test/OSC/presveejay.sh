#!/bin/perl

my $CMD="./sendOSC -h localhost 3492";

system( "$CMD /clip/new,1,1000" );
system( "$CMD /clip/select,1");


for (my $l = 0; $l < 4; $l ++ )
{
	for (my $i = 0; $i < 20; $i ++ )
	{
		system( "$CMD /entry/preset,0,0,151,$i,0,$i,0" );
	}	 

	for ( my $i = 60; $i > 0 ; $i -- )
	{
		system( "$CMD /entry/preset,0,0,151,$i,0,$i,0" );
	}
}



