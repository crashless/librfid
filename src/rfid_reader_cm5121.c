/* Omnikey CardMan 5121 specific RC632 transport layer 
 *
 * (C) 2005 by Harald Welte <laforge@gnumonks.org>
 *
 * The 5121 is an Atmel AT98C5122 based USB CCID reader (probably the same
 * design like the 3121).  It's CL RC632 is connected via address/data bus,
 * not via SPI.
 *
 * The vendor-supplied reader firmware provides some undocumented extensions 
 * to CCID (via PC_to_RDR_Escape) that allow access to registers and FIFO of
 * the RC632.
 * 
 */

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 
 *  as published by the Free Software Foundation
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <librfid/rfid.h>
#include <librfid/rfid_reader.h>
#include <librfid/rfid_asic.h>
#include <librfid/rfid_asic_rc632.h>
#include <librfid/rfid_reader_cm5121.h>

/* FIXME */
#include "rc632.h"

//#define SENDBUF_LEN	40
#define SENDBUF_LEN	100
#define RECVBUF_LEN	40

static
int Write1ByteToReg(struct rfid_asic_transport_handle *rath,
		    unsigned char reg, unsigned char value)
{
	unsigned char sndbuf[SENDBUF_LEN];
	unsigned char rcvbuf[RECVBUF_LEN];
	unsigned int retlen = RECVBUF_LEN;

	sndbuf[0] = 0x20;
	sndbuf[1] = 0x00;
	sndbuf[2] = 0x01;
	sndbuf[3] = 0x00;
	sndbuf[4] = 0x00;
	sndbuf[5] = 0x00;
	sndbuf[6] = reg;
	sndbuf[7] = value;

	DEBUGP("reg=0x%02x, val=%02x: ", reg, value);

	if (PC_to_RDR_Escape(rath->data, sndbuf, 8, rcvbuf, 
			     &retlen) == 0) {
		DEBUGPC("OK\n");
		return 0;
	}

	DEBUGPC("ERROR\n");
	return -1;
}

static int Read1ByteFromReg(struct rfid_asic_transport_handle *rath,
			    unsigned char reg,
			    unsigned char *value)
{
	unsigned char sndbuf[SENDBUF_LEN];
	unsigned char recvbuf[RECVBUF_LEN];
	unsigned int retlen = sizeof(recvbuf);

	sndbuf[0] = 0x20;
	sndbuf[1] = 0x00;
	sndbuf[2] = 0x00;
	sndbuf[3] = 0x00;
	sndbuf[4] = 0x01;
	sndbuf[5] = 0x00;
	sndbuf[6] = reg;

	if (PC_to_RDR_Escape(rath->data, sndbuf, 7, recvbuf, 
			     &retlen) == 0) {
		*value = recvbuf[1];
		DEBUGP("reg=0x%02x, val=%02x: ", reg, *value);
		DEBUGPC("OK\n");
		return 0;
	}

	DEBUGPC("ERROR\n");
	return -1;
}

static int ReadNBytesFromFIFO(struct rfid_asic_transport_handle *rath,
			      unsigned char num_bytes,
			      unsigned char *buf)
{
	unsigned char sndbuf[SENDBUF_LEN];
	unsigned char recvbuf[0x7f];
	unsigned int retlen = sizeof(recvbuf);

	sndbuf[0] = 0x20;
	sndbuf[1] = 0x00;
	sndbuf[2] = 0x00;
	sndbuf[3] = 0x00;
	sndbuf[4] = num_bytes;
	sndbuf[5] = 0x00;
	sndbuf[6] = 0x02;

	DEBUGP("num_bytes=%u: ", num_bytes);
	if (PC_to_RDR_Escape(rath->data, sndbuf, 7, recvbuf, &retlen) == 0) {
		DEBUGPC("%u [%s]\n", retlen,
			rfid_hexdump(recvbuf+1, num_bytes));
		memcpy(buf, recvbuf+1, num_bytes); // len == 0x7f
		return 0;
	}

	DEBUGPC("ERROR\n");
	return -1;
}

static int WriteNBytesToFIFO(struct rfid_asic_transport_handle *rath,
			     unsigned char len,
			     const unsigned char *bytes,
			     unsigned char flags)
{
	unsigned char sndbuf[SENDBUF_LEN];
	unsigned char recvbuf[0x7f];
	unsigned int retlen = sizeof(recvbuf);

	sndbuf[0] = 0x20;
	sndbuf[1] = 0x00;
	sndbuf[2] = len;
	sndbuf[3] = 0x00;
	sndbuf[4] = 0x00;
	sndbuf[5] = flags;
	sndbuf[6] = 0x02;

	DEBUGP("%u [%s]: ", len, rfid_hexdump(bytes, len));

	memcpy(sndbuf+7, bytes, len);

	if (PC_to_RDR_Escape(rath->data, sndbuf, len+7, recvbuf, &retlen) == 0) {
		DEBUGPC("OK (%u [%s])\n", retlen, rfid_hexdump(recvbuf, retlen));
		return 0;
	}

	DEBUGPC("ERROR\n");
	return -1;
}

#if 0
static int TestFIFO(struct rc632_handle *handle)
{
	unsigned char sndbuf[60]; // 0x3c

	// FIXME: repne stosd, call

	memset(sndbuf, 0, sizeof(sndbuf));

	if (WriteNBytesToFIFO(handle, sizeof(sndbuf), sndbuf, 0) < 0)
		return -1;

	return ReadNBytesFromFIFO(handle, sizeof(sndbuf), sndbuf);
}
#endif

static int cm5121_transcieve(struct rfid_reader_handle *rh,
			     enum rfid_frametype frametype,
			     const unsigned char *tx_data, unsigned int tx_len,
			     unsigned char *rx_data, unsigned int *rx_len,
			     u_int64_t timeout, unsigned int flags)
{
	return rh->ah->asic->priv.rc632.fn.transcieve(rh->ah, frametype,
						tx_data, tx_len, rx_data,
						rx_len, timeout, flags);
}

static int cm5121_transcieve_sf(struct rfid_reader_handle *rh,
			       unsigned char cmd, struct iso14443a_atqa *atqa)
{
	return rh->ah->asic->priv.rc632.fn.iso14443a.transcieve_sf(rh->ah,
								   cmd,
								   atqa);
}

static int
cm5121_transcieve_acf(struct rfid_reader_handle *rh,
		      struct iso14443a_anticol_cmd *cmd,
		      unsigned int *bit_of_col)
{
	return rh->ah->asic->priv.rc632.fn.iso14443a.transcieve_acf(rh->ah,
							 cmd, bit_of_col);
}

static int
cm5121_14443a_init(struct rfid_reader_handle *rh)
{
	return rh->ah->asic->priv.rc632.fn.iso14443a.init(rh->ah);
}

static int
cm5121_14443a_set_speed(struct rfid_reader_handle *rh, 
			unsigned int tx,
			unsigned int speed)
{
	u_int8_t rate;
	
	DEBUGP("setting rate: ");
	switch (speed) {
	case RFID_14443A_SPEED_106K:
		rate = 0x00;
		DEBUGPC("106K\n");
		break;
	case RFID_14443A_SPEED_212K:
		rate = 0x01;
		DEBUGPC("212K\n");
		break;
	case RFID_14443A_SPEED_424K:
		rate = 0x02;
		DEBUGPC("424K\n");
		break;
	case RFID_14443A_SPEED_848K:
		rate = 0x03;
		DEBUGPC("848K\n");
		break;
	default:
		return -EINVAL;
		break;
	}
	return rh->ah->asic->priv.rc632.fn.iso14443a.set_speed(rh->ah,
								tx, rate);
}

static int
cm5121_14443b_init(struct rfid_reader_handle *rh)
{
	return rh->ah->asic->priv.rc632.fn.iso14443b.init(rh->ah);
}

static int
cm5121_15693_init(struct rfid_reader_handle *rh)
{
	return rh->ah->asic->priv.rc632.fn.iso15693.init(rh->ah);
}

static int
cm5121_mifare_setkey(struct rfid_reader_handle *rh, const u_int8_t *key)
{
	return rh->ah->asic->priv.rc632.fn.mifare_classic.setkey(rh->ah, key);
}

static int
cm5121_mifare_auth(struct rfid_reader_handle *rh, u_int8_t cmd, 
		   u_int32_t serno, u_int8_t block)
{
	return rh->ah->asic->priv.rc632.fn.mifare_classic.auth(rh->ah, 
							cmd, serno, block);
}

struct rfid_asic_transport cm5121_ccid = {
	.name = "CM5121 OpenCT",
	.priv.rc632 = {
		.fn = {
			.reg_write 	= &Write1ByteToReg,
			.reg_read 	= &Read1ByteFromReg,
			.fifo_write	= &WriteNBytesToFIFO,
			.fifo_read	= &ReadNBytesFromFIFO,
		},
	},
};

static int cm5121_enable_rc632(struct rfid_asic_transport_handle *rath)
{
	unsigned char tx_buf[1] = { 0x01 };	
	unsigned char rx_buf[64];
	unsigned int rx_len = sizeof(rx_buf);

	PC_to_RDR_Escape(rath->data, tx_buf, 1, rx_buf, &rx_len);
	printf("received %u bytes from 01 command\n", rx_len);

	return 0;
}

static struct rfid_reader_handle *
cm5121_open(void *data)
{
	struct rfid_reader_handle *rh;
	struct rfid_asic_transport_handle *rath;

	rh = malloc(sizeof(*rh));
	if (!rh)
		return NULL;
	memset(rh, 0, sizeof(*rh));

	rath = malloc(sizeof(*rath));
	if (!rath)
		goto out_rh;
	memset(rath, 0, sizeof(*rath));

	rath->rat = &cm5121_ccid;
	rh->reader = &rfid_reader_cm5121;

	if (cm5121_source_init(rath) < 0)
		goto out_rath;

	if (cm5121_enable_rc632(rath) < 0)
		goto out_rath;

	rh->ah = rc632_open(rath);
	if (!rh->ah) 
		goto out_rath;

	DEBUGP("returning %p\n", rh);
	return rh;

out_rath:
	free(rath);
out_rh:
	free(rh);

	return NULL;
}

static void
cm5121_close(struct rfid_reader_handle *rh)
{
	struct rfid_asic_transport_handle *rath = rh->ah->rath;
	rc632_close(rh->ah);
	free(rath);
	free(rh);
}

struct rfid_reader rfid_reader_cm5121 = {
	.name 	= "Omnikey CardMan 5121 RFID",
	.open = &cm5121_open,
	.close = &cm5121_close,
	.transcieve = &cm5121_transcieve,
	.iso14443a = {
		.init = &cm5121_14443a_init,
		.transcieve_sf = &cm5121_transcieve_sf,
		.transcieve_acf = &cm5121_transcieve_acf,
		.speed = RFID_14443A_SPEED_106K | RFID_14443A_SPEED_212K |
			 RFID_14443A_SPEED_424K | RFID_14443A_SPEED_848K,
		.set_speed = &cm5121_14443a_set_speed,
	},
	.iso14443b = {
		.init = &cm5121_14443b_init,
	},
	.mifare_classic = {
		.setkey = &cm5121_mifare_setkey,
		.auth = &cm5121_mifare_auth,
	},
};

