/** \file
 *
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

#ifndef NAAAIM_IDmgr_HEADER
#define NAAAIM_IDmgr_HEADER


/* Object type definitions. */
typedef struct NAAAIM_IDmgr * IDmgr;

typedef struct NAAAIM_IDmgr_State * IDmgr_State;

/*  type used to define the type of the request. */
typedef enum {
	IDmgr_none,
	IDmgr_token,
	IDmgr_idhash
} IDmgr_type;

/**
 * External IDmgr object representation.
 */
struct NAAAIM_IDmgr
{
	/* External methods. */
	_Bool (*setup)(const IDmgr);
	_Bool (*attach)(const IDmgr);

	IDmgr_type (*get_idtype)(const IDmgr);
	_Bool (*get_idname)(const IDmgr, const String);

	_Bool (*get_id_key)(const IDmgr, const String, const Buffer, \
			    const Buffer);
	_Bool (*set_id_key)(const IDmgr, const IDtoken);

	_Bool (*get_idtoken)(const IDmgr, const String, const IDtoken);
	_Bool (*set_idtoken)(const IDmgr, const IDtoken);

	void (*whack)(const IDmgr);

	/* Private state. */
	IDmgr_State state;
};


/* IDmgr constructor call. */
extern HCLINK IDmgr NAAAIM_IDmgr_Init(void);
#endif
