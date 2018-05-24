/***************************************************************************
 *   Copyright (C) 2005 by Dominic Rath                                    *
 *   Dominic.Rath@gmx.de                                                   *
 *                                                                         *
 *   Copyright (C) 2007,2008 Øyvind Harboe                                 *
 *   oyvind.harboe@zylin.com                                               *
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

/* 2014-12: Addition of the SWD protocol support is based on the initial work
 * by Paul Fertser and modifications by Jean-Christian de Rivaz. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "bitbang.h"
#include <jtag/interface.h>
#include <jtag/commands.h>

#define SHARE_DATA	(*(volatile unsigned int *)(tcsm2_base+0x0ff0/4))
#define SHARE_DATA2	(*(volatile unsigned int *)(tcsm2_base+0x0ff4/4))

extern uint32_t *tcsm2_base;

/* YUK! - but this is currently a global.... */
extern struct jtag_interface *jtag_interface;

extern int jdi_led(int on);
extern int jdi_state_move(int skip, uint8_t tms_scan, int tms_count);

/**
 * Function bitbang_stableclocks
 * issues a number of clock cycles while staying in a stable state.
 * Because the TMS value required to stay in the RESET state is a 1, whereas
 * the TMS value required to stay in any of the other stable states is a 0,
 * this function checks the current stable state to decide on the value of TMS
 * to use.
 */
//static int bitbang_stableclocks(int num_cycles);

static void bitbang_swd_write_reg(uint8_t cmd, uint32_t value, uint32_t ap_delay_clk);

struct bitbang_interface *bitbang_interface;

/* DANGER!!!! clock absolutely *MUST* be 0 in idle or reset won't work!
 *
 * Set this to 1 and str912 reset halt will fail.
 *
 * If someone can submit a patch with an explanation it will be greatly
 * appreciated, but as far as I can tell (ØH) DCLK is generated upon
 * clk = 0 in TAP_IDLE. Good luck deducing that from the ARM documentation!
 * The ARM documentation uses the term "DCLK is asserted while in the TAP_IDLE
 * state". With hardware there is no such thing as *while* in a state. There
 * are only edges. So clk => 0 is in fact a very subtle state transition that
 * happens *while* in the TAP_IDLE state. "#&¤"#¤&"#&"#&
 *
 * For "reset halt" the last thing that happens before srst is asserted
 * is that the breakpoint is set up. If DCLK is not wiggled one last
 * time before the reset, then the breakpoint is not set up and
 * "reset halt" will fail to halt.
 *
 */
#define CLOCK_IDLE() 0

/* The bitbang driver leaves the TCK 0 when in idle */
static void bitbang_end_state(tap_state_t state)
{
	tap_is_state_stable(state);
	tap_set_end_state(state);
}

static int bitbang_state_move(int skip)
{
	uint8_t tms_scan = tap_get_tms_path(tap_get_state(), tap_get_end_state());
	int tms_count = tap_get_tms_path_len(tap_get_state(), tap_get_end_state());

	jdi_state_move(skip, tms_scan, tms_count);

	tap_set_state(tap_get_end_state());
	return ERROR_OK;
}

int bitbang_execute_queue(void)
{
	struct jtag_command *cmd = jtag_command_queue;	/* currently processed command */
	struct jtag_command *cmd_next = jtag_command_queue;
	int num_bits = 0;
	int scan_size, scan_size_next;
	unsigned temp, size;
	unsigned prepare = 0;
	unsigned mcu_status = 0;
	unsigned turn_num, turn_cnt;
	enum scan_type type, type_next;
	uint8_t *buffer, *buffer_next;

	jdi_led(1);

	while (cmd) {
		if (__builtin_expect(cmd->type == JTAG_SCAN, 1)) {
			if(prepare) {
				prepare = 0;
				type = type_next;
				buffer = buffer_next;
				scan_size = scan_size_next;
			} else {
				scan_size = jtag_build_buffer(cmd->cmd.scan, &buffer);
				type = jtag_scan_type(cmd->cmd.scan);
			}
			if(scan_size == 5) {
//  			0000  0000  0000  0000  0000  0000  0000  0000
//  			      状态  类型   FTMS  次------数  数------据
				SHARE_DATA = buffer[0] | 0x08000000;
				cmd_next = cmd->next;
				if (cmd_next->type == JTAG_SCAN) {
					scan_size_next = jtag_build_buffer_prefetch(cmd_next->cmd.scan, &buffer_next);
					type_next = jtag_scan_type(cmd_next->cmd.scan);
					prepare = 1;
				}
				while(SHARE_DATA & 0x08000000);
			} else if (scan_size == 7) {
//  			0000  0000  0000  0000  0000  0000  0000  0000
//  			      状态  类型   FTMS  次------数  数------据
				SHARE_DATA = buffer[0] | 0x01100000;
				cmd_next = cmd->next;
				if (cmd_next) {
					if (cmd_next->type == JTAG_SCAN) {
						scan_size_next = jtag_build_buffer_prefetch(cmd_next->cmd.scan, &buffer_next);
						type_next = jtag_scan_type(cmd_next->cmd.scan);
						prepare = 1;
					}
				}
				num_bits = cmd->cmd.scan->fields[0].num_bits;
				do mcu_status = SHARE_DATA;
				while(mcu_status & 0x01000000);
				buffer[0] = (uint8_t)(mcu_status & 0x000000ff);
				buf_cpy(buffer, cmd->cmd.scan->fields[0].in_value, num_bits);
			} else {
				turn_num = (scan_size-1)/32;
				size = scan_size;
				for (turn_cnt = 0; turn_cnt <= turn_num; turn_cnt++) {
					if(__builtin_expect(scan_size == 32, 1)) {
						SHARE_DATA2 = buf_get_u32(buffer, turn_cnt, size);
						if (__builtin_expect(size == 32, 1))
							SHARE_DATA = (1 << 28) | (type << 20) | (1 << 16) | 0x02000000;
						if (__builtin_expect(size == 672, 0))
							SHARE_DATA = (type << 20) | (2 << 16) | 0x02000000;
						cmd_next = cmd->next;
						if (cmd_next) {
							if (cmd_next->type == JTAG_SCAN) {
								scan_size_next = jtag_build_buffer_prefetch(cmd_next->cmd.scan, &buffer_next);
								type_next = jtag_scan_type(cmd_next->cmd.scan);
								prepare = 1;
							}
						}
						if (type != SCAN_OUT)
							num_bits = cmd->cmd.scan->fields[0].num_bits;
						while(SHARE_DATA & 0x02000000);
						temp = SHARE_DATA2;
						if (type != SCAN_OUT)
							memcpy(&buffer[turn_cnt*4], &temp, 4);
					} else {
						SHARE_DATA2 = buf_get_u32(buffer, turn_cnt, size);
						if (scan_size == 672)
							SHARE_DATA = (2 << 28) | (type << 20) | 0x02000000;
						else
							SHARE_DATA = (type << 20) | 0x02000000;
						while(SHARE_DATA & 0x02000000);
						temp = SHARE_DATA2;
						if (type != SCAN_OUT)
							memcpy(&buffer[turn_cnt*4], &temp, 4);
						scan_size -= 32;
					}
				}
				if (type != SCAN_OUT)
					buf_cpy(buffer, cmd->cmd.scan->fields[0].in_value, num_bits);
			}
		}
		if (__builtin_expect(cmd->type == JTAG_RESET, 0)) {
			if ((cmd->cmd.reset->trst == 1) ||
					(cmd->cmd.reset->srst && (jtag_get_reset_config() & RESET_SRST_PULLS_TRST)))
				tap_set_state(TAP_RESET);
			if (bitbang_interface->reset(cmd->cmd.reset->trst,
						cmd->cmd.reset->srst) != ERROR_OK)
				return ERROR_FAIL;
		}
		if (__builtin_expect(cmd->type == JTAG_TLR_RESET, 0)) {
			bitbang_end_state(cmd->cmd.statemove->end_state);
			if (bitbang_state_move(0) != ERROR_OK)
				return ERROR_FAIL;
		}
		if (__builtin_expect(cmd->type == JTAG_SLEEP, 0))
			jtag_sleep(cmd->cmd.sleep->us);
		cmd = cmd->next;
	}
	jdi_led(0);

	return ERROR_OK;
}


bool swd_mode;
static int queued_retval;

static int bitbang_swd_init(void)
{
	LOG_DEBUG("bitbang_swd_init");
	swd_mode = true;
	return ERROR_OK;
}

static void bitbang_exchange(bool rnw, uint8_t buf[], unsigned int offset, unsigned int bit_cnt)
{
	LOG_DEBUG("bitbang_exchange");
	int tdi;

	for (unsigned int i = offset; i < bit_cnt + offset; i++) {
		int bytec = i/8;
		int bcval = 1 << (i % 8);
		tdi = !rnw && (buf[bytec] & bcval);

		bitbang_interface->write(0, 0, tdi);

		if (rnw && buf) {
			if (bitbang_interface->swdio_read())
				buf[bytec] |= bcval;
			else
				buf[bytec] &= ~bcval;
		}

		bitbang_interface->write(1, 0, tdi);
	}
}

int bitbang_swd_switch_seq(enum swd_special_seq seq)
{
	LOG_DEBUG("bitbang_swd_switch_seq");

	switch (seq) {
	case LINE_RESET:
		LOG_DEBUG("SWD line reset");
		bitbang_exchange(false, (uint8_t *)swd_seq_line_reset, 0, swd_seq_line_reset_len);
		break;
	case JTAG_TO_SWD:
		LOG_DEBUG("JTAG-to-SWD");
		bitbang_exchange(false, (uint8_t *)swd_seq_jtag_to_swd, 0, swd_seq_jtag_to_swd_len);
		break;
	case SWD_TO_JTAG:
		LOG_DEBUG("SWD-to-JTAG");
		bitbang_exchange(false, (uint8_t *)swd_seq_swd_to_jtag, 0, swd_seq_swd_to_jtag_len);
		break;
	default:
		LOG_ERROR("Sequence %d not supported", seq);
		return ERROR_FAIL;
	}

	return ERROR_OK;
}

void bitbang_switch_to_swd(void)
{
	LOG_DEBUG("bitbang_switch_to_swd");
	bitbang_exchange(false, (uint8_t *)swd_seq_jtag_to_swd, 0, swd_seq_jtag_to_swd_len);
}

static void swd_clear_sticky_errors(void)
{
	bitbang_swd_write_reg(swd_cmd(false,  false, DP_ABORT),
		STKCMPCLR | STKERRCLR | WDERRCLR | ORUNERRCLR, 0);
}

static void bitbang_swd_read_reg(uint8_t cmd, uint32_t *value, uint32_t ap_delay_clk)
{
	LOG_DEBUG("bitbang_swd_read_reg");
	assert(cmd & SWD_CMD_RnW);

	if (queued_retval != ERROR_OK) {
		LOG_DEBUG("Skip bitbang_swd_read_reg because queued_retval=%d", queued_retval);
		return;
	}

	for (;;) {
		uint8_t trn_ack_data_parity_trn[DIV_ROUND_UP(4 + 3 + 32 + 1 + 4, 8)];

		cmd |= SWD_CMD_START | (1 << 7);
		bitbang_exchange(false, &cmd, 0, 8);

		bitbang_interface->swdio_drive(false);
		bitbang_exchange(true, trn_ack_data_parity_trn, 0, 1 + 3 + 32 + 1 + 1);
		bitbang_interface->swdio_drive(true);

		int ack = buf_get_u32(trn_ack_data_parity_trn, 1, 3);
		uint32_t data = buf_get_u32(trn_ack_data_parity_trn, 1 + 3, 32);
		int parity = buf_get_u32(trn_ack_data_parity_trn, 1 + 3 + 32, 1);

		LOG_DEBUG("%s %s %s reg %X = %08"PRIx32,
			  ack == SWD_ACK_OK ? "OK" : ack == SWD_ACK_WAIT ? "WAIT" : ack == SWD_ACK_FAULT ? "FAULT" : "JUNK",
			  cmd & SWD_CMD_APnDP ? "AP" : "DP",
			  cmd & SWD_CMD_RnW ? "read" : "write",
			  (cmd & SWD_CMD_A32) >> 1,
			  data);

		switch (ack) {
		 case SWD_ACK_OK:
			if (parity != parity_u32(data)) {
				LOG_DEBUG("Wrong parity detected");
				queued_retval = ERROR_FAIL;
				return;
			}
			if (value)
				*value = data;
			if (cmd & SWD_CMD_APnDP)
				bitbang_exchange(true, NULL, 0, ap_delay_clk);
			return;
		 case SWD_ACK_WAIT:
			LOG_DEBUG("SWD_ACK_WAIT");
			swd_clear_sticky_errors();
			break;
		 case SWD_ACK_FAULT:
			LOG_DEBUG("SWD_ACK_FAULT");
			queued_retval = ack;
			return;
		 default:
			LOG_DEBUG("No valid acknowledge: ack=%d", ack);
			queued_retval = ack;
			return;
		}
	}
}

static void bitbang_swd_write_reg(uint8_t cmd, uint32_t value, uint32_t ap_delay_clk)
{
	LOG_DEBUG("bitbang_swd_write_reg");
	assert(!(cmd & SWD_CMD_RnW));

	if (queued_retval != ERROR_OK) {
		LOG_DEBUG("Skip bitbang_swd_write_reg because queued_retval=%d", queued_retval);
		return;
	}

	for (;;) {
		uint8_t trn_ack_data_parity_trn[DIV_ROUND_UP(4 + 3 + 32 + 1 + 4, 8)];
		buf_set_u32(trn_ack_data_parity_trn, 1 + 3 + 1, 32, value);
		buf_set_u32(trn_ack_data_parity_trn, 1 + 3 + 1 + 32, 1, parity_u32(value));

		cmd |= SWD_CMD_START | (1 << 7);
		bitbang_exchange(false, &cmd, 0, 8);

		bitbang_interface->swdio_drive(false);
		bitbang_exchange(true, trn_ack_data_parity_trn, 0, 1 + 3 + 1);
		bitbang_interface->swdio_drive(true);
		bitbang_exchange(false, trn_ack_data_parity_trn, 1 + 3 + 1, 32 + 1);

		int ack = buf_get_u32(trn_ack_data_parity_trn, 1, 3);
		LOG_DEBUG("%s %s %s reg %X = %08"PRIx32,
			  ack == SWD_ACK_OK ? "OK" : ack == SWD_ACK_WAIT ? "WAIT" : ack == SWD_ACK_FAULT ? "FAULT" : "JUNK",
			  cmd & SWD_CMD_APnDP ? "AP" : "DP",
			  cmd & SWD_CMD_RnW ? "read" : "write",
			  (cmd & SWD_CMD_A32) >> 1,
			  buf_get_u32(trn_ack_data_parity_trn, 1 + 3 + 1, 32));

		switch (ack) {
		 case SWD_ACK_OK:
			if (cmd & SWD_CMD_APnDP)
				bitbang_exchange(true, NULL, 0, ap_delay_clk);
			return;
		 case SWD_ACK_WAIT:
			LOG_DEBUG("SWD_ACK_WAIT");
			swd_clear_sticky_errors();
			break;
		 case SWD_ACK_FAULT:
			LOG_DEBUG("SWD_ACK_FAULT");
			queued_retval = ack;
			return;
		 default:
			LOG_DEBUG("No valid acknowledge: ack=%d", ack);
			queued_retval = ack;
			return;
		}
	}
}

static int bitbang_swd_run_queue(void)
{
	LOG_DEBUG("bitbang_swd_run_queue");
	/* A transaction must be followed by another transaction or at least 8 idle cycles to
	 * ensure that data is clocked through the AP. */
	bitbang_exchange(true, NULL, 0, 8);

	int retval = queued_retval;
	queued_retval = ERROR_OK;
	LOG_DEBUG("SWD queue return value: %02x", retval);
	return retval;
}

const struct swd_driver bitbang_swd = {
	.init = bitbang_swd_init,
	.switch_seq = bitbang_swd_switch_seq,
	.read_reg = bitbang_swd_read_reg,
	.write_reg = bitbang_swd_write_reg,
	.run = bitbang_swd_run_queue,
};
