/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

#ifndef ENCLAVE_T_H__
#define ENCLAVE_T_H__

#include <stdint.h>
#include <wchar.h>
#include <stddef.h>
#include <stdlib.h>

#include <sgx_edger8r.h>


/* Number of enclave interfaces. */
#define ECALL_NUMBER 1
#define OCALL_NUMBER 6


/* ECALL interface definitions. */
struct ecall0_interface {
	unsigned int test;
};

void test_naaaim(unsigned int);


/* OCALL interface definitions. */
#if 0
sgx_status_t SGX_CDECL ocall_print_string(const char* str);
#endif

#endif
