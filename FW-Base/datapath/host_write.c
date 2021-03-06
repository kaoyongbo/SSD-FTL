/*
 * Copyright (C) 2018-2020 NET-Swift.
 * Initial release: Dengcai Xu <dengcaixu@net-swift.com>
 *
 * ALL RIGHTS RESERVED. These coded instructions and program statements are
 * copyrighted works and confidential proprietary information of NET-Swift Corp.
 * They may not be modified, copied, reproduced, distributed, or disclosed to
 * third parties in any manner, medium, or form, in whole or in part.
 *
 * function: 
 * host write datapath  wdp
 *
 */


//Assign PPA for each CPAs[scpa: scpa+nppas-1]   nppas is 1-base
//assign more extra ppa to skip bb and log, so that the next io can get normal quickly 
bool atc_assign_ppa(u8 band, u32 scpa, u16 nppas, ppa_t *ppalist)
{
	u8 pg_type;
	u16 idx = 0;
	u32 cpa = scpa;
	bool flush_log_page = false;
	bool flush_raif1 = false;
	bool flush_raif2 = false;

	band_info_t *bandinfo = get_band_info(band);
	//ASSERT(bandinfo)

	// assign FPA from current_ppa
	ppa_t current_ppa = bandinfo->current_ppa;

	while(1) {
		pg_type = lookup_page_type_fast(current_ppa);

		if ((pg_type == NORMAL_PAGE) && (idx >= nppas))
			break;   // guarantee the fpa next kick in is Normal
	
		switch (pg_type) {
		case NORMAL_PAGE:
			ppalist[idx] = current_ppa;
			cpa = scpa + idx;
			idx++;
			break;
		case BADBLK_PAGE:
			cpa = SPECIAL_CPA_INVALID;
			break;
		case FTL_LOG_PAGE:
			cpa = SPECIAL_CPA_FTLLOG;
			flush_log_page = true;
			break;
		case RAIF1_PAGE:
			cpa = SPECIAL_CPA_RAIF1;
			break;
		case RAIF2_PAGE:
			cpa = SPECIAL_CPA_RAIF2;			
			break;
		}


		// LOG Page is offset by fpa:
		// write the cpa and timestamp, CPH
		update_log_page();

		// special Page Process
		if (flush_log_page) {
			flush_log_page = false;
			// flush LOG Page to Host Band
			commit_log_page();

			// if CKPT Enable
			ckpt_monitor();		
		}

		if (flush_raif1) {
			flush_raif1 = false;
			commit_raif1();
		}

		if (flush_raif2) {
			flush_raif2 = false;
			commit_raif2();
		}

		//XXX: ep pl ch lun pg blk, when  switch blk, get from free_rbtree
		increase_ppa(current_ppa);
	}

	bandinfo->current_ppa = current_ppa;

	return 0;
}


void wpb_init(u8 band)
{
	base_ppa = atc_assign_ppa(u8 band, u32 scpa, u16 nppas, ppa_t * ppalist);
	wpb->fpa = base_ppa;
}

//write_datapath_atc
//write_datapath_stc
//write_datapath_fdc

// TODO: optimized when message send fail due to part busy, not re-constructured phif_cmd_req
// TODO: enqueue it in another queue host_nvme_cmd_wait_port_q, so next schedule, we only need send it directly.
void write_datapath_hdc(host_nvme_cmd_entry *host_cmd_entry)
{
	phif_cmd_cpl *cpl;

	switch (host_cmd_entry->state) 
	{	
		case WRITE_FLOW_STATE_QUEUED:
			phif_cmd_req req;
			dp_setup_phif_cmd_req(&req, host_cmd_entry);
			host_cmd_entry->state = WRITE_FLOW_STATE_PHIF_REQ_READY;

		case WRITE_FLOW_STATE_PHIF_REQ_READY:
			if (send_phif_cmd_req(&req)) {
				// message port not available
				enqueue_front(&host_nvme_cmd_pend_q, host_cmd_entry->next);
				host_cmd_entry->state = WRITE_FLOW_STATE_QUEUED;	
				break;
			} else {
				host_cmd_entry->state = WRITE_FLOW_STATE_PHIF_REQ_SENDOUT;	
			}
			
		case WRITE_FLOW_STATE_PHIF_REQ_SENDOUT:
			host_cmd_entry->state = WRITE_FLOW_STATE_WAIT_PHIF_RSP;
			
		case WRITE_FLOW_STATE_WAIT_PHIF_RSP:
			break;
			
		case WRITE_FLOW_STATE_HAS_PHIF_RSP:
			cpl = __get_host_cmd_cpl_entry(host_cmd_entry->cmd_tag);
			setup_phif_cmd_cpl(cpl, host_cmd_entry);
			host_cmd_entry->state = WRITE_FLOW_STATE_PHIF_CPL_READY;

		case WRITE_FLOW_STATE_PHIF_CPL_READY
			cpl = __get_host_cmd_cpl_entry(host_cmd_entry->cmd_tag);   // MUST re get
			if (send_phif_cmd_cpl(cpl)) {
				// message Port BUSY
				enqueue_front(&host_nvme_cpl_pend_q, host_cmd_entry->next);
				host_cmd_entry->state = WRITE_FLOW_STATE_PHIF_CPL_READY;				
				break;
			} else {
				host_cmd_entry->state = WRITE_FLOW_STATE_PHIF_CPL_SENDOUT;
			}
			
		case WRITE_FLOW_STATE_PHIF_CPL_SENDOUT:
			// this host cmd is complete,tag will be released
			host_cmd_entry->state = WRITE_FLOW_STATE_COMPLETE;
			
		case WRITE_FLOW_STATE_COMPLETE:
			// get next pending
			host_cmd_entry = get_next_host_cmd_entry();
			if (host_cmd_entry) {
				write_datapath_hdc(host_cmd_entry);
			} else {
				// there is no pending cmd need process
				return;
			}
	}

	return;
}

// FRWMGR ---> ATC, data has write to NAND, ATC update MAP
atc_write_response()
{

}

