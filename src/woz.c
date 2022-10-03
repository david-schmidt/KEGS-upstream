const char rcsid_woz_c[] = "@(#)$KmKId: woz.c,v 1.8 2021-06-30 02:03:59+00 kentd Exp $";

/************************************************************************/
/*			KEGS: Apple //gs Emulator			*/
/*			Copyright 2002-2021 by Kent Dickey		*/
/*									*/
/*	This code is covered by the GNU GPL v3				*/
/*	See the file COPYING.txt or https://www.gnu.org/licenses/	*/
/*	This program is provided with no warranty			*/
/*									*/
/*	The KEGS web page is kegs.sourceforge.net			*/
/*	You may contact the author at: kadickey@alumni.princeton.edu	*/
/************************************************************************/

// Based on WOZ 2.0/1.0 spec at https://applesaucefdc.com/woz/reference2/

#include "defc.h"

extern const char g_kegs_version_str[];

byte g_woz_hdr_bytes[8] = { 0x57, 0x4f, 0x5a, 0x32,		// "WOZ2"
				0xff, 0x0a, 0x0d, 0x0a };

void
woz_parse_meta(Disk *dsk, byte *bptr, int size)
{
	Woz_info *wozinfo_ptr;
	int	c;
	int	i;

	wozinfo_ptr = dsk->wozinfo_ptr;
	if(wozinfo_ptr->meta_bptr) {
		printf("Bad WOZ file, 2 META chunks\n");
		wozinfo_ptr->woz_size = 0;
		return;
	}
	wozinfo_ptr->meta_bptr = bptr;
	wozinfo_ptr->meta_size = size;
	printf("META field, %d bytes:\n", size);
	for(i = 0; i < size; i++) {
		c = bptr[i];
		if(c == 0) {
			break;
		}
		putchar(c);
	}
	putchar('\n');
}

void
woz_parse_info(Disk *dsk, byte *bptr, int size)
{
	Woz_info *wozinfo_ptr;
	byte	*new_str;
	int	info_version, disk_type, write_protect, synchronized;
	int	cleaned, ram, largest_track;

	wozinfo_ptr = dsk->wozinfo_ptr;
	if(wozinfo_ptr->info_bptr) {
		printf("Two INFO chunks, bad WOZ file\n");
		wozinfo_ptr->woz_size = 0;
		return;
	}
	wozinfo_ptr->info_bptr = bptr;
	if(size < 60) {
		printf("INFO field is %d, too short\n", size);
		wozinfo_ptr->woz_size = 0;
		return;
	}
	info_version = bptr[0];		// Only "1" or "2" is supported
	disk_type = bptr[1];		// 1==5.25", 2=3.5"
	write_protect = bptr[2];	// 1==write protected
	synchronized = bptr[3];		// 1==cross track sync during imaging
	cleaned = bptr[4];		// 1==MC3470 fake bits have been removed
	new_str = malloc(32 + 1);
	memcpy(new_str, &(bptr[5]), 32);
	new_str[32] = 0;		// Null terminate
	printf("INFO, %d bytes.  info_version:%d, disk_type:%d, wp:%d, sync:"
		"%d, cleaned:%d\n", size, info_version, disk_type,
		write_protect, synchronized, cleaned);
	printf("Creator: %s\n", new_str);
	free(new_str);
	if(info_version >= 2) {
		ram = bptr[42] + (bptr[43] << 8);
		largest_track = (bptr[44] + (bptr[45] << 8)) * 512;
		printf("Disk sides:%d, boot_format:%d bit_timing:%d, hw:"
			"%02x%02x, ram:%d, largest_track:0x%07x\n", bptr[37],
			bptr[38], bptr[39], bptr[41], bptr[40], ram,
			largest_track);
	}

	if(write_protect) {
		printf("Write protected\n");
		dsk->write_prot = 1;
		dsk->write_through_to_unix = 0;
	}
}

void
woz_parse_tmap(Disk *dsk, byte *bptr, int size)
{
	Woz_info *wozinfo_ptr;
	int	i;

	wozinfo_ptr = dsk->wozinfo_ptr;
	if(wozinfo_ptr->tmap_bptr) {
		printf("Second TMAP chunk, bad WOZ file!\n");
		wozinfo_ptr->woz_size = 0;
		return;
	}
	wozinfo_ptr->tmap_bptr = bptr;
	printf("TMAP field, %d bytes\n", size);
	for(i = 0; i < 36; i++) {
		printf("Track %2d.00: %02x, %2d.25:%02x %2d.50:%02x %2d.75:"
			"%02x\n", i, bptr[0], i, bptr[1],
			i, bptr[2], i, bptr[3]);
		bptr += 4;
	}
	size -= (36*4);
	for(i = 0; i < size; i++) {
		if(bptr[i] != 0xff) {
			printf("TMAP entry at offset +0x%03x is %02x\n",
					36*4 + i, bptr[i]);
		}
	}
}

void
woz_parse_trks(Disk *dsk, byte *bptr, int size)
{
	Woz_info *wozinfo_ptr;

	printf("TRKS field, %d bytes\n", size);
	wozinfo_ptr = dsk->wozinfo_ptr;
	if(wozinfo_ptr->trks_bptr) {
		printf("Second TRKS chunk, illegal Woz file\n");
		wozinfo_ptr->woz_size = 0;
		return;
	}
	wozinfo_ptr->trks_bptr = bptr;
	wozinfo_ptr->trks_size = size;
}

int
woz_add_track(Disk *dsk, int qtr_track, word32 tmap, double dcycs)
{
	Woz_info *wozinfo_ptr;
	Trk	*trk;
	byte	*trks_bptr, *raw_bptr, *sync_ptr, *bptr;
	word32	raw_bytes, num_bytes, trks_size, len_bits, offset, num_blocks;
	word32	block;
	int	i;

	wozinfo_ptr = dsk->wozinfo_ptr;
	trks_bptr = wozinfo_ptr->trks_bptr;
	trks_size = wozinfo_ptr->trks_size;
	if(wozinfo_ptr->version == 1) {
		offset = tmap * 6656;
		if((offset + 6656) > trks_size) {
			printf("Trk %d is out of range!\n", tmap);
			return 0;
		}

		bptr = trks_bptr + offset;
		len_bits = bptr[6648] | (bptr[6649] << 8);
		if(len_bits > (6656*8)) {
			printf("Trk bits: %d too big\n", len_bits);
			return 0;
		}
	} else {
		bptr = &(trks_bptr[tmap * 8]);
		// This is a TRK 8-byte structure
		block = cfg_get_le16(&bptr[0]);		// Starting Block
		num_blocks = cfg_get_le16(&bptr[2]);	// Block Count
		len_bits = cfg_get_le32(&bptr[4]);	// Bits Count
#if 0
		printf("qtr_track:%02x, block:%04x, num_blocks:%04x, "
			"len_bits:%06x\n", qtr_track, block, num_blocks,
			len_bits);
		printf("File offset: %05lx\n", bptr - wozinfo_ptr->wozptr);
#endif

		if(block < 3) {
			return 0;
		}
		offset = (block - 3) * 512;		// Offset from +1536
		offset = offset + 1536 - 256;		// Offset from trks_bptr
		if((offset + (num_blocks * 512)) > trks_size) {
			printf("Trk %d is out of range!\n", tmap);
			return 0;
		}
		bptr = &(trks_bptr[offset]);
#if 0
		printf("Qtr_track %03x offset:%06x, bptr:%p, trks_bptr:%p\n",
			qtr_track, offset, bptr, trks_bptr);
		printf(" len_bits:%d %06x bptr-wozptr: %07lx\n", len_bits,
				len_bits, bptr - wozinfo_ptr->wozptr);
#endif
		if(len_bits > (num_blocks * 512 * 8)) {
			printf("Trk bits: %d too big\n", len_bits);
			return 0;
		}
	}

	raw_bytes = (len_bits + 7) >> 3;
	num_bytes = 2 + raw_bytes + 4;
	trk = &(dsk->trks[qtr_track]);
	trk->track_qbits = len_bits * 4;
	trk->dunix_pos = 256 + offset;
	trk->unix_len = raw_bytes;
	trk->raw_bptr = (byte *)malloc(num_bytes);
	trk->sync_ptr = (byte *)malloc(num_bytes);

	iwm_move_to_track(dsk, qtr_track);
	raw_bptr = &(trk->raw_bptr[2]);
	sync_ptr = &(trk->sync_ptr[2]);
	for(i = 0; i < (int)raw_bytes; i++) {
		raw_bptr[i] = bptr[i];
		sync_ptr[i] = 0xff;
	}

	iwm_recalc_sync(dsk, 0, dcycs);

	if(qtr_track == 0) {
		printf("Track 0 data begins: %02x %02x %02x, offset:%d\n",
			bptr[0], bptr[1], bptr[2], offset);
	}

	return 1;
}

int
woz_parse_header(Disk *dsk, byte *wozptr, word32 woz_size, double dcycs)
{
	Woz_info *wozinfo_ptr;
	byte	*bptr, *tmap_bptr;
	word32	chunk_id, size, tmap;
	int	pos, version, ret, num_tracks;
	int	i;

	wozinfo_ptr = dsk->wozinfo_ptr;
	version = 2;
	if(woz_size < 8) {
		return 0;
	}
	for(i = 0; i < 8; i++) {
		if(wozptr[i] != g_woz_hdr_bytes[i]) {
			if(i == 3) {		// Check for WOZ1
				if(wozptr[i] == 0x31) {		// WOZ1
					version = 1;
					continue;
				}
			}
			printf("WOZ header[%d]=%02x, invalid\n", i, wozptr[i]);
			return 0;
		}
	}
	wozinfo_ptr->version = version;

	printf("WOZ version: %d\n", version);
	pos = 12;
	while(pos < (int)woz_size) {
		bptr = &(wozptr[pos]);
		chunk_id = bptr[0] | (bptr[1] << 8) | (bptr[2] << 16) |
				(bptr[3] << 24);
		size = bptr[4] | (bptr[5] << 8) | (bptr[6] << 16) |
							(bptr[7] << 24);
		pos += 8;
		printf("chunk_id: %08x, size:%08x\n", chunk_id, size);
		if(((pos + size) > woz_size) || (size < 8) ||
							((size >> 30) != 0)) {
			return 0;
		}
		bptr = &(wozptr[pos]);
		if(chunk_id == 0x4f464e49) {		// "INFO"
			woz_parse_info(dsk, bptr, size);
		} else if(chunk_id == 0x50414d54) {	// "TMAP"
			woz_parse_tmap(dsk, bptr, size);
		} else if(chunk_id == 0x534b5254) {	// "TRKS"
			woz_parse_trks(dsk, bptr, size);
		} else if(chunk_id == 0x4154454d) {	// "META"
			woz_parse_meta(dsk, bptr, size);
		} else {
			printf("Chunk header %08x is unknown\n", chunk_id);
		}
		pos += size;
	}

	if(!wozinfo_ptr->tmap_bptr) {
		printf("No TMAP found\n");
		return 0;
	}
	if(!wozinfo_ptr->trks_bptr) {
		printf("No TRKS found\n");
		return 0;
	}
	if(!wozinfo_ptr->info_bptr) {
		printf("No INFO found\n");
		return 0;
	}
	if(wozinfo_ptr->woz_size == 0) {
		return 0;
	}

	tmap_bptr = wozinfo_ptr->tmap_bptr;
	dsk->cur_qbit_pos = 0;
	num_tracks = 4*35;
	if(!dsk->disk_525) {
		num_tracks = 160;
	}
	disk_set_num_tracks(dsk, num_tracks);
	for(i = 0; i < 160; i++) {
		if(dsk->disk_525) {
			if(i & 1) {		// qtr-track
				continue;	// Not supported yet
			}
		}
		tmap = tmap_bptr[i];
		if(tmap >= 0xff) {
			continue;		// Skip
		}
		ret = woz_add_track(dsk, i, tmap, dcycs);
		if(ret == 0) {
			return ret;
		}
	}

	return 1;		// WOZ file is good!
}

int
woz_open(Disk *dsk, double dcycs)
{
	Woz_info *wozinfo_ptr;
	byte	*wozptr;
	word32	woz_size, off;
	int	fd, ret;

	// return 0 for bad WOZ file, 1 for success
	printf("woz_open on file %s, write_prot:%d\n", dsk->name_ptr,
						dsk->write_prot);
	if(dsk->raw_data) {
		wozptr = dsk->raw_data;
		woz_size = dsk->raw_dsize;
		dsk->write_prot = 1;
		dsk->write_through_to_unix = 0;
	} else {
		fd = dsk->fd;
		if(fd < 0) {
			return 0;
		}
		woz_size = cfg_get_fd_size(fd);
		printf("size: %d\n", woz_size);

		wozptr = malloc(woz_size);
		off = cfg_read_from_fd(fd, wozptr, 0, woz_size);
		if(off != woz_size) {
			close(fd);
			return 0;
		}
	}

	wozinfo_ptr = malloc(sizeof(Woz_info));
	if(!wozinfo_ptr) {
		fprintf(stderr, "Out of memory\n");
		exit(1);
	}

	dsk->wozinfo_ptr = wozinfo_ptr;

	wozinfo_ptr->wozptr = wozptr;
	wozinfo_ptr->woz_size = woz_size;
	wozinfo_ptr->version = 0;
	wozinfo_ptr->bad = 0;
	wozinfo_ptr->tmap_bptr = 0;
	wozinfo_ptr->trks_bptr = 0;
	wozinfo_ptr->info_bptr = 0;
	wozinfo_ptr->meta_bptr = 0;
	wozinfo_ptr->meta_size = 0;

	ret = woz_parse_header(dsk, wozptr, woz_size, dcycs);
	printf("woz_parse_header ret:%d, write_prot:%d\n", ret,
							dsk->write_prot);
	return ret;
}

int
woz_new(int fd, const char *str, int size_kb)
{
	byte	buf[0x600];
	word32	size, type;
	int	c;
	int	i;

	// Write to fd (set to byte 0 of the file) to write new WOZ file
	memset(&buf[0], 0, 0x600);
	memcpy(&buf[0], &g_woz_hdr_bytes[0], 8);	// "WOZ2", other stuff

	// INFO
	cfg_set_le32(&buf[12], 0x4f464e49);		// "INFO"
	buf[16] = 60;					// INFO length
	buf[20] = 2;					// WOZ2
	type = 1;					// 5.25"
	if(size_kb > 140) {
		type = 2;				// 3.5"
	}
	buf[21] = type;
	for(i = 0; i < 32; i++) {
		buf[25 + i] = ' ';			// Creator field
	}
	memcpy(&buf[25], "KEGS", 4);
	for(i = 0; i < 20; i++) {
		c = g_kegs_version_str[i];
		if(c == 0) {
			break;
		}
		buf[25 + 5 + i] = c;
	}
	buf[57] = type;					// sides, re-use type
	buf[59] = 32;

	// TMAP
	cfg_set_le32(&buf[80], 0x50414d54);		// TMAP
	buf[84] = 160;
	for(i = 0; i < 160; i++) {
		buf[88 + i] = 0xff;
	}

	// TRKS
	cfg_set_le32(&buf[248], 0x534b5254);		// TRKS
	cfg_set_le32(&buf[252], 1280);			// Room for array only

	size = must_write(fd, &buf[0], 0x600);
	if(size != 0x600) {
		return -1;
	}
	if(str) {
		// Avoid unused var warning
	}

	return 0;
}
