/* Copyright (C) 1992-2010 I/O Performance, Inc. and the
 * United States Departments of Energy (DoE) and Defense (DoD)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named 'Copying'; if not, write to
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139.
 */
/* Principal Author:
 *      Tom Ruwart (tmruwart@ioperformance.com)
 * Contributing Authors:
 *       Steve Hodson, DoE/ORNL
 *       Steve Poole, DoE/ORNL
 *       Brad Settlemyer, DoE/ORNL
 *       Russell Cattelan, Digital Elves
 *       Alex Elder
 * Funding and resources provided by:
 * Oak Ridge National Labs, Department of Energy and Department of Defense
 *  Extreme Scale Systems Center ( ESSC ) http://www.csm.ornl.gov/essc/
 *  and the wonderful people at I/O Performance, Inc.
 */
/*
 * This file contains the subroutines that support the Target threads.
 */
#include "xint.h"

/*----------------------------------------------------------------------------*/
/* xdd_targetpass() - This subroutine will perform a single pass
 */
int32_t
xdd_targetpass(xdd_plan_t* planp, target_data_t *tdp) {

	/* Before we get started, check to see if we need to reset the 
	 * run_status in case we are using the start trigger.
	 */
	if (tdp->td_target_options & TO_WAITFORSTART) {
		if (tdp->td_trigp) tdp->td_trigp->run_status = 0;
	}

	// This barrier is to ensure that all TARGETS start at same time 

	xdd_barrier(&planp->results_targets_startpass_barrier,&tdp->td_occupant,0);

	/* Check to see if any of the other threads have aborted */
	if (xgp->abort) {
		fprintf(xgp->errout,"%s: ERROR: xdd_targetpass: Target number %d name '%s' Aborting due to failure with another target\n",
			xgp->progname,
			tdp->td_target_number,
			tdp->td_target_full_pathname);
		fflush(xgp->errout);
		return(-1);
	}

 	// This will wait for the interactive command processor to say go if we are in interactive mode
	if (planp->plan_options & PLAN_INTERACTIVE) 
		xdd_barrier(&planp->interactive_barrier,&tdp->td_occupant,0);
	if (xgp->abort) 
		return(0);

	// If this run has completed or we've been canceled, then exit this loop
	if (xgp->run_complete)
		return(0);

	// Things to do before this pass is started
	xdd_target_ttd_before_pass(tdp);

	// Get the next available worker thread and give it a task to perform
	// We stay in the following loop for a single PASS
	tdp->td_tgtstp->my_current_byte_location = 0;
	tdp->td_tgtstp->my_current_op_number = 0;
	tdp->td_bytes_issued = 0;
	tdp->td_bytes_completed = 0;
	tdp->td_bytes_remaining = tdp->td_target_bytes_to_xfer_per_pass;

/////////////////////////////// PSEUDO-Loop Starts Here ////////////////////////
// The PSEUDO-Loop just means that the actual "looping" in done in 
// the xdd_targetpass_loop() subroutine (or the e2e equivalent).
	// The pass loops are handled by one of two subroutines depending on 
	// whether this is the Destination Side of an E2E operation or not. 
	tdp->td_tgtstp->my_current_state &= ~CURRENT_STATE_PASS_COMPLETE;
	if (tdp->td_target_options & TO_ENDTOEND) { // E2E operations are *different*
		if (tdp->td_target_options & TO_E2E_SOURCE)
		    xdd_targetpass_e2e_loop_src(planp, tdp);
		else xdd_targetpass_e2e_loop_dst(planp, tdp);
	} else { // Normal operations (other than E2E)
	    xdd_targetpass_loop(planp, tdp);
	}
	tdp->td_tgtstp->my_current_state |= CURRENT_STATE_PASS_COMPLETE;
/////////////////////////////// PSEUDO-Loop Ends  Here /////////////////////////
	// If this is an E2E operation and we had gotten canceled - just return
	if ((tdp->td_target_options & TO_ENDTOEND) && (xgp->canceled))
		return(-1); 

	// Things that the Target Thread needs to do after a pass has completed
	xdd_target_ttd_after_pass(tdp);

	// Release the results_manager() to process/display the 
	// results for this pass
	xdd_barrier(&planp->results_targets_endpass_barrier,&tdp->td_occupant,0);

	// At this point all the Target Threads have completed their pass and 
	// have passed thru the previous barrier releasing the results_manager() 
	// to process/display the results for this pass. 

	// Wait at this barrier for the results_manager() to process/display the 
	// results for this last pass
	xdd_barrier(&planp->results_targets_display_barrier,&tdp->td_occupant,0);

	// This pass is complete - return to the Target Thread
	return(0);
} // End of xdd_targetpass()

/*----------------------------------------------------------------------------*/
/* xdd_targetpass_loop() - This subroutine will assign tasks to Worker Threads 
 * until all bytes have been processed. 
 * 
 * This subroutine is called by xdd_targetpass().
 */
void
xdd_targetpass_loop(xdd_plan_t* planp, target_data_t *tdp) {
	worker_data_t	*wdp;
	int		q;
	int32_t	status;	// Return status from various subroutines


/////////////////////////////// Loop Starts Here ///////////////////////////////
// This loop will transfer all data for a target until it runs out of
// bytes or if we get canceled.
// This loop will block/wait in xdd_get_and_available_worker_thread() until a 
// worker thread becomes available for use. The number of worker threads available to do
// work is determined by the queue_depth for this target. Therefore, if the
// queue_depth is 1 then there is only 1 worker thread available and this loop will
// only ever be able to issue one I/O operation at a time and will have to wait
// for an I/O operation to complete before moving on to the next. 
//
	while (tdp->td_bytes_remaining) {
		// Lock Step Processing (located in lockstep.c)
		// When the -lockstep option is specified, the xdd_lockstep()subroutine 
		// will perform all I/O operations for a pass. Thus, when xdd_lockstep()
		// returns, it is necessary to set the "bytes_remaining" to zero so that
		// it looks like a normal completion.
		if (tdp->td_lsp) {
			status = xdd_lockstep(tdp);
			tdp->td_bytes_remaining = 0;
			break;
		}

		// Get pointer to next Worker Thread to issue a task to
		wdp = xdd_get_any_available_worker_thread(tdp);

		// Things to do before an I/O is issued
		status = xdd_target_ttd_before_io_op(tdp, wdp);
		if (status != XDD_RC_GOOD) {
			// Mark this worker thread NOT BUSY and break out of this loop
			pthread_mutex_lock(&wdp->wd_thread_target_sync_mutex);
			wdp->wd_thread_target_sync &= ~WTSYNC_BUSY; // Mark this Worker Thread NOT Busy
			pthread_mutex_unlock(&wdp->wd_thread_target_sync_mutex);
			break;
		}

		// Set up the task for the Worker Thread
		xdd_targetpass_task_setup(wdp);

		// Release the Worker Thread to let it start working on this task.
		// This effectively causes the I/O operation to be issued.
		xdd_barrier(&wdp->wd_thread_targetpass_wait_for_task_barrier,&tdp->td_occupant,0);

	} // End of WHILE loop that transfers data for a single pass
//
/////////////////////////////// Loop Ends Here /////////////////////////////////

	// Check to see if we've been canceled - if so, we need to leave 
	if (xgp->canceled) {
		fprintf(xgp->errout,"\n%s: xdd_targetpass_loop: Target %d: ERROR: Canceled!\n",
			xgp->progname,
			tdp->td_target_number);
		return;
	}
	// Wait for all Worker Threads to complete their most recent task
	// The easiest way to do this is to get the Worker Thread pointer for each
	// Worker Thread specifically and then reset it's "busy" bit to 0.
	for (q = 0; q < tdp->td_queue_depth; q++) {
		wdp = xdd_get_specific_worker_thread(tdp,q);
		pthread_mutex_lock(&wdp->wd_thread_target_sync_mutex);
		wdp->wd_thread_target_sync &= ~WTSYNC_BUSY; // Mark this Worker Thread NOT Busy
		pthread_mutex_unlock(&wdp->wd_thread_target_sync_mutex);
	}
	if (tdp->td_tgtstp->my_current_io_status != 0) 
		planp->target_errno[tdp->td_target_number] = XDD_RETURN_VALUE_IOERROR;

	return;
} // End of xdd_targetpass_loop()

/*----------------------------------------------------------------------------*/
/* xdd_targetpass_task_setup() - This subroutine will set up the task info for an I/O
 */
void
xdd_targetpass_task_setup(worker_data_t *wdp) {
	target_data_t	*tdp;

	tdp = wdp->wd_tdp;
	// Assign an IO task to this worker thread
	wdp->wd_task_request = TASK_REQ_IO;

	// Get the most recent File Descriptor in case it changed...
	wdp->wd_file_desc = tdp->td_file_desc;

	// Set the Operation Type
	if (tdp->td_seekhdr.seeks[tdp->td_tgtstp->my_current_op_number].operation == SO_OP_WRITE) // Write Operation
		tdp->td_tgtstp->my_current_op_type = OP_TYPE_WRITE;
	else if (tdp->td_seekhdr.seeks[tdp->td_tgtstp->my_current_op_number].operation == SO_OP_READ) // READ Operation
		tdp->td_tgtstp->my_current_op_type = OP_TYPE_READ;
	else tdp->td_tgtstp->my_current_op_type = OP_TYPE_NOOP;

	// Figure out the transfer size to use for this I/O
	if (tdp->td_bytes_remaining < tdp->td_iosize)
		tdp->td_tgtstp->my_current_io_size = tdp->td_bytes_remaining;
	else tdp->td_tgtstp->my_current_io_size = tdp->td_iosize;

	// Set the location to seek to 
	tdp->td_tgtstp->my_current_byte_location = tdp->td_tgtstp->my_current_byte_location;

	// Remember the operation number for this target
	tdp->td_tgtstp->target_op_number = tdp->td_tgtstp->my_current_op_number;
	if (tdp->td_tgtstp->my_current_op_number == 0) 
		nclk_now(&tdp->td_tgtstp->my_first_op_start_time);

   	// If time stamping is on then assign a time stamp entry to this Worker Thread
   	if ((tdp->td_tsp->ts_options & (TS_ON|TS_TRIGGERED))) {
		wdp->wd_tsp->ts_current_entry = tdp->td_tsp->ts_current_entry;	
		tdp->td_tsp->ts_current_entry++;
		if (tdp->td_tsp->ts_options & TS_ONESHOT) { // Check to see if we are at the end of the ts buffer
			if (tdp->td_tsp->ts_current_entry == tdp->td_tsp->ts_size)
				tdp->td_tsp->ts_options &= ~TS_ON; // Turn off Time Stamping now that we are at the end of the time stamp buffer
		} else if (tdp->td_tsp->ts_options & TS_WRAP) {
			tdp->td_tsp->ts_current_entry = 0; // Wrap to the beginning of the time stamp buffer
		}
		wdp->wd_ttp->tte[wdp->wd_tsp->ts_current_entry].pass_number = tdp->td_tgtstp->my_current_pass_number;
		wdp->wd_ttp->tte[wdp->wd_tsp->ts_current_entry].worker_thread_number = wdp->wd_thread_number;
		wdp->wd_ttp->tte[wdp->wd_tsp->ts_current_entry].thread_id     = wdp->wd_thread_id;
		wdp->wd_ttp->tte[wdp->wd_tsp->ts_current_entry].op_type = tdp->td_tgtstp->my_current_op_type;
		wdp->wd_ttp->tte[wdp->wd_tsp->ts_current_entry].op_number = tdp->td_tgtstp->target_op_number;
		wdp->wd_ttp->tte[wdp->wd_tsp->ts_current_entry].byte_location = tdp->td_tgtstp->my_current_byte_location;
	}
	// Update the pointers/counters in the Target PTDS to get 
	// ready for the next I/O operation
	tdp->td_tgtstp->my_current_byte_location += tdp->td_tgtstp->my_current_io_size;
	tdp->td_tgtstp->my_current_op_number++;
	tdp->td_bytes_issued += tdp->td_tgtstp->my_current_io_size;
	tdp->td_bytes_remaining -= tdp->td_tgtstp->my_current_io_size;

} // End of xdd_targetpass_task_setup()

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
