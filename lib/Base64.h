/** \file
 * This file contains the definitions for an object which implements
 * the encoding and decoding of data in Base64 format.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

#ifndef NAAAIM_Base64_HEADER
#define NAAAIM_Base64_HEADER


/* Object type definitions. */
typedef struct NAAAIM_Base64 * Base64;

typedef struct NAAAIM_Base64_State * Base64_State;


/**
 * External Base64 object representation.
 */
struct NAAAIM_Base64
{
	_Bool (*encode)(const Base64, const Buffer, const String);
	_Bool (*decode)(const Base64, const String, const Buffer);

	void (*whack)(const Base64);


	/* Private state. */
	Base64_State state;
};


/* Base64 constructor call. */
extern HCLINK Base64 NAAAIM_Base64_Init(void);
#endif
