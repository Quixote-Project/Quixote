/** \file
 * This file contains the implementation of an object that implements
 * the enclave report generation and verification.  Enclave reports
 * are the fundamental infrastructure that local and remote
 * attestation are based on.
 */

/* Include files. */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>

#include <SRDE.h>
#include <SRDEfusion.h>

#include "NAAAIM.h"
#include "AES128_cmac.h"
#include "Report.h"


/* Verify library/object header file inclusions. */
#if !defined(NAAAIM_LIBID)
#error Library identifier not defined.
#endif

#if !defined(NAAAIM_Report_OBJID)
#error Object identifier not defined.
#endif

/* State extraction macro. */
#define STATE(var) CO(Report_State, var) = this->state


/** Report private state information. */
struct NAAAIM_Report_State
{
	/* The root object. */
	Origin root;

	/* Library identifier. */
	uint32_t libid;

	/* Object identifier. */
	uint32_t objid;

	/* Object status. */
	_Bool poisoned;
};


/**
 * Internal private method.
 *
 * This method is responsible for initializing the NAAAIM_Report_State
 * structure that holds state information for each instantiated object.
 *
 * \param S	A pointer to the object containing the state information
 *		that is to be initialized.
 */

static void _init_state(CO(Report_State, S))

{
	S->libid = NAAAIM_LIBID;
	S->objid = NAAAIM_Report_OBJID;

	S->poisoned = false;

	return;
}


/**
 * External public method.
 *
 * This method implements the generation of a report for an enclave
 * identified by the specified target information.  The report can
 * optionally include up to an additional 64 bytes of data that will
 * be attested to come from the enclave with the identity characteristics
 * specified in the report.
 *
 * \param this		A pointer to the object which is to generate the
 *			report.
 *
 * \param tp		A pointer to structure defining the enclave that
 *			the report is to be verified by.
 *
 * \param data		The object containing the optional data that
 *			is to be included in the report.
 *
 * \param rp		A pointer to the structure that the report will
 *			be copied into.
 *
 * \param data		The object containing the optional data that
 *			is to be included in the report.
 */

static _Bool generate_report(CO(Report, this), struct SGX_targetinfo *tp, \
			     CO(Buffer, data), struct SGX_report *rp)

{
	STATE(S);

	_Bool retn = false;

	struct SGX_targetinfo target __attribute__((aligned(512)));

	char report_data[64] __attribute__((aligned(128)));

	struct SGX_report report __attribute__((aligned(512)));


	/* Validate object and arguement status. */
	if ( S->poisoned )
		ERR(goto done);


	/* Populate the report_data field if data is supplied. */
	if ( data == NULL )
		memset(report_data, '\0', sizeof(report_data));
	else {
		if ( (data->size(data) == 0) || (data->size(data) > 64) )
			ERR(goto done);

		memcpy(&report_data, data->get(data), sizeof(report_data));
	}


	/* Generate the report and copy it to the output structure. */
	target = *tp;
	enclu_ereport(&target, &report, report_data);

	*rp  = report;
	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	memset(&target, '\0', sizeof(target));
	memset(report_data, '\0', sizeof(report_data));
	memset(&report, '\0', sizeof(report));

	return retn;
}


/**
 * External public method.
 *
 * This method validates a report that has been generated by an enclave
 * against identity characteristics that were previously generated
 * by the ->get_target method.
 *
 * \param this		A pointer to the object which is to validate
 *			the report.
 *
 * \param rp		A pointer to the structure containing the
 *			report that was generated by the enclave
 *			against the identity characteristics of the
 *			currently executing enclave.
 *
 * \param status	A pointer to the variable that will be set
 *			with the validation status of the report.
 *
 * \return		A boolean value is used to indicate the status
 *			of validation of the report.  A false value
 *			indicates an error occurred and the status
 *			variable will have indeterminate information.
 *			A true value indicates the status variable
 *			has been set with the status of the report
 *			validation.
 */

static _Bool validate_report(CO(Report, this), CO(struct SGX_report *, rp), \
			     _Bool *status)

{
	STATE(S);

	_Bool retn = false;

	uint8_t keydata[16] __attribute__((aligned(128)));

	struct SGX_keyrequest keyrequest;

	Buffer bufr = NULL;

	AES128_cmac cmac = NULL;


	/* Verify object status. */
	if ( S->poisoned )
		ERR(goto done);


	/* Request the report key. */
	memset(keydata, '\0', sizeof(keydata));
	memset(&keyrequest, '\0', sizeof(keyrequest));

	keyrequest.keyname = SRDE_KEYSELECT_REPORT;
	memcpy(keyrequest.keyid, rp->keyid, sizeof(keyrequest.keyid));

	if ( enclu_egetkey(&keyrequest, keydata) != 0 )
		ERR(goto done);

	INIT(HurdLib, Buffer, bufr, ERR(goto done));
	if ( !bufr->add(bufr, keydata, sizeof(keydata)) )
		ERR(goto done);

	memset(keydata, '\0', sizeof(keydata));
	memset(&keyrequest, '\0', sizeof(keyrequest));


	/* Validate the MAC computed by the remote enclave. */
	INIT(NAAAIM, AES128_cmac, cmac, ERR(goto done));

	if ( !cmac->set_key(cmac, bufr) )
		ERR(goto done);
	bufr->reset(bufr);

	if ( !cmac->add(cmac, (void *) rp, sizeof(struct SGX_reportbody)) )
		ERR(goto done);
	if ( !cmac->compute(cmac) )
		ERR(goto done);

	if ( !bufr->add(bufr, rp->mac, sizeof(rp->mac)) )
		ERR(goto done);
	*status = bufr->equal(bufr, cmac->get_Buffer(cmac));

	retn = true;


 done:
	if ( !retn )
		S->poisoned = false;

	WHACK(bufr);
	WHACK(cmac);

	return retn;
}


/**
 * External public method.
 *
 * This method implements the generation of a target information
 * structure for the current enclave.  This target information structure
 * is conveyed to an enclave that wishes to generate an attestation
 * report that can be verified by the enclave originating the
 * designated target information.
 *
 * \param this	A pointer to the object which is to generate the
 *		target information.
 *
 * \param tp	A pointer to the structure that the target information
 *		is to be copied into.
 *
 * \return	A boolean value is used to indicate the status of
 *		target information generation.  A false value indicates
 *		an error was encounttered and no assumption can be
 *		made regarding the target information structure.  A
 *		true value indicates the target information structure
 *		has been populated with valid data.
 */

static _Bool get_targetinfo(CO(Report, this), struct SGX_targetinfo *tp)

{
	STATE(S);

	_Bool retn = false;

	struct SGX_targetinfo target;

	struct SGX_report report;


	/* Validate object. */
	if ( S->poisoned )
		ERR(goto done);


	/* Generate a 'NULL' report self-identifying the enclave. */
	memset(&target, '\0', sizeof(target));
	if ( !this->generate_report(this, &target, NULL, &report) )
		ERR(goto done);


	/* Copy the relevant data information. */
	memset(tp, '\0', sizeof(struct SGX_targetinfo));
	tp->mrenclave  = report.body.mr_enclave;
	tp->attributes = report.body.attributes;
	tp->miscselect = report.body.miscselect;

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements a destructor for a Report object.
 *
 * \param this	A pointer to the object which is to be destroyed.
 */

static void whack(CO(Report, this))

{
	STATE(S);


	S->root->whack(S->root, this, S);
	return;
}


/**
 * External constructor call.
 *
 * This function implements a constructor call for a Report object.
 *
 * \return	A pointer to the initialized Report.  A null value
 *		indicates an error was encountered in object generation.
 */

extern Report NAAAIM_Report_Init(void)

{
	Origin root;

	Report this = NULL;

	struct HurdLib_Origin_Retn retn;


	/* Get the root object. */
	root = HurdLib_Origin_Init();

	/* Allocate the object and internal state. */
	retn.object_size  = sizeof(struct NAAAIM_Report);
	retn.state_size   = sizeof(struct NAAAIM_Report_State);
	if ( !root->init(root, NAAAIM_LIBID, NAAAIM_Report_OBJID, &retn) )
		return NULL;
	this	    	  = retn.object;
	this->state 	  = retn.state;
	this->state->root = root;

	/* Initialize object state. */
	_init_state(this->state);

	/* Initialize aggregate objects. */

	/* Method initialization. */
	this->whack = whack;

	this->generate_report = generate_report;
	this->validate_report = validate_report;

	this->get_targetinfo = get_targetinfo;

	return this;
}
