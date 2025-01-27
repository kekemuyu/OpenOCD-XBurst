/***************************************************************************
 *   Copyright (C) 2008 by Spencer Oliver                                  *
 *   spen@spen-soft.co.uk                                                  *
 *                                                                         *
 *   Copyright (C) 2008 by David T.L. Wong                                 *
 *                                                                         *
 *   Copyright (C) 2007,2008 Øyvind Harboe                                 *
 *   oyvind.harboe@zylin.com                                               *
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "image.h"
#include "mips32.h"
#include "breakpoints.h"
#include "algorithm.h"
#include "register.h"

static const char *mips_isa_strings[] = {
	"MIPS32", "MIPS16"
};

#define MIPS32_GDB_DUMMY_FP_REG 1

/*
 * GDB registers
 * based on gdb-7.6.2/gdb/features/mips-{fpu,cp0,cpu}.xml
 */
static const struct {
	unsigned id;
	const char *name;
	enum reg_type type;
	const char *group;
	const char *feature;
	int flag;
} mips32_regs[] = {
	{  0,  "r0", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  1,  "r1", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  2,  "r2", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  3,  "r3", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  4,  "r4", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  5,  "r5", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  6,  "r6", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  7,  "r7", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  8,  "r8", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{  9,  "r9", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 10, "r10", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 11, "r11", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 12, "r12", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 13, "r13", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 14, "r14", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 15, "r15", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 16, "r16", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 17, "r17", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 18, "r18", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 19, "r19", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 20, "r20", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 21, "r21", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 22, "r22", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 23, "r23", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 24, "r24", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 25, "r25", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 26, "r26", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 27, "r27", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 28, "r28", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 29, "r29", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 30, "r30", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 31, "r31", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 32, "status", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cp0", 0 },
	{ 33, "lo", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 34, "hi", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },
	{ 35, "badvaddr", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cp0", 0 },
	{ 36, "cause", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cp0", 0 },
	{ 37, "pc", REG_TYPE_INT, NULL, "org.gnu.gdb.mips.cpu", 0 },

	{ 38,  "f0", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 39,  "f1", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 40,  "f2", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 41,  "f3", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 42,  "f4", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 43,  "f5", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 44,  "f6", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 45,  "f7", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 46,  "f8", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 47,  "f9", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 48, "f10", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 49, "f11", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 50, "f12", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 51, "f13", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 52, "f14", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 53, "f15", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 54, "f16", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 55, "f17", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 56, "f18", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 57, "f19", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 58, "f20", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 59, "f21", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 60, "f22", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 61, "f23", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 62, "f24", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 63, "f25", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 64, "f26", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 65, "f27", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 66, "f28", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 67, "f29", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 68, "f30", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 69, "f31", REG_TYPE_IEEE_SINGLE, NULL,
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 70, "fcsr", REG_TYPE_INT, "float",
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
	{ 71, "fir", REG_TYPE_INT, "float",
		"org.gnu.gdb.mips.fpu", MIPS32_GDB_DUMMY_FP_REG },
};

#define MIPS32_NUM_REGS ARRAY_SIZE(mips32_regs)

static const struct {
	unsigned option;
	const char *arg;
} invalidate_cmd[7] = {
	{ INSTNOWB, "instnowb", },
	{ DATA, "data", },
	{ DATANOWB, "datanowb", },
	{ L2, "l2", },
	{ L2NOWB, "l2nowb", },
	{ ALL, "all", },
	{ ALLNOWB, "allnowb", },
};

static int mips32_get_core_reg(struct reg *reg)
{
	int retval;
	struct mips32_core_reg *mips32_reg = reg->arch_info;
	struct target *target = mips32_reg->target;
	struct mips32_common *mips32_target = target_to_mips32(target);

	if (target->state != TARGET_HALTED)
		return ERROR_TARGET_NOT_HALTED;

	retval = mips32_target->read_core_reg(target, mips32_reg->num);

	return retval;
}

static int mips32_set_core_reg(struct reg *reg, uint8_t *buf)
{
	struct mips32_core_reg *mips32_reg = reg->arch_info;
	struct target *target = mips32_reg->target;
	uint32_t value = buf_get_u32(buf, 0, 32);

	if (target->state != TARGET_HALTED)
		return ERROR_TARGET_NOT_HALTED;

	buf_set_u32(reg->value, 0, 32, value);
	reg->dirty = 1;
	reg->valid = 1;

	return ERROR_OK;
}

static int mips32_read_core_reg(struct target *target, unsigned int num)
{
	uint32_t reg_value;

	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);

	if (num >= MIPS32_NUM_REGS)
		return ERROR_COMMAND_SYNTAX_ERROR;

	reg_value = mips32->core_regs[num];
	buf_set_u32(mips32->core_cache->reg_list[num].value, 0, 32, reg_value);
	mips32->core_cache->reg_list[num].valid = 1;
	mips32->core_cache->reg_list[num].dirty = 0;

	return ERROR_OK;
}

int mips32_read_core_info(struct target *target)
{
    LOG_DEBUG("mips32_read_core_info");
	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
    struct target *curr;
    struct target_list *head;

    mips32_pracc_read_core_info(ejtag_info, &mips32->core_info);
    target->core_info = mips32->core_info;

	head = target->head;
	while (head != (struct target_list *)NULL) {
        curr = head->target;  
        if (curr != target){
            curr->core_info = target->core_info;
            LOG_DEBUG("curr->coreid %d", curr->coreid);
        }
	    head = head->next;
    }
    LOG_DEBUG("CORE_INFOmips32_read_core_info : 0x%08x, target_coreid %d", target->core_info, target->coreid);
	return ERROR_OK;
}

int mips32_read_reset_entry(struct target *target){
    LOG_DEBUG("mips32_step_read_core_info");
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
    uint32_t reset_entry;
    mips32_pracc_read_reset_entry(ejtag_info, &reset_entry);
    return reset_entry;
}

static int mips32_write_core_reg(struct target *target, unsigned int num)
{
	uint32_t reg_value;

	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);

	if (num >= MIPS32_NUM_REGS)
		return ERROR_COMMAND_SYNTAX_ERROR;

	reg_value = buf_get_u32(mips32->core_cache->reg_list[num].value, 0, 32);
	mips32->core_regs[num] = reg_value;
	LOG_DEBUG("write core reg %i value 0x%" PRIx32 "", num , reg_value);
	mips32->core_cache->reg_list[num].valid = 1;
	mips32->core_cache->reg_list[num].dirty = 0;

	return ERROR_OK;
}

int mips32_get_gdb_reg_list(struct target *target, struct reg **reg_list[],
		int *reg_list_size, enum target_register_class reg_class)
{
	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);
	unsigned int i;

	/* include floating point registers */
	*reg_list_size = MIPS32_NUM_REGS;
	*reg_list = malloc(sizeof(struct reg *) * (*reg_list_size));

	for (i = 0; i < MIPS32_NUM_REGS; i++)
		(*reg_list)[i] = &mips32->core_cache->reg_list[i];

	return ERROR_OK;
}

int mips32_save_context(struct target *target)
{
	unsigned int i;

	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;

	/* read core registers */
	mips32_pracc_read_regs(ejtag_info, mips32->core_regs);

	for (i = 0; i < MIPS32_NUM_REGS; i++) {
		if (!mips32->core_cache->reg_list[i].valid)
			mips32->read_core_reg(target, i);
	}

	/* If FP Coprocessor available then read FP registers */
	if (mips32->fp_imp == FP_IMP) {
		uint32_t status;

		/* Read Status register, save it and modify to enable CP0 */
		int retval = mips32_cp0_read(ejtag_info, &status, 12, 0);
		if (retval != ERROR_OK) {
			LOG_DEBUG("reading status register failed");
			return retval;
		}

		/* Check if Access to COP1 enabled */
		if (((status & 0x20000000) >> 29) == 0) {
			if ((retval = mips32_cp0_write(ejtag_info, (status | MIPS32_STATUS_CU1_MASK), 12, 0)) != ERROR_OK) {
				LOG_DEBUG("writing status register failed");
				return retval;
			}
		}

		/* read FPU registers */
		retval = mips32_pracc_read_fpu_regs(ejtag_info, (uint32_t *)(&mips32->core_regs[MIPS32_F0]));
		if (retval != ERROR_OK)
			LOG_INFO("mips32->read_fpu_reg failed");

		/* restore previous setting */
		if ((mips32_cp0_write(ejtag_info, status, 12, 0)) != ERROR_OK)
			LOG_DEBUG("writing status register failed");

		for (i = MIPS32_F0; i < MIPS32_NUM_REGS; i++) {
			if (mips32->core_cache->reg_list[i].valid) {
				retval = mips32->read_core_reg(target, i);
				if (retval != ERROR_OK) {
					LOG_DEBUG("mips32->read_core_reg failed");
					return retval;
				}
			}
		}
	}

	return ERROR_OK;
}

int mips32_restore_context(struct target *target)
{
	unsigned int i;

	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;

	for (i = 0; i < MIPS32_NUM_REGS; i++) {
		if (mips32->core_cache->reg_list[i].dirty)
			mips32->write_core_reg(target, i);
	}

	/* If FP Coprocessor available then read FP registers */
	if (mips32->fp_imp == FP_IMP) {
		uint32_t status;

		/* Read Status register, save it and modify to enable CP0 */
		int retval = mips32_cp0_read(ejtag_info, &status, 12, 0);
		if (retval != ERROR_OK) {
			LOG_DEBUG("reading status register failed");
			return retval;
		}

		if ((retval = mips32_cp0_write(ejtag_info, (status | MIPS32_STATUS_CU1_MASK), 12, 0)) != ERROR_OK) {
			LOG_DEBUG("writing status register failed");
			return retval;
		}

		/* write FPU registers */
		retval = mips32_pracc_write_fpu_regs(ejtag_info, (uint32_t *)(&mips32->core_regs[MIPS32_F0]));
		if (retval != ERROR_OK)
			LOG_INFO("mips32->read_fpu_reg failed");

		/* restore previous setting */
		if ((mips32_cp0_write(ejtag_info, status, 12, 0)) != ERROR_OK)
			LOG_DEBUG("writing status register failed");
	}

	/* write core regs */
	mips32_pracc_write_regs(ejtag_info, mips32->core_regs);

	return ERROR_OK;
}

int mips32_arch_state(struct target *target)
{
	struct mips32_common *mips32 = target_to_mips32(target);

	LOG_USER("target halted in %s mode due to %s, pc: 0x%8.8" PRIx32 "",
		mips_isa_strings[mips32->isa_mode],
		debug_reason_name(target),
		buf_get_u32(mips32->core_cache->reg_list[MIPS32_PC].value, 0, 32));

	return ERROR_OK;
}

static const struct reg_arch_type mips32_reg_type = {
	.get = mips32_get_core_reg,
	.set = mips32_set_core_reg,
};

struct reg_cache *mips32_build_reg_cache(struct target *target)
{
	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);

	int num_regs = MIPS32_NUM_REGS;
	struct reg_cache **cache_p = register_get_last_cache_p(&target->reg_cache);
	struct reg_cache *cache = malloc(sizeof(struct reg_cache));
	struct reg *reg_list = calloc(num_regs, sizeof(struct reg));
	struct mips32_core_reg *arch_info = malloc(sizeof(struct mips32_core_reg) * num_regs);
	struct reg_feature *feature;
	int i;

	/* Build the process context cache */
	cache->name = "mips32 registers";
	cache->next = NULL;
	cache->reg_list = reg_list;
	cache->num_regs = num_regs;
	(*cache_p) = cache;
	mips32->core_cache = cache;

	for (i = 0; i < num_regs; i++) {
		arch_info[i].num = mips32_regs[i].id;
		arch_info[i].target = target;
		arch_info[i].mips32_common = mips32;

		reg_list[i].name = mips32_regs[i].name;
		reg_list[i].size = 32;

		reg_list[i].value = calloc(1, 4);
		reg_list[i].valid = 0;
		reg_list[i].type = &mips32_reg_type;
		reg_list[i].arch_info = &arch_info[i];

		reg_list[i].reg_data_type = calloc(1, sizeof(struct reg_data_type));
		if (reg_list[i].reg_data_type)
			reg_list[i].reg_data_type->type = mips32_regs[i].type;
		else
			LOG_ERROR("unable to allocate reg type list");

		reg_list[i].dirty = 0;

		reg_list[i].group = mips32_regs[i].group;
		reg_list[i].number = i;
		reg_list[i].exist = true;
		reg_list[i].caller_save = true;	/* gdb defaults to true */

		feature = calloc(1, sizeof(struct reg_feature));
		if (feature) {
			feature->name = mips32_regs[i].feature;
			reg_list[i].feature = feature;
		} else
			LOG_ERROR("unable to allocate feature list");
	}

	return cache;
}

int mips32_init_arch_info(struct target *target, struct mips32_common *mips32, struct jtag_tap *tap)
{
	target->arch_info = mips32;
	mips32->common_magic = MIPS32_COMMON_MAGIC;
	mips32->fast_data_area = NULL;

	/* has breakpoint/watchpoint unit been scanned */
	mips32->bp_scanned = 0;
	mips32->data_break_list = NULL;

	mips32->ejtag_info.tap = tap;
	mips32->read_core_reg = mips32_read_core_reg;
	mips32->write_core_reg = mips32_write_core_reg;

	mips32->ejtag_info.scan_delay = MIPS32_SCAN_DELAY_LEGACY_MODE;	/* Initial default value */
	mips32->ejtag_info.mode = 0;			/* Initial default value */
	mips32->ejtag_info.config_regs = 0;		/* no config register read */

	return ERROR_OK;
}

int mips32_detect_core_imp(struct target *target)
{
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
	uint32_t prid;
	int retval;

	/* Read CONFIG0 to CONFIG3 CP0 registers and log ISA implementation */
	if (ejtag_info->config_regs == 0)
		for (int i = 0; i != 4; i++) {
			retval = mips32_cp0_read(ejtag_info, &ejtag_info->config[i], 16, i);
			if (retval != ERROR_OK) {
				LOG_ERROR("isa info not available, failed to read cp0 config register: %" PRId32, i);
				ejtag_info->config_regs = 0;
				return retval;
			}
			ejtag_info->config_regs = i + 1;
			if ((ejtag_info->config[i] & (1 << 31)) == 0)
				break;	/* no more config registers implemented */
		}
	else
		return ERROR_OK;	/* already succesfully read */

	LOG_DEBUG("read  %"PRId32" config registers", ejtag_info->config_regs);

	/* Retrive if Float Point CoProcessor Implemented */
	if (ejtag_info->config[1] & MIPS32_CONFIG1_FP_MASK)
		mips32->fp_imp = FP_IMP;

	/* Read PRID registers and determine CPU type from PRID. */
	retval = mips32_cp0_read(ejtag_info, &prid, 15, 0);
	if (retval != ERROR_OK) {
		LOG_DEBUG("READ of PRID Failed");
		return retval;
	} else {
		/* Ingenic cores */
		if (((prid >> 16) & 0xf0) == 0xd0) {
			ejtag_info->core_type = MIPS_INGENIC_XBURST1;
			goto exit;
		}
		if (((prid >> 16) & 0xff) == 0x13) {
			switch ((prid >> 13) & 0x7) {
				case 0:
					ejtag_info->core_type = MIPS_INGENIC_XBURST1;
					break;
				case 1:
					ejtag_info->core_type = MIPS_INGENIC_XBURST2;
					break;
				default:
					ejtag_info->core_type = MIPS_INGENIC_XBURST2;
					LOG_DEBUG("An unrecognized Ingenic CPU type, default is XBurst2.");
					break;
			} /* end of switch */
			goto exit;
		}
		/* MIPS Technologies cores */
		switch ((prid >> 8) & 0xff) {
			case 0x87:
				ejtag_info->core_type = MIPS_MTI_M4K;
				break;
			default:
				ejtag_info->core_type = CORE_TYPE_UNKNOWN;
				break;
		} /* end of switch */
	}
exit:
	return ERROR_OK;
}

/* run to exit point. return error if exit point was not reached. */
static int mips32_run_and_wait(struct target *target, uint32_t entry_point,
		int timeout_ms, uint32_t exit_point, struct mips32_common *mips32)
{
	uint32_t pc;
	int retval;
	/* This code relies on the target specific  resume() and  poll()->debug_entry()
	 * sequence to write register values to the processor and the read them back */
	retval = target_resume(target, 0, entry_point, 0, 1);
	if (retval != ERROR_OK)
		return retval;

	retval = target_wait_state(target, TARGET_HALTED, timeout_ms);
	/* If the target fails to halt due to the breakpoint, force a halt */
	if (retval != ERROR_OK || target->state != TARGET_HALTED) {
		retval = target_halt(target);
		if (retval != ERROR_OK)
			return retval;
		retval = target_wait_state(target, TARGET_HALTED, 500);
		if (retval != ERROR_OK)
			return retval;
		return ERROR_TARGET_TIMEOUT;
	}

	pc = buf_get_u32(mips32->core_cache->reg_list[MIPS32_PC].value, 0, 32);
	if (exit_point && (pc != exit_point)) {
		LOG_DEBUG("failed algorithm halted at 0x%" PRIx32 " ", pc);
		return ERROR_TARGET_TIMEOUT;
	}

	return ERROR_OK;
}

int mips32_run_algorithm(struct target *target, int num_mem_params,
		struct mem_param *mem_params, int num_reg_params,
		struct reg_param *reg_params, uint32_t entry_point,
		uint32_t exit_point, int timeout_ms, void *arch_info)
{
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips32_algorithm *mips32_algorithm_info = arch_info;
	enum mips32_isa_mode isa_mode = mips32->isa_mode;

	uint32_t context[MIPS32_NUM_REGS];
	int retval = ERROR_OK;

	LOG_DEBUG("Running algorithm");

	/* NOTE: mips32_run_algorithm requires that each algorithm uses a software breakpoint
	 * at the exit point */

	if (mips32->common_magic != MIPS32_COMMON_MAGIC) {
		LOG_ERROR("current target isn't a MIPS32 target");
		return ERROR_TARGET_INVALID;
	}

	if (target->state != TARGET_HALTED) {
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	/* refresh core register cache */
	for (unsigned int i = 0; i < MIPS32_NUM_REGS; i++) {
		if (!mips32->core_cache->reg_list[i].valid)
			mips32->read_core_reg(target, i);
		context[i] = buf_get_u32(mips32->core_cache->reg_list[i].value, 0, 32);
	}

	for (int i = 0; i < num_mem_params; i++) {
		retval = target_write_buffer(target, mem_params[i].address,
				mem_params[i].size, mem_params[i].value);
		if (retval != ERROR_OK)
			return retval;
	}

	for (int i = 0; i < num_reg_params; i++) {
		struct reg *reg = register_get_by_name(mips32->core_cache, reg_params[i].reg_name, 0);

		if (!reg) {
			LOG_ERROR("BUG: register '%s' not found", reg_params[i].reg_name);
			return ERROR_COMMAND_SYNTAX_ERROR;
		}

		if (reg->size != reg_params[i].size) {
			LOG_ERROR("BUG: register '%s' size doesn't match reg_params[i].size",
					reg_params[i].reg_name);
			return ERROR_COMMAND_SYNTAX_ERROR;
		}

		mips32_set_core_reg(reg, reg_params[i].value);
	}

	mips32->isa_mode = mips32_algorithm_info->isa_mode;

	retval = mips32_run_and_wait(target, entry_point, timeout_ms, exit_point, mips32);

	if (retval != ERROR_OK)
		return retval;

	for (int i = 0; i < num_mem_params; i++) {
		if (mem_params[i].direction != PARAM_OUT) {
			retval = target_read_buffer(target, mem_params[i].address, mem_params[i].size,
					mem_params[i].value);
			if (retval != ERROR_OK)
				return retval;
		}
	}

	for (int i = 0; i < num_reg_params; i++) {
		if (reg_params[i].direction != PARAM_OUT) {
			struct reg *reg = register_get_by_name(mips32->core_cache, reg_params[i].reg_name, 0);
			if (!reg) {
				LOG_ERROR("BUG: register '%s' not found", reg_params[i].reg_name);
				return ERROR_COMMAND_SYNTAX_ERROR;
			}

			if (reg->size != reg_params[i].size) {
				LOG_ERROR("BUG: register '%s' size doesn't match reg_params[i].size",
						reg_params[i].reg_name);
				return ERROR_COMMAND_SYNTAX_ERROR;
			}

			buf_set_u32(reg_params[i].value, 0, 32, buf_get_u32(reg->value, 0, 32));
		}
	}

	/* restore everything we saved before */
	for (unsigned int i = 0; i < MIPS32_NUM_REGS; i++) {
		uint32_t regvalue;
		regvalue = buf_get_u32(mips32->core_cache->reg_list[i].value, 0, 32);
		if (regvalue != context[i]) {
			LOG_DEBUG("restoring register %s with value 0x%8.8" PRIx32,
				mips32->core_cache->reg_list[i].name, context[i]);
			buf_set_u32(mips32->core_cache->reg_list[i].value,
					0, 32, context[i]);
			mips32->core_cache->reg_list[i].valid = 1;
			mips32->core_cache->reg_list[i].dirty = 1;
		}
	}

	mips32->isa_mode = isa_mode;

	return ERROR_OK;
}

int mips32_examine(struct target *target)
{
	struct mips32_common *mips32 = target_to_mips32(target);

	if (!target_was_examined(target)) {
		target_set_examined(target);

		/* we will configure later */
		mips32->bp_scanned = 0;
		mips32->num_inst_bpoints = 0;
		mips32->num_data_bpoints = 0;
		mips32->num_inst_bpoints_avail = 0;
		mips32->num_data_bpoints_avail = 0;
	}

	return ERROR_OK;
}

static int mips32_configure_ibs(struct target *target)
{
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
	int retval, i;
	uint32_t bpinfo;

	/* get number of inst breakpoints */
	retval = target_read_u32(target, ejtag_info->ejtag_ibs_addr, &bpinfo);
	if (retval != ERROR_OK)
		return retval;

	mips32->num_inst_bpoints = (bpinfo >> 24) & 0x0F;
	mips32->num_inst_bpoints_avail = mips32->num_inst_bpoints;
	mips32->inst_break_list = calloc(mips32->num_inst_bpoints,
		sizeof(struct mips32_comparator));

	for (i = 0; i < mips32->num_inst_bpoints; i++)
		mips32->inst_break_list[i].reg_address =
			ejtag_info->ejtag_iba0_addr +
			(ejtag_info->ejtag_iba_step_size * i);

	/* clear IBIS reg */
	retval = target_write_u32(target, ejtag_info->ejtag_ibs_addr, 0);
	return retval;
}

static int mips32_configure_dbs(struct target *target)
{
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
	int retval, i;
	uint32_t bpinfo;

	/* get number of data breakpoints */
	retval = target_read_u32(target, ejtag_info->ejtag_dbs_addr, &bpinfo);
	if (retval != ERROR_OK)
		return retval;

	mips32->num_data_bpoints = (bpinfo >> 24) & 0x0F;
	mips32->num_data_bpoints_avail = mips32->num_data_bpoints;
	mips32->data_break_list = calloc(mips32->num_data_bpoints,
		sizeof(struct mips32_comparator));

	for (i = 0; i < mips32->num_data_bpoints; i++)
		mips32->data_break_list[i].reg_address =
			ejtag_info->ejtag_dba0_addr +
			(ejtag_info->ejtag_dba_step_size * i);

	/* clear DBIS reg */
	retval = target_write_u32(target, ejtag_info->ejtag_dbs_addr, 0);
	return retval;
}

int mips32_configure_break_unit(struct target *target)
{
	/* get pointers to arch-specific information */
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
	int retval;
	uint32_t dcr;

	if (mips32->bp_scanned)
		return ERROR_OK;

	/* get info about breakpoint support */
	retval = target_read_u32(target, EJTAG_DCR, &dcr);
	if (retval != ERROR_OK)
		return retval;

	/* EJTAG 2.0 defines IB and DB bits in IMP instead of DCR. */
	if (ejtag_info->ejtag_version == EJTAG_VERSION_20) {
		ejtag_info->debug_caps = dcr & EJTAG_DCR_ENM;
		if (!(ejtag_info->impcode & EJTAG_V20_IMP_NOIB))
			ejtag_info->debug_caps |= EJTAG_DCR_IB;
		if (!(ejtag_info->impcode & EJTAG_V20_IMP_NODB))
			ejtag_info->debug_caps |= EJTAG_DCR_DB;
	} else
		/* keep  debug caps for later use */
		ejtag_info->debug_caps = dcr & (EJTAG_DCR_ENM
				| EJTAG_DCR_IB | EJTAG_DCR_DB);


	if (ejtag_info->debug_caps & EJTAG_DCR_IB) {
		retval = mips32_configure_ibs(target);
		if (retval != ERROR_OK)
			return retval;
	}

	if (ejtag_info->debug_caps & EJTAG_DCR_DB) {
		retval = mips32_configure_dbs(target);
		if (retval != ERROR_OK)
			return retval;
	}

	/* check if target endianness settings matches debug control register */
	if (((ejtag_info->debug_caps & EJTAG_DCR_ENM)
			&& (target->endianness == TARGET_LITTLE_ENDIAN)) ||
			(!(ejtag_info->debug_caps & EJTAG_DCR_ENM)
			 && (target->endianness == TARGET_BIG_ENDIAN)))
		LOG_WARNING("DCR endianness settings does not match target settings");

	LOG_DEBUG("DCR 0x%" PRIx32 " numinst %i numdata %i", dcr, mips32->num_inst_bpoints,
			mips32->num_data_bpoints);

	mips32->bp_scanned = 1;

	return ERROR_OK;
}

int mips32_enable_interrupts(struct target *target, int enable)
{
	int retval;
	int update = 0;
	uint32_t dcr;

	/* read debug control register */
	retval = target_read_u32(target, EJTAG_DCR, &dcr);
	if (retval != ERROR_OK)
		return retval;

	if (enable) {
		if (!(dcr & EJTAG_DCR_INTE)) {
			/* enable interrupts */
			dcr |= EJTAG_DCR_INTE;
			update = 1;
		}
	} else {
		if (dcr & EJTAG_DCR_INTE) {
			/* disable interrupts */
			dcr &= ~EJTAG_DCR_INTE;
			update = 1;
		}
	}

	if (update) {
		retval = target_write_u32(target, EJTAG_DCR, dcr);
		if (retval != ERROR_OK)
			return retval;
	}

	return ERROR_OK;
}

int mips32_checksum_memory(struct target *target, uint32_t address,
		uint32_t count, uint32_t *checksum)
{
	struct working_area *crc_algorithm;
	struct reg_param reg_params[2];
	struct mips32_algorithm mips32_info;

	/* see contrib/loaders/checksum/mips32.s for src */

	static const uint32_t mips_crc_code[] = {
		0x248C0000,		/* addiu	$t4, $a0, 0 */
		0x24AA0000,		/* addiu	$t2, $a1, 0 */
		0x2404FFFF,		/* addiu	$a0, $zero, 0xffffffff */
		0x10000010,		/* beq		$zero, $zero, ncomp */
		0x240B0000,		/* addiu	$t3, $zero, 0 */
						/* nbyte: */
		0x81850000,		/* lb		$a1, ($t4) */
		0x218C0001,		/* addi		$t4, $t4, 1 */
		0x00052E00,		/* sll		$a1, $a1, 24 */
		0x3C0204C1,		/* lui		$v0, 0x04c1 */
		0x00852026,		/* xor		$a0, $a0, $a1 */
		0x34471DB7,		/* ori		$a3, $v0, 0x1db7 */
		0x00003021,		/* addu		$a2, $zero, $zero */
						/* loop: */
		0x00044040,		/* sll		$t0, $a0, 1 */
		0x24C60001,		/* addiu	$a2, $a2, 1 */
		0x28840000,		/* slti		$a0, $a0, 0 */
		0x01074826,		/* xor		$t1, $t0, $a3 */
		0x0124400B,		/* movn		$t0, $t1, $a0 */
		0x28C30008,		/* slti		$v1, $a2, 8 */
		0x1460FFF9,		/* bne		$v1, $zero, loop */
		0x01002021,		/* addu		$a0, $t0, $zero */
						/* ncomp: */
		0x154BFFF0,		/* bne		$t2, $t3, nbyte */
		0x256B0001,		/* addiu	$t3, $t3, 1 */
		0x7000003F,		/* sdbbp */
	};

	/* make sure we have a working area */
	if (target_alloc_working_area(target, sizeof(mips_crc_code), &crc_algorithm) != ERROR_OK)
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;

	/* convert mips crc code into a buffer in target endianness */
	uint8_t mips_crc_code_8[sizeof(mips_crc_code)];
	target_buffer_set_u32_array(target, mips_crc_code_8,
					ARRAY_SIZE(mips_crc_code), mips_crc_code);

	target_write_buffer(target, crc_algorithm->address, sizeof(mips_crc_code), mips_crc_code_8);

	mips32_info.common_magic = MIPS32_COMMON_MAGIC;
	mips32_info.isa_mode = MIPS32_ISA_MIPS32;

	init_reg_param(&reg_params[0], "r4", 32, PARAM_IN_OUT);
	buf_set_u32(reg_params[0].value, 0, 32, address);

	init_reg_param(&reg_params[1], "r5", 32, PARAM_OUT);
	buf_set_u32(reg_params[1].value, 0, 32, count);

	int timeout = 20000 * (1 + (count / (1024 * 1024)));

	int retval = target_run_algorithm(target, 0, NULL, 2, reg_params,
			crc_algorithm->address, crc_algorithm->address + (sizeof(mips_crc_code) - 4), timeout,
			&mips32_info);

	if (retval == ERROR_OK)
		*checksum = buf_get_u32(reg_params[0].value, 0, 32);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);

	target_free_working_area(target, crc_algorithm);

	return retval;
}

/** Checks whether a memory region is erased. */
int mips32_blank_check_memory(struct target *target,
		uint32_t address, uint32_t count, uint32_t *blank, uint8_t erased_value)
{
	struct working_area *erase_check_algorithm;
	struct reg_param reg_params[3];
	struct mips32_algorithm mips32_info;

	static const uint32_t erase_check_code[] = {
						/* nbyte: */
		0x80880000,		/* lb		$t0, ($a0) */
		0x00C83024,		/* and		$a2, $a2, $t0 */
		0x24A5FFFF,		/* addiu	$a1, $a1, -1 */
		0x14A0FFFC,		/* bne		$a1, $zero, nbyte */
		0x24840001,		/* addiu	$a0, $a0, 1 */
		0x7000003F		/* sdbbp */
	};

	if (erased_value != 0xff) {
		LOG_ERROR("Erase value 0x%02" PRIx8 " not yet supported for MIPS32",
			erased_value);
		return ERROR_FAIL;
	}

	/* make sure we have a working area */
	if (target_alloc_working_area(target, sizeof(erase_check_code), &erase_check_algorithm) != ERROR_OK)
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;

	/* convert erase check code into a buffer in target endianness */
	uint8_t erase_check_code_8[sizeof(erase_check_code)];
	target_buffer_set_u32_array(target, erase_check_code_8,
					ARRAY_SIZE(erase_check_code), erase_check_code);

	target_write_buffer(target, erase_check_algorithm->address, sizeof(erase_check_code), erase_check_code_8);

	mips32_info.common_magic = MIPS32_COMMON_MAGIC;
	mips32_info.isa_mode = MIPS32_ISA_MIPS32;

	init_reg_param(&reg_params[0], "r4", 32, PARAM_OUT);
	buf_set_u32(reg_params[0].value, 0, 32, address);

	init_reg_param(&reg_params[1], "r5", 32, PARAM_OUT);
	buf_set_u32(reg_params[1].value, 0, 32, count);

	init_reg_param(&reg_params[2], "r6", 32, PARAM_IN_OUT);
	buf_set_u32(reg_params[2].value, 0, 32, erased_value);

	int retval = target_run_algorithm(target, 0, NULL, 3, reg_params,
			erase_check_algorithm->address,
			erase_check_algorithm->address + (sizeof(erase_check_code) - 4),
			10000, &mips32_info);

	if (retval == ERROR_OK)
		*blank = buf_get_u32(reg_params[2].value, 0, 32);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);

	target_free_working_area(target, erase_check_algorithm);

	return retval;
}

int mips32_close_watchdog(struct target *target){
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
	int retval = ERROR_OK;

    retval = mips32_pracc_close_watchdog(ejtag_info);
    if (retval != ERROR_OK)
        return retval;

    return ERROR_OK;
}

static int mips32_verify_pointer(struct command_context *cmd_ctx,
		struct mips32_common *mips32)
{
	if (mips32->common_magic != MIPS32_COMMON_MAGIC) {
		command_print(cmd_ctx, "target is not an MIPS32");
		return ERROR_TARGET_INVALID;
	}
	return ERROR_OK;
}

/**
 * MIPS32 targets expose command interface
 * to manipulate CP0 registers
 */
COMMAND_HANDLER(mips32_handle_cp0_command)
{
	int retval;
	struct target *target = get_current_target(CMD_CTX);
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;


	retval = mips32_verify_pointer(CMD_CTX, mips32);
	if (retval != ERROR_OK)
		return retval;

	if (target->state != TARGET_HALTED) {
		command_print(CMD_CTX, "target must be stopped for \"%s\" command", CMD_NAME);
		return ERROR_OK;
	}

	/* two or more argument, access a single register/select (write if third argument is given) */
	if (CMD_ARGC < 2)
		return ERROR_COMMAND_SYNTAX_ERROR;
	else {
		uint32_t cp0_reg, cp0_sel;
		COMMAND_PARSE_NUMBER(u32, CMD_ARGV[0], cp0_reg);
		COMMAND_PARSE_NUMBER(u32, CMD_ARGV[1], cp0_sel);

		if (CMD_ARGC == 2) {
			uint32_t value;

			retval = mips32_cp0_read(ejtag_info, &value, cp0_reg, cp0_sel);
			if (retval != ERROR_OK) {
				command_print(CMD_CTX,
						"couldn't access reg %" PRIi32,
						cp0_reg);
				return ERROR_OK;
			}
			command_print(CMD_CTX, "cp0 reg %" PRIi32 ", select %" PRIi32 ": %8.8" PRIx32,
					cp0_reg, cp0_sel, value);

		} else if (CMD_ARGC == 3) {
			uint32_t value;
			COMMAND_PARSE_NUMBER(u32, CMD_ARGV[2], value);
			retval = mips32_cp0_write(ejtag_info, value, cp0_reg, cp0_sel);
			if (retval != ERROR_OK) {
				command_print(CMD_CTX,
						"couldn't access cp0 reg %" PRIi32 ", select %" PRIi32,
						cp0_reg,  cp0_sel);
				return ERROR_OK;
			}
			command_print(CMD_CTX, "cp0 reg %" PRIi32 ", select %" PRIi32 ": %8.8" PRIx32,
					cp0_reg, cp0_sel, value);
		}
	}

	return ERROR_OK;
}

COMMAND_HANDLER(mips32_handle_scan_delay_command)
{
	struct target *target = get_current_target(CMD_CTX);
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;

	if (CMD_ARGC == 1)
		COMMAND_PARSE_NUMBER(uint, CMD_ARGV[0], ejtag_info->scan_delay);
	else if (CMD_ARGC > 1)
			return ERROR_COMMAND_SYNTAX_ERROR;

	command_print(CMD_CTX, "scan delay: %d nsec", ejtag_info->scan_delay);
	if (ejtag_info->scan_delay >= MIPS32_SCAN_DELAY_LEGACY_MODE) {
		ejtag_info->mode = 0;
		command_print(CMD_CTX, "running in legacy mode");
	} else {
		ejtag_info->mode = 1;
		command_print(CMD_CTX, "running in fast queued mode");
	}

	return ERROR_OK;
}

COMMAND_HANDLER(mips32_handle_invalidate_cache_command)
{
	int retval = -1;
	struct target *target = get_current_target(CMD_CTX);
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;

	if (target->state != TARGET_HALTED) {
		LOG_WARNING("target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}

	if ((CMD_ARGC >= 2) || (CMD_ARGC == 0)){
		LOG_DEBUG("ERROR_COMMAND_SYNTAX_ERROR");
		return ERROR_COMMAND_SYNTAX_ERROR;
	}

	if (CMD_ARGC == 1) {
		for (int i = 0; i < 7 ; i++) {
			if (strcmp(CMD_ARGV[0], invalidate_cmd[i].arg) == 0) {
				switch (invalidate_cmd[i].option) {
					case INSTNOWB:
						LOG_INFO("Clearing L1 instr cache, no writeback");
						retval = mips32_pracc_invalidate_cache(target, ejtag_info, INSTNOWB);
						break;
					case DATA:
						LOG_INFO("Clearing L1 data cache, writeback");
						retval = mips32_pracc_invalidate_cache(target, ejtag_info, DATA);
						break;
					case DATANOWB:
						LOG_INFO("Clearing L1 data cache, no writeback");
						mips32_pracc_invalidate_cache(target, ejtag_info, DATANOWB);
						break;
					case L2:
						if (ejtag_info->core_type == MIPS_INGENIC_XBURST1) {
							LOG_DEBUG("Ingenic XBurst1 does not support clear L2 cache alone");
							return ERROR_FAIL;
						} else {
							LOG_INFO("Clearing L2 cache, writeback");
							mips32_pracc_invalidate_cache(target, ejtag_info, L2);
						}
						break;
					case L2NOWB:
						if (ejtag_info->core_type == MIPS_INGENIC_XBURST1) {
							LOG_DEBUG("Ingenic XBurst1 does not support clear L2 cache alone");
							return ERROR_FAIL;
						} else {
							LOG_INFO("Clearing L2 cache, no writeback");
							mips32_pracc_invalidate_cache(target, ejtag_info, L2NOWB);
						}
						break;
					case ALL:
						LOG_INFO("Clearing L1 instr cache, no writeback");
						mips32_pracc_invalidate_cache(target, ejtag_info, INSTNOWB);

						LOG_INFO("Clearing L1 data cache, writeback");
						mips32_pracc_invalidate_cache(target, ejtag_info, DATA);

						if (ejtag_info->core_type != MIPS_INGENIC_XBURST1) {
							LOG_INFO("Clearing L2 cache, writeback");
							mips32_pracc_invalidate_cache(target, ejtag_info, L2);
						}
						break;
					case ALLNOWB:
						LOG_INFO("Clearing L1 instr cache, no writeback");
						mips32_pracc_invalidate_cache(target, ejtag_info, INSTNOWB);

						LOG_INFO("Clearing L1 data cache, no writeback");
						mips32_pracc_invalidate_cache(target, ejtag_info, DATANOWB);

						if (ejtag_info->core_type != MIPS_INGENIC_XBURST1) {
							LOG_INFO("Clearing L2 cache, no writeback");
							mips32_pracc_invalidate_cache(target, ejtag_info, L2NOWB);
						}
						break;
					default:
						LOG_ERROR("Invalid command argument '%s' not found", CMD_ARGV[0]);
						return ERROR_COMMAND_SYNTAX_ERROR;
						break;
				}

				if (retval == ERROR_FAIL)
					return ERROR_FAIL;
				else
					break;
			}
		}
	}

	return ERROR_OK;
}

COMMAND_HANDLER(mips32_handle_queue_exec)
{
	struct target *target = get_current_target(CMD_CTX);
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
	uint8_t *buffer = NULL;
	size_t buf_cnt;
	uint32_t image_size = 0;
	struct image image;
	int retval = ERROR_OK;
	uint32_t instr;
	uint32_t length = 0;

	if (CMD_ARGC != 2)
		return ERROR_COMMAND_SYNTAX_ERROR;

	image.base_address_set = 0;
	image.start_address_set = 0;

	if (image_open(&image, CMD_ARGV[0], CMD_ARGV[1]) != ERROR_OK)
		return ERROR_FAIL;

	if (image.num_sections > 1) {
		LOG_DEBUG("Can not support section num:%d", image.num_sections);
		return ERROR_FAIL;
	}
	buffer = malloc(image.sections[0].size);
	if (buffer == NULL) {
		command_print(CMD_CTX, "error allocating buffer for section (%d bytes)", (int)(image.sections[0].size));
		return ERROR_FAIL;
	}
	for (int i = 0; i < image.num_sections; i++) {
		retval = image_read_section(&image, i, 0x0, image.sections[i].size, buffer, &buf_cnt);
		if (retval != ERROR_OK) {
			goto exit;
		}
		length = buf_cnt;
		if (image.sections[i].base_address != 0xff200000) {
			LOG_DEBUG("Can not support base_address:0x%08x", (uint32_t)image.sections[i].base_address);
			retval = ERROR_FAIL;
			goto exit;
		}
		if (length < 0x200) {
			LOG_DEBUG("length < 0x200");
			retval = ERROR_FAIL;
			goto exit;
		}
		image_size += length;
	}

	struct pracc_queue_info ctx;
	pracc_queue_init(&ctx);
	for (uint32_t j = 0x200; j < length; j = j + 4) {
		instr = (buffer[j + 3] << 24) | (buffer[j + 2] << 16) | (buffer[j + 1] << 8) | (buffer[j]);
		pracc_add(&ctx, 0, instr);
	}
	mips32_pracc_queue_exec(ejtag_info, &ctx, NULL);
	pracc_queue_free(&ctx);

exit:
	image_close(&image);
	free(buffer);
	return retval;
}

COMMAND_HANDLER(mips32_handle_fast_exec)
{
	struct target *target = get_current_target(CMD_CTX);
	struct mips32_common *mips32 = target_to_mips32(target);
	struct mips_ejtag *ejtag_info = &mips32->ejtag_info;
	int retval = ERROR_OK;
	uint32_t start_addr;

	if (CMD_ARGC < 4)
		return ERROR_COMMAND_SYNTAX_ERROR;

	COMMAND_PARSE_NUMBER(u32, CMD_ARGV[0], start_addr);
	retval = mips32_pracc_fast_exec(ejtag_info, start_addr, CMD_ARGV[1], CMD_ARGV[2], CMD_ARGV[3]);

	return retval;
}

static const struct command_registration mips32_exec_command_handlers[] = {
	{
		.name = "cp0",
		.handler = mips32_handle_cp0_command,
		.mode = COMMAND_EXEC,
		.usage = "regnum select [value]",
		.help = "display/modify cp0 register",
	},
	{
		.name = "scan_delay",
		.handler = mips32_handle_scan_delay_command,
		.mode = COMMAND_ANY,
		.help = "display/set scan delay in nano seconds",
		.usage = "[value]",
	},
	{
		.name = "invalidate",
		.handler = mips32_handle_invalidate_cache_command,
		.mode = COMMAND_EXEC,
		.help = "Invalidate either or both the instruction and data caches.",
		.usage = "instnowb|data|datanowb|l2|l2nowb|all|allnowb",
	},
	{
		.name = "queue_exec",
		.handler = mips32_handle_queue_exec,
		.mode = COMMAND_EXEC,
		.help = "Execute the executable program in the debug mode.",
		.usage = "[file_name] [bin|elf|ihex|s19]",
	},
	{
		.name = "fast_exec",
		.handler = mips32_handle_fast_exec,
		.mode = COMMAND_EXEC,
		.help = "Execute the executable program in the debug mode.",
		.usage = "[file_name] [bin|elf|ihex|s19]",
	},
	COMMAND_REGISTRATION_DONE
};

const struct command_registration mips32_command_handlers[] = {
	{
		.name = "mips32",
		.mode = COMMAND_ANY,
		.help = "mips32 command group",
		.usage = "",
		.chain = mips32_exec_command_handlers,
	},
	COMMAND_REGISTRATION_DONE
};
