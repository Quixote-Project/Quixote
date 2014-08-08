/** \file
 * This file contains the header definitions used to implement an
 * object which manages a software TPM implementation.
 */

/**************************************************************************
 * (C)Copyright 2014, The Open Hurderos Foundation. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/

#ifndef NAAAIM_SoftwareTPM_HEADER
#define NAAAIM_SoftwareTPM_HEADER


/* Object type definitions. */
typedef struct NAAAIM_SoftwareTPM * SoftwareTPM;

typedef struct NAAAIM_SoftwareTPM_State * SoftwareTPM_State;

/**
 * External SoftwareTPM object representation.
 */
struct NAAAIM_SoftwareTPM
{
	/* External methods. */
	_Bool (*start)(const SoftwareTPM, uid_t);

	void (*whack)(const SoftwareTPM);

	/* Private state. */
	SoftwareTPM_State state;
};


/* SoftwareTPM constructor call. */
extern SoftwareTPM NAAAIM_SoftwareTPM_Init(void);

#endif
