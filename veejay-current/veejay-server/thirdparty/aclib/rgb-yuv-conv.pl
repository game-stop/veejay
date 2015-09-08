#!/usr/bin/perl -w
# Calculate conversion matrices for RGB<->YUV given Kb and Kr

die "Usage: $0 Kb Kr [scale]\n" if @ARGV < 2;
$scale = $ARGV[2] || 1;
$Kb = $ARGV[0];
$Kr = $ARGV[1];
$Kg = 1 - $Kr - $Kb;
$a11 = $Kr;
$a12 = $Kg;
$a13 = $Kb;
$a21 = -$Kr/(1-$Kb)/2;
$a22 = -$Kg/(1-$Kb)/2;
$a23 = 1/2;
$a31 = 1/2;
$a32 = -$Kg/(1-$Kr)/2;
$a33 = -$Kb/(1-$Kr)/2;
print "Y [R] = ".($a11*$scale)."\n";
print "Y [G] = ".($a12*$scale)."\n";
print "Y [B] = ".($a13*$scale)."\n";
print "Cb[R] = ".($a21*$scale)."\n";
print "Cb[G] = ".($a22*$scale)."\n";
print "Cb[B] = ".($a23*$scale)."\n";
print "Cr[R] = ".($a31*$scale)."\n";
print "Cr[G] = ".($a32*$scale)."\n";
print "Cr[B] = ".($a33*$scale)."\n";
$det = $a11*$a22*$a33 - $a11*$a23*$a32
     + $a12*$a23*$a31 - $a12*$a21*$a33
     + $a13*$a21*$a32 - $a13*$a22*$a31;
$b11 = (1/$det)*($a22*$a33-$a23*$a32);
$b12 = (1/$det)*($a13*$a32-$a12*$a33);
$b13 = (1/$det)*($a12*$a23-$a13*$a22);
$b21 = (1/$det)*($a23*$a31-$a21*$a33);
$b22 = (1/$det)*($a11*$a33-$a13*$a31);
$b23 = (1/$det)*($a13*$a21-$a11*$a23);
$b31 = (1/$det)*($a21*$a32-$a22*$a31);
$b32 = (1/$det)*($a12*$a31-$a11*$a32);
$b33 = (1/$det)*($a11*$a22-$a12*$a21);
map {$_ = 0 if abs($_) < 1e-10} ($b11,$b12,$b13,$b21,$b22,$b23,$b31,$b32,$b33);
print "R[Y ] = ".($b11*$scale)."\n";
print "R[Cb] = ".($b12*$scale)."\n";
print "R[Cr] = ".($b13*$scale)."\n";
print "G[Y ] = ".($b21*$scale)."\n";
print "G[Cb] = ".($b22*$scale)."\n";
print "G[Cr] = ".($b23*$scale)."\n";
print "B[Y ] = ".($b31*$scale)."\n";
print "B[Cb] = ".($b32*$scale)."\n";
print "B[Cr] = ".($b33*$scale)."\n";
