/** \file
 * This file contains the C portion of the bridge code which conducts
 * an OCALL from inside an enclave to its implementation in the
 * non protected portion of the application.  It is called from the
 * srde_boot function implemented in assembly language.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

/* Include files. */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <HurdLib.h>

#include "SRDE.h"
#include "SRDEenclave.h"


/**
 * External public function.
 *
 * This method implements the linkage function which conducts the call
 * from the OCALL return point in the boot_tee function into its
 * implementation code written in C.
 *
 * \param slot		The slot number of the OCALL API which is
 *			being requested.
 *
 * \param ocall_api	A pointer to the OCALL API table which
 *			implements the OCALL interfaces between the
 *			enclave and the unprotected portion of the
 *			application.
 *
 * \param interface	The data interface structure which is used to
 *			hold function data from the enclave.
 *
 * \param context	The enclave which owns the ECALL which
 *			generated the enclave outcall.
 *
 * \return	An integer value is returned which is used by
 *		enclave to determine the execution status of the
 *		OCALL.
 */

int srde_ocall(int slot, void *ocall_api, void *interface, SRDEenclave enclave)

{
	return enclave->boot_ocall(enclave, slot, ocall_api, interface);
}
