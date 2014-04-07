/** \file
 * This file implements the creation of an encrypted and authenticated
 * root filesystem image.  This image is designed to be platformed with
 * the boot.root utility.
 */

/**************************************************************************
 * (C)Copyright 2014, IDfusion, LLC. All rights reserved.
 *
 * Please refer to the file named COPYING in the top of the source tree
 * for licensing information.
 **************************************************************************/


/* Local defines. */
/* Encryption blocksize. */
#define AES_BLOCKSIZE 16

/*
 * Offset into header of payload length, must be a value between 0 and
 * AES_BLOCKSIZE - sizeof(network long).
 */
#define FIELD_OFFSET 7


/* Include files. */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

#include <HurdLib.h>
#include <Buffer.h>
#include <File.h>

#include "NAAAIM.h"
#include "AES256_cbc.h"
#include "SHA256_hmac.h"
#include "RandomBuffer.h"


/* Module specific static variables. */

/* Password. */
#if 0
static unsigned char Pwd[] = {
	0x59, 0x55, 0xd5, 0xf7, 0xf7, 0x40, 0x62, 0xb3, \
	0x00, 0xad, 0x3e, 0x16, 0xa3, 0x77, 0x95, 0x98, \
	0x5f, 0xcf, 0x9e, 0x81, 0x1f, 0x49, 0x6a, 0xb4, \
	0x77, 0xa4, 0xae, 0xce, 0xa0, 0x41, 0x24, 0xb4
};

static unsigned char Iv[] = {
	0x70, 0xa6, 0xba, 0x87, 0xdf, 0x1f, 0x32, 0x9e, \
	0x1f, 0x69, 0x50, 0xa6, 0x5f, 0x0c, 0x78, 0xf1
};
#endif


/**
 * Private function.
 *
 * This function is responsible for loading the encryption key and
 * initialization vector.
 *
 * \param fname		A pointer to a character buffer containing the
 *			name of the file containing the keys.
 *
 * \param iv		A Buffer object which will be loaded with the
 *			initialization vector.
 *
 * \param key		A Buffer object which will be loaded with the
 *			encryption key.
 *
 * \return              A boolean return value is used to indicate
 *			whether or not the keys were successfully
 *			loaded.  A true value indicates success while
 *			a false value indicates a failure.
 */

static _Bool load_keys(CO(char *, fname), CO(Buffer, iv), CO(Buffer, key))

{
	_Bool retn = false;

	File file;


	if ( (file = HurdLib_File_Init()) == NULL )
		return false;

	file->open_ro(file, fname);
	file->read_Buffer(file, iv, AES_BLOCKSIZE);
	file->read_Buffer(file, key, 0);

	retn = !file->poisoned(file) && !iv->poisoned(iv) && \
		!key->poisoned(key);

	WHACK(file);
	return retn;
}


/**
 * Private function.
 *
 * This function is responsible for constructing the encrypted
 * image which is to be woutput.  The image consists of three
 * parts:
 *
 *	Authentication header
 *
 *	Encrypted image
 *
 *	Checksum
 *
 * The  header consists of a which has the length of the encrypted
 * payload imbedded in it at a known offset in 32 bit big endian
 * format.
 *
 * The authentication checksum is a 256 bit hmac-SHA1 checksum
 * over the header plus the encrypted image.
 *
 * \param payload	A Buffer object containing the encrypted
 *			filesystem image.
 *
 * \param key		A Buffer object containing the encryption
 *			key used for the payload.
 *
 * \param output	A Buffer object into which
 *
 *
 * \return              A boolean return value is used to indicate
 *			whether or not the image was successfully
 *			created.  A true value indicates success while
 *			failure is indicated with a false value.
 */

static _Bool generate_payload(CO(Buffer, payload), CO(Buffer, key), \
			      CO(Buffer, output))
{
	_Bool retn = false;

	unsigned char *field;

	uint32_t size;

	RandomBuffer nonce;

	SHA256_hmac hmac;

	Buffer mac;


	if ( (hmac = NAAAIM_SHA256_hmac_Init(key)) == NULL )
		goto done;

	if ( (nonce = NAAAIM_RandomBuffer_Init()) == NULL )
		goto done;
	nonce->generate(nonce, AES_BLOCKSIZE);
	output->add_Buffer(output, nonce->get_Buffer(nonce));
	nonce->whack(nonce);

	size = htonl(payload->size(payload));
	field = output->get(output);
	*(uint32_t *)(field+FIELD_OFFSET) = size;

	output->add(output, payload->get(payload), payload->size(payload));

	fprintf(stderr, "Input to mac: %zu\n", output->size(output));
	hmac->add_Buffer(hmac, output);
	hmac->compute(hmac);
	mac = hmac->get_Buffer(hmac);
	output->add(output, mac->get(mac), mac->size(mac));
	fputs("Checksum:\n", stderr);
	mac->print(mac);

	retn = true;

 done:
	if ( hmac != NULL )
		hmac->whack(hmac);
	return retn;
}


/*
 * Program entry point begins here.
 */

extern int main(int argc, char *argv[])

{
	int retn;

	char *root_image = NULL,
	     *key_file	 = NULL,
	     *output	 = NULL;

	File file;

	Buffer image,
	       encrypted,
	       iv,
	       key;

	AES256_cbc cipher;


	while ( (retn = getopt(argc, argv, "k:r:o:")) != EOF )
		switch ( retn ) {
			case 'k':
				key_file = optarg;
				break;
			case 'r':
				root_image = optarg;
				break;
			case 'o':
				output = optarg;
				break;
		}

	if ( root_image == NULL ) {
		fputs("No filesystem image specified.\n", stderr);
		return 1;
	}
	if ( key_file == NULL ) {
		fputs("No keyfile specifed.\n", stderr);
		return 1;
	}
	if ( output == NULL ) {
		fputs("No output file specified.\n", stderr);
		return 1;
	}

	image = HurdLib_Buffer_Init();
	iv    = HurdLib_Buffer_Init();
	key   = HurdLib_Buffer_Init();
	if ( (image == NULL) || (iv == NULL) || (key == NULL) ) {
		fputs("Cannot initialize buffers.\n", stderr);
		goto done;
	}

	if ( !load_keys(key_file, iv, key) ) {
		fputs("Cannot load keys.\n", stderr);
		goto done;
	}
	fputs("IV:  ", stdout);
	iv->print(iv);
	fputs("Key: ", stdout);
	key->print(key);

	if ( (cipher = NAAAIM_AES256_cbc_Init_encrypt(key, iv)) == NULL ) {
		fputs("Cannot initialize cipher.\n", stderr);
		goto done;
	}

	if ( (file = HurdLib_File_Init()) == NULL ) {
		fputs("Cannot initialize file object.\n", stderr);
		goto done;
	}
	file->open_ro(file, root_image);
	file->slurp(file, image);
	fprintf(stdout, "File size: %zu\n", image->size(image));

	if ( (encrypted = cipher->encrypt(cipher, image)) == NULL ) {
		fputs("Encryption failed.\n", stderr);
		goto done;
	}
	fprintf(stdout, "Cipher size: %zu\n", encrypted->size(encrypted));

	image->reset(image);
	generate_payload(encrypted, key, image);

	file->reset(file);
	file->open_rw(file, output);
	file->write_Buffer(file, image);

	retn = !file->poisoned(file);

 done:
	WHACK(image);
	WHACK(file);
	WHACK(iv);
	WHACK(key);
	WHACK(cipher);

	return retn;
}
