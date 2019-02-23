#define share_data	*(volatile unsigned int*)(0xf4001ff0)
#define share_data2	*(volatile unsigned int*)(0xf4001ff4)

void trap_entry(void)
{
	/*Now Noting to do*/
}

int  main()
{
	int i = 0;
	int data  	= 0x00000000;
	int dout 	= 0x00000000;
	int type	= 0x00000000;
	int skip	= 0x00000000;
	int buffer 	= 0x00000000;
	int status  	= 0x00000030;
	int scan_size	= 0x00000000;
	int tms_count	= 0x00000000;
	unsigned tms_scan	= 0x00000000;
	unsigned bit_cnt	= 0x00000000;
	unsigned tms_flag	= 0x00000000;
	unsigned tap_flag	= 0x00000000;

	while(1) {
		dout = share_data;

		/* Finish processor access */
		if(dout == 0x40000000) {
//			0000  0000  0000  0000  0000  0000  0000  0000
//			      状态
			share_data = 0;

			/* 发送irscan头(TMS1100) */
			for (i = 0; i < 4; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 发送EJTAG_INST_CONTROL（0x0A）*/
			for (bit_cnt = 0; bit_cnt < 4; bit_cnt++) {
				status = 0x30 | ((0xA >> bit_cnt) & 1) << 2;					//设置TMS TDI值
				(*(volatile unsigned int *)0x10010340) = status;				//输出TMS TDI CLK0
				(*(volatile unsigned int *)0x10010340) = status | 1;			//输出TMS TDI CLK1
			}
			/* 发送EJTAG_INST_CONTROL最后一位及TMS第一位 */
			status = 0x30 | (((0xA >> 4) & 1) << 2) | (1 << 1);
			(*(volatile unsigned int *)0x10010340) = status;
			(*(volatile unsigned int *)0x10010340) = status | 1;

			/* 发送irscan尾后两位(TMS10) */
			for (i = 1; i < 3; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 发送drscan头(TMS100) */
			for (i = 0; i < 3; i++) {
				status = 0x30 | ((1 >> i) & 1) << 1;							//设置TMS TDI值
				(*(volatile unsigned int *)0x10010340) = status;				//输出TMS TDI CLK0
				(*(volatile unsigned int *)0x10010340) = status | 1;			//输出TMS TDI CLK1
			}

			/* 发送mips32_pracc_finish固定数据（0x0000C000）*/
			for (bit_cnt = 0; bit_cnt < 14; bit_cnt++) {						//将31次的循环展开，以获得更好的速度，此为其中的14次
				(*(volatile unsigned int *)0x10010340) = 0x30;
				(*(volatile unsigned int *)0x10010340) = 0x31;
			}
			for (bit_cnt = 0; bit_cnt < 2; bit_cnt++) {							//将31次的循环展开，以获得更好的速度，此为其中的2次
				(*(volatile unsigned int *)0x10010340) = 0x34;
				(*(volatile unsigned int *)0x10010340) = 0x35;
			}
			for (bit_cnt = 0; bit_cnt < 15; bit_cnt++) {						//将31次的循环展开，以获得更好的速度，此为其中的15次
				(*(volatile unsigned int *)0x10010340) = 0x30;
				(*(volatile unsigned int *)0x10010340) = 0x31;
			}
			/* 发送mips32_pracc_finish固定数据最后一位及TMS第一位 */
			(*(volatile unsigned int *)0x10010340) = 0x36;
			(*(volatile unsigned int *)0x10010340) = 0x37;

			/* 发送drscan尾后两位(TMS10) */
			for (i = 1; i < 3; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 操作完毕，置低时钟线 */
			(*(volatile unsigned int *)0x10010340) = status;
		}

		/* irscan，单核时为5位，双核时10位，三核时15位，四核时20位 */
		if(dout & 0x08000000) {
//			0000  0000  0000  0000  0000  0000  0000  0000
//			      状态  位数   数------------------------据
			share_data = 0;
			data = dout & 0x000fffff;

			/* 发送irscan头(TMS1100) */
			for (i = 0; i < 4; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 判断若dout为常用指令0x8则发送EJTAG_INST_ADDRESS（0x08），此处单列的目的是提高单步访问时的速度 */
			if (data == 0x8) {
				for (bit_cnt = 0; bit_cnt < 4; bit_cnt++) {
					status = 0x30 | ((0x8 >> bit_cnt) & 1) << 2;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				status = 0x30 | (((0x8 >> 4) & 1) << 2) | (1 << 1);
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 判断若dout为常用指令0x9则发送EJTAG_INST_DATA（0x09），此处单列的目的是提高单步访问时的速度 */
			if (data == 0x9) {
				for (bit_cnt = 0; bit_cnt < 4; bit_cnt++) {
					status = 0x30 | ((0x9 >> bit_cnt) & 1) << 2;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				status = 0x30 | (((0x9 >> 4) & 1) << 2) | (1 << 1);
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 判断若dout为常用指令0xA则发送EJTAG_INST_CONTROL（0x0A），此处单列的目的是提高单步访问时的速度 */
			if (data == 0xA) {
				for (bit_cnt = 0; bit_cnt < 4; bit_cnt++) {
					status = 0x30 | ((0xA >> bit_cnt) & 1) << 2;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				status = 0x30 | (((0xA >> 4) & 1) << 2) | (1 << 1);
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 判断若dout为小于0x8大于0xA的不常用指令或是大于5位的指令，则按照传入的dout值发送 */
			if (data < 0x8 || dout > 0xA) {
				/* 指令位数大于5的情况 */
				if (data > 0x1F) {
					scan_size = ((dout & 0x00f00000) >> 20) + 5 - 1;
					for (bit_cnt = 0; bit_cnt < scan_size; bit_cnt++) {
						status = 0x30 | ((data >> bit_cnt) & 1) << 2;
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
					}
					status = 0x30 | (((data >> scan_size) & 1) << 2) | (1 << 1);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				/* 5位的不常用指令 */
				} else {
					for (bit_cnt = 0; bit_cnt < 4; bit_cnt++) {
						status = 0x30 | ((data >> bit_cnt) & 1) << 2;
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
					}
					status = 0x30 | (((data >> 4) & 1) << 2) | (1 << 1);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
			}

			/* 发送irscan尾后两位(TMS10) */
			for (i = 1; i < 3; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 操作完毕，置低时钟线 */
			(*(volatile unsigned int *)0x10010340) = status;
		}

		/* 向目标板写load或dump数据 */
		if(dout & 0x04000000) {
//			0000  0000  0000  0000  0000  0000  0000  0000
//			      状态  类型
			data = share_data2;
			buffer = 0x00000000;
			type = dout & 0x00f00000;

			/* 如果是load（只写），则提前返回操作完毕信号，让主CPU提前准备下一次scan的数据，以获得更快的速度 */
			if (__builtin_expect(type == 0x00200000, 1))
				share_data = 0;

			/* 发送irscan头(TMS1100) */
 			for (i = 0; i < 4; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 发送EJTAG_INST_DATA（0x09）*/
			for (bit_cnt = 0; bit_cnt < 4; bit_cnt++) {
				status = 0x30 | ((0x9 >> bit_cnt) & 1) << 2;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}
			status = 0x30 | (((0x9 >> 4) & 1) << 2) | (1 << 1);
			(*(volatile unsigned int *)0x10010340) = status;
			(*(volatile unsigned int *)0x10010340) = status | 1;

			/* 发送irscan尾后两位(TMS10) */
			for (i = 1; i < 3; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 发送drscan头(TMS100) */
			for (i = 0; i < 3; i++) {
				status = 0x30 | ((1 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 32位drscan，只写不读 */
			if (__builtin_expect(type == 0x00200000, 1)) {
				for (bit_cnt = 0; bit_cnt < 30; bit_cnt++) {
					status = 0x30 | (((data >> bit_cnt) & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				status = 0x30 | (((data >> 30) & 1) << 2);
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
				status = 0x30 | (((data >> 31) & 1) << 2) | (1 << 1);
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			/* 32位drscan，既写也读 */
			} else {
				for (bit_cnt = 0; bit_cnt < 31; bit_cnt++) {
					status = 0x30 | (((data >> bit_cnt) & 1) << 2);				//设置TMS TDI值
					(*(volatile unsigned int *)0x10010340) = status;			//输出TMS TDI CLK0
					(*(volatile unsigned int *)0x10010340) = status | 1;		//输出TMS TDI CLK1
					if((*(volatile unsigned int *)0x10010300) & 0x00000008)		//读取TDO
						buffer = buffer | (1 << bit_cnt);
				}
				status = 0x30 | (((data >> 31) & 1) << 2) | (1 << 1);
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
				if((*(volatile unsigned int *)0x10010300) & 0x00000008)
					buffer = buffer | (1 << 31);
				share_data2 = buffer;
				share_data = 0;
			}

			/* 发送drscan尾(TMS10) */
			for (i = 1; i < 3; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 发送irscan头(TMS1100) */
			for (i = 0; i < 4; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 发送EJTAG_INST_CONTROL（0x0A）*/
			for (bit_cnt = 0; bit_cnt < 4; bit_cnt++) {
				status = 0x30 | ((0xA >> bit_cnt) & 1) << 2;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}
			status = 0x30 | (((0xA >> 4) & 1) << 2) | (1 << 1);
			(*(volatile unsigned int *)0x10010340) = status;
			(*(volatile unsigned int *)0x10010340) = status | 1;

			/* 发送irscan尾(TMS10) */
			for (i = 1; i < 3; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 发送drscan头(TMS100) */
			for (i = 0; i < 3; i++) {
				status = 0x30 | ((1 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 如果是XBurst1则按19位模式发送EJTAG_INST_CONTROL数据（0x0000C000）*/
			if (dout & 0x00000001) {
				for (bit_cnt = 0; bit_cnt < 14; bit_cnt++) {					//将18次的循环展开，以获得更好的速度，此为其中的14次
					(*(volatile unsigned int *)0x10010340) = 0x30;
					(*(volatile unsigned int *)0x10010340) = 0x31;
				}
				for (bit_cnt = 0; bit_cnt < 2; bit_cnt++) {						//将18次的循环展开，以获得更好的速度，此为其中的2次
					(*(volatile unsigned int *)0x10010340) = 0x34;
					(*(volatile unsigned int *)0x10010340) = 0x35;
				}
				for (bit_cnt = 0; bit_cnt < 2; bit_cnt++) {						//将18次的循环展开，以获得更好的速度，此为其中的2次
					(*(volatile unsigned int *)0x10010340) = 0x30;
					(*(volatile unsigned int *)0x10010340) = 0x31;
				}
				(*(volatile unsigned int *)0x10010340) = 0x32;
				(*(volatile unsigned int *)0x10010340) = 0x33;
			/* 如果不是XBurst1则按32位模式发送EJTAG_INST_CONTROL数据（0x8000C000）*/
			} else {
				for (bit_cnt = 0; bit_cnt < 14; bit_cnt++) {					//将31次的循环展开，以获得更好的速度，此为其中的14次
					(*(volatile unsigned int *)0x10010340) = 0x30;
					(*(volatile unsigned int *)0x10010340) = 0x31;
				}
				for (bit_cnt = 0; bit_cnt < 2; bit_cnt++) {						//将31次的循环展开，以获得更好的速度，此为其中的2次
					(*(volatile unsigned int *)0x10010340) = 0x34;
					(*(volatile unsigned int *)0x10010340) = 0x35;
				}
				for (bit_cnt = 0; bit_cnt < 15; bit_cnt++) {					//将31次的循环展开，以获得更好的速度，此为其中的15次
					(*(volatile unsigned int *)0x10010340) = 0x30;
					(*(volatile unsigned int *)0x10010340) = 0x31;
				}
				(*(volatile unsigned int *)0x10010340) = 0x36;
				(*(volatile unsigned int *)0x10010340) = 0x37;
			}

			/* 发送drscan尾(TMS10) */
			for (i = 1; i < 3; i++) {
				status = 0x30 | ((3 >> i) & 1) << 1;
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
			}

			/* 操作完毕，置低时钟线 */
			(*(volatile unsigned int *)0x10010340) = status;
		}

		/* 对目标板的常规数据访问 */
		if(dout & 0x02000000) {
//DATA2		0000  0000  0000  0000  0000  0000  0000  0000
//			低---------32---------位---------数---------据
//DATA		0000  0000  0000  0000  0000  0000  0000  0000
//			FTAP  状态  类型   FTMS  位------数  高 位 数 据
			data = share_data2;
			buffer = 0x00000000;
			type = dout & 0x00f00000;
			tms_flag = dout & 0x00030000;
			tap_flag = dout & 0x30000000;
			scan_size = dout & 0x0000ff00;

			/* 普通drscan的头（TMS100）*/
			if (__builtin_expect(tap_flag == 0x10000000, 1)) {
				for (i = 0; i < 3; i++) {
					status = 0x30 | ((1 >> i) & 1) << 1;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
			}

			/* 启动OpenOCD时672位特殊scan的头（TMS1110100）*/
			if (__builtin_expect(tap_flag == 0x20000000, 0)) {
				for (i = 0; i < 7; i++) {
					status = 0x30 | ((23 >> i) & 1) << 1;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
			}

			/* 判断type，若等于0x00200000则说明drscan没有读操作，否则具有读操作 */
			if (type == 0x00200000) {
				share_data = 0;
				for (bit_cnt = 0; bit_cnt < 30; bit_cnt++) {
					status = 0x30 | (((data >> bit_cnt) & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				status = 0x30 | (((data >> 30) & 1) << 2);
				(*(volatile unsigned int *)0x10010340) = status;
				(*(volatile unsigned int *)0x10010340) = status | 1;
				/* 32位drscan，发送第32位数据 */
				if (scan_size == (32 << 8)) {
					/* 第32位数据 */
					status = 0x30 | (((data >> 31) & 1) << 2) | (1 << 1);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				/* 33位drscan，发送第32和33位数据 */
				if (scan_size == (33 << 8)) {
					/* 第32位数据 */
					status = 0x30 | (((data >> 31) & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
					/* 第33位数据 */
					status = 0x30 | ((dout & 1) << 2) | (1 << 1);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				/* 34位drscan，发送第32至34位数据 */
				if (scan_size == (34 << 8)) {
					/* 第32位数据 */
					status = 0x30 | (((data >> 31) & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
					/* 第33位数据 */
					status = 0x30 | ((dout & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
					/* 第34位数据 */
					status = 0x30 | (((dout >> 1) & 1) << 2) | (1 << 1);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				/* 35位drscan，发送第32至35位数据 */
				if (scan_size == (35 << 8)) {
					/* 第32位数据 */
					status = 0x30 | (((data >> 31) & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
					/* 第33位数据 */
					status = 0x30 | ((dout & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
					/* 第34位数据 */
					status = 0x30 | (((dout >> 1) & 1) << 2);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
					/* 第35位数据 */
					status = 0x30 | (((dout >> 2) & 1) << 2) | (1 << 1);
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
			} else {
				/* drscan，既读也写 */
				if (type != 0x00100000) {
					for (bit_cnt = 0; bit_cnt < 31; bit_cnt++) {
						status = 0x30 | (((data >> bit_cnt) & 1) << 2);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << bit_cnt);
					}
					/* 32位drscan，发送第32位数据 */
					if (scan_size == (32 << 8)) {
						/* 第32位数据 */
						status = 0x30 | (((data >> 31) & 1) << 2) | (tms_flag ? (1 << 1) : 0);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//全部32位数据由share_data2传递
						share_data = 0;											//由share_data传递MCU操作完成信号
					}
					/* 33位drscan，发送第32和33位数据 */
					if (scan_size == (33 << 8)) {
						/* 第32位数据 */
						status = 0x30 | (((data >> 31) & 1) << 2);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//33位数据中低32位数据由share_data2传递
						/* 第33位数据 */
						buffer = 0;
						status = 0x30 | ((dout & 1) << 2) | (1 << 1);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = 1;
						share_data = buffer;									//33位数据中第33位数据和MCU操作完成信号共同由share_data传递
					}
					/* 34位drscan，发送第32至34位数据 */
					if (scan_size == (34 << 8)) {
						/* 第32位数据 */
						status = 0x30 | (((data >> 31) & 1) << 2);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//34位数据中低32位数据由share_data2传递
						/* 第33位数据 */
						buffer = 0;
						status = 0x30 | ((dout & 1) << 2);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = 1;
						/* 第34位数据 */
						status = 0x30 | (((dout >> 1) & 1) << 2) | (1 << 1);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 1);
						share_data = buffer;									//34位数据中第33和34位数据和MCU操作完成信号共同由share_data传递
					}
					/* 35位drscan，发送第32至35位数据 */
					if (scan_size == (35 << 8)) {
						/* 第32位数据 */
						status = 0x30 | (((data >> 31) & 1) << 2);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//35位数据中低32位数据由share_data2传递
						/* 第33位数据 */
						buffer = 0;
						status = 0x30 | ((dout & 1) << 2);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = 1;
						/* 第34位数据 */
						status = 0x30 | (((dout >> 1) & 1) << 2);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 1);
						/* 第35位数据 */
						status = 0x30 | (((dout >> 2) & 1) << 2) | (1 << 1);
						(*(volatile unsigned int *)0x10010340) = status;
						(*(volatile unsigned int *)0x10010340) = status | 1;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 2);
						share_data = buffer;									//35位数据中第33至35位数据和MCU操作完成信号共同由share_data传递
					}
				/* drscan，只读不写 */
				} else {
					for (bit_cnt = 0; bit_cnt < 31; bit_cnt++) {
						(*(volatile unsigned int *)0x10010340) = 0x30;
						(*(volatile unsigned int *)0x10010340) = 0x31;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << bit_cnt);
					}
					/* 32位drscan，发送第32位数据 */
					if (scan_size == (32 << 8)) {
						/* 第32位数据 */
						(*(volatile unsigned int *)0x10010340) = 0x32;
						(*(volatile unsigned int *)0x10010340) = 0x33;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//全部32位数据由share_data2传递
						share_data = 0;											//MCU操作完成信号由share_data传递
					}
					/* 33位drscan，发送第32和33位数据 */
					if (scan_size == (33 << 8)) {
						/* 第32位数据 */
						(*(volatile unsigned int *)0x10010340) = 0x30;
						(*(volatile unsigned int *)0x10010340) = 0x31;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//33位数据中低32位数据由share_data2传递
						/* 第33位数据 */
						buffer = 0;
						(*(volatile unsigned int *)0x10010340) = 0x32;
						(*(volatile unsigned int *)0x10010340) = 0x33;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = 1;
						share_data = buffer;									//33位数据中第33位数据和MCU操作完成信号共同由share_data传递
					}
					/* 34位drscan，发送第32至34位数据 */
					if (scan_size == (34 << 8)) {
						/* 第32位数据 */
						(*(volatile unsigned int *)0x10010340) = 0x30;
						(*(volatile unsigned int *)0x10010340) = 0x31;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//34位数据中低32位数据由share_data2传递
						/* 第33位数据 */
						buffer = 0;
						(*(volatile unsigned int *)0x10010340) = 0x30;
						(*(volatile unsigned int *)0x10010340) = 0x31;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = 1;
						/* 第34位数据 */
						(*(volatile unsigned int *)0x10010340) = 0x32;
						(*(volatile unsigned int *)0x10010340) = 0x33;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 1);
						share_data = buffer;									//34位数据中第33和34位数据和MCU操作完成信号共同由share_data传递
					}
					/* 35位drscan，发送第32至35位数据 */
					if (scan_size == (35 << 8)) {
						/* 第32位数据 */
						(*(volatile unsigned int *)0x10010340) = 0x30;
						(*(volatile unsigned int *)0x10010340) = 0x31;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 31);
						share_data2 = buffer;									//35位数据中低32位数据由share_data2传递
						/* 第33位数据 */
						buffer = 0;
						(*(volatile unsigned int *)0x10010340) = 0x30;
						(*(volatile unsigned int *)0x10010340) = 0x31;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = 1;
						/* 第34位数据 */
						(*(volatile unsigned int *)0x10010340) = 0x30;
						(*(volatile unsigned int *)0x10010340) = 0x31;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 1);
						/* 第35位数据 */
						(*(volatile unsigned int *)0x10010340) = 0x32;
						(*(volatile unsigned int *)0x10010340) = 0x33;
						if((*(volatile unsigned int *)0x10010300) & 0x00000008)
							buffer = buffer | (1 << 2);
						share_data = buffer;									//35位数据中第33至35位数据和MCU操作完成信号共同由share_data传递
					}
				}
			}

			/* 普通drscan的尾（TMS10）*/
			if (__builtin_expect(tms_flag == 0x00010000, 1)) {
				for (i = 1; i < 3; i++) {
					status = 0x30 | ((3 >> i) & 1) << 1;						//TMS
					(*(volatile unsigned int *)0x10010340) = status;			//设置TMS TDI,输出CLK0
					(*(volatile unsigned int *)0x10010340) = status | 1;		//设置TMS TDI,输出CLK1
				}
				/* 操作完毕，置低时钟线 */
				(*(volatile unsigned int *)0x10010340) = status;
			}

			/* 启动OpenOCD时672位特殊scan的尾（TMS1）*/
			if (__builtin_expect(tms_flag == 0x00020000, 0)) {
				for (i = 1; i < 2; i++) {
					status = 0x30 | ((1 >> i) & 1) << 1;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}

				/* 操作完毕，置低时钟线 */
				(*(volatile unsigned int *)0x10010340) = status;
			}
		}

		/* 两种低频次的操作 */
		if(__builtin_expect(dout & 0x01000000, 0)) {
//share_data2	0000  0000  0000  0000  0000  0000  0000  0000
//  			数------------------------------------------据
//share_data	0000  0000  0000  0000  0000  0000  0000  0000
//操作1 			      状态  类型  skip   tms_scan    tms_count
//操作2			      状态  类型                     scan_size
			i = 0;
			buffer = 0x00000000;

			/* 启动OpenOCD时的特殊scan，单核时为7位，双核时12位，三核时17位，四核时22位 */
			if (dout & 0x00100000) {
				data = share_data2;
				scan_size = (dout & 0x000000ff) - 1;

				/* 启动OpenOCD时特殊scan的头（TMS1110010）*/
				for (i = 0; i < 7; i++) {
					status = 0x30 | ((27 >> i) & 1) << 1;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				/* 启动OpenOCD时特殊scan的内容 */
				for (bit_cnt = 0; bit_cnt < scan_size; bit_cnt++) {
					status = 0x30 | ((data >> bit_cnt) & 1) << 2;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
					if((*(volatile unsigned int *)0x10010300) & 0x00000008)
						buffer = buffer | (1 << bit_cnt);
				}
				status = 0x30 | (((data >> scan_size) & 1) << 2) | (1 << 1);	//设置TMS TDI值
				(*(volatile unsigned int *)0x10010340) = status;				//输出TMS TDI CLK0
				(*(volatile unsigned int *)0x10010340) = status | 1;			//输出TMS TDI CLK1
				if((*(volatile unsigned int *)0x10010300) & 0x00000008)			//读取TDO
					buffer = buffer | (1 << bit_cnt);
				share_data2 = buffer;
				share_data = 0;
				/* 启动OpenOCD时特殊scan的尾（TMS10）*/
				for (i = 1; i < 3; i++) {
					status = 0x30 | ((3 >> i) & 1) << 1;
					(*(volatile unsigned int *)0x10010340) = status;
					(*(volatile unsigned int *)0x10010340) = status | 1;
				}
				/* 操作完毕，置低时钟线 */
				(*(volatile unsigned int *)0x10010340) = status;

			/* jdi_state_move */
			} else {
				skip = (dout & 0x000f0000) >> 16;
				tms_scan = (dout & 0x0000ff00) >> 8;
				tms_count = dout & 0x000000ff;
				share_data = 0;
				for (i = skip; i < tms_count; i++) {
					status = 0x30 | ((tms_scan >> i) & 1) << 1;					//设置TMS TDI值
					(*(volatile unsigned int *)0x10010340) = status;			//输出TMS TDI CLK0
					(*(volatile unsigned int *)0x10010340) = status | 1;		//输出TMS TDI CLK1
				}
				/* 操作完毕，置低时钟线 */
				(*(volatile unsigned int *)0x10010340) = status;
			}
		}
	}
	return 0;
}

