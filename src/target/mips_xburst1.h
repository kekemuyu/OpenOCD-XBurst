/***************************************************************************
 *   Copyright (C) 2018 by TianyangLiu                                     *
 *   rick.tyliu@ingenic.com                                                *
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

#ifndef OPENOCD_TARGET_MIPS_XBURST1_H
#define OPENOCD_TARGET_MIPS_XBURST1_H

struct target;

#define MIPSXBURST1_COMMON_MAGIC	0xB321B321

struct mips_xburst1_common {
	uint32_t common_magic;
	bool is_pic32mx;
	struct mips32_common mips32;
};

static inline struct mips_xburst1_common *
target_to_xburst1(struct target *target)
{
	return container_of(target->arch_info,
			struct mips_xburst1_common, mips32);
}

static inline void mips_xburst1_isa_filter(enum mips32_isa_imp isa_imp, target_addr_t  *addr)
{
	if (isa_imp <= 1) {	/* if only one isa implemented */
		target_addr_t address = (*addr & ~1) | isa_imp;

		if (address != *addr) {
			LOG_USER("Warning: isa bit changed due to isa not implemented");
			*addr = address;
		}
	}
}
extern const struct command_registration mips_xburst1_command_handlers[];

#endif /* OPENOCD_TARGET_MIPS_XBURST1_H */
