const char rcsid_iwm_c[] = "@(#)$KmKId: iwm.c,v 1.134 2021-01-16 03:59:36+00 kentd Exp $";

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

#include "defc.h"

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



int	g_fast_disk_emul = 1;
int	g_slow_525_emul_wr = 0;
double	g_dcycs_end_emul_wr = 0.0;
int	g_fast_disk_unnib = 0;
int	g_iwm_fake_fast = 0;


word32	g_from_disk_byte[256];
int	g_from_disk_byte_valid = 0;

Iwm	g_iwm;

extern int g_c031_disk35;

int	g_iwm_motor_on = 0;

int	g_check_nibblization = 0;

void
iwm_init_drive(Disk *dsk, int smartport, int drive, int disk_525)
{
	dsk->dcycs_last_read = 0.0;
	dsk->raw_data = 0;
	dsk->name_ptr = 0;
	dsk->partition_name = 0;
	dsk->partition_num = -1;
	dsk->fd = -1;
	dsk->raw_size = 0;
	dsk->image_start = 0;
	dsk->image_size = 0;
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
	dsk->nib_pos = 0;
	dsk->num_tracks = 0;
	dsk->trks = 0;
}

void
disk_set_num_tracks(Disk *dsk, int num_tracks)
{
	int	i;

	if(dsk->trks != 0) {
		/* This should not be necessary! */
		free(dsk->trks);
		halt_printf("Needed to free dsk->trks: %p\n", dsk->trks);
	}
	dsk->num_tracks = num_tracks;
	dsk->trks = (Trk *)malloc(num_tracks * sizeof(Trk));

	for(i = 0; i < num_tracks; i++) {
		dsk->trks[i].nib_area = 0;
		dsk->trks[i].track_dirty = 0;
		dsk->trks[i].overflow_size = 0;
		dsk->trks[i].track_len = 0;
		dsk->trks[i].unix_pos = -1;
		dsk->trks[i].unix_len = -1;
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
	g_iwm.drive_select = 0;
	g_iwm.iwm_mode = 0;
	g_iwm.enable2 = 0;
	g_iwm.reset = 0;
	g_iwm.iwm_phase[0] = 0;
	g_iwm.iwm_phase[1] = 0;
	g_iwm.iwm_phase[2] = 0;
	g_iwm.iwm_phase[3] = 0;
	g_iwm.previous_write_val = 0;
	g_iwm.previous_write_bits = 0;

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
iwm_flush_disk_to_unix(Disk *dsk)
{
	byte	buffer[0x4000];
	int	num_dirty, ret, unix_pos, unix_len;
	int	j;

	if((dsk->disk_dirty == 0) || (dsk->write_through_to_unix == 0) ||
							(dsk->raw_data != 0)) {
		return;
	}

	printf("Writing disk %s to Unix\n", dsk->name_ptr);
	dsk->disk_dirty = 0;
	num_dirty = 0;

	/* Dirty data! */
	for(j = 0; j < dsk->num_tracks; j++) {

		ret = disk_track_to_unix(dsk, j, &(buffer[0]));

		if(ret != 1 && ret != 0) {
			printf("iwm_flush_disk_to_unix ret: %d, cannot write "
				"image to unix\n", ret);
			halt_printf("Adjusting image not to write through!\n");
			dsk->write_through_to_unix = 0;
			break;
		}

		if(ret != 1) {
			/* not at an even track, or not dirty */
			continue;
		}
		if((j & 3) != 0 && dsk->disk_525) {
			halt_printf("Valid data on a non-whole trk: %03x\n", j);
			continue;
		}

		num_dirty++;

		/* Write it out */
		unix_pos = dsk->trks[j].unix_pos;
		unix_len = dsk->trks[j].unix_len;
		if(unix_pos < 0 || unix_len < 0x1000) {
			halt_printf("Disk:%s trk:%d, unix_pos:%08x, len:%08x\n",
				dsk->name_ptr, j, unix_pos, unix_len);
			break;
		}

		ret = (int)lseek(dsk->fd, unix_pos, SEEK_SET);
		if(ret != unix_pos) {
			halt_printf("lseek 525: %08x, errno: %d\n", ret, errno);
		}

		ret = (int)write(dsk->fd, &(buffer[0]), unix_len);
		if(ret != unix_len) {
			printf("write: %08x, errno:%d, qtrk: %02x, disk: %s\n",
				ret, errno, j, dsk->name_ptr);
		}
	}

	if(num_dirty == 0) {
		halt_printf("Drive %s was dirty, but no track was dirty!\n",
			dsk->name_ptr);
	}

}

/* Check for dirty disk 3 times a second */

void
iwm_vbl_update(int doit_3_persec)
{
	Disk	*dsk;
	int	motor_on;
	int	i;

	if(g_iwm.motor_on && g_iwm.motor_off) {
		if(g_iwm.motor_off_vbl_count <= g_vbl_count) {
			printf("Disk timer expired, drive off: %08x\n",
				g_vbl_count);
			g_iwm.motor_on = 0;
			g_iwm.motor_off = 0;
		}
	}

	if(!doit_3_persec) {
		return;
	}

	motor_on = g_iwm.motor_on;
	if(g_c031_disk35 & 0x40) {
		motor_on = g_iwm.motor_on35;
	}

	if(motor_on == 0 || g_iwm.motor_off) {
		/* Disk not spinning, see if any dirty tracks to flush */
		/*  out to Unix */
		for(i = 0; i < 2; i++) {
			dsk = &(g_iwm.drive525[i]);
			iwm_flush_disk_to_unix(dsk);
		}
		for(i = 0; i < 2; i++) {
			dsk = &(g_iwm.drive35[i]);
			iwm_flush_disk_to_unix(dsk);
		}
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
}

void
iwm_touch_switches(int loc, double dcycs)
{
	int	phase, on, drive;

	if(g_iwm.reset) {
		iwm_printf("IWM under reset: %d, enable2: %d\n", g_iwm.reset,
			g_iwm.enable2);
	}

	on = loc & 1;
	drive = g_iwm.drive_select;
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
				iwm525_phase_change(drive, phase);
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
				engine_recalc_events();
			}

			g_iwm_motor_on = 0;
			g_slow_525_emul_wr = 0;
			break;
		case 0x9:
			iwm_printf("Turning IWM motor on!\n");
			g_iwm.motor_on = 1;
			g_iwm.motor_off = 0;

			if(g_iwm_motor_on == 0) {
				/* recalc current speed */
				engine_recalc_events();
			}
			g_iwm_motor_on = 1;

			break;
		case 0xa:
		case 0xb:
			g_iwm.drive_select = on;
			break;
		case 0xc:
		case 0xd:
			g_iwm.q6 = on;
			break;
		case 0xe:
		case 0xf:
			g_iwm.q7 = on;
			break;
		default:
			printf("iwm_touch_switches: loc: %02x unknown!\n", loc);
			exit(2);
		}
	}

	if(!g_iwm.q7) {
		g_iwm.previous_write_bits = 0;
	}

	if((dcycs > g_dcycs_end_emul_wr) && g_slow_525_emul_wr) {
		engine_recalc_events();
		g_slow_525_emul_wr = 0;
	}
}

void
iwm_move_to_track(Disk *dsk, int new_track)
{
	int	disk_525, dr;

	disk_525 = dsk->disk_525;

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

	if(dsk->cur_qtr_track != new_track) {
		dr = dsk->drive + 1;
		if(disk_525) {
			iwm_printf("s6d%d Track: %d.%02d\n", dr,
				new_track >> 2, 25* (new_track & 3));
		} else {
			iwm_printf("s5d%d Track: %d Side: %d\n", dr,
				new_track >> 1, new_track & 1);
		}

		dsk->cur_qtr_track = new_track;
	}
}

void
iwm525_phase_change(int drive, int phase)
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

	iwm_move_to_track(dsk, qtr_track);

	dsk->last_phase = last_phase;

	iwm_printf("Moving drive to qtr track: %04x (trk:%d.%02d), %d, %d, %d, "
		"%d %d %d %d\n", qtr_track, qtr_track>>2, 25*(qtr_track & 3),
		phase, delta, last_phase, g_iwm.iwm_phase[0],
		g_iwm.iwm_phase[1], g_iwm.iwm_phase[2], g_iwm.iwm_phase[3]);

	/* sanity check stepping algorithm */
	if((qtr_track & 7) == 0) {
		/* check for just access phase 0 */
		if(last_phase != 0 ) {
			halt_printf("last_phase: %d!\n", last_phase);
		}
	}
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

		switch(state) {
		case 0x00:	/* step direction */
			return g_iwm.step_direction35;
			break;
		case 0x01:	/* lower head activate */
			/* also return instantaneous data from head */
			g_iwm.head35 = 0;
			iwm_move_to_track(dsk, (dsk->cur_qtr_track & (-2)));
			return (((int)dcycs) & 1);
			break;
		case 0x02:	/* disk in place */
			/* 1 = no disk, 0 = disk */
			iwm_printf("read disk in place, num_tracks: %d\n",
				dsk->num_tracks);
			tmp = (dsk->num_tracks <= 0);
			return tmp;
			break;
		case 0x03:	/* upper head activate */
			/* also return instantaneous data from head */
			g_iwm.head35 = 1;
			iwm_move_to_track(dsk, (dsk->cur_qtr_track | 1));
			return (((int)dcycs) & 1);
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
			return (((int)dcycs) & 1);
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
			/* set_halt(1); */
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
iwm_read_c0ec(double dcycs)
{
	Disk	*dsk;
	int	drive;

	g_iwm.q6 = 0;

	if(g_iwm.q7 == 0 && g_iwm.enable2 == 0 && g_iwm.motor_on) {
		drive = g_iwm.drive_select;
		if(g_c031_disk35 & 0x40) {
			dsk = &(g_iwm.drive35[drive]);
			return iwm_read_data(dsk, g_fast_disk_emul, dcycs);
		} else {
			dsk = &(g_iwm.drive525[drive]);
			return iwm_read_data(dsk, g_fast_disk_emul, dcycs);
		}

	}

	return read_iwm(0xc, dcycs);
}

int
read_iwm(int loc, double dcycs)
{
	Disk	*dsk;
	word32	status;
	double	diff_dcycs, dcmp;
	int	on, state, drive, val;

	loc = loc & 0xf;
	on = loc & 1;

	if(loc == 0xc) {
		g_iwm.q6 = 0;
	} else {
		iwm_touch_switches(loc, dcycs);
	}

	state = (g_iwm.q7 << 1) + g_iwm.q6;
	drive = g_iwm.drive_select;
	if(g_c031_disk35 & 0x40) {
		dsk = &(g_iwm.drive35[drive]);
	} else {
		dsk = &(g_iwm.drive525[drive]);
	}

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
					return iwm_read_data(dsk,
						g_fast_disk_emul, dcycs);
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

			val = (status << 7) + (g_iwm.motor_on << 5) +
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
				diff_dcycs = dcycs - dsk->dcycs_last_read;
				dcmp = 16.0;
				if(dsk->disk_525 == 0) {
					dcmp = 32.0;
				}
				if(diff_dcycs > dcmp) {
					iwm_printf("Write underrun!\n");
					iwm_printf("cur: %f, dc_last: %f\n",
						dcycs, dsk->dcycs_last_read);
					status = status & 0xbf;
				}
				return status;
			}
			break;
		case 0x03:	/* q7 = 1, q6 = 1 */
			halt_printf("read iwm state 3!\n");
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

	iwm_touch_switches(loc, dcycs);

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
					iwm_write_enable2(val, dcycs);
				} else {
					iwm_write_data(dsk, val,
						fast_writes, dcycs);
				}
			} else {
				/* write mode register */
				// bit 0: latech mode (should set if async hand)
				// bit 1: async handshakr
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
				iwm_write_enable2(val, dcycs);
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
			iwm_write_enable2(val, dcycs);
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
	iwm_printf("Read under enable2!\n");
	return 0xff;
}

int g_cnt_enable2_handshake = 0;

int
iwm_read_enable2_handshake(double dcycs)
{
	int	val;

	iwm_printf("Read handshake under enable2!\n");

	val = 0xc0;
	g_cnt_enable2_handshake++;
	if(g_cnt_enable2_handshake > 3) {
		g_cnt_enable2_handshake = 0;
		val = 0x80;
	}

	return val;
}

void
iwm_write_enable2(int val, double dcycs)
{
	iwm_printf("Write under enable2: %02x!\n", val);

	return;
}

void
iwm_fastemul_start_write(Disk *dsk, double dcycs_passed, double dcycs)
{
	Trk	*trk;
	double	new_dcycs;
	int	bytes, pos, track_len, qtr_track;

	// Nox Archaist doesn't finish reading sector header's checksum, but
	//  instead waits for 7 bytes to pass and then writes.  This code
	//  tries to allow fast_disk_emul mode to not overwrite the checksum.
	// Move the nib_pos forward to try to account for a delay from the
	//  last read to the current write.  But accesses to I/O locations
	//  would still take a lot of time.  Let's assume there were
	//  2 slow cycles, and clamp the skip to a min of 1 byte, max 3 bytes.
	new_dcycs = (dcycs_passed - 2) / engine.fplus_ptr->plus_1;
	iwm_printf("start write, new_dcycs:%lf, was:%lf, plus_1:%lf\n",
			new_dcycs, dcycs_passed, engine.fplus_ptr->plus_1);

	bytes = (int)new_dcycs >> 5;		// 32 cycles per byte
	if(bytes < 1) {
		bytes = 1;
	} else if(bytes > 4) {
		bytes = 4;
	}
	if(!dsk->trks) {
		return;
	}
	qtr_track = dsk->cur_qtr_track;
	trk = &(dsk->trks[qtr_track]);
	track_len = trk->track_len;

	if(track_len == 0) {
		return;
	}

	pos = dsk->nib_pos;
	pos += (2 * bytes);
	if(pos >= track_len) {
		pos = pos - track_len;
	}
	iwm_printf(" adjusted nib_pos from %04x to %04x\n", dsk->nib_pos, pos);
	dsk->nib_pos = pos;
}

word32
iwm_read_data(Disk *dsk, int fast_disk_emul, double dcycs)
{
	Trk	*trk;
	double	dcycs_this_nib, dcycs_next_nib, dcycs_passed, track_dcycs, dtmp;
	double	dcycs_last_read;
	word32	ret, cyc_shift;
	int	pos, pos2, size, next_size, qtr_track, skip_nibs, track_len;
	int	shift, skip, cycs_this_nib, cycs_passed;

	g_iwm.previous_write_bits = 0;
	qtr_track = dsk->cur_qtr_track;
	cyc_shift = 1 + dsk->disk_525;		// 2 for 3.5", 4 cycs for 5.25"
	if(dsk->disk_525) {
		// HACK: fix for Woz
		qtr_track = qtr_track & -4;	// round to nearest whole trk
	}

	trk = 0;
	track_len = 0;
	if(dsk->trks) {
		trk = &(dsk->trks[qtr_track]);
		track_len = trk->track_len;
	}

	dcycs_last_read = dsk->dcycs_last_read;
	dcycs_passed = dcycs - dcycs_last_read;

	cycs_passed = (int)dcycs_passed;

	if(track_len == 0) {
		ret = (cycs_passed & 0x7f) | 0x80;
		iwm_printf("Reading c0ec, track_len 0, returning %02x\n", ret);
		return ret;
	}

	pos = dsk->nib_pos;
	if(pos >= track_len) {
		/* Arm may have moved from inner 3.5 track to outer one, */
		/*  and so must make pos fit on smaller sized track */
		pos = 0;
	}

	size = trk->nib_area[pos];

	while(size == 0) {
		pos += 2;
		if(pos >= track_len) {
			pos = 0;
		}
		size = trk->nib_area[pos];
	}

	cycs_this_nib = size << cyc_shift;	// default to 2 cycs per bit
	dcycs_this_nib = (double)cycs_this_nib;

	if(fast_disk_emul) {
		cycs_passed = cycs_this_nib;
		dcycs_passed = dcycs_this_nib;

		/* pull a trick to make disk motor-on test pass ($bd34 RWTS) */
		/*  if this would be a sync byte, and we didn't just do this */
		/*  then don't return whole byte */
		/* BUT, don't do this if g_fast_disk_unnib, since it will */
		/*  cause the dsk->unix routines to break */
		if((size > 8) && !g_fast_disk_unnib && (g_iwm_fake_fast == 0)) {
			cycs_passed = cycs_passed >> 1;
			dcycs_passed = dcycs_passed * 0.5;
			g_iwm_fake_fast = 1;
		} else {
			g_iwm_fake_fast = 0;
		}
	}

	skip = 0;
	if(cycs_passed >= (cycs_this_nib + 11)) {
		/* skip some bits? */
		skip = 1;
		if(g_iwm.iwm_mode & 1) {
			/* latch mode */

			pos2 = pos + 2;
			if(pos2 >= track_len) {
				pos2 = 0;
			}
			next_size = trk->nib_area[pos2];
			while(next_size == 0) {
				pos2 += 2;
				if(pos2 >= track_len) {
					pos2 = 0;
				}
				next_size = trk->nib_area[pos2];
			}

			dcycs_next_nib = next_size << cyc_shift;

			if(dcycs_passed < (dcycs_this_nib + dcycs_next_nib)) {
				skip = 0;
			}
		}
	}

	if(skip) {
		iwm_printf("skip since cycs_passed: %f, cycs_this_nib: %f\n",
			dcycs_passed, dcycs_this_nib);

		track_dcycs = (double)((track_len * 8) << cyc_shift);

		if(dcycs_passed >= track_dcycs) {
			dtmp = (int)(dcycs_passed / track_dcycs);
			dcycs_passed = dcycs_passed -
					(dtmp * track_dcycs);
			dcycs_last_read += (dtmp * track_dcycs);
		}
		if((dcycs_passed < 0.0) || (dcycs_passed >= track_dcycs)) {
			dcycs_passed = 0.0;		// Ensure it's valid
		}

		cycs_passed = (int)dcycs_passed;

		skip_nibs = ((word32)cycs_passed) >> (3 + cyc_shift);

		pos += skip_nibs * 2;
		while(pos >= track_len) {
			pos -= track_len;
		}

		dcycs_last_read += (skip_nibs << (3 + cyc_shift));

		dsk->dcycs_last_read = dcycs_last_read;

		size = trk->nib_area[pos];
		dcycs_passed = dcycs - dcycs_last_read;
		if(dcycs_passed < 0.0 || dcycs_passed > 64.0) {
			halt_printf("skip, last_read:%f, dcycs:%f, dcyc_p:%f\n",
				dcycs_last_read, dcycs, dcycs_passed);
		}

		while(size == 0) {
			pos += 2;
			if(pos >= track_len) {
				pos = 0;
			}
			size = trk->nib_area[pos];
		}

		cycs_this_nib = size << cyc_shift;
		cycs_passed = (int)dcycs_passed;
		dcycs_this_nib = cycs_this_nib;
	}

	if(cycs_passed < cycs_this_nib) {
		/* partial */
#if 0
		iwm_printf("Disk partial, %f < %f, size: %d\n",
					dcycs_passed, dcycs_this_nib, size);
#endif
		shift = cycs_passed >> cyc_shift;
		ret = trk->nib_area[pos+1] >> (size - shift);
		if(ret & 0x80) {
			halt_printf("Bad shift in partial read: %02x, but "
				"c_pass:%f, this_nib:%f, shift: %d, size: %d\n",
				ret, dcycs_passed, dcycs_this_nib, shift, size);
		}
	} else {
		/* whole thing */
		ret = trk->nib_area[pos+1];
		pos += 2;
		if(pos >= track_len) {
			pos = 0;
		}
		if(!fast_disk_emul) {
			dsk->dcycs_last_read = dcycs_last_read + dcycs_this_nib;
		} else {
			dsk->dcycs_last_read = dcycs;
		}
	}

	dsk->nib_pos = pos;
	if(pos < 0 || pos > track_len) {
		halt_printf("I just set nib_pos: %d!\n", pos);
	}

#if 0
	iwm_printf("Disk read, returning: %02x\n", ret);
#endif

	return ret;
}


void
iwm_write_data(Disk *dsk, word32 val, int fast_disk_emul, double dcycs)
{
	double	dcycs_last_read, dcycs_this_nib, dcycs_passed;
	word32	mask, prev_val, cyc_shift;
	int	sdiff, bits_read, prev_bits;

	if(dsk->fd < 0 || dsk->trks == 0) {
		halt_printf("Tried to write to type: %d, drive: %d, fd: %d!\n",
			dsk->disk_525, dsk->drive, dsk->fd, dsk->trks);
		return;
	}

	cyc_shift = 1 + dsk->disk_525;		// 1 for 3.5", 2 for 5.25"
	dcycs_last_read = dsk->dcycs_last_read;

	dcycs_passed = dcycs - dcycs_last_read;

	prev_val = g_iwm.previous_write_val;
	prev_bits = g_iwm.previous_write_bits;
	mask = 0x100;
	iwm_printf("Iwm write: prev: %x,%d, new:%02x\n", prev_val, prev_bits,
							val);

	if(dsk->disk_525) {
		/* Activate slow write emulation mode */
		g_dcycs_end_emul_wr = dcycs + 64.0;
		if(!g_slow_525_emul_wr) {
			engine_recalc_events();
			g_slow_525_emul_wr = 1;
			if(fast_disk_emul) {
				iwm_fastemul_start_write(dsk, dcycs_passed,
								dcycs);
			}
		}
	} else {
		/* disable slow writes on 3.5" drives */
		if(g_slow_525_emul_wr) {
			engine_recalc_events();
			g_slow_525_emul_wr = 0;
		}
	}

	if(g_iwm.iwm_mode & 2) {
		/* async mode = 3.5" default */
		bits_read = 8;
	} else {
		/* sync mode, 5.25" drives */
		bits_read = ((int)dcycs_passed) >> cyc_shift;
		if(bits_read < 8) {
			bits_read = 8;
		}
	}

	if(fast_disk_emul) {
		bits_read = 8;
	}

	dcycs_this_nib = bits_read << cyc_shift;

	if(fast_disk_emul) {
		dcycs_passed = dcycs_this_nib;
	}

	if(prev_bits > 0) {
		while(((prev_val & 0x80) == 0) && (bits_read > 0)) {
			/* previous byte needs some bits */
			mask = mask >> 1;
			prev_val = (prev_val << 1) + ((val & mask) !=0);
			prev_bits++;
			bits_read--;
		}
	}

	val = val & (mask - 1);
	if(prev_bits) {
		/* force out prev_val if it had many bits before */
		/*  this prevents writes of 0 from messing us up */
		if(((prev_val & 0x80) == 0) && (prev_bits < 10)) {
			/* special case: we still don't have enough to go */
			iwm_printf("iwm_write: zip2: %02x, %d, left:%02x,%d\n",
					prev_val, prev_bits, val, bits_read);
			val = prev_val;
			bits_read = prev_bits;
		} else {
			iwm_printf("iwm_write: prev: %02x, %d, left:%02x, %d\n",
				prev_val, prev_bits, val, bits_read);
			disk_nib_out_raw(dsk, prev_val, prev_bits, dcycs);
		}
	} else if(val & 0x80) {
		iwm_printf("iwm_write: new: %02x, %d\n", val, bits_read);
		disk_nib_out_raw(dsk, val, bits_read, dcycs);
		bits_read = 0;
	} else {
		iwm_printf("iwm_write: zip: %02x, %d, left:%02x,%d\n",
			prev_val, prev_bits, val, bits_read);
	}

	g_iwm.previous_write_val = val;
	g_iwm.previous_write_bits = bits_read;
	if(bits_read < 0) {
		halt_printf("iwm, bits_rd:%d, val:%08x, prev:%02x, prevb:%d\n",
			bits_read, val, prev_val, prev_bits);
	}

	sdiff = dcycs - dcycs_last_read;
	if(sdiff < (dcycs_this_nib) || (sdiff > (2*dcycs_this_nib)) ) {
		dsk->dcycs_last_read = dcycs;
	} else {
		dsk->dcycs_last_read = dcycs_last_read + dcycs_this_nib;
	}
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

	val1 = iwm_read_data(dsk, 1, 0);
	val2 = iwm_read_data(dsk, 1, 0);

	return ((val1 << 1) + 1) & val2;
}

int
iwm_denib_track525(Disk *dsk, Trk *trk, int qtr_track, byte *outbuf)
{
	byte	aux_buf[0x80];
	int	sector_done[16];
	byte	*buf;
	word32	val, val2, prev_val;
	int	track_len, vol, track, phys_sec, log_sec, cksum, x, my_nib_cnt;
	int	save_qtr_track, save_nib_pos, tmp_nib_pos, status;
	int	num_sectors_done;
	int	i;

	save_qtr_track = dsk->cur_qtr_track;
	save_nib_pos = dsk->nib_pos;

	iwm_move_to_track(dsk, qtr_track);

	dsk->nib_pos = 0;
	g_fast_disk_unnib = 1;

	track_len = trk->track_len;

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
			val = iwm_read_data(dsk, 1, 0);
			continue;
		}

		val = iwm_read_data(dsk, 1, 0);
		if(val != 0xaa) {
			continue;
		}

		val = iwm_read_data(dsk, 1, 0);
		if(val != 0x96) {
			continue;
		}

		/* It's a sector start */
		vol = disk_unnib_4x4(dsk);
		track = disk_unnib_4x4(dsk);
		phys_sec = disk_unnib_4x4(dsk);
		if(phys_sec < 0 || phys_sec > 15) {
			printf("Track %02x, read sec as %02x\n", qtr_track>>2,
				phys_sec);
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
				"%02x %02x %02x\n",
				qtr_track>>2, vol, track, phys_sec, cksum);
			break;
		}

		/* see what sector it is */
		if(track != (qtr_track>>2) || (phys_sec < 0)||(phys_sec > 15)) {
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
				val = iwm_read_data(dsk, 1, 0);
				continue;
			}

			val = iwm_read_data(dsk, 1, 0);
			if(val != 0xaa) {
				continue;
			}

			val = iwm_read_data(dsk, 1, 0);
			if(val != 0xad) {
				continue;
			}

			/* got it, just break */
			break;
		}

		if(i >= NIBS_FROM_ADDR_TO_DATA) {
			printf("No data header, track %02x, sec %02x\n",
				qtr_track>>2, phys_sec);
			printf("nib_pos: %08x\n", dsk->nib_pos);
			break;
		}

		buf = outbuf + 0x100*log_sec;

		/* Data start! */
		prev_val = 0;
		for(i = 0x55; i >= 0; i--) {
			val = iwm_read_data(dsk, 1, 0);
			val2 = g_from_disk_byte[val];
			if(val2 >= 0x100) {
				printf("Bad data area1, val:%02x,val2:%03x\n",
								val, val2);
				printf(" i:%03x,n_pos:%04x\n", i, dsk->nib_pos);
				break;
			}
			prev_val = val2 ^ prev_val;
			aux_buf[i] = prev_val;
		}

		/* rest of data area */
		for(i = 0; i < 0x100; i++) {
			val = iwm_read_data(dsk, 1, 0);
			val2 = g_from_disk_byte[val];
			if(val2 >= 0x100) {
				printf("Bad data area2, read: %02x\n", val);
				printf("  nib_pos: %04x\n", dsk->nib_pos);
				break;
			}
			prev_val = val2 ^ prev_val;
			buf[i] = prev_val;
		}

		/* checksum */
		val = iwm_read_data(dsk, 1, 0);
		val2 = g_from_disk_byte[val];
		if(val2 >= 0x100) {
			printf("Bad data area3, read: %02x\n", val);
			printf("  nib_pos: %04x\n", dsk->nib_pos);
			break;
		}
		if(val2 != prev_val) {
			printf("Bad data cksum, got %02x, wanted: %02x\n",
				val2, prev_val);
			printf("  nib_pos: %04x\n", dsk->nib_pos);
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

	tmp_nib_pos = dsk->nib_pos;
	iwm_move_to_track(dsk, save_qtr_track);
	dsk->nib_pos = save_nib_pos;
	g_fast_disk_unnib = 0;

	if(status == 0) {
		return 1;
	}

	printf("Nibblization not done, %02x sectors found on track %02x\n",
		num_sectors_done, qtr_track>>2);
	printf("my_nib_cnt: %05x, nib_pos:%05x, trk_len:%05x\n", my_nib_cnt,
		tmp_nib_pos, track_len);
	for(i = 0; i < 16; i++) {
		printf("sector_done[%d] = %d\n", i, sector_done[i]);
	}
	return -1;
}

int
iwm_denib_track35(Disk *dsk, Trk *trk, int qtr_track, byte *outbuf)
{
	word32	buf_c00[0x100];
	word32	buf_d00[0x100];
	word32	buf_e00[0x100];
	int	sector_done[16];
	byte	*buf;
	word32	tmp_5c, tmp_5d, tmp_5e, tmp_66, tmp_67, val, val2;
	int	num_sectors_done, track_len, phys_track, phys_sec, phys_side;
	int	phys_capacity, cksum, tmp, track, side, num_sectors, x, y;
	int	carry, my_nib_cnt, save_qtr_track, save_nib_pos, status;
	int	i;

	save_qtr_track = dsk->cur_qtr_track;
	save_nib_pos = dsk->nib_pos;

	iwm_move_to_track(dsk, qtr_track);

	dsk->nib_pos = 0;
	g_fast_disk_unnib = 1;

	track_len = trk->track_len;

	num_sectors = g_track_bytes_35[qtr_track >> 5] >> 9;

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
			val = iwm_read_data(dsk, 1, 0);
			continue;
		}

		val = iwm_read_data(dsk, 1, 0);
		if(val != 0xaa) {
			continue;
		}

		val = iwm_read_data(dsk, 1, 0);
		if(val != 0x96) {
			continue;
		}

		/* It's a sector start */
		val = iwm_read_data(dsk, 1, 0);
		phys_track = g_from_disk_byte[val];
		if(phys_track != (track & 0x3f)) {
			printf("Track %02x.%d, read track %02x, %02x\n",
				track, side, phys_track, val);
			break;
		}

		phys_sec = g_from_disk_byte[iwm_read_data(dsk, 1, 0)];
		if(phys_sec < 0 || phys_sec >= num_sectors) {
			printf("Track %02x.%d, read sector %02x??\n",
				track, side, phys_sec);
			break;
		}
		phys_side = g_from_disk_byte[iwm_read_data(dsk, 1, 0)];

		if(phys_side != ((side << 5) + (track >> 6))) {
			printf("Track %02x.%d, read side %02x??\n",
				track, side, phys_side);
			break;
		}
		phys_capacity = g_from_disk_byte[iwm_read_data(dsk, 1, 0)];
		if(phys_capacity != 0x24 && phys_capacity != 0x22) {
			printf("Track %02x.%x capacity: %02x != 0x24/22\n",
				track, side, phys_capacity);
		}
		cksum = g_from_disk_byte[iwm_read_data(dsk, 1, 0)];

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
			val = iwm_read_data(dsk, 1, 0);
			if(val == 0xd5) {
				break;
			}
		}
		if(val != 0xd5) {
			printf("No data header, track %02x.%x, sec %02x\n",
				track, side, phys_sec);
			break;
		}

		val = iwm_read_data(dsk, 1, 0);
		if(val != 0xaa) {
			printf("Bad data hdr1,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			printf("nib_pos: %08x\n", dsk->nib_pos);
			break;
		}

		val = iwm_read_data(dsk, 1, 0);
		if(val != 0xad) {
			printf("Bad data hdr2,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		buf = outbuf + (phys_sec << 9);

		/* check sector again */
		val = g_from_disk_byte[iwm_read_data(dsk, 1, 0)];
		if(val != phys_sec) {
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
			val = iwm_read_data(dsk, 1, 0);
			val2 = g_from_disk_byte[val];
			if(val2 >= 0x100) {
				printf("Bad data area1b, read: %02x\n", val);
				printf(" i:%03x,n_pos:%04x\n", i, dsk->nib_pos);
				break;
			}
			tmp_66 = val2;

			tmp_5c = tmp_5c << 1;
			carry = (tmp_5c >> 8);
			tmp_5c = (tmp_5c + carry) & 0xff;

			val = iwm_read_data(dsk, 1, 0);
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
			val = iwm_read_data(dsk, 1, 0);
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
			val = iwm_read_data(dsk, 1, 0);
			val2 = g_from_disk_byte[val];
			val2 = val2 + ((tmp_66 << 6) & 0xc0);
			val2 = val2 ^ tmp_5d;
			buf_e00[y+1] = val2;

			tmp_5c = val2 + tmp_5c + carry;
			carry = (tmp_5c >> 8);
			tmp_5c = tmp_5c & 0xff;
		}

/* 62d0 */
		val = iwm_read_data(dsk, 1, 0);
		val2 = g_from_disk_byte[val];

		tmp_66 = (val2 << 6) & 0xc0;
		tmp_67 = (val2 << 4) & 0xc0;
		val2 = (val2 << 2) & 0xc0;

		val = iwm_read_data(dsk, 1, 0);
		val2 = g_from_disk_byte[val] + val2;
		if(tmp_5e != (word32)val2) {
			printf("Checksum 5e bad: %02x vs %02x\n", tmp_5e, val2);
			printf("val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		val = iwm_read_data(dsk, 1, 0);
		val2 = g_from_disk_byte[val] + tmp_67;
		if(tmp_5d != (word32)val2) {
			printf("Checksum 5d bad: %02x vs %02x\n", tmp_5e, val2);
			printf("val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		val = iwm_read_data(dsk, 1, 0);
		val2 = g_from_disk_byte[val] + tmp_66;
		if(tmp_5c != (word32)val2) {
			printf("Checksum 5c bad: %02x vs %02x\n", tmp_5e, val2);
			printf("val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			break;
		}

		/* Whew, got it!...check for DE AA */
		val = iwm_read_data(dsk, 1, 0);
		if(val != 0xde) {
			printf("Bad data epi1,val:%02x trk %02x.%x, sec %02x\n",
				val, track, side, phys_sec);
			printf("nib_pos: %08x\n", dsk->nib_pos);
			break;
		}

		val = iwm_read_data(dsk, 1, 0);
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
		printf("dsk->nib_pos: %04x, status: %d\n", dsk->nib_pos,
			status);
		for(i = 0; i < num_sectors; i++) {
			printf("sector done[%d] = %d\n", i, sector_done[i]);
		}
	}

	iwm_move_to_track(dsk, save_qtr_track);
	dsk->nib_pos = save_nib_pos;
	g_fast_disk_unnib = 0;

	if(status == 0) {
		return 1;
	}

	printf("Nibblization not done, %02x sectors found on track %02x\n",
		num_sectors_done, qtr_track>>2);
	return -1;
}

/* ret = 1 -> dirty data written out */
/* ret = 0 -> not dirty, no error */
/* ret < 0 -> error */
int
disk_track_to_unix(Disk *dsk, int qtr_track, byte *outbuf)
{
	Trk	*trk;
	int	disk_525;

	disk_525 = dsk->disk_525;

	trk = &(dsk->trks[qtr_track]);

	if(trk->track_len == 0 || trk->track_dirty == 0) {
		return 0;
	}

	trk->track_dirty = 0;

	if((qtr_track & 3) && disk_525) {
		halt_printf("You wrote to phase %02x!  Can't wr bk to unix!\n",
			qtr_track);
		dsk->write_through_to_unix = 0;
		return -1;
	}

	if(disk_525) {
		return iwm_denib_track525(dsk, trk, qtr_track, outbuf);
	} else {
		return iwm_denib_track35(dsk, trk, qtr_track, outbuf);
	}
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
disk_check_nibblization(Disk *dsk, int qtr_track, byte *buf, int size)
{
	byte	buffer[0x3000];
	Trk	*trk;
	int	ret, ret2;
	int	i;

	if(size > 0x3000) {
		printf("size %08x is > 0x3000, disk_check_nibblization\n",size);
		exit(3);
	}

	for(i = 0; i < size; i++) {
		buffer[i] = 0;
	}

	trk = &(dsk->trks[qtr_track]);

	if(dsk->disk_525) {
		ret = iwm_denib_track525(dsk, trk, qtr_track, &(buffer[0]));
	} else {
		ret = iwm_denib_track35(dsk, trk, qtr_track, &(buffer[0]));
	}

	ret2 = -1;
	for(i = 0; i < size; i++) {
		if(buffer[i] != buf[i]) {
			printf("buffer[%04x]: %02x != %02x\n", i, buffer[i],
				buf[i]);
			ret2 = i;
			break;
		}
	}

	if(ret != 1 || ret2 >= 0) {
		printf("disk_check_nib ret:%d, ret2:%d for q_track %03x\n",
			ret, ret2, qtr_track);
		show_hex_data(buf, 0x1000);
		show_hex_data(buffer, 0x1000);
		iwm_show_a_track(dsk, &(dsk->trks[qtr_track]));

		exit(2);
	}
}


#define TRACK_BUF_LEN		0x2000

void
disk_unix_to_nib(Disk *dsk, int qtr_track, int unix_pos, int unix_len,
	int nib_len)
{
	byte	track_buf[TRACK_BUF_LEN];
	Trk	*trk;
	byte	*bptr;
	int	must_clear_track, ret, len;
	int	i;

	/* Read track from dsk int track_buf */

	must_clear_track = 0;

	if(unix_len > TRACK_BUF_LEN) {
		printf("diks_unix_to_nib: requested len of image %s = %05x\n",
			dsk->name_ptr, unix_len);
	}

	bptr = dsk->raw_data;
	if((unix_pos >= 0) && (dsk->raw_data != 0)) {
		// raw_data is valid, so use it
		if(unix_pos + unix_len > dsk->raw_size) {
			must_clear_track = 1;
		} else {
			bptr += unix_pos;
			for(i = 0; i < unix_len; i++) {
				track_buf[i] = bptr[i];
			}
		}
	} else if(unix_pos >= 0) {
		ret = (int)lseek(dsk->fd, unix_pos, SEEK_SET);
		if(ret != unix_pos) {
			printf("lseek of disk %s len 0x%x ret: %d, errno: %d\n",
				dsk->name_ptr, unix_pos, ret, errno);
			must_clear_track = 1;
		}

		len = (int)read(dsk->fd, track_buf, unix_len);
		if(len != unix_len) {
			printf("read of disk %s q_trk %d ret: %d, errno: %d\n",
				dsk->name_ptr, qtr_track, ret, errno);
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

	dsk->nib_pos = 0;		/* for consistency */

	trk = &(dsk->trks[qtr_track]);
	trk->track_dirty = 0;
	trk->overflow_size = 0;
	trk->track_len = 2*nib_len;
	trk->unix_pos = unix_pos;
	trk->unix_len = unix_len;
	trk->nib_area = (byte *)malloc(trk->track_len);

	/* create nibblized image */

	if(dsk->disk_525 && dsk->image_type == DSK_TYPE_NIB) {
		iwm_nibblize_track_nib525(dsk, trk, track_buf, qtr_track);
	} else if(dsk->disk_525) {
		iwm_nibblize_track_525(dsk, trk, track_buf, qtr_track);
	} else {
		iwm_nibblize_track_35(dsk, trk, track_buf, qtr_track);
	}
}

void
iwm_nibblize_track_nib525(Disk *dsk, Trk *trk, byte *track_buf, int qtr_track)
{
	byte	*nib_ptr;
	byte	*trk_ptr;
	int	len;
	int	i;

	len = trk->track_len;
	trk_ptr = track_buf;
	nib_ptr = &(trk->nib_area[0]);
	for(i = 0; i < len; i += 2) {
		nib_ptr[i] = 8;
		nib_ptr[i+1] = *trk_ptr++;;
	}

	iwm_printf("Nibblized q_track %02x\n", qtr_track);
}

void
iwm_nibblize_track_525(Disk *dsk, Trk *trk, byte *track_buf, int qtr_track)
{
	byte	partial_nib_buf[0x300];
	byte	*bptr;
	word32	val, last_val;
	int	phys_sec, log_sec, num_sync;
	int	i;


	bptr = (byte *)&(trk->nib_area[0]);
	for(i = 0; i < trk->track_len; i += 2) {
		bptr[0] = 8;
		bptr[1] = 0xff;
		bptr += 2;
	}

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
		disk_nib_out(dsk, 0xd5, 10);		/* prolog */
		disk_nib_out(dsk, 0xaa, 8);		/* prolog */
		disk_nib_out(dsk, 0x96, 8);		/* prolog */
		disk_4x4_nib_out(dsk, dsk->vol_num);
		disk_4x4_nib_out(dsk, qtr_track >> 2);
		disk_4x4_nib_out(dsk, phys_sec);
		disk_4x4_nib_out(dsk, dsk->vol_num ^ (qtr_track>>2) ^ phys_sec);
		disk_nib_out(dsk, 0xde, 8);		/* epi */
		disk_nib_out(dsk, 0xaa, 8);		/* epi */
		disk_nib_out(dsk, 0xeb, 8);		/* epi */

		/* Inter sync */
		disk_nib_out(dsk, 0xff, 8);
		for(i = 0; i < 6; i++) {
			disk_nib_out(dsk, 0xff, 10);
		}
		disk_nib_out(dsk, 0xd5, 10);	/* data prolog */
		disk_nib_out(dsk, 0xaa, 8);	/* data prolog */
		disk_nib_out(dsk, 0xad, 8);	/* data prolog */

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
		disk_nib_out(dsk, 0xde, 8);	/* epi */
		disk_nib_out(dsk, 0xaa, 8);	/* epi */
		disk_nib_out(dsk, 0xeb, 8);	/* epi */
	}

	/* finish nibblization */
	disk_nib_end_track(dsk);

	iwm_printf("Nibblized q_track %02x\n", qtr_track);

	if(g_check_nibblization) {
		disk_check_nibblization(dsk, qtr_track, &(track_buf[0]),0x1000);
	}
}

void
iwm_nibblize_track_35(Disk *dsk, Trk *trk, byte *track_buf, int qtr_track)
{
	int	phys_to_log_sec[16];
	word32	buf_c00[0x100];
	word32	buf_d00[0x100];
	word32	buf_e00[0x100];
	byte	*buf, *bptr;
	word32	val, phys_track, phys_side, capacity, cksum, acc_hi;
	word32	tmp_5c, tmp_5d, tmp_5e, tmp_5f, tmp_63, tmp_64, tmp_65;
	int	num_sectors, unix_len, log_sec, track, side, num_sync, carry;
	int	interleave, x, y;
	int	i, phys_sec;

	bptr = (byte *)&(trk->nib_area[0]);
	if(trk->track_len & 3) {
		halt_printf("track_len: %08x is not a multiple of 4\n",
				trk->track_len);
	}

	for(i = 0; i < trk->track_len; i += 2) {
		bptr[0] = 8;
		bptr[1] = 0xff;
		bptr += 2;
	}

	unix_len = trk->unix_len;

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


	disk_nib_end_track(dsk);

	if(g_check_nibblization) {
		disk_check_nibblization(dsk, qtr_track, &(track_buf[0]),
			unix_len);
	}
}

void
disk_4x4_nib_out(Disk *dsk, word32 val)
{
	disk_nib_out(dsk, 0xaa | (val >> 1), 8);
	disk_nib_out(dsk, 0xaa | val, 8);
}

void
disk_nib_out(Disk *dsk, byte val, int size)
{
	disk_nib_out_raw(dsk, val, size, 0.0);
}

void
disk_nib_out_raw(Disk *dsk, byte val, int size, double dcycs)
{
	Trk	*trk;
	int	pos, old_size, track_len, overflow_size, qtr_track;

	qtr_track = dsk->cur_qtr_track;

	track_len = 0;
	trk = 0;
	if(dsk->trks != 0) {
		trk = &(dsk->trks[qtr_track]);
		track_len = trk->track_len;
	}

	if(track_len <= 10) {
		printf("Writing to an invalid qtr track: %02x!\n", qtr_track);
		printf("name: %s, track_len: %08x, val: %08x, size: %d\n",
			dsk->name_ptr, track_len, val, size);
		exit(1);
		return;
	}

	trk->track_dirty = 1;
	dsk->disk_dirty = 1;

	pos = dsk->nib_pos;
	overflow_size = trk->overflow_size;
	if(pos >= track_len) {
		pos = 0;
	}
	if(dcycs != 0.0) {
		dbg_log_info(dcycs, (pos << 16) | (qtr_track << 8) |
				(overflow_size & 0xff),
				(track_len << 16) | (size << 8) | val);
	}

	old_size = trk->nib_area[pos];

	while(size >= (10 + old_size)) {
		size = size - old_size;
		pos += 2;
		if(pos >= track_len) {
			pos = 0;
		}
		old_size = trk->nib_area[pos];
	}

	if(size > 10) {
		size = 10;
	}

	if((val & 0x80) == 0) {
		val |= 0x80;
	}

	trk->nib_area[pos++] = size;
	trk->nib_area[pos++] = val;
	if(pos >= track_len) {
		pos = 0;
	}

	overflow_size += (size - old_size);
	if((overflow_size > 8) && (size > 8)) {
		overflow_size -= trk->nib_area[pos];
		trk->nib_area[pos++] = 0;
		trk->nib_area[pos++] = 0;
		if(pos >= track_len) {
			pos = 0;
		}
	} else if(overflow_size < -64) {
		halt_printf("overflow_sz:%03x, pos:%02x\n",overflow_size,pos);
	}

	dsk->nib_pos = pos;
	trk->overflow_size = overflow_size;

	if((val & 0x80) == 0 || size < 8) {
		halt_printf("disk_nib_out, wrote %02x, size: %d\n", val, size);
	}
}

void
disk_nib_end_track(Disk *dsk)
{
	int	qtr_track;

	dsk->nib_pos = 0;
	qtr_track = dsk->cur_qtr_track;
	dsk->trks[qtr_track].track_dirty = 0;

	dsk->disk_dirty = 0;
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
	int	motor_on;
	int	i;

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

	iwm_flush_disk_to_unix(dsk);

	printf("Ejecting disk: %s\n", dsk->name_ptr);

	/* Free all memory, close file */

	/* free the tracks first */
	if(dsk->trks != 0) {
		for(i = 0; i < dsk->num_tracks; i++) {
			if(dsk->trks[i].nib_area) {
				free(dsk->trks[i].nib_area);
			}
			dsk->trks[i].nib_area = 0;
			dsk->trks[i].track_len = 0;
		}
		free(dsk->trks);
	}
	dsk->num_tracks = 0;
	dsk->trks = 0;

	/* close file, clean up dsk struct */
	if(dsk->fd == 0) {
		free(dsk->raw_data);
	} else {
		close(dsk->fd);
	}

	dsk->fd = -1;
	dsk->raw_size = 0;
	dsk->raw_data = 0;
	dsk->image_start = 0;
	dsk->image_size = 0;
	dsk->nib_pos = 0;
	dsk->disk_dirty = 0;
	dsk->write_through_to_unix = 0;
	dsk->write_prot = 1;
	dsk->just_ejected = 1;

	/* Leave name_ptr valid */
}

void
iwm_show_track(int slot_drive, int track)
{
	Disk	*dsk;
	Trk	*trk;
	int	drive;
	int	sel35;
	int	qtr_track;

	if(slot_drive < 0) {
		drive = g_iwm.drive_select;
		sel35 = (g_c031_disk35 >> 6) & 1;
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

	if(trk->track_len <= 0) {
		dbg_printf("Track_len: %d\n", trk->track_len);
		dbg_printf("No track for type: %d, drive: %d, qtrk:0x%02x\n",
			sel35, drive, qtr_track);
		return;
	}

	dbg_printf("Current slot:%d drive:%d, q_track:0x%02x\n", 6 - sel35,
							drive, qtr_track);

	iwm_show_a_track(dsk, trk);
}

void
iwm_show_a_track(Disk *dsk, Trk *trk)
{
	int	sum;
	int	len;
	int	pos;
	int	i;

	dbg_printf("  Showtrack:dirty: %d, pos: %04x, ovfl: %04x, len: %04x\n",
		trk->track_dirty, dsk->nib_pos,
		trk->overflow_size, trk->track_len);

	len = trk->track_len;
	dbg_printf("Track len in bytes: %04x\n", len);
	if(len >= 2*15000) {
		len = 2*15000;
		dbg_printf("len too big, using %04x\n", len);
	}

	pos = 0;
	for(i = 0; i < len; i += 16) {
		dbg_printf("%04x: %2d,%02x %2d,%02x %2d,%02x %2d,%02x "
			"%2d,%02x %2d,%02x %2d,%02x %2d,%02x\n", pos,
			trk->nib_area[pos], trk->nib_area[pos+1],
			trk->nib_area[pos+2], trk->nib_area[pos+3],
			trk->nib_area[pos+4], trk->nib_area[pos+5],
			trk->nib_area[pos+6], trk->nib_area[pos+7],
			trk->nib_area[pos+8], trk->nib_area[pos+9],
			trk->nib_area[pos+10], trk->nib_area[pos+11],
			trk->nib_area[pos+12], trk->nib_area[pos+13],
			trk->nib_area[pos+14], trk->nib_area[pos+15]);
		pos += 16;
		if(pos >= len) {
			pos -= len;
		}
	}

	sum = 0;
	for(i = 0; i < len; i += 2) {
		sum += trk->nib_area[i];
	}

	dbg_printf("bit_sum: %d, expected: %d, overflow_size: %d\n",
		sum, len*8/2, trk->overflow_size);
}

