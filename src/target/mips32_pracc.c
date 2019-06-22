/***************************************************************************
 *   Copyright (C) 2008 by Spencer Oliver                                  *
 *   spen@spen-soft.co.uk                                                  *
 *                                                                         *
 *   Copyright (C) 2008 by David T.L. Wong                                 *
 *                                                                         *
 *   Copyright (C) 2009 by David N. Claffey <dnclaffey@gmail.com>          *
 *                                                                         *
 *   Copyright (C) 2011 by Drasko DRASKOVIC                                *
 *   drasko.draskovic@gmail.com                                            *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/
/*
 * This version has optimized assembly routines for 32 bit operations:
 * - read word
 * - write word
 * - write array of words
 *
 * One thing to be aware of is that the MIPS32 cpu will execute the
 * instruction after a branch instruction (one delay slot).
 *
 * For example:
 *  LW $2, ($5 +10)
 *  B foo
 *  LW $1, ($2 +100)
 *
 * The LW $1, ($2 +100) instruction is also executed. If this is
 * not wanted a NOP can be inserted:
 *
 *  LW $2, ($5 +10)
 *  B foo
 *  NOP
 *  LW $1, ($2 +100)
 *
 * or the code can be changed to:
 *
 *  B foo
 *  LW $2, ($5 +10)
 *  LW $1, ($2 +100)
 *
 * The original code contained NOPs. I have removed these and moved
 * the branches.
 *
 * These changes result in a 35% speed increase when programming an
 * external flash.
 *
 * More improvement could be gained if the registers do no need
 * to be preserved but in that case the routines should be aware
 * OpenOCD is used as a flash programmer or as a debug tool.
 *
 * Nico Coesel
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <helper/time_support.h>
#include "mips32.h"
#include "mips32_pracc.h"
static int wait_for_pracc_rw(struct mips_ejtag *ejtag_info, uint32_t *ctrl)
{
	uint32_t ejtag_ctrl;
	int64_t then = timeval_ms();
	/* wait for the PrAcc to become "1" */
	mips_ejtag_set_instr(ejtag_info, EJTAG_INST_CONTROL);
	while (1) {
		ejtag_ctrl = ejtag_info->ejtag_ctrl;
		int retval = mips_ejtag_drscan_32(ejtag_info, &ejtag_ctrl);
		if (retval != ERROR_OK)
			return retval;
		if (ejtag_ctrl & EJTAG_CTRL_PRACC)
			break;
		int64_t timeout = timeval_ms() - then;
		if (timeout > 1000) {
			LOG_ERROR("DEBUGMODULE: No memory access in progress!");
			return ERROR_JTAG_DEVICE_ERROR;
		}
	}
	*ctrl = ejtag_ctrl;
	return ERROR_OK;
}

static int try_wait_for_pracc_rw(struct mips_ejtag *ejtag_info, uint32_t *ctrl)
{
	uint32_t ejtag_ctrl;
	mips_ejtag_set_instr(ejtag_info, EJTAG_INST_CONTROL);
	ejtag_ctrl = ejtag_info->ejtag_ctrl;
	int retval = mips_ejtag_drscan_32(ejtag_info, &ejtag_ctrl);
	if (retval != ERROR_OK) return retval;
	*ctrl = ejtag_ctrl;
	return ERROR_OK;
}
/* Shift in control and address for a new processor access, save them in ejtag_info */
static int mips32_pracc_read_ctrl_addr(struct mips_ejtag *ejtag_info)
{
	int retval = wait_for_pracc_rw(ejtag_info, &ejtag_info->pa_ctrl);
	if (retval != ERROR_OK)
		return retval;
	mips_ejtag_set_instr(ejtag_info, EJTAG_INST_ADDRESS);
	ejtag_info->pa_addr = 0;
	retval = mips_ejtag_drscan_32(ejtag_info, &ejtag_info->pa_addr);
	return retval;
}
static int mips32_pracc_try_read_ctrl_addr(struct mips_ejtag *ejtag_info)
{
	int retval = try_wait_for_pracc_rw(ejtag_info, &ejtag_info->pa_ctrl);
	if (retval != ERROR_OK)
		return retval;
	mips_ejtag_set_instr(ejtag_info, EJTAG_INST_ADDRESS);
	ejtag_info->pa_addr = 0;
	retval = mips_ejtag_drscan_32(ejtag_info, &ejtag_info->pa_addr);
	return retval;
}
/* Finish processor access */
static int mips32_pracc_finish(struct mips_ejtag *ejtag_info)
{
	uint32_t ctrl = ejtag_info->ejtag_ctrl & ~EJTAG_CTRL_PRACC;
	mips_ejtag_set_instr(ejtag_info, EJTAG_INST_CONTROL);
	mips_ejtag_drscan_32_out(ejtag_info, ctrl);
	return jtag_execute_queue();
}
int mips32_pracc_clean_text_jump(struct mips_ejtag *ejtag_info)
{
	uint32_t jt_code = MIPS32_J((0x0FFFFFFF & MIPS32_PRACC_TEXT) >> 2);
	int retval;
	uint32_t data;
	/* do 3 0/nops to clean pipeline before a jump to pracc text, NOP in delay slot */
	for (int i = 0; i < 128; i++) {
        LOG_DEBUG("clean_text_jumpi %d", i);
		/* Wait for pracc */
		retval = wait_for_pracc_rw(ejtag_info, &ejtag_info->pa_ctrl);
		if (retval != ERROR_OK) {
            LOG_ERROR("clean test jump fail");
            return retval;
        }
		mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
		if (i < 3) {
			data = MIPS32_NOP;
		} else {
			if (i % 2) {
				data = jt_code;
			} else {
				data = MIPS32_NOP;
			}	
		}
        LOG_DEBUG("data: 0x%08x", data);
		mips_ejtag_drscan_32_out(ejtag_info, data);
		retval = mips32_pracc_finish(ejtag_info);
		if (retval != ERROR_OK) return retval;
		retval = mips32_pracc_read_ctrl_addr(ejtag_info);
        if (retval != ERROR_OK){
            LOG_DEBUG("clean_text_jump fail");
        }
        LOG_ERROR("=========================clean address: 0x%08x", ejtag_info->pa_addr);
		if (ejtag_info->pa_addr == MIPS32_PRACC_TEXT) {
			return ERROR_OK;
		}
	}
	LOG_ERROR("Can not back to MIPS32_PRACC_TEXT");
	return ERROR_FAIL;
}
int mips32_pracc_exec(struct mips_ejtag *ejtag_info, struct pracc_queue_info *ctx, uint32_t *param_out)
{
	int      code_count = 0;
	int      final_instr = 0;
	uint32_t abandoned_count = 0;
	int      store_pending = 0;     /* increases with every store instruction at dmseg, decreases with every store pa */
	uint32_t max_store_addr = 0;    /* for store pa address testing */
	uint32_t instr = 0;
	bool     final_check = 0;       /* set to 1 if in final checks after function code shifted out */
	int      index = 0;
	uint32_t data = 0;
	//uint32_t wait_dret_cnt = 0;
	uint32_t lain = 4;
	int      retval;
    uint32_t rel_code_count = 0;
    LOG_DEBUG("aaaaaaaaaaaa");
	retval = mips32_pracc_read_ctrl_addr(ejtag_info);
    if(retval != ERROR_OK){
        LOG_DEBUG("111111111EEEEEERROR");
        return ERROR_FAIL;
    }
    LOG_DEBUG("======================ejtag_info_ctrl 0x%08x", ejtag_info->pa_ctrl);
	if (ejtag_info->pa_addr != MIPS32_PRACC_TEXT) { /* restart */
        LOG_DEBUG("=========================ejtag_info_addr 0x%08x", ejtag_info->pa_addr);
		retval = mips32_pracc_clean_text_jump(ejtag_info);
		if (retval != ERROR_OK) return retval;
	}
    LOG_DEBUG("aaaaaa:%d", ctx->code_count);
	while (1) {
        LOG_DEBUG("bbbbbbbbbbbb");
		retval = mips32_pracc_read_ctrl_addr(ejtag_info); /* update current pa info: control and address */
        if (retval != ERROR_OK) {
            LOG_DEBUG("EEEEEERROR");
            return ERROR_FAIL;
        }
		if (ejtag_info->pa_ctrl & EJTAG_CTRL_PRNW) { /* write/store access */
			/* Check for pending store from a previous store instruction at dmseg */
			if (store_pending == 0) {
				LOG_ERROR("unexpected write at address %" PRIx32, ejtag_info->pa_addr);
				return ERROR_JTAG_DEVICE_ERROR;
			} else if (ejtag_info->pa_addr < MIPS32_PRACC_PARAM_OUT || ejtag_info->pa_addr > max_store_addr) {
				LOG_ERROR("writing at unexpected address %" PRIx32, ejtag_info->pa_addr);
				return ERROR_JTAG_DEVICE_ERROR;
			}
			mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
			(void)mips_ejtag_drscan_32(ejtag_info, &data);
			/* store data at param out, address based offset */
			param_out[(ejtag_info->pa_addr - MIPS32_PRACC_PARAM_OUT) / lain] = data;
            LOG_DEBUG("store data:0x%08x, store_addr: 0x%08x", data, ejtag_info->pa_addr);
			store_pending--;
            LOG_DEBUG(" -- store_pending:%d", store_pending);
		} else { /* read/fetch access */
			if ((code_count != 0) && (ejtag_info->pa_addr == MIPS32_PRACC_TEXT) && (final_check == 0) && (final_instr)) {
                LOG_DEBUG("AAAAAAAAAAAAA : 0x%08x", code_count);
                LOG_DEBUG("BBBBBBBBBBBBB : 0x%08x", rel_code_count);
				final_check = 1;
				code_count = 0;
                rel_code_count = 0;
			}
			if (!final_check) { /* executing function code */
                LOG_DEBUG("final_instr: %d , code_count %d , final_check %d", final_instr, code_count, final_check);
				index = (ejtag_info->pa_addr - MIPS32_PRACC_TEXT) / lain;
				if ((code_count == 0) && (ejtag_info->pa_addr != MIPS32_PRACC_TEXT)) {
					LOG_DEBUG("reading at unexpected address 0x%08x, expected %x", ejtag_info->pa_addr, MIPS32_PRACC_TEXT);
					return ERROR_JTAG_DEVICE_ERROR;
				}
				if (index < ctx->code_count) {
					instr = ctx->pracc_list[index].instr;
                    final_instr = (index + 1) == ctx->code_count; 
					/* check for store instruction at dmseg */
					uint32_t store_addr = ctx->pracc_list[index].addr;
					if (store_addr != 0) {
						if (store_addr > max_store_addr)
							max_store_addr = store_addr;
						store_pending++;
                        LOG_DEBUG("****store_pending_address: 0x%08x", store_addr);
                        LOG_DEBUG("store_pending:%d", store_pending);
					}
                    rel_code_count++;
				} else {/*for fix IFU prefetch*/
					instr = MIPS32_NOP;
					abandoned_count++;
				}
				code_count++;
				if (code_count > PRACC_MAX_EXEC_CODE_COUNT) {
					LOG_DEBUG("max exec code count is %d", PRACC_MAX_EXEC_CODE_COUNT);
					return ERROR_JTAG_DEVICE_ERROR;
				}
			} else {/* final check after function code shifted out */
				if (store_pending == 0) {
					if (ejtag_info->pa_addr != MIPS32_PRACC_TEXT) {
						instr = MIPS32_B(NEG16(code_count + 1));
						do {
							mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
							mips_ejtag_drscan_32_out(ejtag_info, instr);
							mips32_pracc_finish(ejtag_info);
							instr = MIPS32_NOP;
							retval = mips32_pracc_read_ctrl_addr(ejtag_info);
                            if(retval != ERROR_OK){
                                LOG_DEBUG("hklju;ju;");
                            }
						} while(ejtag_info->pa_addr != MIPS32_PRACC_TEXT);
					}
                    LOG_DEBUG("sucess ==========");
					return ERROR_OK;
				} else { // for fix LSU store delay
					instr = MIPS32_NOP;
					abandoned_count++;
					code_count++;
				}
			}
            LOG_DEBUG("[IF-INST %d]: 0x%08x 0x%08x", index,ejtag_info->pa_addr, instr);
			if (abandoned_count > 256) {
                        	LOG_ERROR("execution abandoned, store pending: %d", store_pending);
                		return ERROR_JTAG_DEVICE_ERROR;
                	}
			/* Send instruction out */
			mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
          //  LOG_DEBUG("[read instr]: 0x%08x 0x%08x", ejtag_info->pa_addr, instr);
			mips_ejtag_drscan_32_out(ejtag_info, instr);
		}
		/* finish processor access, let the processor eat! */
		mips32_pracc_finish(ejtag_info);
		if (instr == MIPS32_DRET) {/* after leaving debug mode and make sure the DRET finish */
            int deret_cache_line = 0;
			while(1) {
                LOG_DEBUG("ccccccccccccc");
				(void)mips32_pracc_try_read_ctrl_addr(ejtag_info);/* update current pa info: control and address */
				//if (((ejtag_info->pa_ctrl & EJTAG_CTRL_BRKST) == 0) ||
            	//	            ((ejtag_info->pa_ctrl & EJTAG_CTRL_PRACC) && (ejtag_info->pa_addr == MIPS32_PRACC_TEXT))) {
                //    LOG_DEBUG("ejtag_info->pa_addr:0x%08x 0x%08x", ejtag_info->pa_addr, ejtag_info->pa_ctrl);
				//	return ERROR_OK;
				//} else if ((ejtag_info->pa_addr != MIPS32_PRACC_TEXT) && (ejtag_info->pa_ctrl & EJTAG_CTRL_PRACC)) {
            	//	        	mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
            	//	        	mips_ejtag_drscan_32_out(ejtag_info, MIPS32_NOP);
            	//	        	mips32_pracc_finish(ejtag_info);
				//}
				//wait_dret_cnt++;
				//if (wait_dret_cnt > 64) {
				//	LOG_DEBUG("mips32_pracc_finish failed");
				//	return ERROR_FAIL;
                //}
                LOG_DEBUG("deret_cache_line  %d", deret_cache_line);
                if(ejtag_info->pa_ctrl & EJTAG_CTRL_PRACC){
                    LOG_DEBUG("ejtag_info->pa_addr:0x%08x 0x%08x", ejtag_info->pa_addr, ejtag_info->pa_ctrl);
                    if (ejtag_info->ejtag_info_next == NULL){
                        LOG_DEBUG("not resume core ****");
                    }
                    if ((deret_cache_line == (L2_CACHE_LINE - 2)) && (ejtag_info->ejtag_info_next != NULL)){
                        LOG_DEBUG("check out to core %d at address : 0x%08x", ejtag_info->next_coreid, ejtag_info->pa_addr);
            		    mips_ejtag_set_instr(ejtag_info->ejtag_info_next, EJTAG_INST_DATA);
            		    mips_ejtag_drscan_32_out(ejtag_info->ejtag_info_next, MIPS32_NOP);
            		    mips32_pracc_finish(ejtag_info->ejtag_info_next);
                        deret_cache_line++;
                    } else {
            		    mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
            		    mips_ejtag_drscan_32_out(ejtag_info, MIPS32_NOP);
            		    mips32_pracc_finish(ejtag_info);
                        deret_cache_line++;
                    }
                }
                /*if core exit debug mode, l2 also will fetch instruction for the size of a l2 cache line*/
                if (deret_cache_line == (L2_CACHE_LINE - 1)){ 
					return ERROR_OK;
                }
			}
		}
	}
	return ERROR_FAIL;
}
inline void pracc_queue_init(struct pracc_queue_info *ctx)
{
	ctx->retval = ERROR_OK;
	ctx->code_count = 0;
	ctx->store_count = 0;
	ctx->pracc_list = malloc(ctx->max_code * sizeof(pa_list));
	if (ctx->pracc_list == NULL) {
		LOG_ERROR("Out of memory");
		ctx->retval = ERROR_FAIL;
	}
}
inline void pracc_add(struct pracc_queue_info *ctx, uint32_t addr, uint32_t instr)
{
	ctx->pracc_list[ctx->code_count].instr = instr;
	ctx->pracc_list[ctx->code_count++].addr = addr;
	if (addr)
		ctx->store_count++;
}
inline void pracc_queue_free(struct pracc_queue_info *ctx)
{
	if (ctx->code_count > ctx->max_code)	/* Only for internal check, will be erased */
		LOG_ERROR("Internal error, code count: %d > max code: %d", ctx->code_count, ctx->max_code);
	if (ctx->pracc_list != NULL)
		free(ctx->pracc_list);
}
int mips32_pracc_queue_exec(struct mips_ejtag *ejtag_info, struct pracc_queue_info *ctx, uint32_t *buf)
{
	if (ejtag_info->mode == 0) {
		LOG_DEBUG("Go to mips32_pracc_exec beause ejtag_info->mode is 0");
		return mips32_pracc_exec(ejtag_info, ctx, buf);
	}
	union scan_in {
		uint8_t scan_96[12];
		struct {
			uint8_t ctrl[4];
			uint8_t data[4];
			uint8_t addr[4];
		} scan_32;
	} *scan_in = malloc(sizeof(union scan_in) * (ctx->code_count + ctx->store_count));
	if (scan_in == NULL) {
		LOG_ERROR("Out of memory");
		return ERROR_FAIL;
	}
	unsigned num_clocks =
		((uint64_t)(ejtag_info->scan_delay) * jtag_get_speed_khz() + 500000) / 1000000;
	uint32_t ejtag_ctrl = ejtag_info->ejtag_ctrl & ~EJTAG_CTRL_PRACC;
	mips_ejtag_set_instr(ejtag_info, EJTAG_INST_ALL);
	int scan_count = 0;
	for (int i = 0; i != 2 * ctx->code_count; i++) {
		jtag_add_clocks(num_clocks);
		mips_ejtag_add_scan_96(ejtag_info, ejtag_ctrl, ctx->pracc_list[i].instr, scan_in[scan_count++].scan_96);
		/* Check store address from previous instruction, if not the first */
		if (i > 0 && ctx->pracc_list[i - 1].addr) {
			jtag_add_clocks(num_clocks);
			mips_ejtag_add_scan_96(ejtag_info, ejtag_ctrl, 0, scan_in[scan_count++].scan_96);
		}
	}
	int retval = jtag_execute_queue();		/* execute queued scans */
	if (retval != ERROR_OK)
		goto exit;
	uint32_t fetch_addr = MIPS32_PRACC_TEXT;		/* start address */
	scan_count = 0;
	for (int i = 0; i != ctx->code_count; i++) {				/* verify every pracc access */
		/* check pracc bit */
		ejtag_ctrl = buf_get_u32(scan_in[scan_count].scan_32.ctrl, 0, 32);
		uint32_t addr = buf_get_u32(scan_in[scan_count].scan_32.addr, 0, 32);
		if (!(ejtag_ctrl & EJTAG_CTRL_PRACC)) {
			LOG_ERROR("Error: access not pending  count: %d", scan_count);
			retval = ERROR_FAIL;
			goto exit;
		}
		if (ejtag_ctrl & EJTAG_CTRL_PRNW) {
			LOG_ERROR("Not a fetch/read access, count: %d", scan_count);
			retval = ERROR_FAIL;
			goto exit;
		}
		if (addr != fetch_addr) {
			LOG_ERROR("Fetch addr mismatch, read: %" PRIx32 " expected: %" PRIx32 " count: %d",
					  addr, fetch_addr, scan_count);
			retval = ERROR_FAIL;
			goto exit;
		}
		fetch_addr += 4;
		scan_count++;
		/* check if previous intrucction is a store instruction at dmesg */
		if (i > 0 && ctx->pracc_list[i - 1].addr) {
			uint32_t store_addr = ctx->pracc_list[i - 1].addr;
			ejtag_ctrl = buf_get_u32(scan_in[scan_count].scan_32.ctrl, 0, 32);
			addr = buf_get_u32(scan_in[scan_count].scan_32.addr, 0, 32);
			if (!(ejtag_ctrl & EJTAG_CTRL_PRNW)) {
				LOG_ERROR("Not a store/write access, count: %d", scan_count);
				retval = ERROR_FAIL;
				goto exit;
			}
			if (addr != store_addr) {
				LOG_ERROR("Store address mismatch, read: %" PRIx32 " expected: %" PRIx32 " count: %d",
							      addr, store_addr, scan_count);
				retval = ERROR_FAIL;
				goto exit;
			}
			int buf_index = (addr - MIPS32_PRACC_PARAM_OUT) / 4;
			buf[buf_index] = buf_get_u32(scan_in[scan_count].scan_32.data, 0, 32);
			scan_count++;
		}
	}
exit:
	free(scan_in);
	return retval;
}
int mips32_pracc_read_u32(struct mips_ejtag *ejtag_info, uint32_t addr, uint32_t *buf)
{
	struct pracc_queue_info ctx = {.max_code = 9};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	pracc_add(&ctx, 0, MIPS32_LUI(15, PRACC_UPPER_BASE_ADDR));			/* $15 = MIPS32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16((addr + 0x8000))));		/* load  $8 with modified upper address */
	pracc_add(&ctx, 0, MIPS32_EHB);
	pracc_add(&ctx, 0, MIPS32_LW(8, LOWER16(addr), 8));				/* lw $8, LOWER16(addr)($8) */
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT, MIPS32_SW(8, PRACC_OUT_OFFSET, 15));			/* sw $8,PRACC_OUT_OFFSET($15) */
	pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16(ejtag_info->reg8)));		/* restore upper 16 of $8 */
	pracc_add(&ctx, 0, MIPS32_ORI(8, 8, LOWER16(ejtag_info->reg8)));		/* restore lower 16 of $8 */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_MFC0(15, 31, 0));					/* move COP0 DeSave to $15 */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, buf);
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
}
int mips32_pracc_read_mem(struct mips_ejtag *ejtag_info, uint32_t addr, int size, int count, void *buf)
{
	if (count == 1 && size == 4)
		return mips32_pracc_read_u32(ejtag_info, addr, (uint32_t *)buf);
	uint32_t *data = NULL;
	struct pracc_queue_info ctx = {.max_code = 256 * 4 + 8 + 1};	/* alloc memory for the worst case */
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	if (size != 4) {
		data = malloc(256 * sizeof(uint32_t));
		if (data == NULL) {
			LOG_ERROR("Out of memory");
			goto exit;
		}
	}
	uint32_t *buf32 = buf;
	uint16_t *buf16 = buf;
	uint8_t *buf8 = buf;
	while (count) {
		ctx.code_count = 0;
		ctx.store_count = 0;
		int this_round_count = (count > 256) ? 256 : count;
		uint32_t last_upper_base_addr = UPPER16((addr + 0x8000));
		pracc_add(&ctx, 0, MIPS32_LUI(15, PRACC_UPPER_BASE_ADDR));			/* $15 = MIPS32_PRACC_BASE_ADDR */
		pracc_add(&ctx, 0, MIPS32_LUI(9, last_upper_base_addr));		/* load the upper memory address in $9 */
		for (int i = 0; i != this_round_count; i++) {			/* Main code loop */
			uint32_t upper_base_addr = UPPER16((addr + 0x8000));
			if (last_upper_base_addr != upper_base_addr) {			/* if needed, change upper address in $9 */
				pracc_add(&ctx, 0, MIPS32_LUI(9, upper_base_addr));
				last_upper_base_addr = upper_base_addr;
			}
			pracc_add(&ctx, 0, MIPS32_EHB);		/* load from memory to $8 */
			if (size == 4)
				pracc_add(&ctx, 0, MIPS32_LW(8, LOWER16(addr), 9));		/* load from memory to $8 */
			else if (size == 2)
				pracc_add(&ctx, 0, MIPS32_LHU(8, LOWER16(addr), 9));
			else
				pracc_add(&ctx, 0, MIPS32_LBU(8, LOWER16(addr), 9));
			pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT + i * 4,
					  MIPS32_SW(8, PRACC_OUT_OFFSET + i * 4, 15));		/* store $8 at param out */
			addr += size;
		}
		pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16(ejtag_info->reg8)));		/* restore upper 16 bits of reg 8 */
		pracc_add(&ctx, 0, MIPS32_ORI(8, 8, LOWER16(ejtag_info->reg8)));	/* restore lower 16 bits of reg 8 */
		pracc_add(&ctx, 0, MIPS32_LUI(9, UPPER16(ejtag_info->reg9)));		/* restore upper 16 bits of reg 9 */
		pracc_add(&ctx, 0, MIPS32_ORI(9, 9, LOWER16(ejtag_info->reg9)));	/* restore lower 16 bits of reg 9 */
		pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));				/* jump to start */
		pracc_add(&ctx, 0, MIPS32_MFC0(15, 31, 0));					/* restore $15 from DeSave */
		if (size == 4) {
			ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, buf32);
			if (ctx.retval != ERROR_OK)
				goto exit;
			buf32 += this_round_count;
		} else {
			ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, data);
			if (ctx.retval != ERROR_OK)
				goto exit;
			uint32_t *data_p = data;
			for (int i = 0; i != this_round_count; i++) {
				if (size == 2)
					*buf16++ = *data_p++;
				else
					*buf8++ = *data_p++;
			}
		}
		count -= this_round_count;
	}
exit:
	pracc_queue_free(&ctx);
	if (data != NULL)
		free(data);
	return ctx.retval;
}
int mips32_cp0_read(struct mips_ejtag *ejtag_info, uint32_t *val, uint32_t cp0_reg, uint32_t cp0_sel)
{
	struct pracc_queue_info ctx = {.max_code = 7};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	pracc_add(&ctx, 0, MIPS32_LUI(15, PRACC_UPPER_BASE_ADDR));			/* $15 = MIPS32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_MFC0(8, 0, 0) | (cp0_reg << 11) | cp0_sel);	/* move COP0 [cp0_reg select] to $8 */
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT,
				MIPS32_SW(8, PRACC_OUT_OFFSET, 15));			/* store $8 to pracc_out */
	pracc_add(&ctx, 0, MIPS32_MFC0(15, 31, 0));					/* move COP0 DeSave to $15 */
	pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16(ejtag_info->reg8)));		/* restore upper 16 bits  of $8 */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_ORI(8, 8, LOWER16(ejtag_info->reg8)));		/* restore lower 16 bits of $8 */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, val);
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
	/**
	 * Note that our input parametes cp0_reg and cp0_sel
	 * are numbers (not gprs) which make part of mfc0 instruction opcode.
	 *
	 * These are not fix, but can be different for each mips32_cp0_read() function call,
	 * and that is why we must insert them directly into opcode,
	 * i.e. we can not pass it on EJTAG microprogram stack (via param_in),
	 * and put them into the gprs later from MIPS32_PRACC_STACK
	 * because mfc0 do not use gpr as a parameter for the cp0_reg and select part,
	 * but plain (immediate) number.
	 *
	 * MIPS32_MTC0 is implemented via MIPS32_R_INST macro.
	 * In order to insert our parameters, we must change rd and funct fields.
	 *
	 * code[2] |= (cp0_reg << 11) | cp0_sel;   change rd and funct of MIPS32_R_INST macro
	 **/
}
int mips32_cp0_write(struct mips_ejtag *ejtag_info, uint32_t val, uint32_t cp0_reg, uint32_t cp0_sel)
{
	struct pracc_queue_info ctx = {.max_code = 6};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	pracc_add(&ctx, 0, MIPS32_LUI(15, UPPER16(val)));				/* Load val to $15 */
	pracc_add(&ctx, 0, MIPS32_ORI(15, 15, LOWER16(val)));
	pracc_add(&ctx, 0, MIPS32_MTC0(15, 0, 0) | (cp0_reg << 11) | cp0_sel);	/* write cp0 reg / sel */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_MFC0(15, 31, 0));					/* move COP0 DeSave to $15 */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, NULL);
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
	/**
	 * Note that MIPS32_MTC0 macro is implemented via MIPS32_R_INST macro.
	 * In order to insert our parameters, we must change rd and funct fields.
	 * code[3] |= (cp0_reg << 11) | cp0_sel;   change rd and funct fields of MIPS32_R_INST macro
	 **/
}
/**
 * \b mips32_pracc_sync_cache
 *
 * Synchronize Caches to Make Instruction Writes Effective
 * (ref. doc. MIPS32 Architecture For Programmers Volume II: The MIPS32 Instruction Set,
 *  Document Number: MD00086, Revision 2.00, June 9, 2003)
 *
 * When the instruction stream is written, the SYNCI instruction should be used
 * in conjunction with other instructions to make the newly-written instructions effective.
 *
 * Explanation :
 * A program that loads another program into memory is actually writing the D- side cache.
 * The instructions it has loaded can't be executed until they reach the I-cache.
 *
 * After the instructions have been written, the loader should arrange
 * to write back any containing D-cache line and invalidate any locations
 * already in the I-cache.
 *
 * If the cache coherency attribute (CCA) is set to zero, it's a write through cache, there is no need
 * to write back.
 *
 * In the latest MIPS32/64 CPUs, MIPS provides the synci instruction,
 * which does the whole job for a cache-line-sized chunk of the memory you just loaded:
 * That is, it arranges a D-cache write-back (if CCA = 3) and an I-cache invalidate.
 *
 * The line size is obtained with the rdhwr SYNCI_Step in release 2 or from cp0 config 1 register in release 1.
 */
static int mips32_pracc_synchronize_cache(struct mips_ejtag *ejtag_info,
					 uint32_t start_addr, uint32_t end_addr, int cached, int rel)
{
	struct pracc_queue_info ctx = {.max_code = 256 * 2 + 5};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	/** Find cache line size in bytes */
	uint32_t clsiz;
	//if (rel) {	/* Release 2 (rel = 1) */
	if (0) {	/* Release 2 (rel = 1) */
		pracc_add(&ctx, 0, MIPS32_LUI(15, PRACC_UPPER_BASE_ADDR));			/* $15 = MIPS32_PRACC_BASE_ADDR */
		pracc_add(&ctx, 0, MIPS32_RDHWR(8, MIPS32_SYNCI_STEP));			/* load synci_step value to $8 */
		pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT,
				MIPS32_SW(8, PRACC_OUT_OFFSET, 15));			/* store $8 to pracc_out */
		pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16(ejtag_info->reg8)));			/* restore upper 16 bits  of $8 */
		pracc_add(&ctx, 0, MIPS32_ORI(8, 8, LOWER16(ejtag_info->reg8)));		/* restore lower 16 bits of $8 */
		pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
		pracc_add(&ctx, 0, MIPS32_MFC0(15, 31, 0));					/* move COP0 DeSave to $15 */
		ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, &clsiz);
		if (ctx.retval != ERROR_OK)
			goto exit;
	} else {			/* Release 1 (rel = 0) */
		uint32_t conf;
		ctx.retval = mips32_cp0_read(ejtag_info, &conf, 16, 1);
		if (ctx.retval != ERROR_OK)
			goto exit;
		uint32_t dl = (conf & MIPS32_CONFIG1_DL_MASK) >> MIPS32_CONFIG1_DL_SHIFT;
		/* dl encoding : dl=1 => 4 bytes, dl=2 => 8 bytes, etc... max dl=6 => 128 bytes cache line size */
		clsiz = 0x2 << dl;
		if (dl == 0)
			clsiz = 0;
	}
	if (clsiz == 0)
		goto exit;  /* Nothing to do */
	/* make sure clsiz is power of 2 */
	if (clsiz & (clsiz - 1)) {
		LOG_DEBUG("clsiz must be power of 2");
		ctx.retval = ERROR_FAIL;
		goto exit;
	}
	/* make sure start_addr and end_addr have the same offset inside de cache line */
	start_addr |= clsiz - 1;
	end_addr |= clsiz - 1;
	ctx.code_count = 0;
	int count = 0;
	uint32_t last_upper_base_addr = UPPER16((start_addr + 0x8000));
	pracc_add(&ctx, 0, MIPS32_LUI(15, last_upper_base_addr));		/* load upper memory base address to $15 */
	while (start_addr <= end_addr) {						/* main loop */
		uint32_t upper_base_addr = UPPER16((start_addr + 0x8000));
		if (last_upper_base_addr != upper_base_addr) {				/* if needed, change upper address in $15 */
			pracc_add(&ctx, 0, MIPS32_LUI(15, upper_base_addr));
			last_upper_base_addr = upper_base_addr;
		}
		//if (rel)
		if (0)
			pracc_add(&ctx, 0, MIPS32_SYNCI(LOWER16(start_addr), 15));		/* synci instruction, offset($15) */
		else {
			if (cached == 3)
				pracc_add(&ctx, 0, MIPS32_CACHE(MIPS32_CACHE_D_HIT_WRITEBACK,
							LOWER16(start_addr), 15));		/* cache Hit_Writeback_D, offset($15) */
			pracc_add(&ctx, 0, MIPS32_CACHE(MIPS32_CACHE_I_HIT_INVALIDATE,
							LOWER16(start_addr), 15));		/* cache Hit_Invalidate_I, offset($15) */
		}
		start_addr += clsiz;
		count++;
		if (count == 256 && start_addr <= end_addr) {				/* more ?, then execute code list */
			pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));		/* jump to start */
			pracc_add(&ctx, 0, MIPS32_NOP);						/* nop in delay slot */
			ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, NULL);
			if (ctx.retval != ERROR_OK)
				goto exit;
			ctx.code_count = 0;
			count = 0;
		}
	}
	pracc_add(&ctx, 0, MIPS32_SYNC);
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_MFC0(15, 31, 0));					/* restore $15 from DeSave*/
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, NULL);
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
}
static int mips32_pracc_write_mem_generic(struct mips_ejtag *ejtag_info,
		uint32_t addr, int size, int count, const void *buf)
{
    LOG_DEBUG("mips32_pracc_write_mem_generic");
	struct pracc_queue_info ctx = {.max_code = 128 * 3 + 5 + 1};	/* alloc memory for the worst case */
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	const uint32_t *buf32 = buf;
	const uint16_t *buf16 = buf;
	const uint8_t *buf8 = buf;
	while (count) {
		ctx.code_count = 0;
		ctx.store_count = 0;
		int this_round_count = (count > 128) ? 128 : count;
		uint32_t last_upper_base_addr = UPPER16((addr + 0x8000));
		pracc_add(&ctx, 0, MIPS32_LUI(15, last_upper_base_addr));		/* load $15 with memory base address */
		for (int i = 0; i != this_round_count; i++) {
			uint32_t upper_base_addr = UPPER16((addr + 0x8000));
			if (last_upper_base_addr != upper_base_addr) {
				pracc_add(&ctx, 0, MIPS32_LUI(15, upper_base_addr));	/* if needed, change upper address in $15*/
				last_upper_base_addr = upper_base_addr;
			}
			if (size == 4) {			/* for word writes check if one half word is 0 and load it accordingly */
			//	if (LOWER16(*buf32) == 0)
			//		pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16(*buf32)));		/* load only upper value */
			//	else if (UPPER16(*buf32) == 0)
			//			pracc_add(&ctx, 0, MIPS32_ORI(8, 0, LOWER16(*buf32)));	/* load only lower */
		//		else {
					pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16(*buf32)));		/* load upper and lower */
					pracc_add(&ctx, 0, MIPS32_ORI(8, 8, LOWER16(*buf32)));
		//		}
				pracc_add(&ctx, 0, MIPS32_SW(8, LOWER16(addr), 15));		/* store word to memory */
				buf32++;
			} else if (size == 2) {
				pracc_add(&ctx, 0, MIPS32_ORI(8, 0, *buf16));		/* load lower value */
				pracc_add(&ctx, 0, MIPS32_SH(8, LOWER16(addr), 15));	/* store half word to memory */
				buf16++;
			} else {
				pracc_add(&ctx, 0, MIPS32_ORI(8, 0, *buf8));		/* load lower value */
				pracc_add(&ctx, 0, MIPS32_SB(8, LOWER16(addr), 15));	/* store byte to memory */
				buf8++;
			}
			addr += size;
		}
		pracc_add(&ctx, 0, MIPS32_LUI(8, UPPER16(ejtag_info->reg8)));		/* restore upper 16 bits of reg 8 */
		pracc_add(&ctx, 0, MIPS32_ORI(8, 8, LOWER16(ejtag_info->reg8)));	/* restore lower 16 bits of reg 8 */
		pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));				/* jump to start */
		pracc_add(&ctx, 0, MIPS32_MFC0(15, 31, 0));				/* restore $15 from DeSave */
		ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, NULL);
		if (ctx.retval != ERROR_OK)
			goto exit;
		count -= this_round_count;
	}
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
}
int mips32_pracc_write_mem(struct mips_ejtag *ejtag_info, uint32_t addr, int size, int count, const void *buf)
{
	int retval = mips32_pracc_write_mem_generic(ejtag_info, addr, size, count, buf);
	if (retval != ERROR_OK)
		return retval;
	/**
	 * If we are in the cacheable region and cache is activated,
	 * we must clean D$ (if Cache Coherency Attribute is set to 3) + invalidate I$ after we did the write,
	 * so that changes do not continue to live only in D$ (if CCA = 3), but to be
	 * replicated in I$ also (maybe we wrote the istructions)
	 */
	uint32_t conf = 0;
	int cached = 0;
	if ((KSEGX(addr) == KSEG1) || ((addr >= 0xff200000) && (addr <= 0xff3fffff)))
		return retval; /*Nothing to do*/
	mips32_cp0_read(ejtag_info, &conf, 16, 0);
	switch (KSEGX(addr)) {
		case KUSEG:
			cached = (conf & MIPS32_CONFIG0_KU_MASK) >> MIPS32_CONFIG0_KU_SHIFT;
			break;
		case KSEG0:
			cached = (conf & MIPS32_CONFIG0_K0_MASK) >> MIPS32_CONFIG0_K0_SHIFT;
			break;
		case KSEG2:
		case KSEG3:
			cached = (conf & MIPS32_CONFIG0_K23_MASK) >> MIPS32_CONFIG0_K23_SHIFT;
			break;
		default:
			/* what ? */
			break;
	}
	/**
	 * Check cachablitiy bits coherency algorithm
	 * is the region cacheable or uncached.
	 * If cacheable we have to synchronize the cache
	 */
	/* CCA Encoding Description                                         */
	/* 0   000      Cacheable, write-throngh, write-allocate            */
	/* 1   001      Uncacheable write accelerated                       */
	/* 2   010      Uncacheable                                         */
	/* 3   011      Cacheable, write-back, write-allocate               */
	/* 4   100      Cacheable, write-throngh, write-allocate, Streaming */
	/* 5   101      Cacheable, write-back, write-allocate, Streaming    */
	/* 6   110      Reserved                                            */
	/* 7   111      Reserved                                            */
	if (cached == 0 || cached == 3 || cached == 4 || cached == 5) {
		uint32_t start_addr = addr;
		uint32_t end_addr = addr + count * size;
		uint32_t rel = (conf & MIPS32_CONFIG0_AR_MASK) >> MIPS32_CONFIG0_AR_SHIFT;
		if (rel > 1) {
			LOG_DEBUG("Unknown release in cache code");
			return ERROR_FAIL;
		}
		retval = mips32_pracc_synchronize_cache(ejtag_info, start_addr, end_addr, cached, rel);
	}
	return retval;
}
int mips32_pracc_write_regs(struct mips_ejtag *ejtag_info, uint32_t *regs)
{
	static const uint32_t cp0_write_code[] = {
		MIPS32_MTC0(1, 12, 0),							/* move $1 to status */
		MIPS32_MTLO(1),									/* move $1 to lo */
		MIPS32_MTHI(1),									/* move $1 to hi */
		MIPS32_MTC0(1, 8, 0),							/* move $1 to badvaddr */
		MIPS32_MTC0(1, 13, 0),							/* move $1 to cause*/
		MIPS32_MTC0(1, 24, 0),							/* move $1 to depc (pc) */
	};
	struct pracc_queue_info ctx = {.max_code = 37 * 2 + 7 + 1};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	/* load registers 2 to 31 with lui and ori instructions, check if some instructions can be saved */
	for (int i = 2; i < 32; i++) {
		if (LOWER16((regs[i])) == 0)					/* if lower half word is 0, lui instruction only */
			pracc_add(&ctx, 0, MIPS32_LUI(i, UPPER16((regs[i]))));
		else if (UPPER16((regs[i])) == 0)					/* if upper half word is 0, ori with $0 only*/
			pracc_add(&ctx, 0, MIPS32_ORI(i, 0, LOWER16((regs[i]))));
		else {									/* default, load with lui and ori instructions */
			pracc_add(&ctx, 0, MIPS32_LUI(i, UPPER16((regs[i]))));
			pracc_add(&ctx, 0, MIPS32_ORI(i, i, LOWER16((regs[i]))));
		}
	}
	for (int i = 0; i != 6; i++) {
		pracc_add(&ctx, 0, MIPS32_LUI(1, UPPER16((regs[i + 32]))));		/* load CPO value in $1, with lui and ori */
		pracc_add(&ctx, 0, MIPS32_ORI(1, 1, LOWER16((regs[i + 32]))));
		pracc_add(&ctx, 0, cp0_write_code[i]);					/* write value from $1 to CPO register */
        if(i == 5)
            LOG_DEBUG("depc::::::0x%08x", regs[i + 32]);
	}
	pracc_add(&ctx, 0, MIPS32_MTC0(15, 31, 0));				/* load $15 in DeSave */
	pracc_add(&ctx, 0, MIPS32_LUI(1, UPPER16((regs[1]))));			/* load upper half word in $1 */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_ORI(1, 1, LOWER16((regs[1]))));		/* load lower half word in $1 */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, NULL);
	ejtag_info->reg8 = regs[8];
	ejtag_info->reg9 = regs[9];
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
}
int mips32_pracc_read_regs(struct mips_ejtag *ejtag_info, uint32_t *regs)
{
	static int cp0_read_code[] = {
		MIPS32_MFC0(8, 12, 0),							/* move status to $8 */
		MIPS32_MFLO(8),									/* move lo to $8 */
		MIPS32_MFHI(8),									/* move hi to $8 */
		MIPS32_MFC0(8, 8, 0),							/* move badvaddr to $8 */
		MIPS32_MFC0(8, 13, 0),							/* move cause to $8 */
		MIPS32_MFC0(8, 24, 0),							/* move depc (pc) to $8 */
	};
	struct pracc_queue_info ctx = {.max_code = 49};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	pracc_add(&ctx, 0, MIPS32_MTC0(1, 31, 0));						/* move $1 to COP0 DeSave */
	pracc_add(&ctx, 0, MIPS32_LUI(1, PRACC_UPPER_BASE_ADDR));				/* $1 = MIP32_PRACC_BASE_ADDR */
	for (int i = 2; i != 32; i++)					/* store GPR's 2 to 31 */
		pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT + (i * 4),
				  MIPS32_SW(i, PRACC_OUT_OFFSET + (i * 4), 1));
	for (int i = 0; i != 6; i++) {
		pracc_add(&ctx, 0, cp0_read_code[i]);				/* load COP0 needed registers to $8 */
		pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT + (i + 32) * 4,			/* store $8 at PARAM OUT */
				  MIPS32_SW(8, PRACC_OUT_OFFSET + (i + 32) * 4, 1));
	}
	pracc_add(&ctx, 0, MIPS32_MFC0(8, 31, 0));					/* move DeSave to $8, reg1 value */
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT + 4,					/* store reg1 value from $8 to param out */
			  MIPS32_SW(8, PRACC_OUT_OFFSET + 4, 1));
	pracc_add(&ctx, 0, MIPS32_MFC0(1, 31, 0));					/* move COP0 DeSave to $1, restore reg1 */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_MTC0(15, 31, 0));					/* load $15 in DeSave */
	//if (ejtag_info->mode == 0)
	//	ctx.store_count++;	/* Needed by legacy code, due to offset from reg0 */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, regs);
	ejtag_info->reg8 = regs[8];	/* reg8 is saved but not restored, next called function should restore it */
	ejtag_info->reg9 = regs[9];
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
}

int mips32_pracc_read_core_info(struct mips_ejtag *ejtag_info, uint32_t *core_info)
{
	struct pracc_queue_info ctx = {.max_code = 9};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	pracc_add(&ctx, 0, MIPS32_MTC0(1, 31, 0));				/* move $1 to COP0 DeSave */
	pracc_add(&ctx, 0, MIPS32_LUI(1, 0xb220));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_ORI(1, 1, 0x40));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_LW(9, 0, 1));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_LUI(1, PRACC_UPPER_BASE_ADDR));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT, MIPS32_SW(9, PRACC_OUT_OFFSET, 1));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_MFC0(1, 31, 0));					/* move COP0 DeSave to $1, restore reg1 */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_MTC0(15, 31, 0));					/* load $15 in DeSave */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, core_info);
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
}

int mips32_pracc_read_reset_entry(struct mips_ejtag *ejtag_info, uint32_t *reset_entry){
    
	struct pracc_queue_info ctx = {.max_code = 9};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	pracc_add(&ctx, 0, MIPS32_MTC0(1, 31, 0));				/* move $1 to COP0 DeSave */
	pracc_add(&ctx, 0, MIPS32_LUI(1, 0xb220));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_ORI(1, 1, 0x0f00));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_LW(9, 0, 1));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_LUI(1, PRACC_UPPER_BASE_ADDR));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT, MIPS32_SW(9, PRACC_OUT_OFFSET, 1));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_MFC0(1, 31, 0));					/* move COP0 DeSave to $1, restore reg1 */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_MTC0(15, 31, 0));					/* load $15 in DeSave */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, reset_entry);
exit:
	pracc_queue_free(&ctx);
	return ctx.retval;
}

int mips32_pracc_check_address(struct mips_ejtag *ejtag_info){
    LOG_DEBUG("mips32_pracc_check_address");
    uint32_t ejtag_ctrl = ejtag_info->ejtag_ctrl;
    uint32_t address;
    int count = 0;
    int deret_cause_break = 0;
    mips_ejtag_set_instr(ejtag_info, EJTAG_INST_CONTROL);
    int retval = mips_ejtag_drscan_32(ejtag_info, &ejtag_ctrl);
    int32_t target_coreid = (int32_t)((ejtag_ctrl & EJTAG_CTRL_COREID) >> EJTAG_CTRL_COREID_POS);
    if(((ejtag_ctrl & EJTAG_CTRL_BRKST) != 0)  && ((ejtag_ctrl & EJTAG_CTRL_ROCC) == 0)){
        while(1){
            LOG_DEBUG("***********count::: %d", count);
                LOG_DEBUG("1coreid %d", ejtag_info->coreid);
                LOG_DEBUG("target_coreid %d", ejtag_info->coreid);
                LOG_DEBUG("1ejtag_ctrl 0x%08x", ejtag_ctrl);
            while(((target_coreid == ejtag_info->coreid) && (ejtag_ctrl & EJTAG_CTRL_PRACC)) == 0){
                LOG_DEBUG("---------------------------------");
                ejtag_ctrl = ejtag_info->ejtag_ctrl;
                LOG_DEBUG("2ejtag_ctrl 0x%08x", ejtag_ctrl);
	            retval = mips_ejtag_drscan_32(ejtag_info, &ejtag_ctrl);
                target_coreid = (int32_t)((ejtag_ctrl & EJTAG_CTRL_COREID) >> EJTAG_CTRL_COREID_POS);
                if ((ejtag_ctrl & EJTAG_CTRL_BRKST) == 0){
                    deret_cause_break = 1;
                    LOG_DEBUG("find DERET residual dm");
                    break;
                }
                LOG_DEBUG("3coreid %d", target_coreid);
                LOG_DEBUG("3ejtag_ctrl 0x%08x", ejtag_ctrl);
            }
            if(deret_cause_break){
                break;
            }
            if (ejtag_ctrl & EJTAG_CTRL_PRNW) {
                LOG_ERROR("Pending write when check address");
                retval = ERROR_FAIL;
            } else {
                mips_ejtag_set_instr(ejtag_info, EJTAG_INST_ADDRESS);
                retval = mips_ejtag_drscan_32(ejtag_info, &address);
                        LOG_DEBUG("      1**********check address: 0x%08x", address);
                retval = mips_ejtag_drscan_32(ejtag_info, &address);
                        LOG_DEBUG("      2**********check address: 0x%08x", address);
                retval = mips_ejtag_drscan_32(ejtag_info, &address);
                        LOG_DEBUG("      3**********check address: 0x%08x", address);

                if ((address >= 0xff200200) && (address <= 0xff20021c)){
                    if (address != 0xff200200){
                        LOG_DEBUG("**********check address: 0x%08x", address);
	                    mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA); 
                        mips_ejtag_drscan_32_out(ejtag_info, MIPS32_NOP);
                        mips32_pracc_finish(ejtag_info);
                        count++;
                    }else {
                        break;
                    }
                    //if (count >= 8){
                    if (count >= 10){
                        LOG_ERROR("can not go back to address 0xff200200");
                        break;
                    }
                } else {
                    LOG_ERROR("the address 0x%08x not between 0xff200200 and 0xff20021c", address);
                    break;
                }
            }   
        }   
    }
    return retval;
}

int mips32_pracc_handle_last_fetch(struct mips_ejtag *target_ejtag_info, struct mips_ejtag *curr_ejtag_info)
{
    LOG_DEBUG("mips32_pracc_handle_last_fetch");
	uint32_t jt_code = MIPS32_J((0x0FFFFFFF & MIPS32_PRACC_TEXT) >> 2);
    uint32_t target_ejtag_ctrl = target_ejtag_info->ejtag_ctrl;
    int residual_dm = 0;
    int32_t target_ctrl_coreid;
    int retval = ERROR_OK;

	mips_ejtag_set_instr(target_ejtag_info, EJTAG_INST_CONTROL);
    LOG_DEBUG("1target_ejtag_ctrl 0x%08x", target_ejtag_ctrl);
	retval = mips_ejtag_drscan_32(target_ejtag_info, &target_ejtag_ctrl);
    LOG_DEBUG("2target_ejtag_ctrl 0x%08x", target_ejtag_ctrl);
    if ((target_ejtag_ctrl & EJTAG_CTRL_ROCC) != 0){
        target_ejtag_ctrl = target_ejtag_info->ejtag_ctrl & ~EJTAG_CTRL_ROCC;
    }
    LOG_DEBUG("3target_ejtag_ctrl 0x%08x", target_ejtag_ctrl);
	retval = mips_ejtag_drscan_32(target_ejtag_info, &target_ejtag_ctrl);
    LOG_DEBUG("4target_ejtag_ctrl 0x%08x", target_ejtag_ctrl);
    target_ctrl_coreid = (int32_t)((target_ejtag_ctrl & EJTAG_CTRL_COREID) >> EJTAG_CTRL_COREID_POS);
    LOG_DEBUG("target_ctrl_coreid : %d target_ejtag_info->coreid %d", target_ctrl_coreid, target_ejtag_info->coreid);
    if(((target_ejtag_ctrl & EJTAG_CTRL_BRKST) != 0)){
        while((((target_ejtag_ctrl & EJTAG_CTRL_PRACC) != 0) && (target_ejtag_info->coreid == target_ctrl_coreid)) == 0) {
            target_ejtag_ctrl = target_ejtag_info->ejtag_ctrl & ~EJTAG_CTRL_ROCC;
	        retval = mips_ejtag_drscan_32(target_ejtag_info, &target_ejtag_ctrl);
            target_ctrl_coreid = (int32_t)((target_ejtag_ctrl & EJTAG_CTRL_COREID) >> EJTAG_CTRL_COREID_POS);
            LOG_DEBUG("-------------------------ejtag_ctrl 0x%08x, ejtag_ctrl_coreid %d", 
                      target_ejtag_ctrl, target_ctrl_coreid);
            if ((target_ejtag_ctrl & EJTAG_CTRL_BRKST) == 0){
                LOG_DEBUG("find DERET residual dm");
                residual_dm = 1;
                break;
            }
        }
    
        if (residual_dm == 0){
            if (target_ejtag_ctrl & EJTAG_CTRL_PRNW) {
                LOG_ERROR("Pending write when handler last fetch");
                retval = ERROR_FAIL;
            } else {
                LOG_DEBUG("do hand last fetch");
                LOG_DEBUG("target_ejtag_ctrl : 0x%08x", target_ejtag_ctrl);
	            mips_ejtag_set_instr(curr_ejtag_info, EJTAG_INST_DATA); 

                mips_ejtag_drscan_32_out(curr_ejtag_info, jt_code);
                mips32_pracc_finish(curr_ejtag_info);


                retval = ERROR_OK;
            }
        }
    }
    retval = mips32_pracc_check_address(curr_ejtag_info);
    return retval;
}

/* fastdata upload/download requires an initialized working area
 * to load the download code; it should not be called otherwise
 * fetch order from the fastdata area
 * A
 * 1. start addr
 * 2. end addr
 * 3. data ...
 */
int mips32_pracc_fastdata_xfer(struct mips_ejtag *ejtag_info, struct working_area *source,
		int write_t, uint32_t addr, int count, uint32_t *buf)
{
	uint32_t val;
	uint32_t req_ctrl;
	uint32_t *ack_ctrl = NULL;
	int      retval;
	uint32_t handler_code[] = {
		/* r15 points to the start of this code */
		MIPS32_SW(8, MIPS32_FASTDATA_HANDLER_SIZE - 4, 15),
		MIPS32_SW(9, MIPS32_FASTDATA_HANDLER_SIZE - 8, 15),
		MIPS32_SW(10, MIPS32_FASTDATA_HANDLER_SIZE - 12, 15),
		MIPS32_SW(11, MIPS32_FASTDATA_HANDLER_SIZE - 16, 15),
		/* start of fastdata area in t0 */
		MIPS32_LUI(8, UPPER16(MIPS32_PRACC_FASTDATA_AREA)),
		MIPS32_ORI(8, 8, LOWER16(MIPS32_PRACC_FASTDATA_AREA)),
		MIPS32_LW(9, 0, 8),						/* start addr in t1 */
		MIPS32_LW(10, 0, 8),						/* end addr to t2 */
					/* loop: */
		write_t ? MIPS32_LW(11, 0, 8) : MIPS32_LW(11, 0, 9),	/* from xfer area : from memory */
		write_t ? MIPS32_SW(11, 0, 9) : MIPS32_SW(11, 0, 8),	/* to memory      : to xfer area */
		MIPS32_BNE(10, 9, NEG16(3)),			/* bne $t2,t1,loop */
		MIPS32_ADDI(9, 9, 4),					/* addi t1,t1,4 */
		MIPS32_LW(8, MIPS32_FASTDATA_HANDLER_SIZE - 4, 15),
		MIPS32_LW(9, MIPS32_FASTDATA_HANDLER_SIZE - 8, 15),
		MIPS32_LW(10, MIPS32_FASTDATA_HANDLER_SIZE - 12, 15),
		MIPS32_LW(11, MIPS32_FASTDATA_HANDLER_SIZE - 16, 15),
		MIPS32_LUI(15, UPPER16(MIPS32_PRACC_TEXT)),
		MIPS32_ORI(15, 15, LOWER16(MIPS32_PRACC_TEXT)),	/* isa bit for JR instr */
		MIPS32_JR(15),								/* jr start */
		MIPS32_MFC0(15, 31, 0),					/* move COP0 DeSave to $15 */
	};
	if (source->size < MIPS32_FASTDATA_HANDLER_SIZE)
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
		/* write program into RAM */
	if (write_t != ejtag_info->fast_access_save) {
		mips32_pracc_write_mem(ejtag_info, source->address, 4, ARRAY_SIZE(handler_code), handler_code);
		/* save previous operation to speed to any consecutive read/writes */
		ejtag_info->fast_access_save = write_t;
	}
	LOG_DEBUG("%s using 0x%.8" TARGET_PRIxADDR " for write handler", __func__, source->address);
	uint32_t jmp_code[] = {
		MIPS32_LUI(15, UPPER16(source->address)),			/* load addr of jump in $15 */
		MIPS32_ORI(15, 15, LOWER16(source->address)),	/* isa bit for JR instr */
		MIPS32_JR(15),						/* jump to ram program */
		MIPS32_NOP,	/* drop isa bit, needed for LW/SW instructions */
	};
	/* execute jump code, with no address check */
	for (unsigned i = 0; i < ARRAY_SIZE(jmp_code); i++) {
		retval = wait_for_pracc_rw(ejtag_info, &ejtag_info->pa_ctrl);
		if (retval != ERROR_OK)
			return retval;
		mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
		mips_ejtag_drscan_32_out(ejtag_info, jmp_code[i]);
		/* Clear the access pending bit (let the processor eat!) */
		mips32_pracc_finish(ejtag_info);
	}
	/* next fetch to dmseg should be in FASTDATA_AREA, check */
	while(1) {
		retval = mips32_pracc_read_ctrl_addr(ejtag_info);
                if (retval != ERROR_OK)
                        return retval;
		if (ejtag_info->pa_addr == MIPS32_PRACC_FASTDATA_AREA) break;
                mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
                mips_ejtag_drscan_32_out(ejtag_info, MIPS32_NOP);
                /* Clear the access pending bit (let the processor eat!) */
                mips32_pracc_finish(ejtag_info);
	}
	if (ejtag_info->ejtag_version > EJTAG_VERSION_25) {
		/* Send the load start address */
		val = addr;
		mips_ejtag_set_instr(ejtag_info, EJTAG_INST_FASTDATA);
		mips_ejtag_fastdata_scan(ejtag_info, 1, &val);
		retval = wait_for_pracc_rw(ejtag_info, &ejtag_info->pa_ctrl);
		if (retval != ERROR_OK)
			return retval;
		/* Send the load end address */
		val = addr + (count - 1) * 4;
		mips_ejtag_set_instr(ejtag_info, EJTAG_INST_FASTDATA);
		mips_ejtag_fastdata_scan(ejtag_info, 1, &val);
		unsigned num_clocks = 0;	/* like in legacy code */
		if (ejtag_info->mode != 0)
			num_clocks = ((uint64_t)(ejtag_info->scan_delay) * jtag_get_speed_khz() + 500000) / 1000000;
		for (int i = 0; i < count; i++) {
			jtag_add_clocks(num_clocks);
			mips_ejtag_fastdata_scan(ejtag_info, write_t, buf++);
		}
		retval = jtag_execute_queue();
		if (retval != ERROR_OK) {
                	LOG_ERROR("fastdata load failed");
                	return retval;
        	}
	} else {
		ack_ctrl = malloc((count + 2) * sizeof(uint32_t));
		if (ack_ctrl == NULL) {
			LOG_ERROR("Out of memory");
			return ERROR_FAIL;
		}
		req_ctrl = ejtag_info->ejtag_ctrl & ~EJTAG_CTRL_PRACC;
		val = addr; /* Send the load start address */
		mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
		mips_ejtag_add_drscan_32(ejtag_info, val, NULL);
		mips_ejtag_set_instr(ejtag_info, EJTAG_INST_CONTROL);
		mips_ejtag_add_drscan_32(ejtag_info, req_ctrl, ack_ctrl);
		val = addr + (count - 1) * 4; /* Send the load end address */
		mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
        mips_ejtag_add_drscan_32(ejtag_info, val, NULL);
        mips_ejtag_set_instr(ejtag_info, EJTAG_INST_CONTROL);
        mips_ejtag_add_drscan_32(ejtag_info, req_ctrl, ack_ctrl + 1);
		/* from xfer area to memory */
		/* from memory to xfer area*/
		for (int i = 0; i < count; i++) {
		    //retval = wait_for_pracc_rw(ejtag_info, &ejtag_info->pa_ctrl);
            retval = mips32_pracc_read_ctrl_addr(ejtag_info);
		    if (retval != ERROR_OK)
			    return retval;
			mips_ejtag_set_instr(ejtag_info, EJTAG_INST_DATA);
			mips_ejtag_add_drscan_32(ejtag_info, *buf, write_t ? NULL : buf);
			mips_ejtag_set_instr(ejtag_info, EJTAG_INST_CONTROL);
			mips_ejtag_add_drscan_32(ejtag_info, req_ctrl, ack_ctrl + 2 + i);
			buf++;
		}
		retval = jtag_execute_queue();              /* execute queued scans */
		if (retval != ERROR_OK) {
			LOG_ERROR("fastdata load execute queue failed");
                        return retval;
		}
		if (debug_level >= LOG_LVL_DEBUG) {	
			for (int i = 0; i < count + 2; i++) {
				if ((ack_ctrl[i] & EJTAG_CTRL_PRACC) == 0) {
					LOG_DEBUG("fastdata load verify failed");
                	        	return ERROR_FAIL;
				}
			}
		}
		free(ack_ctrl);
	}
	retval = mips32_pracc_read_ctrl_addr(ejtag_info);
	if (retval != ERROR_OK)
		return retval;
	if (ejtag_info->pa_addr != MIPS32_PRACC_TEXT)
		LOG_ERROR("mini program did not return to start");
	return retval;
}

int mips32_pracc_close_watchdog(struct mips_ejtag *ejtag_info){
    LOG_DEBUG("mips32_pracc_close_watchdog");
    uint32_t watchdog_reg[3];
	struct pracc_queue_info ctx = {.max_code = 16};
	pracc_queue_init(&ctx);
	if (ctx.retval != ERROR_OK)
		goto exit;
	pracc_add(&ctx, 0, MIPS32_MTC0(1, 31, 0));				/* move $1 to COP0 DeSave */
	pracc_add(&ctx, 0, MIPS32_LUI(1, 0xb000));		
	pracc_add(&ctx, 0, MIPS32_ORI(1, 1, 0x2004));		
	pracc_add(&ctx, 0, MIPS32_LW(9, 40, 1));
	pracc_add(&ctx, 0, MIPS32_LUI(8, 0x1));
	pracc_add(&ctx, 0, MIPS32_ADDU(9, 8, 9));
	pracc_add(&ctx, 0, MIPS32_SW(9, 40, 1));
	pracc_add(&ctx, 0, MIPS32_LW(9, 40, 1));
	pracc_add(&ctx, 0, MIPS32_LUI(2, PRACC_UPPER_BASE_ADDR));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT, MIPS32_SW(9, PRACC_OUT_OFFSET, 2));				/* $1 = MIP32_PRACC_BASE_ADDR */

	pracc_add(&ctx, 0, MIPS32_LW(9, 4, 1));
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT + 4, MIPS32_SW(9, PRACC_OUT_OFFSET+4, 2));				/* $1 = MIP32_PRACC_BASE_ADDR */
	
	pracc_add(&ctx, 0, MIPS32_LW(9, 4, 1));
	pracc_add(&ctx, MIPS32_PRACC_PARAM_OUT + 8, MIPS32_SW(9, PRACC_OUT_OFFSET + 8, 2));				/* $1 = MIP32_PRACC_BASE_ADDR */
	pracc_add(&ctx, 0, MIPS32_B(NEG16(ctx.code_count + 1)));					/* jump to start */
	pracc_add(&ctx, 0, MIPS32_MTC0(15, 31, 0));					/* load $15 in DeSave */
	ctx.retval = mips32_pracc_queue_exec(ejtag_info, &ctx, watchdog_reg);
exit:
	pracc_queue_free(&ctx);
    LOG_DEBUG("111watchdog_reg: 0x%08x", watchdog_reg[0]);
    LOG_DEBUG("111watchdog_reg_counter1: 0x%08x", watchdog_reg[1]);
    LOG_DEBUG("111watchdog_reg_counter2: 0x%08x", watchdog_reg[2]);
	return ctx.retval;
}
