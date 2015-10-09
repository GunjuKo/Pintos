#include "fixed_point.h"

/* change int to fixed point */
int int_to_fp(int n)
{
	return n*F;
}

/* change fixed point to int rounding to nearest */
int fp_to_int_round(int x)
{
	if(x >= 0)
		return (x + F/2)/F;
	else
		return (x - F/2)/F;
}

/* change fixed point to int rouding to zero */
int fp_to_int(int x)
{
	return x/F;
}

/* add fp + fp*/
int add_fp(int x, int y)
{
	return x+y;
}

/* add fp + integer */
int add_mixed(int x, int n)
{
	return x+(n*F);
}

/* sub fp - fp */
int sub_fp(int x, int y)
{
	return x-y;
}

/* sub fp - integer */
int sub_mixed(int x,int n)
{
	return x-(n*F);
}

/* multi fp * fp */
int mult_fp(int x, int y)
{
	return (((int64_t)x)*y)/F;
}

/* multi fp * integer */
int mult_mixed(int x, int n)
{
	return x*n;
}

/* divide fp / fp */
int div_fp(int x, int y)
{
	return (((int64_t)x)*F)/y;
}

/* divide fp / integer */
int div_mixed(int x, int n)
{
	return x/n;
}
