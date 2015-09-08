/* fast int primitives. min,max,rnddiv2
 *
 * WARNING: Assumes 2's complement arithmetic.
 */
static inline int intmax( register int x, register int y )
{
	return x < y ? y : x;
}

static inline int intmin( register int x, register int y )
{
	return x < y ? x : y;
}

static inline int rnddiv2( int x )
{
	return (x+(x>0))>>1;
}
