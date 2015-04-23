/** \file
 * This file implements a test harness for the SmartCard Object.
 */

/**************************************************************************
 * (C)Copyright 2015 IDfusion, LLC. All rights reserved.
 **************************************************************************/


#include <stdio.h>

#include <HurdLib.h>

#include "NAAAIM.h"
#include "SmartCard.h"


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	int retn = 1;

	SmartCard card = NULL;


	INIT(NAAAIM, SmartCard, card, goto done);

	retn = 0;


 done:
	WHACK(card);

	return retn;
}
