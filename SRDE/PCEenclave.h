/** \file
 * This file contains definitions for an object which implements
 * management of the Intel PCE enclave.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

#ifndef NAAAIM_PCEenclave_HEADER
#define NAAAIM_PCEenclave_HEADER


/* Object type definitions. */
typedef struct NAAAIM_PCEenclave * PCEenclave;

typedef struct NAAAIM_PCEenclave_State * PCEenclave_State;

/**
 * External PCEenclave object representation.
 */
struct NAAAIM_PCEenclave
{
	/* External methods. */
	_Bool (*open)(const PCEenclave, const char *);

	void (*get_target_info)(const PCEenclave, struct SGX_targetinfo *);

	_Bool (*get_info)(const PCEenclave, struct SGX_pek *, \
			  struct SGX_report *);
	_Bool (*get_ppid)(const PCEenclave, const Buffer);
	void  (*get_version)(const PCEenclave, uint16_t *, uint16_t *);
	void  (*get_psvn)(const PCEenclave, struct SGX_psvn *);

	_Bool (*certify_enclave)(const PCEenclave, struct SGX_report *, \
				 struct SGX_platform_info *, const Buffer);

	void (*dump)(const PCEenclave);
	void (*whack)(const PCEenclave);


	/* Private state. */
	PCEenclave_State state;
};


/* Sgxmetadata constructor call. */
extern HCLINK PCEenclave NAAAIM_PCEenclave_Init(void);
#endif
