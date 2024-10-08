/** \file
 * This file contains the implementation of an object which is used to
 * load the binary contents of an Intel Software Guard Extension (SGX)
 * enclave.
 */

/**************************************************************************
 * Copyright (c) Enjellic Systems Development, LLC. All rights reserved.
 *
 * Please refer to the file named Documentation/COPYRIGHT in the top of
 * the source tree for copyright and licensing information.
 **************************************************************************/

/* Loader bug/compatibility flags. */
#define NULL_PADDING_NEEDED 0x1


/* Include files. */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <libelf.h>

#include <Origin.h>
#include <HurdLib.h>
#include <Buffer.h>

#include "NAAAIM.h"
#include "SRDE.h"
#include "SRDEenclave.h"
#include "SRDEmetadata.h"
#include "SRDEloader.h"


/* Object state extraction macro. */
#define STATE(var) CO(SRDEloader_State, var) = this->state

/* Verify library/object header file inclusions. */
#if !defined(NAAAIM_LIBID)
#error Library identifier not defined.
#endif

#if !defined(NAAAIM_SRDEloader_OBJID)
#error Object identifier not defined.
#endif


/**
 * The following structure is used to define an enclave program segment.
 * For simplicity sake the structure uses the ELF program header to
 * save all of the information about the segment.   The physical data
 * is saved in the Buffer object which the structure references.
 */
struct segment {
	uint64_t flags;
	Elf64_Phdr phdr;
	Buffer data;
};


/** SRDEloader private state information. */
struct NAAAIM_SRDEloader_State
{
	/* The root object. */
	Origin root;

	/* Library identifier. */
	uint32_t libid;

	/* Object identifier. */
	uint32_t objid;

	/* Object status. */
	_Bool poisoned;

	/* Debug status. */
	_Bool debug;

	/* Enclave file descriptor. */
	int fd;

	/* Enclave file memory mapping information. */
	void *soimage;
	size_t sosize;

	/* ELF library descriptor. */
	Elf *elf;

	/* SGX metadata. */
	SRDEmetadata metadata;

	/* The array of segments. */
	Buffer segments;

	/* A pointer to the TLS segment. */
	struct segment *tls;

	/* A pointer to the dynamic symbol segment. */
	Elf64_Dyn *dynptr;
};


/**
 * Internal private method.
 *
 * This method is responsible for initializing the NAAAIM_SRDEloader_State
 * structure which holds state information for each instantiated object.
 *
 * \param S A pointer to the object containing the state information which
 *        is to be initialized.
 */

static void _init_state(CO(SRDEloader_State, S)) {

	S->libid = NAAAIM_LIBID;
	S->objid = NAAAIM_SRDEloader_OBJID;

	S->poisoned = false;
	S->debug    = false;

	S->fd = -1;

	S->soimage = NULL;
	S->sosize  = 0;

	S->metadata = NULL;
	S->segments = NULL;
	S->dynptr   = NULL;

	return;
}


/**
 * Internal private method.
 *
 * This method searches the dynamic symbol table for the tag specified
 * by the caller.
 *
 * \param this		A pointer to the loader object whose dynamic
 *			symbol table is to be searched.
 *
 * \param tag		The tag value to be located.
 *
 * \return	If the dynamic symbol tag is not found a NULL pointer
 *		is returned.  If the entry is found a pointer to the
 *		Elf64_Dyn structure is returned.
 */

static Elf64_Dyn *_get_tag(CO(SRDEloader_State, S), Elf64_Sxword tag)

{
	Elf64_Dyn *dptr = S->dynptr;


	while ( dptr->d_tag != DT_NULL ) {
		if ( dptr->d_tag == tag )
			goto done;
		++dptr;
	}
	dptr = NULL;


 done:
	return dptr;
}


/**
 * Internal private method.
 *
 * This method implements saving a PT_LOAD segment.
 *
 * \param this		A pointer to the loader object to the segment is
 *			to be saved.
 *
 * \param phdr		A pointer to the ELF program header which
 *			references the segment to be loaded.
 *
 * \return	If an error is encountered while saving the segment a
 *		false value is returned.  A true value indicates the
 *		segment was saved.
 */

static _Bool _pt_load_segment(CO(SRDEloader_State, S), CO(Elf64_Phdr *, phdr))

{
	_Bool retn = false;

	struct segment segment;


	/**
	 * Allocate the buffer for the segment array and clear the segment
	 * description structure.
	 */
	if ( S->segments == NULL )
		INIT(HurdLib, Buffer, S->segments, ERR(goto done));

	memset(&segment, '\0', sizeof(struct segment));


	/**
	 * Translate the ELF page protection flags into their SGX
	 * equivalents.  All pages of PT_LOAD type are considered to
	 * be regular pages.
	 */
	segment.flags = SGX_SECINFO_REG;
	if ( phdr->p_flags & PF_R )
		segment.flags |= SGX_SECINFO_R;
	if ( phdr->p_flags & PF_W )
		segment.flags |= SGX_SECINFO_W;
	if ( phdr->p_flags & PF_X )
		segment.flags |= SGX_SECINFO_X;

	segment.phdr = *phdr;

	INIT(HurdLib, Buffer, segment.data, ERR(goto done));

	if ( !segment.data->add(segment.data, S->soimage + phdr->p_offset, \
				phdr->p_filesz) )
		ERR(goto done);

	if ( !S->segments->add(S->segments, (unsigned char *) &segment, \
			       sizeof(struct segment)) )
		ERR(goto done);

	retn = true;


 done:
	return retn;
}


/**
 * Internal private method.
 *
 * This method builds a relocation bitmap which details pages in the
 * TEXT section which have relocations and would thus necessitate an
 * enclave page being writable in order to support the runtime
 * patching which is needed.
 *
 * \param this		A pointer to the internal state of the loader
 *			object whose relocation map is to be built.
 *
 * \return	A true value is returned if a text relocation bitmap
 *		is needed and was created.  A false value indicates
 *		no such relocations were present.
 */

static _Bool _build_relocation_map(CO(SRDEloader_State, S))

{
	_Bool retn = false;


	/*
	 * Verify there is something to do by first checking to see
	 * if a dynamic symbol table is present.  If this check passes
	 * verify that there are possible TEXT relocations present.
	 */
	if ( S->dynptr == NULL )
		goto done;
	if ( _get_tag(S, DT_TEXTREL) == NULL )
		goto done;

	retn = true;


 done:
	return retn;
}


/**
 * External public method.
 *
 * This method implements the loading of the SGX binary data from the
 * enclave file.
 *
 * \param this		A pointer to the object which is to hold the
 *			metadata.
 *
 * \param enclave	A pointer to the null-terminated character
 *			buffer which holds the name of the enclave.
 *
 * \param debug		A boolean value which indicates whether or not
 *			the enclave is to be loaded in debug mode.
 *
 * \return	If an error is encountered while loading the metadata
 *		a false value is returned.  A true value is returned
 *		if the object contains valid metadata.
 */

static _Bool load(CO(SRDEloader, this), CO(char *, enclave), const _Bool debug)

{
	STATE(S);

	_Bool retn = false;

	uint32_t segindex;

	size_t phnum;

	struct stat statbuf;

	Elf64_Ehdr *ehdr;

	Elf64_Phdr *phdr;


	/* Sanity check the object. */
	if ( S->poisoned )
		goto done;


	/* Load the SGA metadata. */
	INIT(NAAAIM, SRDEmetadata, S->metadata, ERR(goto done));
	if ( S->debug )
		S->metadata->debug(S->metadata, true);
	if ( !S->metadata->load(S->metadata, enclave) )
		ERR(goto done);
	if ( !S->metadata->compute_attributes(S->metadata, debug) )
		ERR(goto done);


	/* Open the shared object enclave and memory map the file. */
	if ( (S->fd = open(enclave, O_RDONLY, 0)) < 0 )
		ERR(goto done);

	if ( fstat(S->fd, &statbuf) == -1 )
		ERR(goto done);
	S->sosize = statbuf.st_size;
	if ( (S->soimage = mmap(NULL, S->sosize, PROT_READ | PROT_WRITE, \
				MAP_PRIVATE, S->fd, 0)) == NULL )
		ERR(goto done);


	/* Patch the enclave shared image. */
	S->metadata->patch_enclave(S->metadata, S->soimage);


	/* Setup and initialize ELF access to the enclave. */
	if ( elf_version(EV_CURRENT) == EV_NONE )
		ERR(goto done);
	if ( (S->elf = elf_begin(S->fd, ELF_C_READ, NULL)) == NULL )
		ERR(goto done);
	if ( (ehdr = elf64_getehdr(S->elf)) == NULL )
		ERR(goto done);


	/*
	 * Iterate through the program segments and save references to
	 * relevant segments.
	 */
	if ( elf_getphdrnum(S->elf, &phnum) == -1 )
		ERR(goto done);
	if ( (phdr = elf64_getphdr(S->elf)) == NULL )
		ERR(goto done);

	for(segindex= 0; segindex < phnum; ++segindex, ++phdr) {
		if ( phdr->p_type == PT_LOAD ) {
			if ( !_pt_load_segment(S, phdr) )
				ERR(goto done);
		}
		if ( phdr->p_type == PT_DYNAMIC )
			S->dynptr = (Elf64_Dyn *) (S->soimage + \
						   phdr->p_offset);
	}

	if ( _build_relocation_map(S) ) {
		fputs("Writable TEXT relocations present but not " \
		      "supported.\n", stderr);
		goto done;
	}

	retn = true;


 done:
	return retn;
}


/**
 * External public method.
 *
 * This method implements the loading of the SGX binary data from the
 * enclave file with return of the SGX Enclave Control Structure.  This
 * method allows a single call to load the enclave and return the
 * information needed to create the enclave.
 *
 * \param this		A pointer to the object which is to hold the
 *			metadata.
 *
 * \param enclave	A pointer to the null-terminated character
 *			buffer which holds the name of the enclave.
 *
 * \param secs		A pointer to the structure which will be loaded
 *			with the SECS information.
 *
 * \param debug		A boolean value which indicates whether or not
 *			the enclave is to be loaded in debug mode.
 *
 * \return	If an error is encountered while loading the enclave
 *		a false value is returned.  A true value is returned
 *		if the object contains valid metadata.
 */

static _Bool load_secs(CO(SRDEloader, this), CO(char *, enclave), \
		       struct SGX_secs *secs, const _Bool debug)

{
	STATE(S);

	_Bool retn = false;


	/* Verify object status. */
	if ( S->poisoned )
		return false;


	/* Call the standard load method. */
	if ( !this->load(this, enclave, debug) )
		ERR(goto done);


	/* Get the SECS information from the enclave metadata. */
	if ( !S->metadata->get_secs(S->metadata, secs) )
		ERR(goto done);
	if ( S->debug )
		fprintf(stdout, "Loaded SECS size: 0x%lx\n", secs->size);


	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements the loading of the SGX binary data from a
 * memory image of the enclave.
 *
 * \param this		A pointer to the object which is to hold the
 *			metadata.
 *
 * \param enclave	A pointer to the memory buffer which holds
 *			the enclave image.
 *
 * \param enclave_size	The size of the memory image.
 *
 * \param debug		A boolean value which indicates whether or not
 *			the enclave is to be loaded in debug mode.
 *
 * \return	If an error is encountered while loading the metadata
 *		a false value is returned.  A true value is returned
 *		if the object contains valid metadata.
 */

static _Bool load_memory(CO(SRDEloader, this), const char * enclave, \
			 const size_t enclave_size, const _Bool debug)

{
	STATE(S);

	_Bool retn = false;

	uint32_t segindex;

	size_t phnum;

	Elf64_Ehdr *ehdr;

	Elf64_Phdr *phdr;


	/* Sanity check the object. */
	if ( S->poisoned )
		goto done;


	/* Load the enclave metadata. */
	INIT(NAAAIM, SRDEmetadata, S->metadata, ERR(goto done));
	if ( S->debug )
		S->metadata->debug(S->metadata, true);
	if ( !S->metadata->load_memory(S->metadata, (char *) enclave, \
				       enclave_size) )
		ERR(goto done);
	if ( !S->metadata->compute_attributes(S->metadata, debug) )
		ERR(goto done);


	/*
	 * Set the shared object pointer to the supplied buffer.  This
	 * should be dynamically allocated in order to avoid changes
	 * to the memory region by the caller.
	 */
	S->sosize  = enclave_size;
	S->soimage = (char *) enclave;


	/* Patch the enclave shared image. */
	S->metadata->patch_enclave(S->metadata, S->soimage);


	/* Setup and initialize ELF access to the enclave. */
	if ( elf_version(EV_CURRENT) == EV_NONE )
		ERR(goto done);
	if ( (S->elf = elf_memory(S->soimage, S->sosize)) == NULL )
		ERR(goto done);
	if ( (ehdr = elf64_getehdr(S->elf)) == NULL )
		ERR(goto done);


	/*
	 * Iterate through the program segments and save references to
	 * relevant segments.
	 */
	if ( elf_getphdrnum(S->elf, &phnum) == -1 )
		ERR(goto done);
	if ( (phdr = elf64_getphdr(S->elf)) == NULL )
		ERR(goto done);

	for(segindex= 0; segindex < phnum; ++segindex, ++phdr) {
		if ( phdr->p_type == PT_LOAD ) {
			if ( !_pt_load_segment(S, phdr) )
				ERR(goto done);
		}
		if ( phdr->p_type == PT_DYNAMIC )
			S->dynptr = (Elf64_Dyn *) (S->soimage + \
						   phdr->p_offset);
	}

	if ( _build_relocation_map(S) ) {
		fputs("Writable TEXT relocations present but not " \
		      "supported.\n", stderr);
		goto done;
	}

	retn = true;


 done:
	return retn;
}


/**
 * External public method.
 *
 * This method implements the loading of the SGX binary data from a
 * memory image of an enclave with return of the SGX Enclave Control
 * Structure.  This method implements a single call to load the
 * encalve and return the information needed to create the enclave.
 *
 * \param this		A pointer to the object which is to hold the
 *			metadata.
 *
 * \param enclave	A pointer to the memory buffer holding the
 *			image of the enclave.
 *
 * \param enclave_size	The size of the enclave image.
 *
 * \param debug		A boolean value which indicates whether or not
 *			the enclave is to be loaded in debug mode.
 *
 * \return	If an error is encountered while loading the enclave
 *		a false value is returned.  A true value is returned
 *		if the object contains valid metadata.
 */

static _Bool load_secs_memory(CO(SRDEloader, this), const char * enclave,  \
			      size_t enclave_size, struct SGX_secs *secs,  \
			      const _Bool debug)

{
	STATE(S);

	_Bool retn = false;


	/* Verify object status. */
	if ( S->poisoned )
		return false;


	/* Call the standard load method. */
	if ( !this->load_memory(this, enclave, enclave_size, debug) )
		ERR(goto done);


	/* Get the SECS information from the enclave metadata. */
	if ( !S->metadata->get_secs(S->metadata, secs) )
		ERR(goto done);
	if ( S->debug )
		fprintf(stdout, "Loaded SECS size: 0x%lx\n", secs->size);


	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * Internal private function.
 *
 * This is an inline function which rounds the provided value up to
 * the next integral page value.  Note that this function assumes a
 * page size of 4096 bytes.
 *
 * \param addr	The address which is to be aligned to the next
 *		integral page boundary.
 *
 * \return	The next value which is an integral multiple of
 *		the provided address is returned.
 */

static inline uint64_t r2p(uint64_t addr) {
	return (addr + (4096-1)) & ~(4096-1);
}


/**
 * Internal private function.
 *
 * This function finalizes adding pages for the previous segment.
 *
 * \param enclave	The enclave object to which the page is to be
 *			added.
 *
 * \param segment	The description of the segement which is to
 *			be finalized.
 *
 * \return	A boolean value is returned to indicate whether or not
 *		adding the finalized pages has succeeded.  A true
 *		value indicates the addition succeeded a false value
 *		indicates the addition has failed.
 */

static _Bool _finish_segment(CO(SRDEenclave, enclave), \
			     CO(struct segment *, segment))

{
	_Bool retn = false;


	if ( (r2p(segment->phdr.p_memsz + segment->phdr.p_vaddr) < 	  \
	      r2p(r2p(segment->phdr.p_memsz + segment->phdr.p_vaddr))) && \
	     (r2p(segment->phdr.p_vaddr + segment->phdr.p_memsz) <	  \
	      (segment->phdr.p_vaddr & (~(4096-1)))) ) {
		fputs("No final page support\n", stderr);
		ERR(goto done);
	}

	retn = true;


 done:
	return retn;
}


/**
 * Internal private function.
 *
 * This function loads all of the remaining pages in a segment after
 * the first page has been loaded.
 *
 * \param enclave	The enclave object to which the page is to be
 *			added.
 *
 * \param segment	The description of the segement which is to
 *			be added.
 *
 * \param offset	The offset which was used to establish a
 *			page boundary alignment for the first page of
 *			the segment.
 *
 * \param debug		A flag used to indicate whether debug
 *			information is to be generated.
 *
 *
 * \return	A boolean value is returned to indicate whether or not
 *		building the segment pages has succeeded.  A true
 *		value indicates the build succeeded a false value
 *		indicates the build has failed.
 */

static _Bool _build_segment(CO(SRDEenclave, enclave),	   \
			    CO(struct segment *, segment), \
			    const uint64_t offset,	   \
			    const uint64_t compatibility, const _Bool debug)
{
	_Bool retn = false;

	unsigned long int vaddr;

	uint8_t *data_ptr,
		page[4096];

	uint32_t lp,
		 page_cnt,
		 residual;

	uint64_t build_offset,
		 size	= 4096 - offset,
		 loaded = 4096;

	struct SGX_secinfo secinfo;


	/*
	 * Compute the size of the segment to be built.  Return if this
	 * size is less then a page since the first page has been loaded.
	 */
	if ( segment->phdr.p_filesz < size )
		size = segment->phdr.p_filesz;
	size = segment->phdr.p_filesz - size;

	if ( (segment->phdr.p_memsz + offset) <= 4096 )
		return true;


	/*
	 * Iterate through the pages in the segment and add them to
	 * the enclave.
	 */
	if ( debug )
	     fprintf(stdout, "Building segment: vaddr=0x%lx, "		    \
		     "file size=0x%lx, memory size=0x%lx, offset=0x%lx, "   \
		     "size=0x%lx\n", segment->phdr.p_vaddr,		    \
		     segment->phdr.p_filesz, segment->phdr.p_memsz, offset, \
		     size);

	memset(&secinfo, '\0', sizeof(struct SGX_secinfo));

	data_ptr = (uint8_t *) segment->data->get(segment->data);
	if ( offset == 0 )
		data_ptr += 4096;
	else
		data_ptr += 4096 - offset;

	page_cnt = size / 4096;
	residual = size % 4096;
	if ( debug )
		fprintf(stdout, "Iteration size: %lx, pages = 0x%x, "	   \
			"residual = 0x%x\n", (size & ~(4096-1)), page_cnt, \
			residual);

	for (lp= 0; lp < page_cnt; ++lp) {
		/* Need flags write bitmap adjustment when available. */
		secinfo.flags = segment->flags;

		vaddr = enclave->get_address(enclave);
		if ( !enclave->add_page(enclave, data_ptr, &secinfo,
					SGX_PAGE_EXTEND) )
			ERR(goto done);
		loaded	 += 4096;
		data_ptr += 4096;

		if ( debug )
			fprintf(stdout, "\tAdded page: 0x%lx/0x%lx/0x%lx\n", \
				vaddr, segment->flags,			     \
				*((uint64_t *) data_ptr));
	}


	/*
	 * If the amount of data to be loaded is not an even multiple of
	 * a page the residual amount of data needs to be loaded as
	 * an independent page.
	 */
	if ( residual > 0 ) {
		secinfo.flags = segment->flags;

		memset(page, '\0', sizeof(page));
		memcpy(page, data_ptr, residual);

		vaddr = enclave->get_address(enclave);
		if ( !enclave->add_page(enclave, page, &secinfo, \
					SGX_PAGE_EXTEND) )
			ERR(goto done);
		loaded += 4096;

		if ( debug )
			fprintf(stdout, "\tAdded residual: "	\
				 "0x%lx/0x%lx/0x%lx\n", vaddr,	\
				segment->flags, *((uint64_t *) data_ptr));
	 }


	 /*
	  * If the virtual memory size of the segment is larger then
	  * the physical size of the segment the difference in size
	  * is added as a series of uninitialized pages.
	  */
        if ( segment->phdr.p_memsz > segment->phdr.p_filesz ) {
		memset(page, '\0', sizeof(page));
		secinfo.flags = segment->flags;

		if ( (loaded - offset) >= segment->phdr.p_memsz )
			size = 0;
		else
			size = segment->phdr.p_memsz - (loaded - offset);
		size = r2p(size);

		if ( compatibility & NULL_PADDING_NEEDED )
			size += 4096;

		if ( debug ) {
			fprintf(stdout, "\tMemory/file size mismatch, "	      \
				"%lu/%lu, size=%lu, loaded=%lu\n",	      \
				segment->phdr.p_memsz,			      \
				segment->phdr.p_filesz,			      \
				segment->phdr.p_memsz -			      \
				segment->phdr.p_filesz, loaded);
			if ( compatibility & NULL_PADDING_NEEDED )
				fputs("\tImplemented NULL padding " \
				      "workaround.\n", stdout);
			fprintf(stdout, "\tZero page count: %lu\n\n", \
				size / 4096);
		}


		build_offset = 0;
		while ( build_offset < size ) {
			if ( !enclave->add_page(enclave, page, &secinfo, \
						SGX_PAGE_EXTEND) )
				ERR(goto done);
			build_offset += 4096;
		}
	}

	retn = true;


  done:
	 return retn;
 }


 /**
  * External public method.
  *
  * This method implements loading the TEXT segments of the shared
  * enclave object file into the enclave.
  *
  * \param this		A pointer to the object which represents the
  *			enclave binary data.
  *
  * \param enclave	A pointer to the enclave object into which the
  *			pages are to be loaded.
  *
  * \return	If an error is encountered while loading segments
  *		a false value is returned.  A true value indicates the
  *		enclave was successfully loaded.
  */

 static _Bool load_segments(CO(SRDEloader, this), CO(SRDEenclave, enclave))

 {
	 STATE(S);

	 _Bool retn = false;

	 unsigned long int vaddr;

	 uint8_t *data,
		 page[4096];

	 uint32_t lp,
		  major,
		  minor,
		  segcnt;

	 uint64_t *payload,
		  offset,
		  size,
		  max_rva	= 0,
		  compatibility = 0;

	 struct segment *segptr;

	 struct SGX_secinfo secinfo;


	 /* Verify object status. */
	 if ( S->poisoned )
		 ERR(goto done);


	 /* Setup compatibility mask. */
	 if ( !S->metadata->get_version(S->metadata, &major, &minor) )
		 ERR(goto done);
	 if ( (major == 1) && (minor == 3) )
		 compatibility |= NULL_PADDING_NEEDED;


	 /* Iterate through the list of segments loading each one. */
	 segcnt = S->segments->size(S->segments) / sizeof(struct segment);
	 segptr = (struct segment *) S->segments->get(S->segments);

	 for (lp= 0; lp < segcnt; ++lp, ++segptr) {
		 data = segptr->data->get(segptr->data);

		 if ( segptr->phdr.p_vaddr > max_rva )
			 max_rva = segptr->phdr.p_vaddr;

		 if ( lp > 0 ) {
			 if ( !_finish_segment(enclave, segptr-1) )
				 ERR(goto done);
		 }

		 /* Build the first page of the enclave. */
		 offset = segptr->phdr.p_vaddr & (4096-1);
		 size = 4096 - offset;
		 if( segptr->phdr.p_filesz < size)
			 size = segptr->phdr.p_filesz;
		 memset(page, '\0', 4096);
		 memcpy(&page[offset], data, size);

		 /*
		  * Advance the enclave virtual address until it matches
		  * the start address of the segment.
		  */
		 if ( S->debug )
			 fprintf(stdout, "Enclave address: 0x%lx, vaddr: " \
				 "0x%lx\n", enclave->get_address(enclave),  \
				 segptr->phdr.p_vaddr & (~(4096-1)));
		while ( enclave->get_address(enclave) <
			(segptr->phdr.p_vaddr & (~(4096-1))) ) {
			if ( !enclave->add_hole(enclave) )
				ERR(goto done);
		}

		memset(&secinfo, '\0', sizeof(struct SGX_secinfo));
		secinfo.flags = segptr->flags;
		vaddr = enclave->get_address(enclave);
		if ( !enclave->add_page(enclave, page, &secinfo, \
					SGX_PAGE_EXTEND) )
			ERR(goto done);
		if ( S->debug ) {
			fprintf(stdout, "Segment: %u\n", lp);
			fprintf(stdout, "\tFirst page: "		      \
				"vaddr=0x%lx/rva=0x%lx offset=0x%lx, "	      \
				"size=0x%lx loaded.\n", segptr->phdr.p_vaddr, \
				segptr->phdr.p_vaddr & (~(4096-1)), offset,   \
				size);
		}
		payload = (uint64_t *) page;
		if ( S->debug )
			fprintf(stdout, "\tAdded page: 0x%lx/0x%lx/0x%lx\n", \
				vaddr, secinfo.flags, *payload);

		/* Build remaining pages. */
		if ( !_build_segment(enclave, segptr, offset, compatibility, \
				     S->debug) )
			ERR(goto done);
	}


	/* Finish last section if there was more then one segment. */
	if ( segcnt > 1 ) {
		if ( !_finish_segment(enclave, segptr-1) )
			ERR(goto done);
	}

	retn = true;

 done:

	return retn;
}


/**
 * External public method.
 *
 * This method is an interface method which requests that the
 * SGXmetada object load the layout sections of the enclave.
 *
 * \param this		A pointer to the object which represents the
 *			enclave loader data which contains the
 *			SGX metadata.
 *
 * \param enclave	A pointer to the enclave object which the
 *			layout sections are to be constructed in.
 *
 * \return	If an error is encountered while loading the layouts
 *		a false value is returned.  A true value indicates
 *		the layouts were loaded.
 */

static _Bool load_layouts(CO(SRDEloader, this), CO(SRDEenclave, enclave))

{
	STATE(S);

	_Bool retn = false;


	/* Verify the object status. */
	if ( S->poisoned )
		ERR(goto done);

	if ( !S->metadata->load_layouts(S->metadata, enclave) )
		ERR(goto done);

	retn = true;


 done:
	return retn;
}


/**
 * External public method.
 *
 * This method implements the loading of the SGX signature structure
 * from the enclave represented by the object.  This is essentially
 * a passthrough accessor call to the SGX metadata object.
 *
 * \param this	A pointer to the object representing the enclave
 *		whose signature is to be returned.
 *
 * \return	If an error is encountered while access the metadata
 *		a false value is returned.  A true value is returned
 *		if the caller is returning a valid signature structure.
 */

static _Bool get_sigstruct(CO(SRDEloader, this), \
			   struct SGX_sigstruct *sigstruct)

{
	STATE(S);

	_Bool retn = false;


	/* Verify object status. */
	if ( S->poisoned )
		ERR(goto done);


	/* Get the signature structure from the enclave metadata. */
	if ( !S->metadata->get_sigstruct(S->metadata, sigstruct) )
		ERR(goto done);

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements the loading of the SGX attributes from the
 * enclave represented by the object.  This is a passthrough accessor
 * call to the SGX metadata object.
 *
 * \param this	A pointer to the object representing the enclave
 *		whose attributes are to be returned.
 *
 * \return	If an error is encountered while retrieving the attributes
 *		a false value is returned.  A true value is returned
 *		if a valid attribute structure is being returned to the
 *		caller.
 */

static _Bool get_attributes(CO(SRDEloader, this), sgx_attributes_t *attributes)

{
	STATE(S);

	_Bool retn = false;


	/* Verify object status. */
	if ( S->poisoned )
		ERR(goto done);


	/* Get the signature structure from the enclave metadata. */
	if ( !S->metadata->get_attributes(S->metadata, attributes) )
		ERR(goto done);

	retn = true;


 done:
	if ( !retn )
		S->poisoned = true;

	return retn;
}


/**
 * External public method.
 *
 * This method implements setting the debug status of the loader.
 *
 * \param this		A pointer to the object whose debug status is
 *			to be modified.
 *
 * \return	No return value is defined.
 */

static void debug(CO(SRDEloader, this), const _Bool debug)

{
	STATE(S);


	S->debug = debug;
	return;
}


/**
 * External public method.
 *
 * This method implements a diagnostic dump of the enclave binaryd ata.
 *
 * \param this		A pointer to the object whose content is to be
 *			dumped.
 *
 * \return	No return value is defined.
 */

static void dump(CO(SRDEloader, this))

{
	STATE(S);

	uint32_t lp,
		 segcnt;

	struct segment *segptr;

	Elf64_Dyn *dynptr = S->dynptr;

	Buffer bf;


	segcnt = S->segments->size(S->segments) / sizeof(struct segment);
	segptr = (struct segment *) S->segments->get(S->segments);
	for (lp= 0; lp < segcnt; ++lp, ++segptr) {
		fprintf(stdout, "Segment #%d:\n", lp);
		fprintf(stdout, "\ttype: 0x%x\n", segptr->phdr.p_type);
		fprintf(stdout, "\tflags: 0x%x\n", segptr->phdr.p_flags);
		fprintf(stdout, "\toffset: 0x%lx\n", segptr->phdr.p_offset);
		fprintf(stdout, "\tvaddr: 0x%lx\n", segptr->phdr.p_vaddr);
		fprintf(stdout, "\tfile size: 0x%lx\n", \
			segptr->phdr.p_filesz);
		fprintf(stdout, "\tmem size: 0x%lx\n", segptr->phdr.p_memsz);
		fprintf(stdout, "\talign: 0x%lx\n", segptr->phdr.p_align);
		fprintf(stdout, "\tenclave flags: 0x%lx\n", segptr->flags);

		fprintf(stdout, "\nSegment #%d contents:\n", lp);
		bf = segptr->data;
		bf->hprint(bf);
		fputc('\n', stdout);
	}

	fputs("Dynamic symbol table:\n", stdout);
	while ( dynptr->d_tag != DT_NULL ) {
		fprintf(stdout, "\tTag: 0x%lx\n", dynptr->d_tag);
		++dynptr;
	}

	return;
}


/**
 * External public method.
 *
 * This method implements a destructor for an SRDEloader object.
 *
 * \param this	A pointer to the object which is to be destroyed.
 */

static void whack(CO(SRDEloader, this))

{
	STATE(S);

	uint32_t lp,
		 segcnt;

	struct segment *segptr;


	if ( S->soimage != NULL )
		munmap(S->soimage, S->sosize);
	if ( S->elf != NULL )
		elf_end(S->elf);
	if ( S->fd != -1 )
		close(S->fd);

	WHACK(S->metadata);

	if ( S->segments != NULL ) {
		segcnt = S->segments->size(S->segments) \
			/ sizeof(struct segment);
		segptr = (struct segment *) S->segments->get(S->segments);
		for (lp= 0; lp < segcnt; ++lp, ++segptr)
			WHACK(segptr->data);
	}
	WHACK(S->segments);

	S->root->whack(S->root, this, S);
	return;
}


/**
 * External constructor call.
 *
 * This function implements a constructor call for a SRDEloader object.
 *
 * \return	A pointer to the initialized SRDEloader.  A null value
 *		indicates an error was encountered in object generation.
 */

extern SRDEloader NAAAIM_SRDEloader_Init(void)

{
	auto Origin root;

	auto SRDEloader this = NULL;

	auto struct HurdLib_Origin_Retn retn;


	/* Get the root object. */
	root = HurdLib_Origin_Init();

	/* Allocate the object and internal state. */
	retn.object_size  = sizeof(struct NAAAIM_SRDEloader);
	retn.state_size   = sizeof(struct NAAAIM_SRDEloader_State);
	if ( !root->init(root, NAAAIM_LIBID, NAAAIM_SRDEloader_OBJID, &retn) )
		return NULL;
	this	    	  = retn.object;
	this->state 	  = retn.state;
	this->state->root = root;

	/* Initialize aggregate objects. */

	/* Initialize object state. */
	_init_state(this->state);

	/* Method initialization. */
	this->load	= load;
	this->load_secs = load_secs;

	this->load_memory      = load_memory;
	this->load_secs_memory = load_secs_memory;

	this->load_segments = load_segments;
	this->load_layouts  = load_layouts;

	this->get_sigstruct  = get_sigstruct;
	this->get_attributes = get_attributes;

	this->debug = debug;
	this->dump  = dump;
	this->whack = whack;

	return this;
}
