/*
 * XDD - a data movement and benchmarking toolkit
 *
 * Copyright (C) 1992-23 I/O Performance, Inc.
 * Copyright (C) 2009-23 UT-Battelle, LLC
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */
/*
 * This file contains the subroutines that perform an "open" operation
 * on the target device/file.
 */
#include "xint.h"
#include <errno.h>

/*----------------------------------------------------------------------------*/
/* xdd_target_open() - open the target device and do all necessary
 * sanity checks.  This routine simply calls the appropriate open routine
 * for the given operating system it is compiled for.
 * Each of the open routines will set the file descriptor in the Target Data
 * Struct to an appropriate value. If the open routine succeeds then a
 * successful status of 0 (zero) is returned.
 * Otherwise, a -1 returned to indicate there was an error.
 * The OS-specific open routine will issue the appropriate error messages.
 */
int32_t
xdd_target_open(target_data_t *tdp) {
	int32_t		status;		// Status of the open call


	// If this is a NULL target then don't bother openning it
	if (tdp->td_target_options & TO_NULL_TARGET)
		return(0);

	/* create the fully qualified target name */
	xdd_target_name(tdp);

	// Check to see if this target really exists and record what kind of target it is
	status = xdd_target_existence_check(tdp);
	if (status < 0)
		return(-1);

	nclk_now(&tdp->td_open_start_time); // Record the starting time of the open
	// Set the default and optional open flags
	tdp->td_open_flags |= O_CREAT;

	// Call the OS-specific open call
	xdd_target_open_for_os(tdp);

	nclk_now(&tdp->td_open_end_time); // Record the ending time of the open
	// Check the status of the OPEN operation to see if it worked
	if (tdp->td_file_desc < 0) {
			fprintf(xgp->errout, "%s: xdd_target_open: ERROR: Could not open target number %d name %s\n",
				xgp->progname,
				tdp->td_target_number,
				tdp->td_target_full_pathname);
			fflush(xgp->errout);
			perror("reason");
			return(-1);
	}


	return(0);

} // End of xdd_target_open()

/*----------------------------------------------------------------------------*/
/* xdd_target_reopen() - This subroutine will close and reopen a new target
 * file and then request that all Worker Threads do the same.
 */
void
xdd_target_reopen(target_data_t *tdp) {
	int32_t		status;		// Status of the open call


	// Close the existing target
	status = close(tdp->td_file_desc);
	if (status != 0) { /* error opening target */
		fprintf(xgp->errout, "%s: xdd_target_reopen: ERROR: Aborting I/O for target number %d "
			"name %s due to close failure\n",
			xgp->progname,
			tdp->td_target_number,
			tdp->td_target_full_pathname);
		fflush(xgp->errout);
		xgp->abort = 1;
	}

	// If we need to "recreate" the file for each pass then we should delete it here before we re-open it
	if (tdp->td_target_options & TO_RECREATE) {
		unlink(tdp->td_target_full_pathname);
	}
	/* open the old/new/recreated target file */
	status = xdd_target_open(tdp);
	if (status != 0) { /* error opening target */
		fprintf(xgp->errout, "%s: xdd_target_reopen: ERROR: Aborting I/O for target number %d "
			"name %s due to open failure\n",
			xgp->progname,
			tdp->td_target_number,
			tdp->td_target_full_pathname);
		fflush(xgp->errout);
		xgp->abort = 1;
	}
	return;
} // End of xdd_target_reopen()

/*----------------------------------------------------------------------------*/
/* xdd_target_shallow_open() - Do all necessary sanity checks, but instead of
 * doing the open, simply copy the file descriptor from the target thread.
 * Only works for Worker Threads spawned from a target thread that has already
 * opened the file, and OS that support pread/pwrite.
 * Otherwise, a -1 returned to indicate there was an error.
 */
int32_t
xdd_target_shallow_open(worker_data_t *wdp) {
	target_data_t	*tdp;

	tdp = wdp->wd_tdp;

	// If this is a NULL target then don't bother opening it
	if (tdp->td_target_options & TO_NULL_TARGET)
		return(0);

	/* create the fully qualified target name */
	xdd_target_name(tdp);

	nclk_now(&tdp->td_open_start_time); // Record the starting time of the open

	wdp->wd_task.task_file_desc = tdp->td_file_desc;

	nclk_now(&tdp->td_open_end_time); // Record the ending time of the open

	// Check the status of the OPEN operation to see if it worked
	if (tdp->td_file_desc < 0) {
		fprintf(xgp->errout, "%s: xdd_target_shallow_open: ERROR: Could not shallow open target "
			"number %d name %s\n",
			xgp->progname,
			tdp->td_target_number,
			tdp->td_target_full_pathname);
			fflush(xgp->errout);
		perror("reason");
		return(-1);
	}

	return(0);

} // End of xdd_target_shallow_open()

/*----------------------------------------------------------------------------*/
/* xdd_target_name() - Generate the name of the target
 */
void
xdd_target_name(target_data_t *tdp) {
	int			target_name_length;
	char 		target_full_pathname[MAX_TARGET_NAME_LENGTH]; /* target directory + target name */

	/* Set the extension to correspond with the current pass number only if the operation is
	 * not an e2e islocal operation. Else the output files will be updated with extensions */
	if ((tdp->td_target_options & TO_CREATE_NEW_FILES) &&
		!(tdp->td_planp->plan_options & PLAN_ENDTOEND_LOCAL)) { // Create a new file name for this target
		sprintf(tdp->td_target_extension,"%08d",tdp->td_counters.tc_pass_number);
	}

	/* create the fully qualified target name */
	memset(target_full_pathname,0,sizeof(target_full_pathname));
	if (strlen(tdp->td_target_directory) > 0) {
		sprintf(target_full_pathname, "%s%s", tdp->td_target_directory, tdp->td_target_basename);
	}
	else {
		sprintf(target_full_pathname, "%s",tdp->td_target_basename);
	}

	if ((tdp->td_target_options & TO_CREATE_NEW_FILES) &&
		!(tdp->td_planp->plan_options & PLAN_ENDTOEND_LOCAL)) { // Add the target extension to the name
		strcat(target_full_pathname, ".");
		strcat(target_full_pathname, tdp->td_target_extension);
	}
	target_name_length = strlen(target_full_pathname);
	if (tdp->td_target_full_pathname == NULL) {
		tdp->td_target_full_pathname = (char *)malloc(target_name_length+32);
	}
	sprintf(tdp->td_target_full_pathname,"%s",target_full_pathname);

} // End of xdd_target_name()

/*----------------------------------------------------------------------------*/
/* xdd_target_existence_check() - Check to see if the target exists.
 * Return -1 is it does not exist and this is a read or 0 if it does exist.
 */
int32_t
xdd_target_existence_check(target_data_t *tdp) {
	int32_t  	status; /* Status of various system function calls */



	/* Stat the file before it is opened */
	status = stat(tdp->td_target_full_pathname,&tdp->td_statbuf);

	// If stat did not work then it could be that the file does not exist.
	// For write operations this is ok because the file will just be created.
	// Hence, if the errno is ENOENT then set the target file type to
	// be a "regular" file, issue an information message, and continue.
	// If this is a read or a mixed read/write operation then issue an
	// error message and return a -1 indicating an error on open.
	//
	if (status < 0) {
		if ((ENOENT == errno) && (tdp->td_rwratio == 0.0)) { // File does not yet exist
			tdp->td_target_options |= TO_REGULARFILE;
			fprintf(xgp->errout, "%s: xdd_target_existence_check: NOTICE: target number %d name %s does not "
				"exist so it will be created.\n",
				xgp->progname,
				tdp->td_target_number,
				tdp->td_target_full_pathname);
		} else { // Something is wrong with the file
			fprintf(xgp->errout, "%s: xdd_target_existence_check: ERROR: Could not stat target number %d name %s\n",
				xgp->progname,
				tdp->td_target_number,
				tdp->td_target_full_pathname);
			fflush(xgp->errout);
			perror("reason");
			return(-1);
		}
	}
	// Check to see what kind of target file this really is
	if (S_ISREG(tdp->td_statbuf.st_mode)) {  // Is this a Regular File?
		tdp->td_target_options |= TO_REGULARFILE;
	} else if (S_ISDIR(tdp->td_statbuf.st_mode)) { // Is this a Directory?
		tdp->td_target_options |= TO_REGULARFILE;
		fprintf(xgp->errout, "%s: xdd_target_existence_check: WARNING: Target number %d name %s is a DIRECTORY\n",
			xgp->progname,
			tdp->td_target_number,
			tdp->td_target_full_pathname);
		fflush(xgp->errout);
	} else if (S_ISCHR(tdp->td_statbuf.st_mode)) { // Is this a Character Device?
		tdp->td_target_options |= TO_DEVICEFILE;
	} else if (S_ISBLK(tdp->td_statbuf.st_mode)) {  // Is this a Block Device?
		tdp->td_target_options |= TO_DEVICEFILE;
	} else if (S_ISSOCK(tdp->td_statbuf.st_mode)) {  // Is this a Socket?
		tdp->td_target_options |= TO_SOCKET;
	}
	/* If this is a regular file, and we are trying to read it, then
 	* check to see that we are not going to read off the end of the
 	* file. Send out a WARNING if this is a possibility
 	*/
	if ( (tdp->td_target_options & TO_REGULARFILE) && !(tdp->td_rwratio < 1.0)) { // This is a purely read operation
		if (tdp->td_target_bytes_to_xfer_per_pass > (uint64_t)tdp->td_statbuf.st_size) { // Check to make sure that the we won't read off the end of the file
			fprintf(xgp->errout, "%s: xdd_target_existence_check: WARNING: Target number %d name %s <%lld bytes> "
				"is shorter than the the total requested transfer size <%lld bytes>\n",
				xgp->progname,
				tdp->td_target_number,
				tdp->td_target_full_pathname,
				(long long)tdp->td_statbuf.st_size,
				(long long)tdp->td_target_bytes_to_xfer_per_pass);
			fflush(xgp->errout);
		}
	}
	/* Perform sanity checks */
	if (tdp->td_target_options & TO_DIO) {
		/* make sure it is a regular file(even if it does NOT exist) or block device, otherwise fail */
  		if ( !(tdp->td_target_options & TO_REGULARFILE) && !(S_ISBLK(tdp->td_statbuf.st_mode))) {
			fprintf(xgp->errout, "%s: xdd_target_existence_check: WARNING: Target number %d name %s must be a "
				"regular file or block special file (aka block device) when used with the -dio flag\n",
				xgp->progname,
				tdp->td_target_number,
				tdp->td_target_full_pathname);
			fflush(xgp->errout);
		}
	}
	return(0);
} // End of xdd_target_existence_check()

/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/
/*                             OS-specific Open Routines                                                    */
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/*----------------------------------------------------------------------------*/
/* xdd_target_open_for_linux() - Open routine for linux
 * This routine currently always returns 0.
 * The "td_file_desc" member of the Target Data Struct is the actual return
 * value from the open() system call and will indicate if there was an error.
 * The errno should also be valid upon return from this subroutine.
 */
int32_t
xdd_target_open_for_os(target_data_t *tdp) {

	// Set the Open Flags to indicate DirectIO if requested
	if (tdp->td_target_options & TO_DIO)
		tdp->td_open_flags |= O_DIRECT;
	else tdp->td_open_flags &= ~O_DIRECT;

	// Check to see if this is a SCSI Generic (/dev/sg) device - SPECIFIC to LINUX
	if (tdp->td_target_options & TO_DEVICEFILE) {
		if (strncmp(tdp->td_target_full_pathname, "/dev/sg", 7) == 0 ) // is this an "sg" device?
			tdp->td_target_options |= TO_SGIO; // Yes - turn on SGIO Mode
	}

	/* open the target */
	if (tdp->td_rwratio == 0.0) {
		if (tdp->td_target_options & TO_SGIO) {
			tdp->td_file_desc = open(tdp->td_target_full_pathname,tdp->td_open_flags|O_RDWR, 0777); /* Must open RDWR for SGIO */

			if (xgp->global_options & GO_DEBUG_OPEN) {
				fprintf(stderr, "DEBUG_OPEN: %lld: xdd_target_open_for_os: Target: %d: Worker: %d: "
					"SGIO WRITE: file_desc: %d\n ",
					(long long int)pclk_now(),
					tdp->td_target_number,
					tdp->td_queue_depth,
					tdp->td_file_desc);
			}

			xdd_sg_set_reserved_size(tdp,tdp->td_file_desc);
			xdd_sg_get_version(tdp,tdp->td_file_desc);
		} else {
			tdp->td_file_desc = open(tdp->td_target_full_pathname,tdp->td_open_flags|O_WRONLY, 0666); /* write only */

			if (xgp->global_options & GO_DEBUG_OPEN) {
				fprintf(stderr, "DEBUG_OPEN: %lld: xdd_target_open_for_os: Target: %d: Worker: %d: "
					"WRITE ONLY: file_desc: %d\n ",
					(long long int)pclk_now(),
					tdp->td_target_number,
					tdp->td_queue_depth,
					tdp->td_file_desc);
			}
		}
	} else if (tdp->td_rwratio == 1.0) { /* read only */
		tdp->td_open_flags &= ~O_CREAT;
		if (tdp->td_target_options & TO_SGIO) {
			tdp->td_file_desc = open(tdp->td_target_full_pathname,tdp->td_open_flags|O_RDWR, 0777); /* Must open RDWR for SGIO  */

			if (xgp->global_options & GO_DEBUG_OPEN) {
				fprintf(stderr, "DEBUG_OPEN: %lld: xdd_target_open_for_os: Target: %d: Worker: %d: "
					"SGIO READ: file_desc: %d\n ",
					(long long int)pclk_now(),
					tdp->td_target_number,
					tdp->td_queue_depth,
					tdp->td_file_desc);
			}

			xdd_sg_set_reserved_size(tdp,tdp->td_file_desc);
			xdd_sg_get_version(tdp,tdp->td_file_desc);
		} else {
			tdp->td_file_desc = open(tdp->td_target_full_pathname,tdp->td_open_flags|O_RDONLY, 0777); /* Read only */

			if (xgp->global_options & GO_DEBUG_OPEN) {
				fprintf(stderr, "DEBUG_OPEN: %lld: xdd_target_open_for_os: Target: %d: Worker: %d: "
					"READ ONLY: file_desc: %d\n ",
					(long long int)pclk_now(),
					tdp->td_target_number,
					tdp->td_queue_depth,
					tdp->td_file_desc);
			}
		}
	} else if ((tdp->td_rwratio > 0.0) && (tdp->td_rwratio < 1.0)) { /* read/write mix */
		tdp->td_open_flags &= ~O_CREAT;
		tdp->td_file_desc = open(tdp->td_target_full_pathname,tdp->td_open_flags|O_RDWR, 0666);

		if (xgp->global_options & GO_DEBUG_OPEN) {
			fprintf(stderr, "DEBUG_OPEN: %lld: xdd_target_open_for_os: Target: %d: Worker: %d: "
				"MIXED RW: file_desc: %d\n ",
				(long long int)pclk_now(),
				tdp->td_target_number,
				tdp->td_queue_depth,
				tdp->td_file_desc);
		}
	}

	return(0);

} // End of xdd_target_open_for_linux()

/*
 * Local variables:
 *  indent-tabs-mode: t
 *  default-tab-width: 4
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sts=4 sw=4 noexpandtab
 */
