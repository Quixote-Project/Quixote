/** \file
 * This file implements the methods and API definitions for an object
 * which implements operations on assymetric RSA keys.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

#ifndef NAAAIM_RSAkey_HEADER
#define NAAAIM_RSAkey_HEADER


/* Object type definitions. */
typedef struct NAAAIM_RSAkey * RSAkey;

typedef struct NAAAIM_RSAkey_State * RSAkey_State;

/**
 * Enumerated definitions for encryption padding.
 */
typedef enum {
	RSAkey_pad_none,
	RSAkey_pad_pkcs1,
	RSAkey_pad_oaep
} RSAkey_padding;

/**
 * External RSAkey object representation.
 */
struct NAAAIM_RSAkey
{
	/* External methods. */
	_Bool (*generate_key)(const RSAkey, int);

	_Bool (*get_public_key)(const RSAkey, const Buffer);
	_Bool (*get_private_key)(const RSAkey, const Buffer);
	_Bool (*get_modulus)(const RSAkey, const Buffer);

	_Bool (*load_public)(const RSAkey, const Buffer);
	_Bool (*load_private)(const RSAkey, const Buffer);

	_Bool (*load_certificate)(const RSAkey, const Buffer);

	_Bool (*load_public_key)(const RSAkey, const char *, const char *);
	_Bool (*load_private_key)(const RSAkey, const char *, const char *);

	_Bool (*encrypt)(const RSAkey, Buffer);
	_Bool (*decrypt)(const RSAkey, Buffer);

	_Bool (*verify)(const RSAkey, const Buffer, const Buffer, _Bool *);
	_Bool (*sign)(const RSAkey, const Buffer, const Buffer);

	_Bool (*init_engine)(const RSAkey, const char **);
	_Bool (*set_padding)(const RSAkey, const int);

	int (*size)(const RSAkey);
	void (*print)(const RSAkey);
	void (*whack)(const RSAkey);

	/* Private state. */
	RSAkey_State state;
};


/* RSAkey constructor call. */
extern HCLINK RSAkey NAAAIM_RSAkey_Init(void);
#endif
