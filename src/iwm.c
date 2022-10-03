const char rcsid_iwm_c[] = "@(#)$KmKId: iwm.c,v 1.160 2021-08-12 04:03:08+00 kentd Exp $";

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

// Information from Beneath Apple DOS, Apple IIgs HW reference, Apple IIgs
//  Firmware reference, and Inside Macintosh Vol 3 (for status35 info).
// Neil Parker wrote about the Apple 3.5 drive using IWM at
//  http://nparker.llx.com/a2/disk and it is very useful.
//  http://nparker.llx.com/a2/sethook lists hooks for IWM routines, from
//   $e1/0c00 - 0fab.
// ff/5fa4 is STAT35.  ff/5fae is CONT35
// When testing DOS3.3, set bp at $b942, which is bad sector detected
// IWM mode register: 7:reserved; 6:5: 0; 4: 1=8MHz,0=7MHz (should always be 0);
//	3: bit-cells are 2usec, 2: 1sec timer off; 1: async handshake (writes)
//	0: latch mode enabled for reads (holds data for 8 bit times)
// dsk->fd >= 0 indicates a valid disk.  dsk->raw_data != 0 indicates this is
//  a compressed disk mounted read-only, and ignore dsk->fd (which will always
//  be 0, to indicate a valid disk).

#include "defc.h"

int g_halt_arm_move = 0;

extern int Verbose;
extern int g_vbl_count;
extern int g_c036_val_speed;
extern int g_config_kegs_update_needed;
extern Engine_reg engine;

const byte phys_to_dos_sec[] = {
	0x00, 0x07, 0x0e, 0x06,  0x0d, 0x05, 0x0c, 0x04,
	0x0b, 0x03, 0x0a, 0x02,  0x09, 0x01, 0x08, 0x0f
};

const byte phys_to_prodos_sec[] = {
	0x00, 0x08, 0x01, 0x09,  0x02, 0x0a, 0x03, 0x0b,
	0x04, 0x0c, 0x05, 0x0d,  0x06, 0x0e, 0x07, 0x0f
};


const byte to_disk_byte[] = {
	0x96, 0x97, 0x9a, 0x9b,  0x9d, 0x9e, 0x9f, 0xa6,
	0xa7, 0xab, 0xac, 0xad,  0xae, 0xaf, 0xb2, 0xb3,
/* 0x10 */
	0xb4, 0xb5, 0xb6, 0xb7,  0xb9, 0xba, 0xbb, 0xbc,
	0xbd, 0xbe, 0xbf, 0xcb,  0xcd, 0xce, 0xcf, 0xd3,
/* 0x20 */
	0xd6, 0xd7, 0xd9, 0xda,  0xdb, 0xdc, 0xdd, 0xde,
	0xdf, 0xe5, 0xe6, 0xe7,  0xe9, 0xea, 0xeb, 0xec,
/* 0x30 */
	0xed, 0xee, 0xef, 0xf2,  0xf3, 0xf4, 0xf5, 0xf6,
	0xf7, 0xf9, 0xfa, 0xfb,  0xfc, 0xfd, 0xfe, 0xff
};

int	g_track_bytes_35[] = {
	0x200*12,
	0x200*11,
	0x200*10,
	0x200*9,
	0x200*8
};

int	g_track_nibs_35[] = {
	816*12,
	816*11,
	816*10,
	816*9,
	816*8
};

int	g_fast_disk_emul_en = 1;
int	g_fast_disk_emul = 0;
int	g_slow_525_emul_wr = 0;
double	g_dcycs_end_emul_wr = 0.0;
int	g_fast_disk_unnib = 0;
int	g_iwm_fake_fast = 0;

word32	g_from_disk_byte[256];
int	g_from_disk_byte_valid = 0;

Iwm	g_iwm;

extern int g_c031_disk35;

int	g_iwm_motor_on = 0;
// g_iwm_motor_on is set when drive turned on, 0 when $c0e8 is touched.
//  Use this to throttle speed to 1MHz.
// g_iwm.motor_on=1 means drive is spinning.  g_iwm.motor_off=1 means we're
//  in the 1-second countdown of g_iwm.motor_off_vbl_count.

int	g_check_nibblization = 0;

void
iwm_init_drive(Disk *dsk, int smartport, int drive, int disk_525)
{
	int	num_tracks;
	int	i;

	dsk->dcycs_last_read = 0.0;
	dsk->raw_data = 0;
	dsk->wozinfo_ptr = 0;
	dsk->dynapro_info_ptr = 0;
	dsk->name_ptr = 0;
	dsk->partition_name = 0;
	dsk->partition_num = -1;
	dsk->fd = -1;
	dsk->dynapro_size = 0;
	dsk->raw_dsize = 0;
	dsk->dimage_start = 0;
	dsk->dimage_size = 0;
	dsk->smartport = smartport;
	dsk->disk_525 = disk_525;
	dsk->drive = drive;
	dsk->cur_qtr_track = 0;
	dsk->image_type = 0;
	dsk->vol_num = 254;
	dsk->write_prot = 1;
	dsk->write_through_to_unix = 0;
	dsk->disk_dirty = 0;
	dsk->just_ejected = 0;
	dsk->last_phase = 0;
	dsk->cur_qbit_pos = 0;
	dsk->cur_track_qbits = 0;
	dsk->num_tracks = 0;
	dsk->trks = 0;
	if(!smartport) {
		// 3.5" and 5.25" drives.  This is only called at init time,
		//  so one-time malloc can be done now
		num_tracks = 2*80;		// 3.5" needs 160
		dsk->trks = (Trk *)malloc(num_tracks * sizeof(Trk));

		for(i = 0; i < num_tracks; i++) {
			dsk->trks[i].raw_bptr = 0;
			dsk->trks[i].sync_ptr = 0;
			dsk->trks[i].track_qbits = 0;
			dsk->trks[i].dunix_pos = 0;
			dsk->trks[i].unix_len = 0;
		}
		// num_tracks != 0 indicates a valid disk, do not set it here
	}
}

void
disk_set_num_tracks(Disk *dsk, int num_tracks)
{
	if(num_tracks <= 2*80) {
		dsk->num_tracks = num_tracks;
	} else {
		printf("num_tracks out of range: %d\n", num_tracks);
	}
}

void
iwm_init()
{
	word32	val;
	int	i;

	for(i = 0; i < 2; i++) {
		iwm_init_drive(&(g_iwm.drive525[i]), 0, i, 1);
		iwm_init_drive(&(g_iwm.drive35[i]), 0, i, 0);
	}

	for(i = 0; i < MAX_C7_DISKS; i++) {
		iwm_init_drive(&(g_iwm.smartport[i]), 1, i, 0);
	}

	if(g_from_disk_byte_valid == 0) {
		for(i = 0; i < 256; i++) {
			g_from_disk_byte[i] = 0x100 + i;
		}
		for(i = 0; i < 64; i++) {
			val = to_disk_byte[i];
			g_from_disk_byte[val] = i;
		}
		g_from_disk_byte_valid = 1;
	} else {
		halt_printf("iwm_init called twice!\n");
	}

	iwm_reset();
}

void
iwm_reset()
{
	g_iwm.q6 = 0;
	g_iwm.q7 = 0;
	g_iwm.motor_on = 0;
	g_iwm.motor_on35 = 0;
	g_iwm.motor_off = 0;
	g_iwm.motor_off_vbl_count = 0;
	g_iwm.step_direction35 = 0;
	g_iwm.head35 = 0;
	g_iwm.last_sel35 = 0;
	g_iwm.drive_select = 0;
	g_iwm.iwm_mode = 0;
	g_iwm.enable2 = 0;
	g_iwm.reset = 0;
	g_iwm.iwm_phase[0] = 0;
	g_iwm.iwm_phase[1] = 0;
	g_iwm.iwm_phase[2] = 0;
	g_iwm.iwm_phase[3] = 0;
	g_iwm.write_val = 0;
	g_iwm.qbit_wr_start = 32*12345;
	g_iwm.qbit_wr_last = 0;
	g_iwm.forced_sync_qbit = 0;

	g_iwm_motor_on = 0;
	g_c031_disk35 = 0;
}

void
draw_iwm_status(int line, char *buf)
{
	char	*flag[2][2];
	int	apple35_sel;

	flag[0][0] = " ";
	flag[0][1] = " ";
	flag[1][0] = " ";
	flag[1][1] = " ";

	apple35_sel = (g_c031_disk35 >> 6) & 1;
	if(g_iwm.motor_on) {
		flag[apple35_sel][g_iwm.drive_select] = "*";
	}

	sprintf(buf, "s6d1:%2d%s   s6d2:%2d%s   s5d1:%2d/%d%s   "
		"s5d2:%2d/%d%s fast_disk_emul:%d,%d c036:%02x",
		g_iwm.drive525[0].cur_qtr_track >> 2, flag[0][0],
		g_iwm.drive525[1].cur_qtr_track >> 2, flag[0][1],
		g_iwm.drive35[0].cur_qtr_track >> 1,
		g_iwm.drive35[0].cur_qtr_track & 1, flag[1][0],
		g_iwm.drive35[1].cur_qtr_track >> 1,
		g_iwm.drive35[1].cur_qtr_track & 1, flag[1][1],
		g_fast_disk_emul, g_slow_525_emul_wr, g_c036_val_speed);

	video_update_status_line(line, buf);
}

void
iwm_flush_cur_disk()
{
	Disk	*dsk;
	int	drive;

	drive = g_iwm.drive_select;
	if(g_c031_disk35 & 0x40) {
		dsk = &(g_iwm.drive35[drive]);
	} else {
		dsk = &(g_iwm.drive525[drive]);
	}
	iwm_flush_disk_to_unix(dsk);
}

void
iwm_flush_disk_to_unix(Disk *dsk)
{
	byte	buffer[0x4000];
	int	ret;

	if((dsk->disk_dirty == 0) || (dsk->write_through_to_unix == 0)) {
		return;
	}
	if((dsk->raw_data != 0) && !dsk->dynapro_info_ptr) {
		return;
	}

	printf("Writing disk %s to Unix\n", dsk->name_ptr);
	dsk->disk_dirty = 0;

	ret = disk_track_to_unix(dsk, &(buffer[0]));

	if((ret != 1) && (ret != 0)) {
		printf("iwm_flush_disk_to_unix ret: %d, cannot write "
				"image to unix\n", ret);
		halt_printf("Adjusting image not to write through!\n");
		dsk->write_through_to_unix = 0;
		return;
	}

}

/* Check for dirty disk 3 times a second */

int	g_iwm_dynapro_last_vbl_count = 0;

void
iwm_vbl_update()
{
	int	i;

	if(g_iwm.motor_on && g_iwm.motor_off) {
		if(g_iwm.motor_off_vbl_count <= g_vbl_count) {
			printf("Disk timer expired, drive off: %08x\n",
				g_vbl_count);
			g_iwm.motor_on = 0;
			g_iwm.motor_off = 0;
		}
	}
	if((g_vbl_count - g_iwm_dynapro_last_vbl_count) >= 60) {
		for(i = 0; i < 2; i++) {
			dynapro_try_fix_damaged_disk(&(g_iwm.drive525[i]));
			dynapro_try_fix_damaged_disk(&(g_iwm.drive35[i]));
		}
		for(i = 0; i < MAX_C7_DISKS; i++) {
			dynapro_try_fix_damaged_disk(&(g_iwm.smartport[i]));
		}
		g_iwm_dynapro_last_vbl_count = g_vbl_count;
	}
}

void
iwm_update_fast_disk_emul(int fast_disk_emul_en)
{
	if(g_iwm_motor_on == 0) {
		g_fast_disk_emul = fast_disk_emul_en;
	}
}

void
iwm_show_stats()
{
	dbg_printf("IWM stats: q7,q6: %d, %d, reset,en2:%d,%d, mode:%02x\n",
		g_iwm.q7, g_iwm.q6, g_iwm.reset, g_iwm.enable2, g_iwm.iwm_mode);
	dbg_printf("motor: %d,%d, motor35:%d drive: %d, c031:%02x "
		"phs: %d %d %d %d\n",
		g_iwm.motor_on, g_iwm.motor_off, g_iwm_motor_on,
		g_iwm.drive_select, g_c031_disk35,
		g_iwm.iwm_phase[0], g_iwm.iwm_phase[1], g_iwm.iwm_phase[2],
		g_iwm.iwm_phase[3]);
	dbg_printf("g_iwm.drive525[0].fd: %d, [1].fd: %d\n",
		g_iwm.drive525[0].fd, g_iwm.drive525[1].fd);
	dbg_printf("g_iwm.drive525[0].last_phase: %d, [1].last_phase: %d\n",
		g_iwm.drive525[0].last_phase, g_iwm.drive525[1].last_phase);
	dbg_printf("g_iwm.write_val:%02x, qbit_wr_start:%06x, qbit_wr_last:"
		"%06x, forced_sync_qbit:%06x\n", g_iwm.write_val,
		g_iwm.qbit_wr_start, g_iwm.qbit_wr_last,
		g_iwm.forced_sync_qbit);
}

Disk *
iwm_get_dsk(int drive)
{
	Disk	*dsk;

	if(g_c031_disk35 & 0x40) {
		dsk = &(g_iwm.drive35[drive]);
	} else {
		dsk = &(g_iwm.drive525[drive]);
	}

	return dsk;
}

Disk *
iwm_touch_switches(int loc, double dcycs)
{
	Disk	*dsk;
	word32	track_qbits, qbit_pos;
	int	phase, on, drive, cycs_passed;

	if(g_iwm.reset) {
		iwm_printf("IWM under reset: %d, enable2: %d\n", g_iwm.reset,
			g_iwm.enable2);
	}

	drive = g_iwm.drive_select;
	dsk = iwm_get_dsk(drive);

	track_qbits = dsk->cur_track_qbits;
	qbit_pos = track_qbits * 4;
	if(g_iwm.motor_on) {
		cycs_passed = (long)(dcycs - dsk->dcycs_last_read);
		dsk->dcycs_last_read = dcycs;

		if(g_iwm.iwm_mode & 8) {		// 2-usec bit times
			cycs_passed = cycs_passed << 1;
		}
		// track_qbits can be 0, indicating no valid data
		if(track_qbits) {
			if(cycs_passed >= (int)track_qbits) {
				cycs_passed = cycs_passed % track_qbits;
			}
		}
		qbit_pos = dsk->cur_qbit_pos + cycs_passed;
		if(qbit_pos >= track_qbits) {
			qbit_pos -= track_qbits;
		}
		if(g_slow_525_emul_wr || g_iwm.q7 || !g_fast_disk_emul ||
								!track_qbits) {
			dsk->cur_qbit_pos = qbit_pos;
		} else {
			qbit_pos = 4*track_qbits;
		}
	}

	dbg_log_info(dcycs, dsk->cur_qbit_pos * 8,
		((loc & 0xff) << 24) | (drive << 23) |
		(g_iwm.motor_on << 22) | (g_iwm.motor_off << 21) |
		(g_iwm_motor_on << 20) | ((dsk->cur_qtr_track & 0x1ff) << 8) |
		(g_c031_disk35 & 0xc0) |
		(g_iwm.q7 << 1) | g_iwm.q6, 0xe0);		// t:e0

	if(loc < 0) {
		// Not a real access, just a way to update qbit_pos
		return dsk;
	}

	on = loc & 1;
	phase = loc >> 1;
	if(loc < 8) {
		/* phase adjustments.  See if motor is on */

		g_iwm.iwm_phase[phase] = on;
		iwm_printf("Iwm phase %d=%d, all phases: %d %d %d %d (%f)\n",
			phase, on, g_iwm.iwm_phase[0], g_iwm.iwm_phase[1],
			g_iwm.iwm_phase[2], g_iwm.iwm_phase[3], dcycs);

		if(g_iwm.motor_on) {
			if(g_c031_disk35 & 0x40) {
				if(phase == 3 && on) {
					iwm_do_action35(dcycs);
				}
			} else if(on) {
				/* Move apple525 head */
				iwm525_phase_change(drive, phase, dcycs);
			}
		}
		/* See if enable or reset is asserted */
		if(g_iwm.iwm_phase[0] && g_iwm.iwm_phase[2]) {
			g_iwm.reset = 1;
			iwm_printf("IWM reset active\n");
		} else {
			g_iwm.reset = 0;
		}
		if(g_iwm.iwm_phase[1] && g_iwm.iwm_phase[3]) {
			g_iwm.enable2 = 1;
			iwm_printf("IWM ENABLE2 active\n");
		} else {
			g_iwm.enable2 = 0;
		}
	} else {
		/* loc >= 8 */
		switch(loc) {
		case 0x8:
			iwm_printf("Turning IWM motor off!\n");
			if(g_iwm.iwm_mode & 0x04) {
				/* Turn off immediately */
				g_iwm.motor_off = 0;
				g_iwm.motor_on = 0;
			} else {
				/* 1 second delay */
				if(g_iwm.motor_on && !g_iwm.motor_off) {
					g_iwm.motor_off = 1;
					g_iwm.motor_off_vbl_count = g_vbl_count
									+ 60;
				}
			}

			if(g_iwm_motor_on || g_slow_525_emul_wr) {
				/* recalc current speed */
				g_fast_disk_emul = g_fast_disk_emul_en;
				engine_recalc_events();
				iwm_flush_cur_disk();
			}

			g_iwm_motor_on = 0;
			g_slow_525_emul_wr = 0;
			break;
		case 0x9:
			iwm_printf("Turning IWM motor on!\n");
			g_iwm.motor_on = 1;
			g_iwm.motor_off = 0;
			g_iwm.last_sel35 = (g_c031_disk35 >> 6) & 1;
			dsk->dcycs_last_read = dcycs;

			if(g_iwm_motor_on == 0) {
				/* recalc current speed */
				if(dsk->wozinfo_ptr) {
					g_fast_disk_emul = 0;
				}
				engine_recalc_events();
			}
			g_iwm_motor_on = 1;

			break;
		case 0xa:
		case 0xb:
			if(g_iwm.drive_select != on) {
				iwm_flush_cur_disk();
			}
			dsk = iwm_get_dsk(on);
			if(dsk->wozinfo_ptr && g_iwm_motor_on) {
				g_fast_disk_emul = 0;
			} else {
				g_fast_disk_emul = g_fast_disk_emul_en;
			}
			g_iwm.drive_select = on;
			break;
		case 0xc:
		case 0xd:
			if(g_iwm.q6 && !on && g_iwm.motor_on &&
							!g_iwm.enable2) {
				// Switched from $c08d to $c08c, resync now
				g_iwm.forced_sync_qbit = qbit_pos;
				dbg_log_info(dcycs, qbit_pos * 8, 0, 0xed);
			}
			g_iwm.q6 = on;
			break;
		case 0xe:
		case 0xf:
			if(g_iwm.q7 && !on && g_iwm.motor_on &&
							!g_iwm.enable2) {
#if 0
				printf("q6:%d q7:%d and new q7:%d, write_end\n",
					g_iwm.q6, g_iwm.q7, on);
#endif
				iwm_write_end(dsk, dcycs);
				// printf("write end complete, the track:\n");
				// iwm_check_nibblization(dcycs);
			}
			g_iwm.q7 = on;
			break;
		default:
			printf("iwm_touch_switches: loc: %02x unknown!\n", loc);
			exit(2);
		}
	}

	if(!g_iwm.q7) {
		g_iwm.qbit_wr_start = 32 * 12345;
		if(g_slow_525_emul_wr) {
			g_slow_525_emul_wr = 0;
			engine_recalc_events();
		}
	}

	return dsk;
}

void
iwm_move_to_track(Disk *dsk, int new_track)
{
	word32	qbit_pos, track_qbits;
	int	disk_525, drive;

	if(dsk->smartport) {
		return;
	}
	disk_525 = dsk->disk_525;
#if 0
	printf("iwm_move_to_track: %03x, num_tracks:%03x (cur:%04x)\n",
		new_track, dsk->num_tracks, dsk->cur_qtr_track);
#endif

	if(new_track < 0) {
		new_track = 0;
	}
	if(new_track >= dsk->num_tracks) {
		if(disk_525) {
			new_track = dsk->num_tracks - 4;
		} else {
			new_track = dsk->num_tracks - 2 + g_iwm.head35;
		}

		if(new_track <= 0) {
			new_track = 0;
		}
	}

	if((dsk->cur_qtr_track != new_track) || (dsk->cur_trk_ptr == 0)) {
		iwm_flush_disk_to_unix(dsk);
		drive = dsk->drive + 1;
		if(1) {
			// Don't do this printf
		} else if(disk_525) {
			printf("s6d%d Track: %d.%02d\n", drive,
				new_track >> 2, 25* (new_track & 3));
		} else {
			printf("s5d%d Track: %d Side: %d\n", drive,
				new_track >> 1, new_track & 1);
		}

		dsk->cur_qtr_track = new_track;
		dsk->cur_trk_ptr = &(dsk->trks[new_track]);
		track_qbits = dsk->cur_trk_ptr->track_qbits;
		dsk->cur_track_qbits = track_qbits;
		qbit_pos = dsk->cur_qbit_pos & -4;
		if(track_qbits) {
			// Moving to a valid track.  Ensure qbit_pos in range
			qbit_pos = qbit_pos % track_qbits;
		}
		dsk->cur_qbit_pos = qbit_pos;
	}
}

void
iwm525_phase_change(int drive, int phase, double dcycs)
{
	Disk	*dsk;
	int	qtr_track, last_phase, phase_up, phase_down, delta;

	phase_up = (phase - 1) & 3;
	phase_down = (phase + 1) & 3;

	dsk = &(g_iwm.drive525[drive]);
	last_phase = dsk->last_phase;

	qtr_track = dsk->cur_qtr_track;

	delta = 0;
	if(last_phase == phase_up) {
		delta = 2;
		last_phase = phase;
	} else if(last_phase == phase_down) {
		delta = -2;
		last_phase = phase;
	}

	if(delta && g_halt_arm_move) {
		halt_printf("Halt on arm move\n");
		g_halt_arm_move = 0;
	}
	qtr_track += delta;
	if(qtr_track < 0) {
		printf("GRIND...GRIND...GRIND\n");
		qtr_track = 0;
		last_phase = 0;
	}
	if(qtr_track > 4*34) {
		printf("Disk arm moved past track 34, moving it back\n");
		qtr_track = 4*34;
		last_phase = 0;
	}

	dbg_log_info(dcycs, (last_phase << 8) | (phase << 4) | (delta & 0xf),
							qtr_track, 0xe1);
	iwm_move_to_track(dsk, qtr_track);

	dsk->last_phase = last_phase;

	iwm_printf("Moving drive to qtr track: %04x (trk:%d.%02d), %d, %d, %d, "
		"%d %d %d %d\n", qtr_track, qtr_track >> 2, 25*(qtr_track & 3),
		phase, delta, last_phase, g_iwm.iwm_phase[0],
		g_iwm.iwm_phase[1], g_iwm.iwm_phase[2], g_iwm.iwm_phase[3]);

#if 0
	/* sanity check stepping algorithm */
	if((qtr_track & 7) == 0) {
		/* check for just access phase 0 */
		if(last_phase != 0) {
			halt_printf("last_phase: %d!\n", last_phase);
		}
	}
#endif
}

int
iwm_read_status35(double dcycs)
{
	Disk	*dsk;
	int	drive, state, tmp;

	drive = g_iwm.drive_select;
	dsk = &(g_iwm.drive35[drive]);

	if(g_iwm.motor_on) {
		/* Read status */
		state = (g_iwm.iwm_phase[1] << 3) + (g_iwm.iwm_phase[0] << 2) +
			((g_c031_disk35 >> 6) & 2) + g_iwm.iwm_phase[2];

		iwm_printf("Iwm status read state: %02x\n", state);
		dbg_log_info(dcycs, 0, state, 0xe2);

		switch(state) {
		case 0x00:	/* step direction */
			return g_iwm.step_direction35;
			break;
		case 0x01:	/* lower head activate */
			/* also return instantaneous data from head */
			if(g_iwm.head35) {
				iwm_move_to_track(dsk,
						(dsk->cur_qtr_track & (-2)));
			}
			g_iwm.head35 = 0;
			return (dsk->cur_qbit_pos >> 6) & 1;
			break;
		case 0x02:	/* disk in place */
			/* 1 = no disk, 0 = disk */
			iwm_printf("read disk in place, num_tracks: %d\n",
				dsk->num_tracks);
			tmp = (dsk->num_tracks <= 0);
			dbg_log_info(dcycs, 0, dsk->num_tracks, 0xe3);
			return tmp;
			break;
		case 0x03:	/* upper head activate */
			/* also return instantaneous data from head */
			if(!g_iwm.head35) {
				iwm_move_to_track(dsk, dsk->cur_qtr_track | 1);
			}
			g_iwm.head35 = 1;
			return (dsk->cur_qbit_pos >> 6) & 1;
			break;
		case 0x04:	/* disk is stepping? */
			/* 1 = not stepping, 0 = stepping */
			return 1;
			break;
		case 0x05:	/* Unknown function of ROM 03? */
			/* 1 = or $20 into 0xe1/f24+drive, 0 = don't */
			return 1;
			break;
		case 0x06:	/* disk is locked */
			/* 0 = locked, 1 = unlocked */
			return (!dsk->write_prot);
			break;
		case 0x08:	/* motor on */
			/* 0 = on, 1 = off */
			return !g_iwm.motor_on35;
			break;
		case 0x09:	/* number of sides */
			/* 1 = 2 sides, 0 = 1 side */
			return 1;
			break;
		case 0x0a:	/* at track 0 */
			/* 1 = not at track 0, 0 = there */
			tmp = (dsk->cur_qtr_track != 0);
			iwm_printf("Read at track0_35: %d\n", tmp);
			return tmp;
			break;
		case 0x0b:	/* disk ready??? */
			/* 0 = ready, 1 = not ready? */
			tmp = !g_iwm.motor_on35;
			iwm_printf("Read disk ready, ret: %d\n", tmp);
			return tmp;
			break;
		case 0x0c:	/* disk switched?? */
			/* 0 = not switched, 1 = switched? */
			tmp = (dsk->just_ejected != 0);
			iwm_printf("Read disk switched: %d\n", tmp);
			return tmp;
			break;
		case 0x0d:	/* false read when ejecting disk */
			return 1;
		case 0x0e:	/* tachometer */
			halt_printf("Reading tachometer!\n");
			return (dsk->cur_qbit_pos & 1);
			break;
		case 0x0f:	/* drive installed? */
			/* 0 = drive exists, 1 = no drive */
			if(drive) {
				/* pretend no drive 1 */
				return 1;
			}
			return 0;
			break;
		default:
			halt_printf("Read 3.5 status, state: %02x\n", state);
			return 1;
		}
	} else {
		iwm_printf("Read 3.5 status with drive off!\n");
		return 1;
	}
}

void
iwm_do_action35(double dcycs)
{
	Disk	*dsk;
	int	drive, state;

	drive = g_iwm.drive_select;
	dsk = &(g_iwm.drive35[drive]);

	if(dcycs || 1) {
		state = 0;		// Use dcycs
	}

	if(g_iwm.motor_on) {
		/* Perform action */
		state = (g_iwm.iwm_phase[1] << 3) + (g_iwm.iwm_phase[0] << 2) +
			((g_c031_disk35 >> 6) & 2) + g_iwm.iwm_phase[2];
		switch(state) {
		case 0x00:	/* Set step direction inward */
			/* towards higher tracks */
			g_iwm.step_direction35 = 0;
			iwm_printf("Iwm set step dir35 = 0\n");
			break;
		case 0x01:	/* Set step direction outward */
			/* towards lower tracks */
			g_iwm.step_direction35 = 1;
			iwm_printf("Iwm set step dir35 = 1\n");
			break;
		case 0x03:	/* reset disk-switched flag? */
			iwm_printf("Iwm reset disk switch\n");
			dsk->just_ejected = 0;
			break;
		case 0x04:	/* step disk */
			if(g_iwm.step_direction35) {
				iwm_move_to_track(dsk, dsk->cur_qtr_track - 2);
			} else {
				iwm_move_to_track(dsk, dsk->cur_qtr_track + 2);
			}
			break;
		case 0x08:	/* turn motor on */
			iwm_printf("Iwm set motor_on35 = 1\n");
			g_iwm.motor_on35 = 1;
			break;
		case 0x09:	/* turn motor off */
			iwm_printf("Iwm set motor_on35 = 0\n");
			g_iwm.motor_on35 = 0;
			break;
		case 0x0d:	/* eject disk */
			printf("Action 0x0d, will eject disk\n");
			iwm_eject_disk(dsk);
			break;
		case 0x02:
		case 0x07:
		case 0x0b: /* hacks to allow AE 1.6MB driver to not crash me */
			break;
		default:
			halt_printf("Do 3.5 action, state: %02x\n", state);
			return;
		}
	} else {
		halt_printf("Set 3.5 status with drive off!\n");
		return;
	}
}

int
read_iwm(int loc, double dcycs)
{
	Disk	*dsk;
	word32	status, qbit_diff, track_qbits;
	int	on, state, val;

	loc = loc & 0xf;
	on = loc & 1;

	dsk = iwm_touch_switches(loc, dcycs);

	state = (g_iwm.q7 << 1) + g_iwm.q6;

	if(on) {
		/* odd address, return 0 */
		return 0;
	} else {
		/* even address */
		switch(state) {
		case 0x00:	/* q7 = 0, q6 = 0 */
			if(g_iwm.enable2) {
				return iwm_read_enable2(dcycs);
			} else {
				if(g_iwm.motor_on) {
					return iwm_read_data(dsk, dcycs);
				} else {
					iwm_printf("read iwm st 0, m off!\n");
/* HACK!!!! */
					return 0xff;
					//return (((int)dcycs) & 0x7f) + 0x80;
				}
			}
			break;
		case 0x01:	/* q7 = 0, q6 = 1 */
			/* read IWM status reg */
			if(g_iwm.enable2) {
				iwm_printf("Read status under enable2: 1\n");
				status = 1;
			} else {
				if(g_c031_disk35 & 0x40) {
					status = iwm_read_status35(dcycs);
				} else {
					status = dsk->write_prot;
				}
			}

			val = ((status & 1) << 7) | (g_iwm.motor_on << 5) |
				g_iwm.iwm_mode;
			iwm_printf("Read status: %02x\n", val);

			return val;
			break;
		case 0x02:	/* q7 = 1, q6 = 0 */
			/* read handshake register */
			if(g_iwm.enable2) {
				return iwm_read_enable2_handshake(dcycs);
			} else {
				status = 0xc0;
				qbit_diff = dsk->cur_qbit_pos -
							g_iwm.qbit_wr_last;
				track_qbits = dsk->cur_track_qbits;
				if(qbit_diff >= track_qbits) {
					qbit_diff += track_qbits;
				}
				if(qbit_diff > 32) {
					iwm_printf("Write underrun!\n");
					status = status & 0xbf;
				}
				return status;
			}
			break;
		case 0x03:	/* q7 = 1, q6 = 1 */
			iwm_printf("read iwm state 3!\n");
			return 0;
		break;
		}
	}
	halt_printf("Got to end of read_iwm, loc: %02x!\n", loc);

	return 0;
}

void
write_iwm(int loc, int val, double dcycs)
{
	Disk	*dsk;
	int	on, state, drive, fast_writes;

	loc = loc & 0xf;
	on = loc & 1;

	dsk = iwm_touch_switches(loc, dcycs);

	state = (g_iwm.q7 << 1) + g_iwm.q6;
	drive = g_iwm.drive_select;
	fast_writes = g_fast_disk_emul;
	if(g_c031_disk35 & 0x40) {
		dsk = &(g_iwm.drive35[drive]);
	} else {
		dsk = &(g_iwm.drive525[drive]);
		fast_writes = !g_slow_525_emul_wr && fast_writes;
	}

	if(on) {
		/* odd address, write something */
		if(state == 0x03) {
			/* q7, q6 = 1,1 */
			if(g_iwm.motor_on) {
				if(g_iwm.enable2) {
					iwm_write_enable2(val);
				} else {
					iwm_write_data(dsk, val, dcycs);
				}
			} else {
				/* write mode register */
				// bit 0: latch mode (should set if async hand)
				// bit 1: async handshake
				// bit 2: immediate motor off (no 1 sec delay)
				// bit 3: 2us bit timing
				// bit 4: Divide input clock by 8 (instead of 7)
				val = val & 0x1f;
				g_iwm.iwm_mode = val;
				if(val & 0x10) {
					iwm_printf("set iwm_mode:%02x!\n",val);
				}
			}
		} else {
			if(g_iwm.enable2) {
				iwm_write_enable2(val);
			} else {
#if 0
// Flobynoid writes to 0xc0e9 causing these messages...
				printf("Write iwm1, st: %02x, loc: %x: %02x\n",
					state, loc, val);
#endif
			}
		}
		return;
	} else {
		/* even address */
		if(g_iwm.enable2) {
			iwm_write_enable2(val);
		} else {
			iwm_printf("Write iwm2, st: %02x, loc: %x: %02x\n",
				state, loc, val);
		}
		return;
	}

	return;
}



int
iwm_read_enable2(double dcycs)
{
	iwm_printf("Read under enable2 %f!\n", dcycs);
	return 0xff;
}

int g_cnt_enable2_handshake = 0;

int
iwm_read_enable2_handshake(double dcycs)
{
	int	val;

	iwm_printf("Read handshake under enable2, %f!\n", dcycs);

	val = 0xc0;
	g_cnt_enable2_handshake++;
	if(g_cnt_enable2_handshake > 3) {
		g_cnt_enable2_handshake = 0;
		val = 0x80;
	}

	return val;
}

void
iwm_write_enable2(int val)
{
	// Smartport is selected (PH3=1, PH1=1, Sel35=0), just ignore this data
	iwm_printf("Write under enable2: %02x!\n", val);

	return;
}

word32
iwm_fastemul_start_write(Disk *dsk, double dcycs)
{
	double	dcycs_passed, new_dcycs;
	word32	qbit_pos, qbit_diff, track_qbits;

	// Nox Archaist doesn't finish reading sector header's checksum, but
	//  instead waits for 7 bytes to pass and then writes.  This code
	//  tries to allow fast_disk_emul mode to not overwrite the checksum.
	// Move the qbit_pos forward to try to account for a delay from the
	//  last read to the current write.  But accesses to I/O locations
	//  would still take a lot of time.  Let's assume there were
	//  2 slow cycles, and clamp the skip to a min of 1 byte, max 3 bytes.
	dcycs_passed = dcycs - g_iwm.dcycs_last_fastemul_read;
	new_dcycs = (dcycs_passed - 2) / engine.fplus_ptr->plus_1;
	iwm_printf("start write, dcycs:%f, new_dcycs:%f, plus_1:%f\n",
			dcycs_passed, new_dcycs, engine.fplus_ptr->plus_1);

	if(new_dcycs < 0.0) {
		qbit_diff = 32;
	} else if(new_dcycs > (32.0*4)) {
		qbit_diff = 4*32;
	} else {
		qbit_diff = (long)new_dcycs;		// each cycle is a qbit
		if(qbit_diff < 32) {
			qbit_diff = 32;
		} else if(qbit_diff > (4*32)) {
			qbit_diff = 4*32;
		}
	}
	track_qbits = dsk->cur_track_qbits;
	qbit_pos = dsk->cur_qbit_pos;

	if(track_qbits == 0) {
		return qbit_pos;
	}

	qbit_pos = qbit_pos + qbit_diff;
	if(qbit_pos >= track_qbits) {
		qbit_pos = qbit_pos - track_qbits;
	}
#if 0
	printf(" adjusted qbit_pos*8 from %06x to %06x\n",
					dsk->cur_qbit_pos * 8, qbit_pos * 8);
#endif
	dsk->cur_qbit_pos = qbit_pos;

	return qbit_pos;
}

word32
iwm_read_data_fast(Disk *dsk, double dcycs)
{
	byte	*bptr, *sync_ptr;
	word32	qbit_pos, track_qbits, byte_pos, bit_pos, sync_info, val;
	word32	lsb_bit_pos, shift, skip;

	if(!g_fast_disk_unnib) {
		g_iwm.dcycs_last_fastemul_read = dcycs;
	}

	// qbit_pos points directly at the MSB bit we should be returning.
	qbit_pos = dsk->cur_qbit_pos & -4;
					// quarter-bits (32 qbits==8 bits)
	track_qbits = dsk->cur_track_qbits;
	byte_pos = qbit_pos >> 5;
	bit_pos = (qbit_pos >> 2) & 7;
		// bit pos is counting from the byte MSB which is 0 to the byte
		//  LSB is 7
	lsb_bit_pos = 7 - bit_pos;
#if 0
	printf("irdf: %06x track_qb:%06x, byte_pos:%04x, raw_bptr:%p\n",
		qbit_pos, track_qbits, byte_pos,
		&(dsk->cur_trk_ptr->raw_bptr[0]));
#endif
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2 + byte_pos]);
	sync_info = sync_ptr[0];
		// sync_info is the bit, counting from LSB bit, which is
		//  the MSB of a synced byte, with the value 8 or 9 indicating
		//  it's in the previous disk nibble
	sync_info = sync_info & 0xf;
	bptr = &(dsk->cur_trk_ptr->raw_bptr[2 + byte_pos]);
	val = (bptr[0] << 16) | (bptr[1] << 8) | bptr[2];
	// if lsb_bit_pos==sync_info, then this is the hi-bit of an 8-bit
	//  byte, just return it.
	shift = 9 + lsb_bit_pos;
	if(sync_info == lsb_bit_pos) {
		val = (val >> shift) & 0xff;
		qbit_pos += 32;
		if(qbit_pos >= track_qbits) {
#if 0
			printf("We were synced, qbit_pos*8:%06x will wrap to:"
				"%06x, val:%02x\n", qbit_pos * 8,
				(qbit_pos - track_qbits) * 8, val);
#endif
			qbit_pos -= track_qbits;
		}
		dsk->cur_qbit_pos = qbit_pos;
		return val;
	}

	// Otherwise, we're not aligned (or there was a 10-/9-bit sync nibble)
	if(sync_info < lsb_bit_pos) {
		skip = lsb_bit_pos - sync_info;
		shift = 9 + sync_info;
	} else {
		// It's in the next sync_info
		sync_info = sync_ptr[1];
		sync_info = sync_info & 0xf;
		skip = ((8 + lsb_bit_pos) - sync_info) & 15;
		shift = 1 + sync_info;
		if(skip < 1) {
			skip = 1;
		}
	}
	val = (val >> (shift & 31)) & 0xff;
	qbit_pos += 32 + (skip * 4);
	if(!g_fast_disk_unnib) {
		// Pull a trick to make disk motor-on test pass ($bd34
		//  in RWTS): if this is a sync byte, don't return whole
		//  byte, but fix qbit_pos so it's correct next time
		val = val & 0x7f;
		qbit_pos -= 32;
	}
	if(qbit_pos >= track_qbits) {
		printf("qbit_pos*8 to %06x, wrap to: %06x, val:%02x\n",
			qbit_pos * 8, (qbit_pos - track_qbits) * 8, val);
		qbit_pos -= track_qbits;
	}
	dsk->cur_qbit_pos = qbit_pos;

	return val;
}

word32
iwm_return_rand_data(Disk *dsk, word32 val, double dcycs)
{
	dword64	dval;

	if(dsk) {
		// Use dsk
	}
	dval = (dword64)dcycs;
	dval = (dval >> 8) ^ (dval >> 16);
	return (val | dval) & 0xff;
}

word32
iwm_read_data(Disk *dsk, double dcycs)
{
	byte	*bptr, *sync_ptr;
	word32	track_qbits, ret, qbit_pos, lsb_qbit_pos, lsb_byte_pos, mask;
	word32	lsb_bit_pos, lsb_bit_pos_le, sync0_info, sync0_tmp, sync_0_mask;

	// Return read data from drive.  If !fast_disk_emul, then return
	//  partial data if 32 qbits haven't passed.  qbit_pos points to just
	//  after the most recent bit, so qbit_pos=4 means the first bit of
	//  the track has just been latched.  5.25" holds data once MSB is set
	//  for 7 cycles, which is 7 qbits, which is one bit plus 3 cycles.
	//  By working with (qbit_pos - 4) as the LSb to return, we just allow
	//  3 more cycles by rounding down qbit_pos, and then one full bit by
	//  "reaching" to allow sync==8 to return as valid. If (iwm_mode & 1),
	//  "latch mode", then hold data for 32 qbits (3.5" disks) using
	//  g_iwm.qbit_wr_last to remember last read LSb.

	g_iwm.qbit_wr_start = 32 * 12345;		// Not in a write
	track_qbits = dsk->cur_track_qbits;

	qbit_pos = dsk->cur_qbit_pos;
	if((track_qbits == 0) || (dsk->cur_trk_ptr == 0)) {
		ret = ((qbit_pos * 5) & 0xff);
		iwm_printf("Reading c0ec, track_len 0, returning %02x\n", ret);
		return ret;
	}

	if(g_fast_disk_emul) {
		return iwm_read_data_fast(dsk, dcycs);
	}
	if(g_iwm.iwm_mode & 1) {		// Latch mode (3.5" disk)
		return iwm_read_data_latch(dsk, dcycs);
	}

	lsb_qbit_pos = (qbit_pos - 4) & -4;		// LSB to return
	if(lsb_qbit_pos >= track_qbits) {	// unsigned underflow
		lsb_qbit_pos = lsb_qbit_pos + track_qbits;
	}
	// Calculate the position of the LSb of the last
	//  8 bit byte to pass under the drive head

	lsb_byte_pos = lsb_qbit_pos >> 5;
	lsb_bit_pos = (lsb_qbit_pos >> 2) & 7;
	lsb_bit_pos_le = 7 - lsb_bit_pos;		// Convert to LE form

	bptr = &(dsk->cur_trk_ptr->raw_bptr[2 + lsb_byte_pos]);
	ret = (bptr[-2] << 16) | (bptr[-1] << 8) | bptr[0];
	ret = ret >> lsb_bit_pos_le;		// Shift into position

	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2 + lsb_byte_pos]);
	sync0_tmp = sync_ptr[0];
	sync0_info = sync0_tmp & 0xf;
	sync_0_mask = 0;
	if(sync0_tmp & 0x10) {
		sync_0_mask = ((2 << sync0_info) - 1) >> lsb_bit_pos_le;
	}
	if(sync0_info <= lsb_bit_pos_le) {
		// sync is for the lsb, or after it.  Move to previous byte_pos
		if(sync0_tmp & 0x20) {
			sync_0_mask = 0xfff;
		}
		sync0_tmp = sync_ptr[-1] + 8;
		sync0_info = sync0_tmp & 0xf;
		if(sync0_tmp & 0x10) {
			sync_0_mask |= 0xff;
		}
	}

	mask = 0xff;
	if(g_iwm.forced_sync_qbit < track_qbits) {
		ret = iwm_calc_forced_sync(dsk, lsb_qbit_pos, dcycs);
	} else {
		sync0_info = sync0_info - lsb_bit_pos_le;
		if(sync0_info >= 8) {
			// IWM holds valid data for 7 cycles (7 qbit_pos), and
			//  we rounded down 3 cycles, so if valid MSB was 9
			//  bits ago (sync0_info==8), return it as valid data.
			// IWM also holds data once MSB is set until a new 1
			//  shifts in.  If sync0_info is > 8, then that has
			//  happened so return it
			ret = ret >> (sync0_info - 7);
			sync_0_mask = sync_0_mask >> (sync0_info - 7);
			sync0_info = 7;
		}
		mask = (2 << (sync0_info & 15)) - 1;		// So 7-->0xff
	}

	if(sync_0_mask) {
		ret = iwm_return_rand_data(dsk, ret, dcycs);
		dbg_log_info(dcycs, (sync0_tmp << 24) |
			(qbit_pos * 8), (lsb_byte_pos << 16) |
			((lsb_bit_pos_le & 0xff) << 8) | (ret & 0xff), 0xeb);
	}
	dbg_log_info(dcycs, (sync0_info << 24) | (qbit_pos * 8),
			(lsb_byte_pos << 16) | ((lsb_bit_pos_le & 0xff) << 8) |
						(ret & mask & 0xff), 0xec);
	g_iwm.qbit_wr_last = lsb_qbit_pos & -4;

	return ret & mask;
}

word32
iwm_calc_forced_sync(Disk *dsk, word32 lsb_qbit_pos, double dcycs)
{
	byte	*bptr, *sync_ptr;
	word32	track_qbits, forced_sync_qbit, ret, new_sync_qbit, val, bit;
	word32	bit_val, qbit, sync, eff_sync;
	int	i;

	// Something like the "E7" protection scheme has toggled
	//  $c0ed in the middle of reading a nibble, causing a new sync.  This
	//  is used to "move" sync bits inside disk nibbles, to defeat
	//  nibble copiers (since a 36-cycle sync nibble cannot be
	//  differentiated from a 40-cycle sync nibble in one read pass)
	// Return these misaligned nibbles until we re-sync (then clear
	//  g_wim.forced_sync_qbit.  Toggling $c0ed can set the data latch
	//  to $ff is the disk is write-protected, but this gets cleared
	//  to $00 within 4 CPU cycles always (or less).  A phantom 1 will
	//  also shift in, which this code doesn't try to model
	lsb_qbit_pos = lsb_qbit_pos & -4;
	forced_sync_qbit = g_iwm.forced_sync_qbit & -4;
	track_qbits = dsk->cur_track_qbits;
	ret = 0;
	bptr = &(dsk->cur_trk_ptr->raw_bptr[2]);
	dbg_log_info(dcycs, forced_sync_qbit * 8, (dsk->cur_qbit_pos*8), 0xe4);
	if(forced_sync_qbit == (dsk->cur_qbit_pos & -4)) {
		// While Q6=1, the data latch becomes set (within 4 cycles) to
		//  be the write-protect status in all 8 bits.
		ret = (0xff * dsk->write_prot) & 0xff;	// 0xff if WP, else 0
		return ret;			// Just started, nothing to do
	}

	new_sync_qbit = forced_sync_qbit;
	// After changing Q6 from 1 to 0, after 2-4 cycles, the data latch
	//  is cleared, and 2 cycles later, a 1 bit is always shifted in.
	//  So model it so the bit at forced_sync_qbit is always treated as 1
	for(i = 0; i < 96; i++) {
		if(forced_sync_qbit >= track_qbits) {
			forced_sync_qbit -= track_qbits;
		}
		val = bptr[forced_sync_qbit >> 5];
		bit = (forced_sync_qbit >> 2) & 7;
		bit_val = ((val << bit) >> 7) & 1;
		if(i == 0) {
			bit_val = 1;
		}
		// Model data latch: each new bit simply shifts in--except
		//  if the data latch has the MSB set and the bit to shift in
		//  is a 0, then don't shift it.
		// Also model the data latch holding the valid data for 2 bit
		//  times--allow latch to shift from having MSB set (0x80) to
		//  having bit 9 set (0x100).  If it's set to 0x100, return it
		//  shifted right 1.  If we shift to 0x200, then we've passed
		//  this position, so update forced_sync_bit.
		if(((ret & 0x180) == 0x80) && bit_val) {
			new_sync_qbit = forced_sync_qbit;
			dbg_log_info(dcycs, (new_sync_qbit * 8) + 1,
						(i << 16) | ret, 0xe4);
		}
		if(((ret & 0x180) != 0x80) || bit_val) {
			ret = (ret << 1) | bit_val;
		}
		if(ret & 0x200) {
			// We are two bits past the nibble, form new nibble
			g_iwm.forced_sync_qbit = new_sync_qbit;
			dbg_log_info(dcycs, (new_sync_qbit * 8) + 2,
						(i << 16) | ret, 0xe4);
			ret = ret & 3;
		}
		dbg_log_info(dcycs, (forced_sync_qbit * 8) | (i & 7),
				(lsb_qbit_pos * 8) | ((i >> 3) & 7), 0xe5);
		if(forced_sync_qbit == lsb_qbit_pos) {
			// We have caught up to the current head position, done
			break;
		}
		forced_sync_qbit += 4;
		if(i > 90) {
			// Too many bits to skip over, turn special resync off
			g_iwm.forced_sync_qbit = 4*track_qbits;
			break;
		}
	}

	if(ret & 0x100) {
		ret = ret >> 1;
	}

	// We'll return ret.  Check if current dsk->forced_sync_qbit matches
	//  sync_ptr, and if it does, turn off forced_sync_qbit
	qbit = g_iwm.forced_sync_qbit;
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2]);
	sync = sync_ptr[forced_sync_qbit >> 5];
	eff_sync = 7 - ((qbit >> 2) & 7);
	if(sync == eff_sync) {
		// We are back in sync, turn off forced_sync_qbit
		g_iwm.forced_sync_qbit = track_qbits * 4;
		dbg_log_info(dcycs, (qbit * 8) + 3,
			((sync & 0xff) << 24) | ((eff_sync & 0xff) << 16),
			0xe4);
		dbg_log_info(dcycs, (forced_sync_qbit * 8) + 4, 0, 0xe4);
	}
	dbg_log_info(dcycs, (g_iwm.forced_sync_qbit * 8) + 5,
				(eff_sync << 24) | (sync << 16) | ret, 0xe4);

	return ret;
}

word32
iwm_return_bits_to_msb(Disk *dsk, word32 lsb_qbit_pos)
{
	byte	*sync_ptr;
	word32	track_qbits, lsb_byte_pos, lsb_bit_pos, lsb_bit_pos_le;
	word32	sync_info;

	// Return number of bits from lsb_qbit_pos to the nearest MSB sync.
	//  Return 0 if we point directly to a sync bit.
	track_qbits = dsk->cur_track_qbits;

	if(lsb_qbit_pos >= track_qbits) {	// unsigned underflow
		lsb_qbit_pos = lsb_qbit_pos + track_qbits;
	}
	// Calculate the position of the LSb of the last
	//  8 bit byte to pass under the drive head

	lsb_byte_pos = lsb_qbit_pos >> 5;
	lsb_bit_pos = (lsb_qbit_pos >> 2) & 7;
	lsb_bit_pos_le = 7 - lsb_bit_pos;		// Convert to LE form

	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2 + lsb_byte_pos]);
	sync_info = sync_ptr[0];
	if(sync_info < lsb_bit_pos_le) {
		// sync is for the lsb, or after it.  Move to previous byte_pos
		sync_info = sync_ptr[-1] + 8;
	}

	return (sync_info - lsb_bit_pos_le);
}

word32
iwm_read_data_latch(Disk *dsk, double dcycs)
{
	byte	*bptr;
	word32	track_qbits, ret, qbit_pos, lsb_qbit_pos, lsb_byte_pos, mask;
	word32	lsb_bit_pos, lsb_bit_pos_le, qbit_wr_last, qbit_diff, use_latch;
	word32	bits_to_wr_last, bits_to_msb, bits_to_msb1;

	track_qbits = dsk->cur_track_qbits;

	qbit_pos = dsk->cur_qbit_pos;
	lsb_qbit_pos = (qbit_pos - 4) & -4;		// LSB to return
	if(lsb_qbit_pos >= track_qbits) {	// unsigned underflow
		lsb_qbit_pos = lsb_qbit_pos + track_qbits;
	}
	bits_to_msb = iwm_return_bits_to_msb(dsk, lsb_qbit_pos);
	bits_to_msb1 = iwm_return_bits_to_msb(dsk,
					lsb_qbit_pos - 4*bits_to_msb - 4);
	bits_to_msb1 = bits_to_msb1 + bits_to_msb + 1;
	if(bits_to_msb >= 8) {
		bits_to_msb1 = bits_to_msb;
	}

	use_latch = 0;
	qbit_wr_last = g_iwm.qbit_wr_last;
	bits_to_wr_last = 32;
	if(qbit_wr_last >= track_qbits) {
		use_latch = 1;
	} else {
		qbit_diff = lsb_qbit_pos - qbit_wr_last;
		if(qbit_diff >= track_qbits) {
			qbit_diff += track_qbits;
		}
		bits_to_wr_last = (qbit_diff >> 2);
		if(bits_to_wr_last <= (bits_to_msb1 - 7)) {
			use_latch = 0;
		} else {
			use_latch = 1;
		}
	}
	dbg_log_info(dcycs, ((bits_to_msb & 0xff) << 24) |
			((bits_to_msb1 & 0xff) << 16) |
			((bits_to_wr_last & 0xff) << 8) | use_latch,
			lsb_qbit_pos * 8, 0xe6);

	if(use_latch) {
		bits_to_msb = 7;
		lsb_qbit_pos = lsb_qbit_pos - 4*bits_to_msb1 + 7*4;
	}
	if(lsb_qbit_pos >= track_qbits) {
		lsb_qbit_pos += track_qbits;
	}
	lsb_byte_pos = lsb_qbit_pos >> 5;
	lsb_bit_pos = (lsb_qbit_pos >> 2) & 7;
	lsb_bit_pos_le = 7 - lsb_bit_pos;		// Convert to LE form

	bptr = &(dsk->cur_trk_ptr->raw_bptr[2 + lsb_byte_pos]);
	ret = (bptr[-2] << 16) | (bptr[-1] << 8) | bptr[0];
	ret = ret >> lsb_bit_pos_le;		// Shift into position

	mask = (2 << (bits_to_msb & 15)) - 1;		// So 7-->0xff
	dbg_log_info(dcycs, ((bits_to_msb & 0xff) << 24) | (qbit_pos * 8) | 2,
			(lsb_byte_pos << 16) |
			((lsb_bit_pos_le & 0xff) << 8) | (ret & 0xff), 0xe6);
	g_iwm.qbit_wr_last = lsb_qbit_pos & -4;

	return ret & mask & 0xff;
}

void
iwm_write_data(Disk *dsk, word32 val, double dcycs)
{
	Trk	*trk;
	word32	track_qbits, qbit_pos, qbit_last, tmp_val, qbit_diff;
	int	bits;

	trk = dsk->cur_trk_ptr;
	track_qbits = dsk->cur_track_qbits;
	qbit_pos = dsk->cur_qbit_pos;

	if((track_qbits == 0) || (trk == 0)) {
		return;
	}
#if 0
	printf("iwm_write_data: %02x %f, qbit*8:%06x\n", val, dcycs,
								qbit_pos *8);
#endif
	if(dsk->disk_525) {
		if(!g_slow_525_emul_wr) {
			g_slow_525_emul_wr = 1;
			engine_recalc_events();
			if(g_fast_disk_emul) {
				qbit_pos = iwm_fastemul_start_write(dsk, dcycs);
			}
		}
	}
	qbit_pos = (qbit_pos + 3) & -4;
	dsk->cur_qbit_pos = qbit_pos;
	if(g_iwm.qbit_wr_start >= track_qbits) {
		// No write was pending, enter write mode now
		// printf("Starting write of data to the track, track now:\n");
		// iwm_show_track(-1, -1, dcycs);
		// printf("Write data to track at qbit*8:%06x\n", qbit_pos);
		g_iwm.qbit_wr_start = qbit_pos;
		g_iwm.qbit_wr_last = qbit_pos;
		g_iwm.write_val = val;
		return;
	}
	if(g_iwm.iwm_mode & 2) {		// async handshake mode, 3.5"
		iwm_write_data35(dsk, val, dcycs);
		return;
	}

	// From here on, it's 5.25" disks only
	qbit_last = g_iwm.qbit_wr_last;
	qbit_diff = qbit_pos - qbit_last;
	if(qbit_diff >= track_qbits) {
		qbit_diff += track_qbits;
	}
	bits = qbit_diff >> 2;
	tmp_val = g_iwm.write_val;
	if(bits >= 500) {
		halt_printf("bits are %d. qbit*8:%06x, qbit_last*8:%06x\n",
				bits, qbit_pos * 8, qbit_last * 8);
		bits = 40;
	}
	if(bits <= 8) {
		tmp_val = tmp_val >> (8 - bits);
		disk_nib_out_raw(dsk, tmp_val, bits, qbit_last, dcycs);
	} else {
		qbit_last = disk_nib_out_raw(dsk, tmp_val, 8, qbit_last, dcycs);
		disk_nib_out_zeroes(dsk, bits - 8, qbit_last, dcycs);
	}
	g_iwm.write_val = val;
	g_iwm.qbit_wr_last = qbit_pos;
}

void
iwm_write_data35(Disk *dsk, word32 val, double dcycs)
{
	word32	qbit_pos, qbit_last;

	// Just always write 8 bits to the track, update qbit_wr_last
	qbit_last = g_iwm.qbit_wr_last;
	qbit_pos = disk_nib_out_raw(dsk, g_iwm.write_val, 8, qbit_last, dcycs);
	g_iwm.write_val = val;
	g_iwm.qbit_wr_last = qbit_pos;
	dsk->cur_qbit_pos = qbit_pos;
}

void
iwm_write_end(Disk *dsk, double dcycs)
{
	// Flush out previous write, then turn writing off
	if(g_iwm.qbit_wr_start > dsk->cur_track_qbits) {
		return;			// Invalid, not in a write
	}
	iwm_write_data(dsk, 0, dcycs);

#if 0
	printf("iwm_write_end, calling iwm_recalc_sync with qbit*8:%06x\n",
		g_iwm.qbit_wr_start * 8);
#endif
	iwm_recalc_sync(dsk, g_iwm.qbit_wr_start, dcycs);
	g_iwm.qbit_wr_start = 32*12345;
}

void
iwm_fix_first_last_bytes(Disk *dsk)
{
	byte	*bptr;
	word32	track_bits, bit_end, val, shift, mask;
	int	byte_pos;

	// Fix up first bytes bptr[-2], bptr[-1] to be last 2 bytes (shifted)
	track_bits = dsk->cur_track_qbits >> 2;
	bptr = &(dsk->cur_trk_ptr->raw_bptr[2]);
	byte_pos = (track_bits - 1) >> 3;	// Point to valid data
	bit_end = (track_bits - 1) & 7;		// Point to last valid bit (0-7)
	val = (bptr[byte_pos - 2] << 16) | (bptr[byte_pos - 1] << 8) |
							bptr[byte_pos];
	shift = 7 - bit_end;
	val = val >> shift;
	bptr[-1] = val;
	bptr[-2] = val >> 8;
#if 0
	printf("Set [-2]:%02x [-1]:%02x, shift:%02x\n", bptr[-2], bptr[-1],
								shift);
#endif

	// Copy from [0],[1] to byte_pos+1, byte_pos+2
	val = (bptr[0] << 16) | (bptr[1] << 8) | bptr[2];
	mask = 0xff >> (bit_end + 1);		// If bit_end==7, mask==0
	val = val >> (bit_end + 1);
	bptr[byte_pos + 2] = val;
	bptr[byte_pos + 1] = val >> 8;
	val = val >> 16;
	bptr[byte_pos] = (bptr[byte_pos] & (~mask)) | (val & mask);
#if 0
	printf("byte_pos:%02x, %02x, %02x, mask:%04x, val:%06x, bit_end:%d\n",
		bptr[byte_pos], bptr[byte_pos + 1], bptr[byte_pos + 2], mask,
		val, bit_end);
#endif
}

void
iwm_recalc_sync(Disk *dsk, word32 qbit_pos, double dcycs)
{
	byte	*sync_ptr;
	word32	track_qbits, byte_pos, sync_val;

	track_qbits = dsk->cur_track_qbits;

	// Loop through all newly written bytes (check sync_ptr == 0xff), and
	//  ensure there are not too many 0's.
	byte_pos = qbit_pos >> 5;
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2]);

	iwm_fix_first_last_bytes(dsk);

	// Now, prepare
	qbit_pos -= 4*10;
	if(qbit_pos <= 31) {		// Near the start--just go to the end
		dsk->cur_trk_ptr->sync_ptr[2+0] = 0xff;
		dsk->cur_trk_ptr->sync_ptr[2+1] = 0xff;
		qbit_pos -= 32;		// Move one byte
	}
	if(qbit_pos >= track_qbits) {
		qbit_pos += track_qbits;
	}
#if 0
	printf("iwm_recalc_sync, qbit_pos*8:%06x, track_qbits*8:%06x\n",
					qbit_pos *8, track_qbits * 8);
#endif
	byte_pos = qbit_pos >> 5;
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2 + byte_pos]);
	sync_val = sync_ptr[0];
#if 0
	printf(" byte_pos:%04x, sync_val:%02x, sync[-1]:%02x\n", byte_pos,
						sync_val, sync_ptr[-1]);
#endif
	if((sync_val == 0xff) || (sync_ptr[-1] == 0xff)) {
		iwm_find_any_sync(dsk, dcycs);
		return;
	}
	if(sync_val >= 8) {
		byte_pos--;
		sync_val -= 8;
		if(byte_pos >= 0x5000) {
			halt_printf("iwm_recalc_sync byte_pos:%08x "
				"qbit_pos*8:%06x\n", byte_pos, qbit_pos);
		}
	}
	sync_val = (7 - sync_val) & 7;
#if 0
	printf("sync_val:%02x, byte_pos:%05x, sync_ptr[0]:%02x\n", sync_val,
		byte_pos, sync_ptr[0]);
#endif
	iwm_recalc_sync_from(dsk, (byte_pos * 32) + (sync_val * 4), dcycs);
}

void
iwm_recalc_sync_from(Disk *dsk, word32 qbit_pos, double dcycs)
{
	byte	*bptr, *sync_ptr;
	word32	track_bits, bits, val, mask, last_sync, sync, hunting;
	word32	bit_pos, bit_end, val1, val2, sync_0_mask;
	int	byte_pos, matches, wraps, bad_cnt;

#if 0
	printf("iwm_recalc_sync_from %06x for dsk: %p\n", qbit_pos * 8, dsk);
#endif

	track_bits = dsk->cur_track_qbits >> 2;
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2]);
	bptr = &(dsk->cur_trk_ptr->raw_bptr[2]);

	bit_pos = qbit_pos >> 2;

	// We are told qbit_pos points to a sync bit (MSB of an 8-bit disk
	//  nibble).  Work through the track, wrapping as needed, finding
	//  the next nibbles, and update the sync_ptr[] bytes with the
	//  MSB bit positions.  One a sync_ptr is found which matches what
	//  we would write in for 3 times, we are done, stop.

	matches = 0;
	wraps = 0;
	last_sync = 0x17;
	hunting = 0;		// Not searching for next 1 bit
	while(1) {
		if(bit_pos >= track_bits) {
			bit_pos -= track_bits;
			wraps++;
			if(wraps >= 3) {
				// Looping through twice has not resulted in a
				//  stable sync.  This can only happen if the
				//  track never has 8 sync bits in adjacent
				//  nibbles.  Just use bit_pos.
				break;
			}
			if((track_bits & 7) && (last_sync < 16)) {
				// Track is not a multiple of 8 bits.  Adjust
				//  last_sync to account for these extra bits
				last_sync = last_sync - (7 - (track_bits & 7));
			}
		}
		byte_pos = bit_pos >> 3;
		val = bptr[byte_pos];
		val1 = (bptr[byte_pos - 1] << 8) | val;
		val2 = (val1 | (val1 >> 1) | (val1 >> 2) | (val1 >> 3)) & 0xff;
		val2 = (~val2) & 0xff;		// has '1' after each 4-bit==0
#if 0
		printf("  pos:%05x_%d, val:%02x cur_sync:%02x\n", bit_pos >> 3,
			bit_pos & 7, val, sync_ptr[bit_pos >> 3]);
#endif
		mask = 0x80 >> (bit_pos & 7);
		sync = sync_ptr[byte_pos];
		sync_0_mask = 0;
		if(val2) {
			// There were too many 0's in this byte
			if((val2 > mask) && !hunting) {
				// These 0's would interfere with the previous
				//  nibble.
				sync_0_mask |= 0x20;
				val |= mask;		// Just force this bit
				sync_ptr[byte_pos - 1] |= 0x10;
			}
			if(val2 & (mask - 1)) {
				// These 0's would interfere with this nibble
				sync_0_mask |= 0x10;
			}
#if 0
				printf("byte_pos:%04x, val2:%03x, sync_0:%02x "
					"dcycs:%f\n", byte_pos, val2,
					sync_0_mask, dcycs);
				printf("val:%02x, mask:%02x, last_sync:%02x\n",
					val, mask, last_sync);
#endif
			dbg_log_info(dcycs, bit_pos * 32,
					(byte_pos << 8) | sync_0_mask, 0xe7);
			//g_halt_arm_move = 1;
			//val = val | val2;		// Find some 1's
		}
		if(val & mask) {
			sync = 7 - (bit_pos & 7);
		} else {
			sync_ptr[byte_pos] = last_sync | sync_0_mask;
			bit_pos++;
			hunting = 1;
			continue;
		}
		sync |= sync_0_mask;
		if((sync == sync_ptr[byte_pos]) && (sync < 16)) {
			matches++;
			if(matches >= 3) {
				break;
			}
		} else {
			matches = 0;
		}
		sync_ptr[byte_pos] = sync;
#if 0
		if((bit_pos <= 23) || (bit_pos >= (track_bits - 24))) {
			printf("sync_ptr[%04x]=%02x (%05x)\n", bit_pos >> 3,
								sync, bit_pos);
		}
		printf("Set sync[%04x]=%02x\n", bit_pos >> 3, sync);
#endif
		last_sync = (sync + 8) & 0xf;
		bit_pos += 8;
		hunting = 0;
	}

	// printf("sync done at qbit*8=%06x\n", bit_pos * 4 * 8);

	// Fix up sync[track_bits/8], which may not have been written
	byte_pos = (track_bits - 1) >> 3;	// Point to valid data
	bit_end = (track_bits - 1) & 7;		// Point to last valid bit
	bits = bit_end + 1;			// num valid bits at byte_pos
	sync = sync_ptr[byte_pos];
	// printf("sync[byte_pos,%04x]=%02x\n", byte_pos, sync);
	if(sync & 0x80) {			// Not set
		sync = sync_ptr[0] - bits;
		if(sync & 0x80) {			// unsigned overflow
			sync = sync_ptr[byte_pos - 1] + 8;
		}
		sync_ptr[byte_pos] = sync;
#if 0
		printf("Set sync_ptr[byte_pos]=%02x, [byte_pos-1]:%02x, [0]="
			"%02x, bits:%d\n", sync, sync_ptr[byte_pos - 1],
			sync_ptr[0], bits);
#endif
	}

	// Fix up sync[byte_pos+1] as well
	sync = 8 + sync_ptr[0] - bits;	// guaranteed to be from 0..14
	if((sync & 0xf) < 8) {
		sync_ptr[byte_pos + 1] = sync;
	} else {
		sync = sync_ptr[1] - bits;
		if(sync & 0x80) {			// unsigned overflow
			sync = sync_ptr[0] + 8 - bits;
		}
		sync_ptr[byte_pos + 1] = sync;
	}

	// Fix up sync[-1], sync[-2]
	if((sync_ptr[0] & 0xf) >= 8) {
		sync_ptr[-1] = sync_ptr[0] - 8;
		printf("sync[0] was >= 8, so [-1]=%02x\n", sync_ptr[-1]);
	} else {
		sync = sync - (8 - bits);
			// converts byte_pos sync to be relative to sync_ptr[0]
		// printf(" sync is now:%03x\n", sync);
		if(sync & 0x80) {		// unsigned overflow
			// [byte_pos] indicates a sync_ptr[0] bit.  Go back
			sync = sync_ptr[byte_pos - 1] + bits;
		}
		sync_ptr[-1] = sync;
#if 0
		printf("Set [-1]=%02x, bits:%d, [byte_pos-1]:%02x\n", sync,
			bits, sync_ptr[byte_pos - 1]);
#endif
	}
	sync_ptr[-2] = 0;		// Should not be used

	byte_pos = ((track_bits + 7) >> 3) + 2;
	bad_cnt = 0;
#if 0
	for(i = 0; i < byte_pos; i++) {
		if(sync_ptr[i - 2] & 0x80) {
			printf("sync at %05x:%02x\n", i - 2, sync_ptr[i - 2]);
			bad_cnt++;
		}
	}
#endif
	if(bad_cnt) {
		iwm_show_track(-1, -1, dcycs);
		halt_printf("Exiting\n");
		//exit(1);
	}
}

void
iwm_find_any_sync(Disk *dsk, double dcycs)
{
	byte	*bptr, *sync_ptr;
	word32	mask, new_mask, this_val, slide;
	int	track_bytes;
	int	i, j;

	track_bytes = (dsk->cur_track_qbits + 31) >> 5;
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2]);
	bptr = &(dsk->cur_trk_ptr->raw_bptr[2]);

	// printf("iwm_find_any_sync\n");

	// We need to start at all 8 bit positions of byte 0 on the track,
	//  and move forward until all 8 agree on the same position.
	mask = 0xff;
	slide = 0;
	for(i = 0; i < track_bytes; i++) {
		new_mask = 0;
		this_val = bptr[i];
		if(this_val == 0xff) {
			continue;
		}
		//printf("bptr[%04x]=%02x, mask:%02x, slide:%02x\n", i,
		//					this_val, mask, slide);
		for(j = 7; j >= 0; j--) {
			if((this_val >> j) & 1) {
				new_mask |= (slide << j) | (mask & (1 << j));
				slide = 0;
			} else {
				slide = slide | ((mask >> j) & 1);
			}
		}
		mask = new_mask;
		// printf("   now mask=%02x\n", mask);
		if(((mask & (mask - 1)) == 0) && (slide == 0)) {
			// We've got it.
			for(j = 0; j < 8; j++) {
				mask = mask >> 1;
				if(mask == 0) {
					sync_ptr[i] = j;
#if 0
					printf("Set sync_ptr[%04x]=%02x\n",
						i, j);
#endif
					iwm_recalc_sync_from(dsk, i << 5,
									dcycs);
					return;
				}
			}
		}
	}

	// Entire track is all ff's (or close to it) so there's no reliable
	//  singular sync bit.  So just return whatever.
	this_val = bptr[0];
	for(i = 0; i < 8; i++) {
		if((this_val >> i) & 1) {
			sync_ptr[0] = i;
			iwm_recalc_sync_from(dsk, 0, dcycs);
			return;
		}
	}

	sync_ptr[0] = 1;
	halt_printf("No sync found and first byte was 0!\n");
	exit(2);
}

/* c600 */
void
sector_to_partial_nib(byte *in, byte *nib_ptr)
{
	byte	*aux_buf, *nib_out;
	word32	val, val2;
	int	x;
	int	i;

	/* Convert 256(+1) data bytes to 342+1 disk nibbles */

	aux_buf = nib_ptr;
	nib_out = nib_ptr + 0x56;

	for(i = 0; i < 0x56; i++) {
		aux_buf[i] = 0;
	}

	x = 0x55;
	for(i = 0x101; i >= 0; i--) {
		val = in[i];
		if(i >= 0x100) {
			val = 0;
		}
		val2 = (aux_buf[x] << 1) + (val & 1);
		val = val >> 1;
		val2 = (val2 << 1) + (val & 1);
		val = val >> 1;
		nib_out[i] = val;
		aux_buf[x] = val2;
		x--;
		if(x < 0) {
			x = 0x55;
		}
	}
}


int
disk_unnib_4x4(Disk *dsk)
{
	int	val1;
	int	val2;

	val1 = iwm_read_data_fast(dsk, 0);
	val2 = iwm_read_data_fast(dsk, 0);

	return ((val1 << 1) + 1) & val2;
}

int
iwm_denib_track525(Disk *dsk, byte *outbuf)
{
	byte	aux_buf[0x80];
	int	sector_done[16];
	byte	*buf;
	word32	val, val2, prev_val;
	int	track_len, vol, track, phys_sec, log_sec, cksum, x, my_nib_cnt;
	int	qtr_track, save_qbit_pos, tmp_qbit_pos, status, ret;
	int	num_sectors_done;
	int	i;

	//printf("iwm_denib_track525\n");

	save_qbit_pos = dsk->cur_qbit_pos;

	dsk->cur_qbit_pos = 0;
	qtr_track = dsk->cur_qtr_track;
	g_fast_disk_unnib = 1;

	track_len = dsk->cur_track_qbits >> 5;

	for(i = 0; i < 16; i++) {
		sector_done[i] = 0;
	}

	num_sectors_done = 0;

	val = 0;
	status = -1;
	my_nib_cnt = 0;
	while(my_nib_cnt++ < 2*track_len) {
		/* look for start of a sector */
		if(val != 0xd5) {
			val = iwm_read_data_fast(dsk, 0);
			continue;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xaa) {
			continue;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0x96) {
			continue;
		}

		/* It's a sector start */
		vol = disk_unnib_4x4(dsk);
		track = disk_unnib_4x4(dsk);
		phys_sec = disk_unnib_4x4(dsk);
		if(phys_sec < 0 || phys_sec > 15) {
			printf("Track %02x, read sec as %02x\n",
						qtr_track >> 2, phys_sec);
			break;
		}
		if(dsk->image_type == DSK_TYPE_DOS33) {
			log_sec = phys_to_dos_sec[phys_sec];
		} else {
			log_sec = phys_to_prodos_sec[phys_sec];
		}
		cksum = disk_unnib_4x4(dsk);
		if((vol ^ track ^ phys_sec ^ cksum) != 0) {
			/* not correct format */
			printf("Track %02x not DOS 3.3 since hdr cksum, %02x "
				"%02x %02x %02x\n", qtr_track >> 2,
				vol, track, phys_sec, cksum);
			break;
		}

		/* see what sector it is */
		if(track != (qtr_track >> 2) || (phys_sec < 0) ||
							(phys_sec > 15)) {
			printf("Track %02x bad since track: %02x, sec: %02x\n",
				qtr_track>>2, track, phys_sec);
			break;
		}

		if(sector_done[phys_sec]) {
			printf("Already done sector %02x on track %02x!\n",
				phys_sec, qtr_track>>2);
			break;
		}

		/* So far so good, let's do it! */
		val = 0;
		i = 0;
		while(i < NIBS_FROM_ADDR_TO_DATA) {
			i++;
			if(val != 0xd5) {
				val = iwm_read_data_fast(dsk, 0);
				continue;
			}

			val = iwm_read_data_fast(dsk, 0);
			if(val != 0xaa) {
				continue;
			}

			val = iwm_read_data_fast(dsk, 0);
			if(val != 0xad) {
				continue;
			}

			/* got it, just break */
			break;
		}

		if(i >= NIBS_FROM_ADDR_TO_DATA) {
			printf("No data header, track %02x, sec %02x\n",
				qtr_track>>2, phys_sec);
			printf("qbit_pos*8: %08x\n", dsk->cur_qbit_pos * 8);
			break;
		}

		buf = outbuf + 0x100*log_sec;

		/* Data start! */
		prev_val = 0;
		for(i = 0x55; i >= 0; i--) {
			val = iwm_read_data_fast(dsk, 0);
			val2 = g_from_disk_byte[val];
			if(val2 >= 0x100) {
				printf("Bad data area1, val:%02x,val2:%03x\n",
								val, val2);
				printf(" i:%03x, qbit_pos*8:%06x\n", i,
							dsk->cur_qbit_pos * 8);
				break;
			}
			prev_val = val2 ^ prev_val;
			aux_buf[i] = prev_val;
		}

		/* rest of data area */
		for(i = 0; i < 0x100; i++) {
			val = iwm_read_data_fast(dsk, 0);
			val2 = g_from_disk_byte[val];
			if(val2 >= 0x100) {
				printf("Bad data area2, read: %02x\n", val);
				printf("  qbit_pos*8: %06x\n",
							dsk->cur_qbit_pos * 8);
				break;
			}
			prev_val = val2 ^ prev_val;
			buf[i] = prev_val;
		}

		/* checksum */
		val = iwm_read_data_fast(dsk, 0);
		val2 = g_from_disk_byte[val];
		if(val2 >= 0x100) {
			printf("Bad data area3, read: %02x\n", val);
			printf("  qbit_pos*8: %04x\n", dsk->cur_qbit_pos * 8);
			break;
		}
		if(val2 != prev_val) {
			printf("Bad data cksum, got %02x, wanted: %02x\n",
				val2, prev_val);
			printf("  qbit_pos*8: %04x\n", dsk->cur_qbit_pos * 8);
			break;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xde) {
			printf("No 0xde at end of sector data:%02x\n", val);
			printf("  qbit_pos*8: %04x\n", dsk->cur_qbit_pos * 8);
			break;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xaa) {
			printf("No 0xde,0xaa at end of sector:%02x\n", val);
			printf("  qbit_pos*8: %04x\n", dsk->cur_qbit_pos * 8);
			break;
		}

		/* Got this far, data is good, merge aux_buf into buf */
		x = 0x55;
		for(i = 0; i < 0x100; i++) {
			val = aux_buf[x];
			val2 = (buf[i] << 1) + (val & 1);
			val = val >> 1;
			val2 = (val2 << 1) + (val & 1);
			buf[i] = val2;
			val = val >> 1;
			aux_buf[x] = val;
			x--;
			if(x < 0) {
				x = 0x55;
			}
		}
		sector_done[phys_sec] = 1;
		num_sectors_done++;
		if(num_sectors_done >= 16) {
			status = 0;
			break;
		}
	}

	tmp_qbit_pos = dsk->cur_qbit_pos;
	dsk->cur_qbit_pos = save_qbit_pos;
	g_fast_disk_unnib = 0;

	if(status == 0) {
		//printf("iwm_denib_track525 succeeded\n");
		return 0;
	}

	printf("Nibblization not done, %02x sectors found on track %02x, "
		"drive:%d, slot:%d\n", num_sectors_done, qtr_track >> 2,
		dsk->drive, dsk->disk_525 + 5);
	printf("my_nib_cnt: %05x, qbit_pos*8:%06x, trk_len:%05x\n", my_nib_cnt,
		tmp_qbit_pos * 8, track_len);
	ret = 16;
	for(i = 0; i < 16; i++) {
		printf("sector_done[%d] = %d\n", i, sector_done[i]);
		if(sector_done[i]) {
			ret--;
		}
	}
	iwm_show_a_track(dsk, dsk->cur_trk_ptr, 0.0);
	if(ret) {
		return ret;
	}
	return -1;
}

int
iwm_denib_track35(Disk *dsk, byte *outbuf)
{
	word32	buf_c00[0x100];
	word32	buf_d00[0x100];
	word32	buf_e00[0x100];
	int	sector_done[16];
	byte	*buf;
	word32	tmp_5c, tmp_5d, tmp_5e, tmp_66, tmp_67, val, val2;
	int	num_sectors_done, track_len, phys_track, phys_sec, phys_side;
	int	phys_capacity, cksum, tmp, track, side, num_sectors, x, y;
	int	carry, my_nib_cnt, qtr_track, save_qbit_pos, status;
	int	i;

	save_qbit_pos = dsk->cur_qbit_pos;
	if(dsk->cur_trk_ptr == 0) {
		return 0;
	}

	qtr_track = dsk->cur_qtr_track;
	dsk->cur_qbit_pos = 0;
	g_fast_disk_unnib = 1;

	track_len = dsk->cur_track_qbits >> 5;

	num_sectors = g_track_bytes_35[dsk->cur_qtr_track >> 5] >> 9;

	for(i = 0; i < num_sectors; i++) {
		sector_done[i] = 0;
	}

	num_sectors_done = 0;

	val = 0;
	status = -1;
	my_nib_cnt = 0;

	track = qtr_track >> 1;
	side = qtr_track & 1;

	while(my_nib_cnt++ < 2*track_len) {
		/* look for start of a sector */
		if(val != 0xd5) {
			val = iwm_read_data_fast(dsk, 0);
			continue;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xaa) {
			continue;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0x96) {
			continue;
		}

		/* It's a sector start */
		val = iwm_read_data_fast(dsk, 0);
		phys_track = g_from_disk_byte[val];
		if(phys_track != (track & 0x3f)) {
			printf("Track %02x.%d, read track %02x, %02x\n",
				track, side, phys_track, val);
			break;
		}

		phys_sec = g_from_disk_byte[iwm_read_data_fast(dsk, 0)];
		if((phys_sec < 0) || (phys_sec >= num_sectors)) {
			printf("Track %02x.%d, read sector %02x??\n",
				track, side, phys_sec);
			break;
		}
		phys_side = g_from_disk_byte[iwm_read_data_fast(dsk, 0)];

		if(phys_side != ((side << 5) + (track >> 6))) {
			printf("Track %02x.%d, read side %02x??\n",
				track, side, phys_side);
			break;
		}
		phys_capacity = g_from_disk_byte[iwm_read_data_fast(dsk, 0)];
		if(phys_capacity != 0x24 && phys_capacity != 0x22) {
			printf("Track %02x.%x capacity: %02x != 0x24/22\n",
				track, side, phys_capacity);
		}
		cksum = g_from_disk_byte[iwm_read_data_fast(dsk, 0)];

		tmp = phys_track ^ phys_sec ^ phys_side ^ phys_capacity;
		if(cksum != tmp) {
			printf("Track %02x.%d, sector %02x, cksum: %02x.%02x\n",
				track, side, phys_sec, cksum, tmp);
			break;
		}


		if(sector_done[phys_sec]) {
			printf("Already done sector %02x on track %02x.%x!\n",
				phys_sec, track, side);
			break;
		}

		/* So far so good, let's do it! */
		val = 0;
		for(i = 0; i < 38; i++) {
			val = iwm_read_data_fast(dsk, 0);
			if(val == 0xd5) {
				break;
			}
		}
		if(val != 0xd5) {
			printf("No data header, track %02x.%x, sec %02x\n",
				track, side, phys_sec);
			break;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xaa) {
			printf("Bad data hdr1,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			printf("qbit_pos*8: %08x\n", dsk->cur_qbit_pos * 8);
			break;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xad) {
			printf("Bad data hdr2,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			printf("dsk->cur_qbit_pos*8:%06x\n",
					dsk->cur_qbit_pos * 8);
			break;
		}

		buf = outbuf + (phys_sec << 9);

		/* check sector again */
		tmp = g_from_disk_byte[iwm_read_data_fast(dsk, 0)];
		if(tmp != phys_sec) {
			printf("Bad data hdr3,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		/* Data start! */
		tmp_5c = 0;
		tmp_5d = 0;
		tmp_5e = 0;
		y = 0xaf;
		carry = 0;

		while(y > 0) {
/* 626f */
			val = iwm_read_data_fast(dsk, 0);
			val2 = g_from_disk_byte[val];
			if(val2 >= 0x100) {
				printf("Bad data area1b, read: %02x\n", val);
				printf(" i:%03x, qbit_pos*8:%04x\n", i,
							dsk->cur_qbit_pos * 8);
				break;
			}
			tmp_66 = val2;

			tmp_5c = tmp_5c << 1;
			carry = (tmp_5c >> 8);
			tmp_5c = (tmp_5c + carry) & 0xff;

			val = iwm_read_data_fast(dsk, 0);
			val2 = g_from_disk_byte[val];
			if(val2 >= 0x100) {
				printf("Bad data area2, read: %02x\n", val);
				break;
			}

			val2 = val2 + ((tmp_66 << 2) & 0xc0);

			val2 = val2 ^ tmp_5c;
			buf_c00[y] = val2;

			tmp_5e = val2 + tmp_5e + carry;
			carry = (tmp_5e >> 8);
			tmp_5e = tmp_5e & 0xff;
/* 62b8 */
			val = iwm_read_data_fast(dsk, 0);
			val2 = g_from_disk_byte[val];
			val2 = val2 + ((tmp_66 << 4) & 0xc0);
			val2 = val2 ^ tmp_5e;
			buf_d00[y] = val2;
			tmp_5d = val2 + tmp_5d + carry;

			carry = (tmp_5d >> 8);
			tmp_5d = tmp_5d & 0xff;

			y--;
			if(y <= 0) {
				break;
			}

/* 6274 */
			val = iwm_read_data_fast(dsk, 0);
			val2 = g_from_disk_byte[val];
			val2 = val2 + ((tmp_66 << 6) & 0xc0);
			val2 = val2 ^ tmp_5d;
			buf_e00[y+1] = val2;

			tmp_5c = val2 + tmp_5c + carry;
			carry = (tmp_5c >> 8);
			tmp_5c = tmp_5c & 0xff;
		}

/* 62d0 */
		val = iwm_read_data_fast(dsk, 0);
		val2 = g_from_disk_byte[val];

		tmp_66 = (val2 << 6) & 0xc0;
		tmp_67 = (val2 << 4) & 0xc0;
		val2 = (val2 << 2) & 0xc0;

		val = iwm_read_data_fast(dsk, 0);
		val2 = g_from_disk_byte[val] + val2;
		if(tmp_5e != (word32)val2) {
			printf("Checksum 5e bad: %02x vs %02x\n", tmp_5e, val2);
			printf("val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		val = iwm_read_data_fast(dsk, 0);
		val2 = g_from_disk_byte[val] + tmp_67;
		if(tmp_5d != (word32)val2) {
			printf("Checksum 5d bad: %02x vs %02x\n", tmp_5e, val2);
			printf("val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		val = iwm_read_data_fast(dsk, 0);
		val2 = g_from_disk_byte[val] + tmp_66;
		if(tmp_5c != (word32)val2) {
			printf("Checksum 5c bad: %02x vs %02x\n", tmp_5e, val2);
			printf("val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		/* Whew, got it!...check for DE AA */
		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xde) {
			printf("Bad data epi1,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			printf("qbit_pos*8: %08x\n", dsk->cur_qbit_pos * 8);
			break;
		}

		val = iwm_read_data_fast(dsk, 0);
		if(val != 0xaa) {
			printf("Bad data epi2,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		/* Now, convert buf_c/d/e to output */
/* 6459 */
		y = 0;
		for(x = 0xab; x >= 0; x--) {
			*buf++ = buf_c00[x];
			y++;
			if(y >= 0x200) {
				break;
			}

			*buf++ = buf_d00[x];
			y++;
			if(y >= 0x200) {
				break;
			}

			*buf++ = buf_e00[x];
			y++;
			if(y >= 0x200) {
				break;
			}
		}

		sector_done[phys_sec] = 1;
		num_sectors_done++;
		if(num_sectors_done >= num_sectors) {
			status = 0;
			break;
		}
		val = 0;
	}

	if(status < 0) {
		printf("dsk->qbit_pos*8: %06x, status: %d\n",
						dsk->cur_qbit_pos * 8, status);
		for(i = 0; i < num_sectors; i++) {
			printf("sector done[%d] = %d\n", i, sector_done[i]);
		}
	}

	dsk->cur_qbit_pos = save_qbit_pos;
	g_fast_disk_unnib = 0;

	if(status == 0) {
		return 0;
	}

	printf("Nibblization not done, %02x blocks found on track %02x.%d\n",
		num_sectors_done, qtr_track >> 1, qtr_track & 1);
	return -1;
}

/* ret = 1 -> dirty data written out */
/* ret = 0 -> not dirty, no error */
/* ret < 0 -> error */
int
disk_track_to_unix(Disk *dsk, byte *outbuf)
{
	Trk	*trk;
	dword64	dunix_pos, dret, unix_len;
	int	ret;

	trk = dsk->cur_trk_ptr;
	if(trk == 0) {
		return -1;
	}
	if(trk->track_qbits == 0) {
		return 0;
	}

#if 0
	if((qtr_track & 3) && disk_525) {
		halt_printf("You wrote to phase %02x!  Can't wr bk to unix!\n",
			qtr_track);
		dsk->write_through_to_unix = 0;
		return -1;
	}
#endif

	if(dsk->wozinfo_ptr) {				// WOZ disk
		outbuf = trk->raw_bptr;
		ret = 0;
	} else if(dsk->disk_525) {
		ret = iwm_denib_track525(dsk, outbuf);
	} else {
		ret = iwm_denib_track35(dsk, outbuf);
	}

	if(ret != 0) {
		return -1;
	}

	/* Write it out */
	trk = dsk->cur_trk_ptr;
	dunix_pos = trk->dunix_pos;
	unix_len = trk->unix_len;
	if(unix_len < 0x1000) {
		halt_printf("Disk:%s trk:%d, dunix_pos:%08llx, len:%08x\n",
			dsk->name_ptr, dsk->cur_qtr_track, dunix_pos, unix_len);
		return -1;
	}

	if(dsk->dynapro_info_ptr) {
		return dynapro_write(dsk, outbuf, dunix_pos, unix_len);
	}
	dret = lseek(dsk->fd, dunix_pos, SEEK_SET);
	if(dret != dunix_pos) {
		halt_printf("lseek 525: %08llx, errno: %d\n", dret, errno);
		return -1;
	}

	dret = write(dsk->fd, &(outbuf[0]), unix_len);
	if(dret != unix_len) {
		printf("write: %08llx, errno:%d, qtrk: %02x, disk: %s\n",
			dret, errno, dsk->cur_qtr_track, dsk->name_ptr);
	}

	return 1;
}

void
show_hex_data(byte *buf, int count)
{
	int	i;

	for(i = 0; i < count; i += 16) {
		printf("%04x: %02x %02x %02x %02x %02x %02x %02x %02x "
			"%02x %02x %02x %02x %02x %02x %02x %02x\n", i,
			buf[i+0], buf[i+1], buf[i+2], buf[i+3],
			buf[i+4], buf[i+5], buf[i+6], buf[i+7],
			buf[i+8], buf[i+9], buf[i+10], buf[i+11],
			buf[i+12], buf[i+13], buf[i+14], buf[i+15]);
	}

}

void
iwm_check_nibblization(double dcycs)
{
	Disk	*dsk;
	int	slot, drive, sel35;

	drive = g_iwm.drive_select;
	slot = 6;
	if(g_iwm.motor_on) {
		sel35 = (g_c031_disk35 >> 6) & 1;
	} else {
		sel35 = g_iwm.last_sel35 & 1;
	}
	if(sel35) {
		dsk = &(g_iwm.drive35[drive]);
		slot = 5;
	} else {
		dsk = &(g_iwm.drive525[drive]);
	}
	printf("iwm_check_nibblization, s%d d%d\n", slot, drive);
	disk_check_nibblization(dsk, 0, 4096, dcycs);
}

void
disk_check_nibblization(Disk *dsk, byte *in_buf, int size, double dcycs)
{
	byte	buffer[0x3000];
	int	ret, ret2;
	int	i;

	if(size > 0x3000) {
		printf("size %08x is > 0x3000, disk_check_nibblization\n",size);
		exit(3);
	}

	for(i = 0; i < size; i++) {
		buffer[i] = 0;
	}

	//printf("About to call iwm_denib_track*, here's the track:\n");
	//iwm_show_a_track(dsk, dsk->cur_trk_ptr, dcycs);

	if(dsk->disk_525) {
		ret = iwm_denib_track525(dsk, &(buffer[0]));
	} else {
		ret = iwm_denib_track35(dsk, &(buffer[0]));
	}

	ret2 = -1;
	if(in_buf) {
		for(i = 0; i < size; i++) {
			if(buffer[i] != in_buf[i]) {
				printf("buffer[%04x]: %02x != %02x\n", i,
							buffer[i], in_buf[i]);
				ret2 = i;
				break;
			}
		}
	}

	if((ret != 0) || (ret2 >= 0)) {
		printf("disk_check_nib ret:%d, ret2:%d for q_track %03x\n",
			ret, ret2, dsk->cur_qtr_track);
		if(in_buf) {
			show_hex_data(in_buf, 0x1000);
		}
		show_hex_data(buffer, 0x1000);
		iwm_show_a_track(dsk, dsk->cur_trk_ptr, dcycs);
		if(ret == 16) {
			printf("No sectors found, ignore it\n");
			return;
		}

		//halt_printf("Stop\n");
		//exit(2);
	}
}

#define TRACK_BUF_LEN		0x2000

void
disk_unix_to_nib(Disk *dsk, int qtr_track, dword64 dunix_pos, word32 unix_len,
	int len_bits, double dcycs)
{
	byte	track_buf[TRACK_BUF_LEN];
	Trk	*trk;
	byte	*bptr;
	dword64	dret, dlen;
	word32	num_bytes, must_clear_track;
	int	i;

	/* Read track from dsk int track_buf */
#if 0
	printf("disk_unix_to_nib: qtr:%04x, unix_pos:%08llx, unix_len:%08x, "
		"len_bits:%06x\n", qtr_track, dunix_pos, unix_len, len_bits);
#endif

	must_clear_track = 0;

	if(unix_len > TRACK_BUF_LEN) {
		printf("diks_unix_to_nib: requested len of image %s = %05x\n",
			dsk->name_ptr, unix_len);
	}

	bptr = dsk->raw_data;
	if(bptr != 0) {
		// raw_data is valid, so use it
		if((dunix_pos + unix_len) > dsk->raw_dsize) {
			must_clear_track = 1;
		} else {
			bptr += dunix_pos;
			for(i = 0; i < (int)unix_len; i++) {
				track_buf[i] = bptr[i];
			}
		}
	} else if(unix_len > 0) {
		dret = lseek(dsk->fd, dunix_pos, SEEK_SET);
		if(dret != dunix_pos) {
			printf("lseek of disk %s len 0x%llx ret: %lld, errno:"
				"%d\n", dsk->name_ptr, dunix_pos, dret, errno);
			must_clear_track = 1;
		}

		dlen = read(dsk->fd, track_buf, unix_len);
		if(dlen != unix_len) {
			printf("read of disk %s q_trk %d ret: %lld, errno:%d\n",
				dsk->name_ptr, qtr_track, dlen, errno);
			must_clear_track = 1;
		}
	}

	if(must_clear_track) {
		for(i = 0; i < TRACK_BUF_LEN; i++) {
			track_buf[i] = 0;
		}
	}

#if 0
	printf("Q_track %02x dumped out\n", qtr_track);

	for(i = 0; i < 4096; i += 32) {
		printf("%04x: %02x%02x%02x%02x%02x%02x%02x%02x "
			"%02x%02x%02x%02x%02x%02x%02x%02x "
			"%02x%02x%02x%02x%02x%02x%02x%02x "
			"%02x%02x%02x%02x%02x%02x%02x%02x\n", i,
			track_buf[i+0], track_buf[i+1], track_buf[i+2],
			track_buf[i+3], track_buf[i+4], track_buf[i+5],
			track_buf[i+6], track_buf[i+7], track_buf[i+8],
			track_buf[i+9], track_buf[i+10], track_buf[i+11],
			track_buf[i+12], track_buf[i+13], track_buf[i+14],
			track_buf[i+15], track_buf[i+16], track_buf[i+17],
			track_buf[i+18], track_buf[i+19], track_buf[i+20],
			track_buf[i+21], track_buf[i+22], track_buf[i+23],
			track_buf[i+24], track_buf[i+25], track_buf[i+26],
			track_buf[i+27], track_buf[i+28], track_buf[i+29],
			track_buf[i+30], track_buf[i+31]);
	}
#endif

	dsk->cur_qbit_pos = 0;		/* for consistency */

	trk = &(dsk->trks[qtr_track]);
	num_bytes = 2 + ((len_bits + 7) >> 3) + 4;
	trk->track_qbits = len_bits * 4;
	trk->dunix_pos = dunix_pos;
	trk->unix_len = unix_len;
	trk->raw_bptr = (byte *)malloc(num_bytes);
	trk->sync_ptr = (byte *)malloc(num_bytes);

	iwm_move_to_track(dsk, qtr_track);

	/* create nibblized image */

	if(dsk->disk_525 && (dsk->image_type == DSK_TYPE_NIB)) {
		iwm_nibblize_track_nib525(dsk, track_buf, qtr_track, unix_len);
	} else if(dsk->disk_525) {
		iwm_nibblize_track_525(dsk, track_buf, qtr_track, dcycs);
	} else {
		iwm_nibblize_track_35(dsk, track_buf, qtr_track, unix_len,
									dcycs);
	}
}

void
iwm_nibblize_track_nib525(Disk *dsk, byte *track_buf, int qtr_track,
						word32 unix_len)
{
	byte	*bptr, *sync_ptr;
	int	len;
	int	i;

	// This is the old, dumb .nib format.  It consists of 0x1a00 bytes
	//  per track, but there's no sync information.  Just mark each byte
	//  as being sync=7
	len = unix_len;
	bptr = &(dsk->cur_trk_ptr->raw_bptr[2]);
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[0]);
	for(i = 0; i < len; i++) {
		bptr[i] = track_buf[i];
	}
	bptr[-1] = bptr[len - 1];
	bptr[-2] = bptr[len - 2];
	bptr[len] = bptr[0];
	bptr[len + 1] = bptr[1];
	for(i = 0; i < len + 4; i++) {
		sync_ptr[i] = 7;
	}
	if(dsk->cur_track_qbits != (unix_len * 32)) {
		fatal_printf("Track %d.%02d of nib image should be qbit*8:%06x "
			"but it is: %06x\n", qtr_track >> 2, (qtr_track & 3)*25,
			unix_len * 256, dsk->cur_track_qbits * 8);
	}

	iwm_printf("Nibblized q_track %02x\n", qtr_track);
}

void
iwm_nibblize_track_525(Disk *dsk, byte *track_buf, int qtr_track, double dcycs)
{
	byte	partial_nib_buf[0x300];
	word32	val, last_val;
	int	phys_sec, log_sec, num_sync;
	int	i;

#if 0
	printf("nibblize track 525, qtr_track:%04x, trk:%p, trk->raw_bptr:%p, "
		"sync_ptr:%p\n", qtr_track, trk, trk->raw_bptr, trk->sync_ptr);
#endif

	for(phys_sec = 0; phys_sec < 16; phys_sec++) {
		if(dsk->image_type == DSK_TYPE_DOS33) {
			log_sec = phys_to_dos_sec[phys_sec];
		} else {
			log_sec = phys_to_prodos_sec[phys_sec];
		}

		/* Create sync headers */
		if(phys_sec == 0) {
			num_sync = 70;
		} else {
			num_sync = 22;
		}

		for(i = 0; i < num_sync; i++) {
			disk_nib_out(dsk, 0xff, 10);
		}
		disk_nib_out(dsk, 0xd5, 8);		// prolog: d5,aa,96
		disk_nib_out(dsk, 0xaa, 8);
		disk_nib_out(dsk, 0x96, 8);
		disk_4x4_nib_out(dsk, dsk->vol_num);
		disk_4x4_nib_out(dsk, qtr_track >> 2);
		disk_4x4_nib_out(dsk, phys_sec);
		disk_4x4_nib_out(dsk, dsk->vol_num ^ (qtr_track>>2) ^ phys_sec);
		disk_nib_out(dsk, 0xde, 8);		// epilog: de,aa,eb
		disk_nib_out(dsk, 0xaa, 8);
		disk_nib_out(dsk, 0xeb, 8);

		/* Inter sync */
		disk_nib_out(dsk, 0xff, 10);
		for(i = 0; i < 6; i++) {
			disk_nib_out(dsk, 0xff, 10);
		}
		disk_nib_out(dsk, 0xd5, 8);	// data prolog: d5,aa,ad
		disk_nib_out(dsk, 0xaa, 8);
		disk_nib_out(dsk, 0xad, 8);

		sector_to_partial_nib( &(track_buf[log_sec*256]),
			&(partial_nib_buf[0]));

		last_val = 0;
		for(i = 0; i < 0x156; i++) {
			val = partial_nib_buf[i];
			disk_nib_out(dsk, to_disk_byte[last_val ^ val], 8);
			last_val = val;
		}
		disk_nib_out(dsk, to_disk_byte[last_val], 8);

		/* data epilog */
		disk_nib_out(dsk, 0xde, 8);	// data epilog: de,aa,eb
		disk_nib_out(dsk, 0xaa, 8);
		disk_nib_out(dsk, 0xeb, 8);
	}

	/* finish nibblization */
	disk_nib_end_track(dsk, dcycs);

	iwm_printf("Nibblized q_track %02x\n", qtr_track);

	if(g_check_nibblization) {
		disk_check_nibblization(dsk, &(track_buf[0]), 0x1000, dcycs);
	}

	//printf("Showing track after nibblization:\n");
	//iwm_show_a_track(dsk, dsk->cur_trk_ptr, dcycs);
}

void
iwm_nibblize_track_35(Disk *dsk, byte *track_buf, int qtr_track,
						word32 unix_len, double dcycs)
{
	int	phys_to_log_sec[16];
	word32	buf_c00[0x100];
	word32	buf_d00[0x100];
	word32	buf_e00[0x100];
	byte	*buf;
	word32	val, phys_track, phys_side, capacity, cksum, acc_hi;
	word32	tmp_5c, tmp_5d, tmp_5e, tmp_5f, tmp_63, tmp_64, tmp_65;
	int	num_sectors, log_sec, track, side, num_sync, carry;
	int	interleave, x, y;
	int	i, phys_sec;

	if(dsk->cur_track_qbits & 3) {
		halt_printf("track_len*8: %08x is not bit aligned\n",
				dsk->cur_track_qbits*8);
	}
	if(dsk->cur_qbit_pos & 3) {
		halt_printf("qbit_pos*8:%06x is not bit-aligned!\n",
							dsk->cur_qbit_pos);
	}

	num_sectors = (unix_len >> 9);

	for(i = 0; i < num_sectors; i++) {
		phys_to_log_sec[i] = -1;
	}

	phys_sec = 0;
	interleave = 2;
	for(log_sec = 0; log_sec < num_sectors; log_sec++) {
		while(phys_to_log_sec[phys_sec] >= 0) {
			phys_sec++;
			if(phys_sec >= num_sectors) {
				phys_sec = 0;
			}
		}
		phys_to_log_sec[phys_sec] = log_sec;
		phys_sec += interleave;
		if(phys_sec >= num_sectors) {
			phys_sec -= num_sectors;
		}
	}

	track = qtr_track >> 1;
	side = qtr_track & 1;
	for(phys_sec = 0; phys_sec < num_sectors; phys_sec++) {

		log_sec = phys_to_log_sec[phys_sec];
		if(log_sec < 0) {
			printf("Track: %02x.%x phys_sec: %02x = %d!\n",
				track, side, phys_sec, log_sec);
			exit(2);
		}

		/* Create sync headers */
		if(phys_sec == 0) {
			num_sync = 400;
		} else {
			num_sync = 54;
		}

		for(i = 0; i < num_sync; i++) {
			disk_nib_out(dsk, 0xff, 10);
		}

		disk_nib_out(dsk, 0xd5, 10);		/* prolog */
		disk_nib_out(dsk, 0xaa, 8);		/* prolog */
		disk_nib_out(dsk, 0x96, 8);		/* prolog */

		phys_track = track & 0x3f;
		phys_side = (side << 5) + (track >> 6);
		capacity = 0x22;
		disk_nib_out(dsk, to_disk_byte[phys_track], 8);	/* trk */
		disk_nib_out(dsk, to_disk_byte[log_sec], 8);	/* sec */
		disk_nib_out(dsk, to_disk_byte[phys_side], 8);	/* sides+trk */
		disk_nib_out(dsk, to_disk_byte[capacity], 8);	/* capacity*/

		cksum = (phys_track ^ log_sec ^ phys_side ^ capacity) & 0x3f;
		disk_nib_out(dsk, to_disk_byte[cksum], 8);	/* cksum*/

		disk_nib_out(dsk, 0xde, 8);		/* epi */
		disk_nib_out(dsk, 0xaa, 8);		/* epi */

		/* Inter sync */
		for(i = 0; i < 5; i++) {
			disk_nib_out(dsk, 0xff, 10);
		}
		disk_nib_out(dsk, 0xd5, 10);	/* data prolog */
		disk_nib_out(dsk, 0xaa, 8);	/* data prolog */
		disk_nib_out(dsk, 0xad, 8);	/* data prolog */
		disk_nib_out(dsk, to_disk_byte[log_sec], 8);	/* sec again */

		/* do nibblizing! */
		buf = track_buf + (log_sec << 9);

/* 6320 */
		tmp_5e = 0;
		tmp_5d = 0;
		tmp_5c = 0;
		y = 0;
		x = 0xaf;
		buf_c00[0] = 0;
		buf_d00[0] = 0;
		buf_e00[0] = 0;
		buf_e00[1] = 0;
		for(y = 0x4; y > 0; y--) {
			buf_c00[x] = 0;
			buf_d00[x] = 0;
			buf_e00[x] = 0;
			x--;
		}

		while(x >= 0) {
/* 6338 */
			tmp_5c = tmp_5c << 1;
			carry = (tmp_5c >> 8);
			tmp_5c = (tmp_5c + carry) & 0xff;

			val = buf[y];
			tmp_5e = val + tmp_5e + carry;
			carry = (tmp_5e >> 8);
			tmp_5e = tmp_5e & 0xff;

			val = val ^ tmp_5c;
			buf_c00[x] = val;
			y++;
/* 634c */
			val = buf[y];
			tmp_5d = tmp_5d + val + carry;
			carry = (tmp_5d >> 8);
			tmp_5d = tmp_5d & 0xff;
			val = val ^ tmp_5e;
			buf_d00[x] = val;
			y++;
			x--;
			if(x <= 0) {
				break;
			}

/* 632a */
			val = buf[y];
			tmp_5c = tmp_5c + val + carry;
			carry = (tmp_5c >> 8);
			tmp_5c = tmp_5c & 0xff;

			val = val ^ tmp_5d;
			buf_e00[x+1] = val;
			y++;
		}

/* 635f */
		val = ((tmp_5c >> 2) ^ tmp_5d) & 0x3f;
/* 6367 */
		val = (val ^ tmp_5d) >> 2;
/* 636b */
		val = (val ^ tmp_5e) & 0x3f;
/* 636f */
		val = (val ^ tmp_5e) >> 2;
/* 6373 */
		tmp_5f = val;
/* 6375 */
		tmp_63 = 0;
		tmp_64 = 0;
		tmp_65 = 0;
		acc_hi = 0;


		y = 0xae;
		while(y >= 0) {
/* 63e4 */
			/* write out acc_hi */
			val = to_disk_byte[acc_hi & 0x3f];
			disk_nib_out(dsk, val, 8);

/* 63f2 */
			val = to_disk_byte[tmp_63 & 0x3f];
			tmp_63 = buf_c00[y];
			acc_hi = tmp_63 >> 6;
			disk_nib_out(dsk, val, 8);
/* 640b */
			val = to_disk_byte[tmp_64 & 0x3f];
			tmp_64 = buf_d00[y];
			acc_hi = (acc_hi << 2) + (tmp_64 >> 6);
			disk_nib_out(dsk, val, 8);
			y--;
			if(y < 0) {
				break;
			}

/* 63cb */
			val = to_disk_byte[tmp_65 & 0x3f];
			tmp_65 = buf_e00[y+1];
			acc_hi = (acc_hi << 2) + (tmp_65 >> 6);
			disk_nib_out(dsk, val, 8);
		}
/* 6429 */
		val = to_disk_byte[tmp_5f & 0x3f];
		disk_nib_out(dsk, val, 8);

		val = to_disk_byte[tmp_5e & 0x3f];
		disk_nib_out(dsk, val, 8);

		val = to_disk_byte[tmp_5d & 0x3f];
		disk_nib_out(dsk, val, 8);

		val = to_disk_byte[tmp_5c & 0x3f];
		disk_nib_out(dsk, val, 8);

/* 6440 */
		/* data epilog */
		disk_nib_out(dsk, 0xde, 8);	/* epi */
		disk_nib_out(dsk, 0xaa, 8);	/* epi */
		disk_nib_out(dsk, 0xff, 8);
	}

	disk_nib_end_track(dsk, dcycs);

	if(g_check_nibblization) {
		disk_check_nibblization(dsk, &(track_buf[0]), unix_len, dcycs);
	}
}

void
disk_4x4_nib_out(Disk *dsk, word32 val)
{
	disk_nib_out(dsk, 0xaa | (val >> 1), 8);
	disk_nib_out(dsk, 0xaa | val, 8);
}

void
disk_nib_out(Disk *dsk, word32 val, int size)
{
	word32	qbit_pos;

	qbit_pos = dsk->cur_qbit_pos;

	if(size > 8) {
		val = val << (size - 8);
	}
	dsk->cur_qbit_pos = disk_nib_out_raw(dsk, val, size, qbit_pos, 0.0);
}

void
disk_nib_end_track(Disk *dsk, double dcycs)
{
	// printf("disk_nib_end_track %p\n", dsk);

	dsk->cur_qbit_pos = 0;
	dsk->disk_dirty = 0;
	iwm_recalc_sync(dsk, 0, dcycs);
}

void
disk_nib_out_zeroes(Disk *dsk, int bits, word32 qbit_pos, double dcycs)
{
	int	this_bits;

	while(bits > 0) {
		this_bits = 8;
		if(bits < this_bits) {
			this_bits = bits;
		}
		qbit_pos = disk_nib_out_raw(dsk, 0, this_bits, qbit_pos, dcycs);
		bits -= this_bits;
	}
}

word32
disk_nib_out_raw(Disk *dsk, word32 val, int bits, word32 qbit_pos, double dcycs)
{
	word32	track_qbits, bits_left, tmp_val;

	qbit_pos = qbit_pos & -4;			// Align to a bit

	track_qbits = dsk->cur_track_qbits;
	if(track_qbits < 1) {
		printf("track_qbits:%05x!\n", track_qbits);
		return qbit_pos;
	}
	if(dcycs != 0.0) {
		dbg_log_info(dcycs, (bits << 24) | (qbit_pos << 3),
				(track_qbits << 14) | val, 0xea);
#if 0
		printf("disk_nib_out_raw %02x, bits:%d, qbit_pos*8:%06x, "
			"track_qbits*8:%06x\n", val, bits, qbit_pos * 8,
			track_qbits * 8);
#endif
	}

	bits_left = (track_qbits - qbit_pos) >> 2;
	if(bits_left < (word32)bits) {
		tmp_val = val >> (bits - bits_left);
		disk_nib_out_raw_act(dsk, tmp_val, bits_left, qbit_pos, dcycs);
#if 0
		printf("At end, split: bits:%d, bits_left:%d, qbit*8:%06x, "
			"val:%02x, tmp_val:%02x\n", bits, bits_left,
			qbit_pos * 8, val, tmp_val);
#endif
		bits -= bits_left;
		qbit_pos = 0;
	}
	return disk_nib_out_raw_act(dsk, val, bits, qbit_pos, dcycs);
}

word32
disk_nib_out_raw_act(Disk *dsk, word32 val, int bits, word32 qbit_pos,
								double dcycs)
{
	byte	*bptr, *sync_ptr;
	word32	track_qbits, shift, mask, val1;
	word32	byte_pos, bit_pos, mask0, mask1, mask2;

	// We're guaranteed not to overflow off the end, so just write in bits
	// Write in val upper bits at qbit_pos (so first valid bit is
	//  val & 0x80)
	track_qbits = dsk->cur_track_qbits;
#if 0
	printf("disk_nib_out: %03x, bits:%d, %f, qbit_b_q:%04x_%d_%d, trk_qb:"
		"%05x_%d_%d\n", val, bits, dcycs, qbit_pos >> 5,
		(qbit_pos >> 2) & 7, qbit_pos & 3, track_qbits >> 5,
		(track_qbits >> 2) & 7, track_qbits & 3);
#endif
	if(bits >= 16) {
		halt_printf("BAD write, bits:%d\n", bits);
		return qbit_pos;
	}

	bit_pos = (qbit_pos >> 2) & 7;
	byte_pos = (qbit_pos >> 5);
	bptr = &(dsk->cur_trk_ptr->raw_bptr[2 + byte_pos]);
	sync_ptr = &(dsk->cur_trk_ptr->sync_ptr[2 + byte_pos]);

	shift = (24 - bits - bit_pos) & 31;
	mask = ((1 << bits) - 1) << shift;
	val1 = val << shift;
	mask0 = (mask >> 16) & 0xff;
	bptr[0] = (bptr[0] & (~mask0)) | ((val1 >> 16) & mask0);
	mask1 = (mask >> 8) & 0xff;
	bptr[1] = (bptr[1] & (~mask1)) | ((val1 >> 8) & mask1);
	mask2 = mask & 0xff;
	bptr[2] = (bptr[2] & (~mask2)) | (val1 & mask2);
	if(dcycs != 0) {
#if 0
		printf(" set [%04x]=%02x,%02x,%02x mask:%06x, bit_pos:%d, "
			"val:%02x dcycs:%f\n", byte_pos, bptr[0], bptr[1],
			bptr[2], mask, bit_pos, val, dcycs);
#endif
	}

	sync_ptr[0] = 0xff;
	sync_ptr[1] = 0xff;
	sync_ptr[2] = 0xff;

	qbit_pos += (4 * bits);
	if(qbit_pos >= track_qbits) {
		qbit_pos -= track_qbits;
	}
#if 0
	printf(" new qbit*8:%06x\n", qbit_pos * 8);
#endif
	dsk->disk_dirty = 1;

	return qbit_pos;
}

Disk *
iwm_get_dsk_from_slot_drive(int slot, int drive)
{
	Disk	*dsk;
	int	max_drive;

	// pass in slot=5,6,7 drive=0,1 (or more for slot 7)
	max_drive = 2;
	switch(slot) {
	case 5:
		dsk = &(g_iwm.drive35[drive]);
		break;
	case 6:
		dsk = &(g_iwm.drive525[drive]);
		break;
	default:	// slot 7
		max_drive = MAX_C7_DISKS;
		dsk = &(g_iwm.smartport[drive]);
	}
	if(drive >= max_drive) {
		dsk -= drive;		// Move back to drive 0 effectively
	}

	return dsk;
}

void
iwm_eject_named_disk(int slot, int drive, const char *name,
						const char *partition_name)
{
	Disk	*dsk;

	dsk = iwm_get_dsk_from_slot_drive(slot, drive);
	if(dsk->fd < 0) {
		return;
	}

	/* If name matches, eject the disk! */
	if(!strcmp(dsk->name_ptr, name)) {
		/* It matches, eject it */
		if((partition_name != 0) && (dsk->partition_name != 0)) {
			/* If both have partitions, and they differ, then */
			/*  don't eject.  Otherwise, eject */
			if(strcmp(dsk->partition_name, partition_name) != 0) {
				/* Don't eject */
				return;
			}
		}
		iwm_eject_disk(dsk);
	}
}

void
iwm_eject_disk_by_num(int slot, int drive)
{
	Disk	*dsk;

	dsk = iwm_get_dsk_from_slot_drive(slot, drive);

	iwm_eject_disk(dsk);
}

void
iwm_eject_disk(Disk *dsk)
{
	Woz_info *wozinfo_ptr;
	int	motor_on;
	int	i;

	printf("Ejecting dsk: %s, fd:%d\n", dsk->name_ptr, dsk->fd);

	if(dsk->fd < 0) {
		return;
	}

	g_config_kegs_update_needed = 1;

	motor_on = g_iwm.motor_on;
	if(g_c031_disk35 & 0x40) {
		motor_on = g_iwm.motor_on35;
	}
	if(motor_on) {
		halt_printf("Try eject dsk:%s, but motor_on!\n", dsk->name_ptr);
	}

	dynapro_try_fix_damaged_disk(dsk);
	iwm_flush_disk_to_unix(dsk);

	printf("Ejecting disk: %s\n", dsk->name_ptr);

	/* Free all memory, close file */

	/* free the tracks first */
	if(dsk->trks != 0) {
		for(i = 0; i < dsk->num_tracks; i++) {
			free(dsk->trks[i].raw_bptr);
			free(dsk->trks[i].sync_ptr);
			dsk->trks[i].raw_bptr = 0;
			dsk->trks[i].sync_ptr = 0;
			dsk->trks[i].track_qbits = 0;
		}
	}
	dsk->num_tracks = 0;

	wozinfo_ptr = dsk->wozinfo_ptr;
	if(wozinfo_ptr) {
		if(dsk->raw_data == 0) {
			free(wozinfo_ptr->wozptr);
		}
		wozinfo_ptr->wozptr = 0;
		free(wozinfo_ptr);
	}
	dsk->wozinfo_ptr = 0;

	dynapro_free_dynapro_info(dsk);

	/* close file, clean up dsk struct */
	if(dsk->raw_data) {
		free(dsk->raw_data);
	} else {
		close(dsk->fd);
	}

	dsk->fd = -1;
	dsk->raw_dsize = 0;
	dsk->raw_data = 0;
	dsk->dimage_start = 0;
	dsk->dimage_size = 0;
	dsk->cur_qbit_pos = 0;
	dsk->cur_track_qbits = 0;
	dsk->disk_dirty = 0;
	dsk->write_through_to_unix = 0;
	dsk->write_prot = 1;
	dsk->just_ejected = 1;

	/* Leave name_ptr valid */
}

void
iwm_show_track(int slot_drive, int track, double dcycs)
{
	Disk	*dsk;
	Trk	*trk;
	int	drive;
	int	sel35;
	int	qtr_track;

	if(slot_drive < 0) {
		drive = g_iwm.drive_select;
		if(g_iwm.motor_on) {
			sel35 = (g_c031_disk35 >> 6) & 1;
		} else {
			sel35 = g_iwm.last_sel35 & 1;
		}
	} else {
		drive = slot_drive & 1;
		sel35 = !((slot_drive >> 1) & 1);
	}
	if(sel35) {
		dsk = &(g_iwm.drive35[drive]);
	} else {
		dsk = &(g_iwm.drive525[drive]);
	}

	if(track < 0) {
		qtr_track = dsk->cur_qtr_track;
	} else {
		qtr_track = track;
	}
	if(dsk->trks == 0) {
		return;
	}
	trk = &(dsk->trks[qtr_track]);

	if(trk->track_qbits == 0) {
		dbg_printf("Track_qbits: %d\n", trk->track_qbits);
		dbg_printf("No track for type: %d, drive: %d, qtrk:0x%02x\n",
			sel35, drive, qtr_track);
		return;
	}

	dbg_printf("Current s%dd%d, q_track:0x%02x\n", 6 - sel35,
							drive + 1, qtr_track);

	iwm_show_a_track(dsk, trk, dcycs);
}

void
iwm_show_a_track(Disk *dsk, Trk *trk, double dcycs)
{
	byte	*bptr;
	byte	*sync_ptr;
	word32	val, track_qbits, len, shift, line_len, sync;
	int	i, j;

	track_qbits = trk->track_qbits;
	dbg_printf("  Showtrack:track_qbits*8: %06x, qbit_pos*8: %06x, "
		"qtrk:%03x, dcycs:%f\n", track_qbits*8, dsk->cur_qbit_pos*8,
		dsk->cur_qtr_track, dcycs);
	dbg_printf("  disk_525:%d, drive:%d name:%s fd:%d, dimage_start:"
		"%08llx, dimage_size:%08llx\n", dsk->disk_525, dsk->drive,
		dsk->name_ptr, dsk->fd, dsk->dimage_start, dsk->dimage_size);
	dbg_printf("  image_type:%d, vol_num:%02x, write_prot:%d, "
		"write_through:%d, disk_dirty:%d\n", dsk->image_type,
		dsk->vol_num, dsk->write_prot, dsk->write_through_to_unix,
		dsk->disk_dirty);
	dbg_printf("  just_ejected:%d, last_phase:%d, num_tracks:%d\n",
		dsk->just_ejected, dsk->last_phase, dsk->num_tracks);

	len = (track_qbits + 31) >> 5;
	if(len >= 0x3000) {
		len = 0x3000;
		dbg_printf("len too big, using %04x\n", len);
	}

	bptr = trk->raw_bptr;
	sync_ptr = trk->sync_ptr;
	dbg_printf("-2,-1: %02x %02x.  Syncs: %02x %02x\n", bptr[0], bptr[1],
				sync_ptr[0], sync_ptr[1]);
	bptr += 2;
	sync_ptr += 2;

	len = len + 2;		// Show an extra 2 bytes
	for(i = 0; i < (int)len; i += 16) {
		line_len = 16;
		if((i + line_len) > len) {
			line_len = len - i;
		}
		// First, print raw bptr bytes
		dbg_printf("%04x:  ", i);
		for(j = 0; j < (int)line_len; j++) {
			dbg_printf(" %02x", bptr[i + j]);
			if(((i + j) * 32U) >= track_qbits) {
				dbg_printf("*");
			}
		}
		dbg_printf("\n");
		dbg_printf("  sync:");
		for(j = 0; j < (int)line_len; j++) {
			dbg_printf(" %2d", sync_ptr[i + j]);
		}
		dbg_printf("\n");
		dbg_printf("  nibs:");
		for(j = 0; j < (int)line_len; j++) {
			sync = sync_ptr[i+j];
			if(sync >= 8) {
				dbg_printf(" XX");
			} else {
				shift = (7 - sync) & 7;
				val = (bptr[i + j] << 8) | bptr[i + j + 1];
				val = ((val << shift) >> 8) & 0xff;
				dbg_printf(" %02x", val);
			}
		}
		dbg_printf("\n");
	}
}


void
dummy1(word32 psr)
{
	printf("dummy1 psr: %05x\n", psr);
}

void
dummy2(word32 psr)
{
	printf("dummy2 psr: %05x\n", psr);
}
