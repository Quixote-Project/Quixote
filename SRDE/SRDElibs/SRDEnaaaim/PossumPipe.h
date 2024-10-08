/** \file
 * This file contains header definitions for the PossumPipe object
 * which implements secured communications based on the identity
 * and mutual attestation state of two devices.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

#ifndef NAAAIM_PossumPipe_HEADER
#define NAAAIM_PossumPipe_HEADER


/* Object type definitions. */
typedef struct NAAAIM_PossumPipe * PossumPipe;

typedef struct NAAAIM_PossumPipe_State * PossumPipe_State;

/**
 * Enumerated definitions for internal error states.
 */
typedef enum {
	PossumPipe_error_internal=1,
	PossumPipe_error_closed_pipe,
	PossumPipe_error_no_identity,
	PossumPipe_error_invalid_identity
} PossumPipe_error_code;

/**
 * Enumerated definitions for packet types.
 */
typedef enum {
	PossumPipe_failure,
	PossumPipe_error,
	PossumPipe_setup,
	PossumPipe_data,
	PossumPipe_rekey,
	PossumPipe_eop
} PossumPipe_type;

/**
 * External PossumPipe object representation.
 */
struct NAAAIM_PossumPipe
{
	/* External methods. */
	_Bool (*init_server)(const PossumPipe, const char *, int port, _Bool);
	_Bool (*init_client)(const PossumPipe, const char *, int port);

	_Bool (*accept_connection)(const PossumPipe);

	_Bool (*start_host_mode)(const PossumPipe, const Buffer);
	_Bool (*start_client_mode)(const PossumPipe, const Buffer);

	_Bool (*start_host_mode2)(const PossumPipe, const Buffer);
	_Bool (*start_client_mode2)(const PossumPipe, const RSAkey);

	_Bool (*send_packet)(const PossumPipe, PossumPipe_type, const Buffer);
	PossumPipe_type (*receive_packet)(const PossumPipe, const Buffer);

	_Bool (*verify)(const PossumPipe, const Buffer, _Bool *status);
	void * (*get_client)(const PossumPipe);
	_Bool (*get_connection)(const PossumPipe, uint64_t *, const Buffer, \
				const Buffer, uint16_t *, uint16_t *);
	void (*display_connection)(const PossumPipe);

	PossumPipe_error_code (*get_error)(const PossumPipe);

	void (*debug)(const PossumPipe, _Bool debug);
	void (*reset)(const PossumPipe);
	void (*whack)(const PossumPipe);

	/* Private state. */
	PossumPipe_State state;
};


/* PossumPipe constructor call. */
extern HCLINK PossumPipe NAAAIM_PossumPipe_Init(void);
#endif
