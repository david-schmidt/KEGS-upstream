const char rcsid_sound_c[] = "@(#)$KmKId: sound.c,v 1.140 2021-08-01 15:47:37+00 kentd Exp $";

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

#define INCLUDE_RCSID_C
#include "sound.h"
#undef INCLUDE_RCSID_C

#if 0
# define DO_DOC_LOG
#endif

extern int Verbose;
extern int g_use_shmem;
extern word32 g_vbl_count;
extern int g_preferred_rate;

extern word32 g_c03ef_doc_ptr;

extern double g_last_vbl_dcycs;

byte doc_ram[0x10000 + 16];

word32 g_doc_sound_ctl = 0;
word32 g_doc_saved_val = 0;
int	g_doc_num_osc_en = 1;
double	g_dcycs_per_doc_update = 1.0;
double	g_dupd_per_dcyc = 1.0;
double	g_drecip_osc_en_plus_2 = 1.0 / (double)(1 + 2);

int	g_doc_saved_ctl = 0;
int	g_queued_samps = 0;
int	g_queued_nonsamps = 0;
int	g_num_osc_interrupting = 0;

#if defined(HPUX) || defined(__linux__) || defined(_WIN32) || defined(MAC)
int	g_audio_enable = -1;
#else
int	g_audio_enable = 0;		/* Not supported: default to off */
#endif
int	g_sound_min_msecs = 32;			// 32 msecs
int	g_sound_min_msecs_pulse = 150;		// 150 msecs
int	g_sound_max_multiplier = 6;		// 6*32 = ~200 msecs
int	g_sound_min_samples = 48000 * 32/1000;	// 32 msecs

Doc_reg g_doc_regs[32];

word32 doc_reg_e0 = 0xff;

/* local function prototypes */
void doc_write_ctl_reg(int osc, int val, double dsamps);

Mockingboard g_mockingboard;

// The AY8913 chip has non-linear amplitudes (it has 16 levels) and the
//  documentation does not match measured results.  But all the measurements
//  should really be done at the final speaker/jack since all the stuff in
//  the path affects it.  But: no one's done this for Mockingboard that I
//  have found, so I'm taking measurements from the AY8913 chip itself.
// AY8913 amplitudes from https://groups.google.com/forum/#!original/
//				comp.sys.sinclair/-zCR2kxMryY/XgvaDICaldUJ
// by Matthew Westcott on December 21, 2001.
double g_ay8913_ampl_factor_westcott[16] = {		// NOT USED
	0.000,	// level[0]
	0.010,	// level[1]
	0.015,	// level[2]
	0.022,	// level[3]
	0.031,	// level[4]
	0.046,	// level[5]
	0.064,	// level[6]
	0.106,	// level[7]
	0.132,	// level[8]
	0.216,	// level[9]
	0.297,	// level[10]
	0.391,	// level[11]
	0.513,	// level[12]
	0.637,	// level[13]
	0.819,	// level[14]
	1.000,	// level[15]
};
// https://sourceforge.net/p/fuse-emulator/mailman/message/34065660/
//  refers to some Russian-language measurements at:
//  http://forum.tslabs.info/viewtopic.php?f=6&t=539 (translate from
//  Russian), they give:
// 0000,028F,03B3,0564, 07DC,0BA9,1083,1B7C,
// 2068,347A,4ACE,5F72, 7E16,A2A4,CE3A,FFFF
double g_ay8913_ampl_factor[16] = {
	0.000,	// level[0]
	0.010,	// level[1]
	0.014,	// level[2]
	0.021,	// level[3]
	0.031,	// level[4]
	0.046,	// level[5]
	0.064,	// level[6]
	0.107,	// level[7]
	0.127,	// level[8]
	0.205,	// level[9]
	0.292,	// level[10]
	0.373,	// level[11]
	0.493,	// level[12]
	0.635,	// level[13]
	0.806,	// level[14]
	1.000,	// level[15]
};
// MAME also appears to try to figure out how the channels get "summed"
//  together.  KEGS code adds them in a completely independent way, and due
//  to the circuit used on the AY8913, this is certainly incorrect.
#define MAX_MOCK_ENV_SAMPLES	2000
int	g_mock_env_vol[MAX_MOCK_ENV_SAMPLES];
byte	g_mock_noise_bytes[MAX_MOCK_ENV_SAMPLES];
int	g_mock_volume[16];		// Sample for each of the 16 amplitudes

int	g_audio_rate = 0;
double	g_daudio_rate = 0.0;
double	g_drecip_audio_rate = 0.0;
double	g_dsamps_per_dcyc = 0.0;
double	g_dcycs_per_samp = 0.0;
float	g_fsamps_per_dcyc = 0.0;

int	g_doc_vol = 8;

#define MAX_C030_TIMES		18000

double g_last_sound_play_dsamp = 0.0;

float c030_fsamps[MAX_C030_TIMES + 1];
int g_num_c030_fsamps = 0;

#define DOC_SCAN_RATE	(DCYCS_28_MHZ/32.0)

word32	*g_sound_shm_addr = 0;
int	g_sound_shm_pos = 0;

#define LEN_DOC_LOG	128

STRUCT(Doc_log) {
	char	*msg;
	int	osc;
	double	dsamps;
	double	dtmp2;
	int	etc;
	Doc_reg	doc_reg;
};

Doc_log g_doc_log[LEN_DOC_LOG];
int	g_doc_log_pos = 0;

#define DO_DOC_LOG

#ifdef DO_DOC_LOG
# define DOC_LOG(a,b,c,d)	doc_log_rout(a,b,c,d)
#else
# define DOC_LOG(a,b,c,d)
#endif

#define UPDATE_G_DCYCS_PER_DOC_UPDATE(osc_en)				\
	g_dcycs_per_doc_update = (double)((osc_en + 2) * DCYCS_1_MHZ) /	\
		DOC_SCAN_RATE;						\
	g_dupd_per_dcyc = 1.0 / g_dcycs_per_doc_update;			\
	g_drecip_osc_en_plus_2 = 1.0 / (double)(osc_en + 2);

#define SND_PTR_SHIFT		14
#define SND_PTR_SHIFT_DBL	((double)(1 << SND_PTR_SHIFT))

void
doc_log_rout(char *msg, int osc, double dsamps, int etc)
{
	int	pos;

#ifndef DO_DOC_LOG
	return;
#endif
	pos = g_doc_log_pos;
	g_doc_log[pos].msg = msg;
	g_doc_log[pos].osc = osc;
	g_doc_log[pos].dsamps = dsamps;
	g_doc_log[pos].dtmp2 = g_last_sound_play_dsamp;
	g_doc_log[pos].etc = etc;
	if(osc >= 0 && osc < 32) {
		g_doc_log[pos].doc_reg = g_doc_regs[osc];
	}
	pos++;
	if(pos >= LEN_DOC_LOG) {
		pos = 0;
	}

	doc_printf("log: %s, osc:%d dsamp:%f, etc:%d\n", msg, osc, dsamps, etc);

	g_doc_log_pos = pos;
}

extern double g_cur_dcycs;

void
show_doc_log(void)
{
	FILE	*docfile;
	Doc_reg	*rptr;
	double	dsamp_start;
	int	osc, ctl, freq;
	int	pos;
	int	i;

	docfile = fopen("doc_log_out", "w");
	if(docfile == 0) {
		printf("fopen failed, errno: %d\n", errno);
		return;
	}
	pos = g_doc_log_pos;
	fprintf(docfile, "DOC log pos: %d\n", pos);
	dsamp_start = g_doc_log[pos].dsamps;
	for(i = 0; i < LEN_DOC_LOG; i++) {
		rptr = &(g_doc_log[pos].doc_reg);
		osc = g_doc_log[pos].osc;
		ctl = rptr->ctl;
		freq = rptr->freq;
		if(osc < 0) {
			ctl = 0;
			freq = 0;
		}
		fprintf(docfile, "%03x:%03x: %-11s ds:%11.1f dt2:%10.1f "
			"etc:%08x o:%02x c:%02x fq:%04x\n",
			i, pos, g_doc_log[pos].msg,
			g_doc_log[pos].dsamps - dsamp_start,
			g_doc_log[pos].dtmp2,
			g_doc_log[pos].etc, osc & 0xff, ctl, freq);
		if(osc >= 0) {
			fprintf(docfile, "          ire:%d,%d,%d ptr4:%08x "
				"inc4:%08x comp_ds:%.1f left:%04x, vol:%02x "
				"wptr:%02x, wsz:%02x, 4st:%08x, 4end:%08x\n",
				rptr->has_irq_pending, rptr->running,
				rptr->event, 4*rptr->cur_acc, 4*rptr->cur_inc,
				rptr->complete_dsamp - dsamp_start,
				rptr->samps_left, rptr->vol, rptr->waveptr,
				rptr->wavesize, 4*rptr->cur_start,
				4*rptr->cur_end);
		}
		pos++;
		if(pos >= LEN_DOC_LOG) {
			pos = 0;
		}
	}

	fprintf(docfile, "cur_dcycs: %f\n", g_cur_dcycs);
	fprintf(docfile, "dsamps_now: %f\n",
		(g_cur_dcycs * g_dsamps_per_dcyc) - dsamp_start);
	fprintf(docfile, "g_doc_num_osc_en: %d\n", g_doc_num_osc_en);
	fclose(docfile);
}

void
sound_init()
{
	Doc_reg	*rptr;
	int	i;

	for(i = 0; i < 32; i++) {
		rptr = &(g_doc_regs[i]);
		rptr->dsamp_ev = 0.0;
		rptr->dsamp_ev2 = 0.0;
		rptr->complete_dsamp = 0.0;
		rptr->samps_left = 0;
		rptr->cur_acc = 0;
		rptr->cur_inc = 0;
		rptr->cur_start = 0;
		rptr->cur_end = 0;
		rptr->cur_mask = 0;
		rptr->size_bytes = 0;
		rptr->event = 0;
		rptr->running = 0;
		rptr->has_irq_pending = 0;
		rptr->freq = 0;
		rptr->vol = 0;
		rptr->waveptr = 0;
		rptr->ctl = 1;
		rptr->wavesize = 0;
		rptr->last_samp_val = 0;
	}

	snddrv_init();
}

void
sound_set_audio_rate(int rate)
{
	g_audio_rate = rate;
	g_daudio_rate = (rate)*1.0;
	g_drecip_audio_rate = 1.0/(rate);
	g_dsamps_per_dcyc = ((rate*1.0) / DCYCS_1_MHZ);
	g_dcycs_per_samp = (DCYCS_1_MHZ / (rate*1.0));
	g_fsamps_per_dcyc = (float)((rate*1.0) / DCYCS_1_MHZ);
	g_sound_min_samples = rate * g_sound_min_msecs / 1000;

	printf("Set g_audio_rate = %d in main KEGS process, min_samples:%d\n",
		rate, g_sound_min_samples);
}

void
sound_reset(double dcycs)
{
	double	dsamps;
	int	i;

	dsamps = dcycs * g_dsamps_per_dcyc;
	for(i = 0; i < 32; i++) {
		doc_write_ctl_reg(i, g_doc_regs[i].ctl | 1, dsamps);
		doc_reg_e0 = 0xff;
		if(g_doc_regs[i].has_irq_pending) {
			halt_printf("reset: has_irq[%02x] = %d\n", i,
				g_doc_regs[i].has_irq_pending);
		}
		g_doc_regs[i].has_irq_pending = 0;
	}
	if(g_num_osc_interrupting) {
		halt_printf("reset: num_osc_int:%d\n", g_num_osc_interrupting);
	}
	g_num_osc_interrupting = 0;

	g_doc_num_osc_en = 1;
	UPDATE_G_DCYCS_PER_DOC_UPDATE(1);
	mockingboard_reset(dcycs);
}

void
sound_shutdown()
{
	snddrv_shutdown();
}

void
sound_update(double dcycs)
{
	double	dsamps;
	/* Called every VBL time to update sound status */

	/* "play" sounds for this vbl */

	dsamps = dcycs * g_dsamps_per_dcyc;
	DOC_LOG("do_snd_pl", -1, dsamps, 0);
	sound_play(dsamps);
}

#define MAX_SND_BUF	65536

int g_samp_buf[2*MAX_SND_BUF];
word32 zero_buf[SOUND_SHM_SAMP_SIZE];

double g_doc_dsamps_extra = 0.0;

float	g_fvoices = 0.0;

word32 g_cycs_in_sound1 = 0;
word32 g_cycs_in_sound2 = 0;
word32 g_cycs_in_sound3 = 0;
word32 g_cycs_in_sound4 = 0;
word32 g_cycs_in_start_sound = 0;
word32 g_cycs_in_est_sound = 0;

int	g_num_snd_plays = 0;
int	g_num_doc_events = 0;
int	g_num_start_sounds = 0;
int	g_num_scan_osc = 0;
int	g_num_recalc_snd_parms = 0;

word32	g_last_mock_vbl_count = 0;
word32	g_last_c030_vbl_count = 0;
int	g_c030_state = 0;

#define VAL_C030_RANGE		(32768)
#define VAL_C030_BASE		(-16384)

#define VAL_MOCK_RANGE		(39000)

int	g_sound_file_num = 0;
int	g_sound_file_fd = -1;
int	g_send_sound_to_file = 0;
int	g_send_file_bytes = 0;

void
open_sound_file()
{
	char	name[256];
	int	fd;

	sprintf(name, "snd.out.%d", g_sound_file_num);

	fd = open(name, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0x1ff);
	if(fd < 0) {
		printf("open_sound_file open ret: %d, errno: %d\n", fd, errno);
		exit(1);
	}

	g_sound_file_fd = fd;
	g_sound_file_num++;
	g_send_file_bytes = 0;
}

void
close_sound_file()
{
	if(g_sound_file_fd >= 0) {
		close(g_sound_file_fd);
	}

	g_sound_file_fd = -1;
}

void
check_for_range(word32 *addr, int num_samps, int offset)
{
	short	*shortptr;
	int	i;
	int	left;
	int	right;
	int	max;

	max = -32768;

	if(num_samps > SOUND_SHM_SAMP_SIZE) {
		halt_printf("num_samps: %d > %d!\n", num_samps,
							SOUND_SHM_SAMP_SIZE);
	}

	for(i = 0; i < num_samps; i++) {
		shortptr = (short *)&(addr[i]);
		left = shortptr[0];
		right = shortptr[1];
		if((left > 0x3000) || (right > 0x3000)) {
			halt_printf("Sample %d of %d at snd_buf: %p is: "
				"%d/%d\n", i + offset, num_samps,
				&addr[i], left, right);
			return;
		}

		max = MY_MAX(max, left);
		max = MY_MAX(max, right);
	}

	printf("check4 max: %d over %d\n", max, num_samps);
}

void
send_sound_to_file(word32 *addr, int shm_pos, int num_samps)
{
	int	size, ret;

	if(g_sound_file_fd < 0) {
		open_sound_file();
	}

	size = 0;
	if((num_samps + shm_pos) > SOUND_SHM_SAMP_SIZE) {
		size = SOUND_SHM_SAMP_SIZE - shm_pos;
		g_send_file_bytes += (size * 4);

		ret = (int)write(g_sound_file_fd, &(addr[shm_pos]), 4*size);
		if(ret != (4*size)) {
			halt_printf("wrote %d not %d\n", ret, 4*size);
		}

		if(g_doc_vol < 3) {
			check_for_range(&(addr[shm_pos]), size, 0);
		} else {
			printf("Not checking %d bytes since vol: %d\n",
				4*size, g_doc_vol);
		}
		shm_pos = 0;
		num_samps -= size;
	}

	g_send_file_bytes += (num_samps * 4);

	ret = (int)write(g_sound_file_fd, &(addr[shm_pos]), 4*num_samps);
	if(ret != (4*num_samps)) {
		halt_printf("wrote %d not %d\n", ret, 4*num_samps);
	}

	if(g_doc_vol < 3) {
		check_for_range(&(addr[shm_pos]), num_samps, size);
	} else {
		printf("Not checking2 %d bytes since vol: %d\n",
			4*num_samps, g_doc_vol);
	}

}

void
show_c030_state()
{
	show_c030_samps(&(g_samp_buf[0]), 100);
}

void
show_c030_samps(int *outptr, int num)
{
	int	i;

	printf("c030_fsamps[]: %d\n", g_num_c030_fsamps);

	for(i = 0; i < g_num_c030_fsamps+2; i++) {
		printf("%3d: %5.3f\n", i, c030_fsamps[i]);
	}

	printf("Samples[] = %d\n", num);

	for(i = 0; i < num+2; i++) {
		printf("%4d: %d  %d\n", i, outptr[0], outptr[1]);
		outptr += 2;
	}
}

int g_sound_play_depth = 0;

// sound_play(): forms the samples from the last sample time to the current
//  time.  Can be called anytime from anywhere.  This is how KEGS handles
//  dynamic sound changes (say, disabling an Ensoniq oscillator manually):
//  when it's turned off, call sound_play() to play up to this moment, then
//  the next time sound_play() is called, it will just know this osc is off
// So, on any sound-related state change, call sound_play().

void
sound_play(double dsamps)
{
	Doc_reg *rptr;
	Ay8913	*ay8913ptr;
	int	*outptr;
	int	*outptr_start;
	word32	*sndptr;
	double	complete_dsamp, cur_dsamp, last_dsamp, dsamp_now, dnum_samps;
	double	dvolume;
	float	ftmp, fsampnum, next_fsampnum, fc030_range, fc030_base;
	float	fpercent;
	word32	start_time1, start_time2, start_time3, start_time4;
	word32	end_time1, end_time2, end_time3, uval1, uval0;
	word32	cur_acc, cur_pos, cur_mask, cur_inc, cur_end;
	int	val, val2, new_val, imul, off, num, c030_lo_val, c030_hi_val;
	int	sampnum, next_sampnum, c030_state, val0, val1, ctl, num_osc_en;
	int	samps_left, samps_to_do, samps_played, samp_offset, pos;
	int	snd_buf_init, num_samps, osc, done, num_pairs, sound_mask, ivol;
	int	i, j;

	GET_ITIMER(start_time1);

	g_num_snd_plays++;
	if(g_sound_play_depth) {
		halt_printf("Nested sound_play!\n");
	}

	g_sound_play_depth++;

	/* calc sample num */

	last_dsamp = g_last_sound_play_dsamp;
	num_samps = (int)(dsamps - g_last_sound_play_dsamp);
	dnum_samps = (double)num_samps;

	dsamp_now = last_dsamp + dnum_samps;

	if(num_samps < 1) {
		/* just say no */
		g_sound_play_depth--;
		return;
	}

	DOC_LOG("sound_play", -1, dsamp_now, num_samps);

	if(num_samps > MAX_SND_BUF) {
		printf("num_samps: %d, too big!\n", num_samps);
		g_sound_play_depth--;
		return;
	}


	GET_ITIMER(start_time4);

	outptr_start = &(g_samp_buf[0]);
	outptr = outptr_start;

	snd_buf_init = 0;
	samps_played = 0;

	num = g_num_c030_fsamps;

	// Handle $C030 speaker clicks
	if(num || ((g_vbl_count - g_last_c030_vbl_count) < 240)) {

		if(num) {
			g_last_c030_vbl_count = g_vbl_count;
		}

		pos = 0;
		outptr = outptr_start;
		c030_state = g_c030_state;

		c030_hi_val = ((VAL_C030_BASE + VAL_C030_RANGE)*g_doc_vol) >> 4;
		c030_lo_val = (VAL_C030_BASE * g_doc_vol) >> 4;

		fc030_range = (float)(((VAL_C030_RANGE) * g_doc_vol) >> 4);
		fc030_base = (float)(((VAL_C030_BASE) * g_doc_vol) >> 4);

		val = c030_lo_val;
		if(c030_state) {
			val = c030_hi_val;
		}

		snd_buf_init++;

		c030_fsamps[num] = (float)(num_samps);
		c030_fsamps[num+1] = (float)(num_samps+1);

		ftmp = (float)num_samps;
		/* ensure that all samps are in range */
		for(i = num - 1; i >= 0; i--) {
			if(c030_fsamps[i] > ftmp) {
				c030_fsamps[i] = ftmp;
			}
		}

		num++;
		fsampnum = c030_fsamps[0];
		sampnum = (int)fsampnum;
		fpercent = (float)0.0;
		i = 0;

		while(i < num) {
			next_fsampnum = c030_fsamps[i+1];
			next_sampnum = (int)next_fsampnum;
			if(sampnum < 0 || sampnum > num_samps) {
				halt_printf("play c030: [%d]:%f is %d, > %d\n",
					i, fsampnum, sampnum, num_samps);
				break;
			}

			/* write in samples to all samps < me */
			new_val = c030_lo_val;
			if(c030_state) {
				new_val = c030_hi_val;
			}
			for(j = pos; j < sampnum; j++) {
				outptr[0] = new_val;
				outptr[1] = new_val;
				outptr += 2;
				pos++;
			}

			/* now, calculate me */
			fpercent = (float)0.0;
			if(c030_state) {
				fpercent = (fsampnum - (float)sampnum);
			}

			c030_state = !c030_state;

			while(next_sampnum == sampnum) {
				if(c030_state) {
					fpercent += (next_fsampnum - fsampnum);
				}
				i++;
				fsampnum = next_fsampnum;

				next_fsampnum = c030_fsamps[i+1];
				next_sampnum = (int)next_fsampnum;
				c030_state = !c030_state;
			}

			if(c030_state) {
				/* add in fractional time */
				ftmp = (int)(fsampnum + (float)1.0);
				fpercent += (ftmp - fsampnum);
			}

			if((fpercent < (float)0.0) || (fpercent > (float)1.0)) {
				halt_printf("fpercent: %d = %f\n", i, fpercent);
				show_c030_samps(outptr_start, num_samps);
				break;
			}

			val = (int)((fpercent * fc030_range) + fc030_base);
			outptr[0] = val;
			outptr[1] = val;
			outptr += 2;
			pos++;
			i++;

			sampnum = next_sampnum;
			fsampnum = next_fsampnum;
		}

		samps_played += num_samps;

		/* since we pretended to get one extra sample, we will */
		/*  have toggled the speaker one time too many.  Fix it */
		g_c030_state = !c030_state;

		if(g_send_sound_to_file) {
			show_c030_samps(outptr_start, num_samps);
		}
	}

	g_num_c030_fsamps = 0;

	GET_ITIMER(start_time2);

	num_osc_en = g_doc_num_osc_en;

	done = 0;
	// Do Ensoniq oscillators
	while(!done) {
		done = 1;
		for(j = 0; j < num_osc_en; j++) {
			osc = j;
			rptr = &(g_doc_regs[osc]);
			complete_dsamp = rptr->complete_dsamp;
			samps_left = rptr->samps_left;
			cur_acc = rptr->cur_acc;
			cur_mask = rptr->cur_mask;
			cur_inc = rptr->cur_inc;
			cur_end = rptr->cur_end;
			if(!rptr->running || cur_inc == 0 ||
						(complete_dsamp >= dsamp_now)) {
				continue;
			}

			done = 0;
			ctl = rptr->ctl;

			samp_offset = 0;
			if(complete_dsamp > last_dsamp) {
				samp_offset = (int)(complete_dsamp- last_dsamp);
				if(samp_offset > num_samps) {
					rptr->complete_dsamp = dsamp_now;
					continue;
				}
			}
			outptr = outptr_start + 2 * samp_offset;
			if(ctl & 0x10) {
				/* other channel */
				outptr += 1;
			}

			imul = (rptr->vol * g_doc_vol);
			off = imul * 128;

			samps_to_do = MY_MIN(samps_left,
						num_samps - samp_offset);
			if(imul == 0 || samps_to_do == 0) {
				/* produce no sound */
				samps_left = samps_left - samps_to_do;
				cur_acc += cur_inc * samps_to_do;
				rptr->samps_left = samps_left;
				rptr->cur_acc = cur_acc;
				cur_dsamp = last_dsamp +
					(double)(samps_to_do + samp_offset);
				DOC_LOG("nosnd", osc, cur_dsamp, samps_to_do);
				rptr->complete_dsamp = dsamp_now;
				cur_pos = rptr->cur_start+(cur_acc & cur_mask);
				if(samps_left <= 0) {
					doc_sound_end(osc, 1, cur_dsamp,
								dsamp_now);
					val = 0;
					j--;
				} else {
					val = doc_ram[cur_pos >> SND_PTR_SHIFT];
				}
				rptr->last_samp_val = val;
				continue;
			}
			if(snd_buf_init == 0) {
				memset(outptr_start, 0,
					2*sizeof(outptr_start[0])*num_samps);
				snd_buf_init++;
			}
			val = 0;
			rptr->complete_dsamp = dsamp_now;
			cur_pos = rptr->cur_start + (cur_acc & cur_mask);
			pos = 0;
			for(i = 0; i < samps_to_do; i++) {
				pos = cur_pos >> SND_PTR_SHIFT;
				cur_pos += cur_inc;
				cur_acc += cur_inc;
				val = doc_ram[pos];
				val2 = (val * imul - off) >> 4;
				if((val == 0) || (cur_pos >= cur_end)) {
					cur_dsamp = last_dsamp +
						(double)(samp_offset + i + 1);
					rptr->cur_acc = cur_acc;
					rptr->samps_left = 0;
					DOC_LOG("end or 0", osc, cur_dsamp,
						((pos & 0xffffU) << 16) |
						((i &0xff) << 8) | val);
					doc_sound_end(osc, val, cur_dsamp,
								dsamp_now);
					val = 0;
					break;
				}
				val2 = outptr[0] + val2;
				samps_left--;
				*outptr = val2;
				outptr += 2;
			}
			rptr->last_samp_val = val;

			if(val != 0) {
				rptr->cur_acc = cur_acc;
				rptr->samps_left = samps_left;
				rptr->complete_dsamp = dsamp_now;
			}
			samps_played += samps_to_do;
			DOC_LOG("splayed", osc, dsamp_now,
				(samps_to_do << 16) + (pos & 0xffff));
		}
	}

	num_pairs = 0;
	// Do Mockinboard channels
	for(i = 0; i < 2; i++) {			// Pair: 0 or 1
		ay8913ptr = &(g_mockingboard.pair[i].ay8913);
		for(j = 0; j < 3; j++) {		// Channels: A, B, or C
			if((ay8913ptr->regs[8 + j] & 0x1f) == 0) {
				continue;
			}
			num_pairs = 2;
			g_last_mock_vbl_count = g_vbl_count;
			break;
		}
	}
	if((g_vbl_count - g_last_mock_vbl_count) < 120) {
		// Keep playing for 2 seconds, to avoid some static issues
		num_pairs = 2;
	}
	if(num_pairs) {
		sound_mask = -1;
		if(snd_buf_init == 0) {
			sound_mask = 0;
			snd_buf_init++;
		}
		outptr = outptr_start;
		ivol = -((VAL_MOCK_RANGE * 3 / (8 * 15)) * g_doc_vol);
			// Do 3/8 of range below 0, leaving 5/8 above 0
		for(i = 0; i < num_samps; i++) {
			outptr[0] = (outptr[0] & sound_mask) + ivol;
			outptr[1] = (outptr[1] & sound_mask) + ivol;
			outptr += 2;
		}
		for(i = 0; i < 16; i++) {
			dvolume = (g_doc_vol * VAL_MOCK_RANGE) / (15.0 * 3.0);
			ivol = (int)(g_ay8913_ampl_factor[i] * dvolume);
			g_mock_volume[i] = ivol;
		}
	}
	for(i = 0; i < num_pairs; i++) {
		if(g_mockingboard.disable_mask) {
			printf("dsamp:%lf\n", dsamps);
		}

		sound_mock_envelope(i, &(g_mock_env_vol[0]), num_samps,
							&(g_mock_volume[0]));
		sound_mock_noise(i, &(g_mock_noise_bytes[0]), num_samps);
		for(j = 0; j < 3; j++) {
			sound_mock_play(i, j, outptr_start,
				&(g_mock_env_vol[0]), &(g_mock_noise_bytes[0]),
				&(g_mock_volume[0]), num_samps);
		}
	}

	GET_ITIMER(end_time2);

	g_cycs_in_sound2 += (end_time2 - start_time2);

	g_last_sound_play_dsamp = dsamp_now;

	GET_ITIMER(start_time3);

	outptr = outptr_start;

	pos = g_sound_shm_pos;
	sndptr = g_sound_shm_addr;

#if 0
	printf("samps_left: %d, num_samps: %d\n", samps_left, num_samps);
#endif

	if(g_audio_enable != 0) {
		if(snd_buf_init) {
			/* convert sound buf */
			for(i = 0; i < num_samps; i++) {
				val0 = outptr[0];
				val1 = outptr[1];
				val = val0;
				if(val0 > 32767) {
					val = 32767;
				}
				if(val0 < -32768) {
					val = -32768;
				}
				uval0 = val & 0xffffU;
				val = val1;
				if(val1 > 32767) {
					val = 32767;
				}
				if(val1 < -32768) {
					val = -32768;
				}
				uval1 = val & 0xffffU;
				outptr += 2;

#if defined(__linux__) || defined(OSS)
				/* Linux seems to expect little-endian */
				/*  samples always, even on PowerPC */
# ifdef KEGS_BIG_ENDIAN
				sndptr[pos] = ((uval1 & 0xff) << 24) +
						((uval1 & 0xff00) << 8) +
						((uval0 & 0xff) << 8) +
						((uval0 >> 8) & 0xff);
# else
				sndptr[pos] = (uval1 << 16) + (uval0 & 0xffff);
# endif
#else
# ifdef KEGS_BIG_ENDIAN
				sndptr[pos] = (uval0 << 16) + uval1;
# else
				sndptr[pos] = (uval1 << 16) + uval0;
# endif
#endif
				pos++;
				if(pos >= SOUND_SHM_SAMP_SIZE) {
					pos = 0;
				}
			}
			if(g_queued_nonsamps) {
				/* force out old 0 samps */
				snddrv_send_sound(0, g_queued_nonsamps);
				g_queued_nonsamps = 0;
			}
			if(g_send_sound_to_file) {
				send_sound_to_file(g_sound_shm_addr,
						g_sound_shm_pos, num_samps);
			}
			g_queued_samps += num_samps;
		} else {
			/* move pos */
			pos += num_samps;
			while(pos >= SOUND_SHM_SAMP_SIZE) {
				pos -= SOUND_SHM_SAMP_SIZE;
			}
			if(g_send_sound_to_file) {
				send_sound_to_file(zero_buf, g_sound_shm_pos,
					num_samps);
			}
			if(g_queued_samps) {
				/* force out old non-0 samps */
				snddrv_send_sound(1, g_queued_samps);
				g_queued_samps = 0;
			}
			g_queued_nonsamps += num_samps;
		}

	}

	g_sound_shm_pos = pos;


	GET_ITIMER(end_time3);

	g_fvoices += ((float)(samps_played) * (float)(g_drecip_audio_rate));

	if(g_audio_enable != 0) {
		if(g_queued_samps >= (g_audio_rate/60)) {
			snddrv_send_sound(1, g_queued_samps);
			g_queued_samps = 0;
		}

		if(g_queued_nonsamps >= (g_audio_rate/60)) {
			snddrv_send_sound(0, g_queued_nonsamps);
			g_queued_nonsamps = 0;
		}
	}

	GET_ITIMER(end_time1);

	g_cycs_in_sound1 += (end_time1 - start_time1);
	g_cycs_in_sound3 += (end_time3 - start_time3);
	g_cycs_in_sound4 += (start_time2 - start_time4);

	g_last_sound_play_dsamp = dsamp_now;

	g_sound_play_depth--;
}

void
sound_mock_envelope(int pair, int *env_ptr, int num_samps, int *vol_ptr)
{
	Ay8913	*ay8913ptr;
	double	dmul, denv_period;
	dword64	env_dsamp, dsamp_inc;
	word32	ampl, eff_ampl, reg13, env_val, env_period;
	int	i;

	// This routine calculates a fixed-point increment to apply
	//  to env_dsamp, where the envelope value is in bits 44:40 (bit
	//  44 is to track the alternating waveform, 43:40 is the env_ampl).
	// This algorithm does not properly handle dynamically changing the
	//  envelope period in the middle of a step.  In the AY8913, the
	//  part counts up to the env_period, and if the period is changed
	//  to a value smaller than the current count, it steps immediately
	//  to the next step.  This routine will wait for enough fraction
	//  to accumulate before stepping.  At most, this can delay the step
	//  by almost the new count time (if the new period is smaller), but
	//  no more.  I suspect this is not noticeable.
	if(num_samps > MAX_MOCK_ENV_SAMPLES) {
		halt_printf("envelope overflow!: %d\n", num_samps);
		return;
	}

	ay8913ptr = &(g_mockingboard.pair[pair].ay8913);
	ampl = ay8913ptr->regs[8] | ay8913ptr->regs[9] | ay8913ptr->regs[10];
	if((ampl & 0x10) == 0) {
		// No one uses the envelope
		return;
	}

	env_dsamp = ay8913ptr->env_dsamp;
	env_period = ay8913ptr->regs[11] + (256 * ay8913ptr->regs[12]);
	if(env_period == 0) {
		denv_period = 0.5;		// To match MAME
	} else {
		denv_period = (double)env_period;
	}
	dmul = (1.0 / 16.0) * (1 << 20) * (1 << 20);	// (1ULL << 40) / 16.0
	// Calculate amount counter will count in one sample.
	// inc_per_tick 62.5KHz tick: (1/env_period)
	// inc_per_dcyc: (1/(16*env_period))
	// inc_per_samp = inc_per_dcyc * g_dcycs_per_samp
	dsamp_inc = (dword64)((dmul * g_dcycs_per_samp / denv_period));
			// Amount to inc per sample, fixed point, 40 bit frac

	reg13 = ay8913ptr->regs[13];			// "reg15", env ctrl
	eff_ampl = 0;
	for(i = 0; i < num_samps; i++) {
		env_dsamp = (env_dsamp + dsamp_inc) & 0x9fffffffffffULL;
		env_val = (env_dsamp >> 40) & 0xff;
		eff_ampl = env_val & 0xf;
		if((reg13 & 4) == 0) {
			eff_ampl = 15 - eff_ampl;	// not attack
		}
		if((reg13 & 8) && (reg13 & 2)) {
			// continue and alternate
			if(env_val & 0x10) {
				eff_ampl = 15 - eff_ampl;
			}
		}
		if(((reg13 & 8) == 0) && (env_val >= 0x10)) {
			eff_ampl = 0;
			ampl = 0;		// Turn off envelope
			env_dsamp |= (0x80ULL << 40);
		} else if((reg13 & 1) && (env_val >= 0x10)) {
			eff_ampl = ((reg13 >> 1) ^ (reg13 >> 2)) & 1;
			eff_ampl = eff_ampl * 15;
			ampl = eff_ampl;	// Turn off envelope
			env_dsamp |= (0x80ULL << 40);
		}
		*env_ptr++ = vol_ptr[eff_ampl & 0xf];
	}
	ay8913ptr->env_dsamp = env_dsamp;
}

void
sound_mock_noise(int pair, byte *noise_ptr, int num_samps)
{
	Ay8913	*ay8913ptr;
	word32	ampl, mix, noise_val, noise_samp, noise_period, xor, samp_inc;
	int	doit;
	int	i;

	if(num_samps > MAX_MOCK_ENV_SAMPLES) {
		halt_printf("noise overflow!: %d\n", num_samps);
		return;
	}

	ay8913ptr = &(g_mockingboard.pair[pair].ay8913);
	doit = 0;
	for(i = 0; i < 3; i++) {
		ampl = ay8913ptr->regs[8 + i];
		mix = ay8913ptr->regs[7] >> i;
		if((ampl != 0) && ((mix & 8) == 0)) {
			doit = 1;
			break;
		}
	}
	if(!doit) {
		// No channel looks at noise, don't bother
		return;
	}

	noise_val = ay8913ptr->noise_val;
	noise_samp = ay8913ptr->noise_samp;
	noise_period = (ay8913ptr->regs[6] & 0x1f);
	noise_period = noise_period << 16;
	samp_inc = (word32)(65536 * g_dcycs_per_samp / 16.0);
			// Amount to inc per sample
	if(noise_samp >= noise_period) {
		// Period changed during sound, reset
		noise_samp = noise_period;
	}
	for(i = 0; i < num_samps; i++) {
		noise_samp += samp_inc;
		if(noise_samp >= noise_period) {
			// HACK: handle fraction
			// 17-bit LFSR, algorithm from MAME:sound/ay8910.cpp
			// val = val ^ (((val & 1) ^ ((val >> 3) & 1)) << 17)
			xor = 0;
			xor = (noise_val ^ (noise_val >> 3)) & 1;
			noise_val = (noise_val ^ (xor << 17)) >> 1;
			noise_samp -= noise_period;
		}
		noise_ptr[i] = noise_val & 1;
	}
	ay8913ptr->noise_samp = noise_samp;
	ay8913ptr->noise_val = noise_val;
}

int g_did_mock_print = 100;

void
sound_mock_play(int pair, int channel, int *outptr, int *env_ptr,
				byte *noise_ptr, int *vol_ptr, int num_samps)
{
	Ay8913	*ay8913ptr;
	word32	ampl, mix, tone_samp, tone_period, toggle_tone;
	word32	samp_inc, noise_val;
	int	out, ival, do_print;
	int	i;

	if((g_mockingboard.disable_mask >> ((pair * 3) + channel)) & 1) {
		// This channel is disabled
		return;
	}

	ay8913ptr = &(g_mockingboard.pair[pair].ay8913);
	ampl = ay8913ptr->regs[8 + channel] & 0x1f;
	if(ampl == 0) {
		return;
	}
	toggle_tone = ay8913ptr->toggle_tone[channel];		// 0 or 1
	mix = (ay8913ptr->regs[7] >> channel) & 9;
	if(mix == 9) {
		// constant tone: output will be ampl for this channel.
		if(ampl & 0x10) {		// Envelope!
			// The envelope can make the tone, so must calculate it
		} else {
			// HACK: do nothing for now
			return;
		}
	}
	outptr += pair;			// pair[1] is right
	tone_samp = ay8913ptr->tone_samp[channel];
	tone_period = ay8913ptr->regs[2*channel] +
					(256 * ay8913ptr->regs[2*channel + 1]);
	tone_period = tone_period << 16;
	samp_inc = (word32)(65536 * g_dcycs_per_samp / 8.0);
			// Amount to inc per sample
	do_print = 0;
	if(g_mockingboard.disable_mask) {
		printf("Doing %d samps, mix:%d, ampl:%02x\n", num_samps, mix,
									ampl);
		do_print = 1;
		g_did_mock_print = 0;
	}
	if((num_samps > 500) && (g_did_mock_print == 0)) {
		do_print = 1;
		g_did_mock_print = 1;
		printf("Start of %d sample, channel %d mix:%02x ampl:%02x "
			"toggle_tone:%02x\n", num_samps, channel, mix, ampl,
			toggle_tone);
		printf(" tone_period:%08x, tone_samp:%08x, samp_inc:%08x\n",
			tone_period, tone_samp, samp_inc);
	}
	if(tone_samp >= tone_period) {
		// Period changed during sound, reset it
		tone_samp = tone_period;
	}
	for(i = 0; i < num_samps; i++) {
		tone_samp += samp_inc;
		if(tone_samp >= tone_period) {
			// HACK: handle toggling mid-sample...
			toggle_tone ^= 1;
			if(do_print) {
				printf("i:%d tone_toggled to %d, tone_period:"
					"%04x, pre tone_samp:%08x\n", i,
					toggle_tone, tone_period, tone_samp);
			}
			tone_samp -= tone_period;
			if(do_print) {
				printf("post tone_samp:%08x\n", tone_samp);
			}
		}
		noise_val = noise_ptr[i] & 1;
		out = (toggle_tone || (mix & 1)) &
						((noise_val & 1) || (mix & 8));
			// Careful mix of || and & above...
		ival = vol_ptr[ampl & 0xf];
		if(ampl & 0x10) {			// Envelope
			ival = env_ptr[i];
		}
		*outptr += ival*out;
		outptr += 2;
	}
	ay8913ptr->tone_samp[channel] = tone_samp;
	ay8913ptr->toggle_tone[channel] = toggle_tone;
}

void
doc_handle_event(int osc, double dcycs)
{
	double	dsamps;

	/* handle osc stopping and maybe interrupting */

	g_num_doc_events++;

	dsamps = dcycs * g_dsamps_per_dcyc;

	DOC_LOG("doc_ev", osc, dcycs, 0);

	g_doc_regs[osc].event = 0;

	sound_play(dsamps);

}

void
doc_sound_end(int osc, int can_repeat, double eff_dsamps, double dsamps)
{
	Doc_reg	*rptr, *orptr;
	int	mode, omode;
	int	other_osc;
	int	one_shot_stop;
	int	ctl;

	/* handle osc stopping and maybe interrupting */

	if(osc < 0 || osc > 31) {
		printf("doc_handle_event: osc: %d!\n", osc);
		return;
	}

	rptr = &(g_doc_regs[osc]);
	ctl = rptr->ctl;

	if(rptr->event) {
		remove_event_doc(osc);
	}
	rptr->event = 0;
	rptr->cur_acc = 0;		/* reset internal accumulator*/

	/* check to make sure osc is running */
	if(ctl & 0x01) {
		/* Oscillator already stopped. */
		halt_printf("Osc %d interrupt, but it was already stop!\n",osc);
#ifdef HPUX
		U_STACK_TRACE();
#endif
		return;
	}

	if(ctl & 0x08) {
		if(rptr->has_irq_pending == 0) {
			add_sound_irq(osc);
		}
	}

	if(!rptr->running) {
		halt_printf("Doc event for osc %d, but ! running\n", osc);
	}

	rptr->running = 0;

	mode = (ctl >> 1) & 3;
	other_osc = osc ^ 1;
	orptr = &(g_doc_regs[other_osc]);
	omode = (orptr->ctl >> 1) & 3;

	/* If either this osc or it's partner is in swap mode, treat the */
	/*  pair as being in swap mode.  This Ensoniq feature pointed out */
	/*  by Ian Schmidt */
	if(mode == 0 && can_repeat) {
		/* free-running mode with no 0 byte! */
		/* start doing it again */

		start_sound(osc, eff_dsamps, dsamps);

		return;
	} else if((mode == 3) || (omode == 3)) {
		/* swap mode (even if we're one_shot and partner is swap)! */
		/* unless we're one-shot and we hit a 0-byte--then */
		/* Olivier Goguel says just stop */
		rptr->ctl |= 1;
		one_shot_stop = (mode == 1) && (!can_repeat);
		if(!one_shot_stop && !orptr->running &&
							(orptr->ctl & 0x1)) {
			orptr->ctl = orptr->ctl & (~1);
			start_sound(other_osc, eff_dsamps, dsamps);
		}
		return;
	} else {
		/* stop the oscillator */
		rptr->ctl |= 1;
	}

	return;
}

void
add_sound_irq(int osc)
{
	int	num_osc_interrupting;

	if(g_doc_regs[osc].has_irq_pending) {
		halt_printf("Adding sound_irq for %02x, but irq_p: %d\n", osc,
			g_doc_regs[osc].has_irq_pending);
	}

	num_osc_interrupting = g_num_osc_interrupting + 1;
	g_doc_regs[osc].has_irq_pending = num_osc_interrupting;
	g_num_osc_interrupting = num_osc_interrupting;

	add_irq(IRQ_PENDING_DOC);
	if(num_osc_interrupting == 1) {
		doc_reg_e0 = 0x00 + (osc << 1);
	}

	DOC_LOG("add_irq", osc, g_cur_dcycs * g_dsamps_per_dcyc, 0);
}

void
remove_sound_irq(int osc, int must)
{
	Doc_reg	*rptr;
	int	num_osc_interrupt;
	int	has_irq_pending;
	int	first;
	int	i;

	doc_printf("remove irq for osc: %d, has_irq: %d\n",
		osc, g_doc_regs[osc].has_irq_pending);

	num_osc_interrupt = g_doc_regs[osc].has_irq_pending;
	first = 0;
	if(num_osc_interrupt) {
		g_num_osc_interrupting--;
		g_doc_regs[osc].has_irq_pending = 0;
		DOC_LOG("rem_irq", osc, g_cur_dcycs * g_dsamps_per_dcyc, 0);
		if(g_num_osc_interrupting == 0) {
			remove_irq(IRQ_PENDING_DOC);
		}

		first = 0x40 | (doc_reg_e0 >> 1);
					/* if none found, then def = no ints */
		for(i = 0; i < g_doc_num_osc_en; i++) {
			rptr = &(g_doc_regs[i]);
			has_irq_pending = rptr->has_irq_pending;
			if(has_irq_pending > num_osc_interrupt) {
				has_irq_pending--;
				rptr->has_irq_pending = has_irq_pending;
			}
			if(has_irq_pending == 1) {
				first = i;
			}
		}
		if(num_osc_interrupt == 1) {
			doc_reg_e0 = (first << 1);
		} else {
#if 0
			halt_printf("remove_sound_irq[%02x]=%d, first:%d\n",
				osc, num_osc_interrupt, first);
#endif
		}
	} else {
#if 0
		/* make sure no int pending */
		if(doc_reg_e0 != 0xff) {
			halt_printf("remove_sound_irq[%02x]=0, but e0: %02x\n",
				osc, doc_reg_e0);
		}
#endif
		if(must) {
			halt_printf("REMOVE_sound_irq[%02x]=0, but e0: %02x\n",
				osc, doc_reg_e0);
		}
	}

	if(doc_reg_e0 & 0x80) {
		for(i = 0; i < 0x20; i++) {
			has_irq_pending = g_doc_regs[i].has_irq_pending;
			if(has_irq_pending) {
				halt_printf("remove_sound_irq[%02x], but "
					"[%02x]=%d!\n", osc,i,has_irq_pending);
				printf("num_osc_int: %d, first: %02x\n",
					num_osc_interrupt, first);
			}
		}
	}
}

void
start_sound(int osc, double eff_dsamps, double dsamps)
{
	register word32 start_time1;
	register word32 end_time1;
	Doc_reg	*rptr;
	int	ctl;
	int	mode;
	word32	sz;
	word32	size;
	word32	wave_size;

	if(osc < 0 || osc > 31) {
		halt_printf("start_sound: osc: %02x!\n", osc);
	}

	g_num_start_sounds++;

	rptr = &(g_doc_regs[osc]);

	if(osc >= g_doc_num_osc_en) {
		rptr->ctl |= 1;
		return;
	}

	GET_ITIMER(start_time1);

	ctl = rptr->ctl;

	mode = (ctl >> 1) & 3;

	wave_size = rptr->wavesize;

	sz = ((wave_size >> 3) & 7) + 8;
	size = 1 << sz;

	if(size < 0x100) {
		halt_printf("size: %08x is too small, sz: %08x!\n", size, sz);
	}

	if(rptr->running) {
		halt_printf("start_sound osc: %d, already running!\n", osc);
	}

	rptr->running = 1;

	rptr->complete_dsamp = eff_dsamps;

	doc_printf("Starting osc %02x, dsamp: %f\n", osc, dsamps);
	doc_printf("size: %04x\n", size);

	if((mode == 2) && ((osc & 1) == 0)) {
		printf("Sync mode osc %d starting!\n", osc);
		/* set_halt(1); */

		/* see if we should start our odd partner */
		if((rptr[1].ctl & 7) == 5) {
			/* odd partner stopped in sync mode--start him */
			rptr[1].ctl &= (~1);
			start_sound(osc + 1, eff_dsamps, dsamps);
		} else {
			printf("Osc %d starting sync, but osc %d ctl: %02x\n",
				osc, osc+1, rptr[1].ctl);
		}
	}

	wave_end_estimate(osc, eff_dsamps, dsamps);

	DOC_LOG("st playing", osc, eff_dsamps, size);
#if 0
	if(rptr->cur_acc != 0) {
		halt_printf("Start osc %02x, acc: %08x\n", osc, rptr->cur_acc);
	}
#endif

	GET_ITIMER(end_time1);

	g_cycs_in_start_sound += (end_time1 - start_time1);
}

void
wave_end_estimate(int osc, double eff_dsamps, double dsamps)
{
	register word32 start_time1;
	register word32 end_time1;
	Doc_reg *rptr;
	byte	*ptr1;
	double	event_dsamp;
	double	event_dcycs;
	double	dcycs_per_samp;
	double	dsamps_per_byte;
	double	num_dsamps;
	double	dcur_inc;
	word32	tmp1;
	word32	cur_inc;
	word32	save_val;
	int	save_size;
	int	pos;
	int	size;
	int	estimate;

	GET_ITIMER(start_time1);

	dcycs_per_samp = g_dcycs_per_samp;

	rptr = &(g_doc_regs[osc]);

	cur_inc = rptr->cur_inc;
	dcur_inc = (double)cur_inc;
	dsamps_per_byte = 0.0;
	if(cur_inc) {
		dsamps_per_byte = SND_PTR_SHIFT_DBL / (double)dcur_inc;
	}

	/* see if there's a zero byte */
	tmp1 = rptr->cur_start + (rptr->cur_acc & rptr->cur_mask);
	pos = tmp1 >> SND_PTR_SHIFT;
	size = ((rptr->cur_end) >> SND_PTR_SHIFT) - pos;

	ptr1 = &doc_ram[pos];

	estimate = 0;
	if(rptr->ctl & 0x08 || g_doc_regs[osc ^ 1].ctl & 0x08) {
		estimate = 1;
	}

#if 0
	estimate = 1;
#endif
	if(estimate) {
		save_size = size;
		save_val = ptr1[size];
		ptr1[size] = 0;
		size = (int)strlen((char *)ptr1);
		ptr1[save_size] = save_val;
	}

	/* calc samples to play */
	num_dsamps = (dsamps_per_byte * (double)size) + 1.0;

	rptr->samps_left = (int)num_dsamps;

	if(rptr->event) {
		remove_event_doc(osc);
	}
	rptr->event = 0;

	event_dsamp = eff_dsamps + num_dsamps;
	if(estimate) {
		rptr->event = 1;
		rptr->dsamp_ev = event_dsamp;
		rptr->dsamp_ev2 = dsamps;
		event_dcycs = (event_dsamp * dcycs_per_samp) + 1.0;
		add_event_doc(event_dcycs, osc);
	}

	GET_ITIMER(end_time1);

	g_cycs_in_est_sound += (end_time1 - start_time1);
}


void
remove_sound_event(int osc)
{
	if(g_doc_regs[osc].event) {
		g_doc_regs[osc].event = 0;
		remove_event_doc(osc);
	}
}


void
doc_write_ctl_reg(int osc, int val, double dsamps)
{
	Doc_reg *rptr;
	double	eff_dsamps;
	word32	old_halt, new_halt;
	int	old_val;

	if(osc < 0 || osc >= 0x20) {
		halt_printf("doc_write_ctl_reg: osc: %02x, val: %02x\n",
			osc, val);
		return;
	}

	eff_dsamps = dsamps;
	rptr = &(g_doc_regs[osc]);
	old_val = rptr->ctl;
	g_doc_saved_ctl = old_val;

	if(old_val == val) {
		return;
	}

	DOC_LOG("ctl_reg", osc, dsamps, (old_val << 16) + val);

	old_halt = (old_val & 1);
	new_halt = (val & 1);

	/* bits are:	28: old int bit */
	/*		29: old halt bit */
	/*		30: new int bit */
	/*		31: new halt bit */

#if 0
	if(osc == 0x10) {
		printf("osc %d new_ctl: %02x, old: %02x\n", osc, val, old_val);
	}
#endif

	/* no matter what, remove any pending IRQs on this osc */
	remove_sound_irq(osc, 0);

#if 0
	if(old_halt) {
		printf("doc_write_ctl to osc %d, val: %02x, old: %02x\n",
			osc, val, old_val);
	}
#endif

	if(new_halt != 0) {
		/* make sure sound is stopped */
		remove_sound_event(osc);
		if(old_halt == 0) {
			/* it was playing, finish it up */
#if 0
			halt_printf("Aborted osc %d at eff_dsamps: %f, ctl: "
				"%02x, oldctl: %02x\n", osc, eff_dsamps,
				val, old_val);
#endif
			sound_play(eff_dsamps);
		}
		if(((old_val >> 1) & 3) > 0) {
			/* don't clear acc if free-running */
			g_doc_regs[osc].cur_acc = 0;
		}

		g_doc_regs[osc].ctl = val;
		g_doc_regs[osc].running = 0;
	} else {
		/* new halt == 0 = make sure sound is running */
		if(old_halt != 0) {
			/* start sound */
			DOC_LOG("ctl_sound_play", osc, eff_dsamps, val);
			sound_play(eff_dsamps);
			g_doc_regs[osc].ctl = val;

			start_sound(osc, eff_dsamps, dsamps);
		} else {
			/* was running, and something changed */
			doc_printf("osc %d old ctl:%02x new:%02x!\n",
				osc, old_val, val);
#if 0
			sound_play(eff_dsamps);
/* HACK: fix this??? */
#endif
			g_doc_regs[osc].ctl = val;
			if((old_val ^ val) & val & 0x8) {
				/* now has ints on */
				wave_end_estimate(osc, dsamps, dsamps);
			}
		}
	}
}

void
doc_recalc_sound_parms(int osc, double dsamps)
{
	Doc_reg	*rptr;
	double	dfreq, dtmp1, dacc, dacc_recip;
	word32	res, sz, size, wave_size, cur_start, shifted_size;

	g_num_recalc_snd_parms++;

	rptr = &(g_doc_regs[osc]);

	wave_size = rptr->wavesize;

	dfreq = (double)rptr->freq;

	sz = ((wave_size >> 3) & 7) + 8;
	size = 1 << sz;
	rptr->size_bytes = size;
	res = wave_size & 7;

	shifted_size = size << SND_PTR_SHIFT;
	cur_start = (rptr->waveptr << (8 + SND_PTR_SHIFT)) & (-shifted_size);

	dtmp1 = dfreq * (DOC_SCAN_RATE * g_drecip_audio_rate);
	dacc = (double)(1 << (20 - (17 - sz + res)));
	dacc_recip = (SND_PTR_SHIFT_DBL) / ((double)(1 << 20));
	dtmp1 = dtmp1 * g_drecip_osc_en_plus_2 * dacc * dacc_recip;

	rptr->cur_inc = (int)(dtmp1);
	rptr->cur_start = cur_start;
	rptr->cur_end = cur_start + shifted_size;
	rptr->cur_mask = (shifted_size - 1);

	DOC_LOG("recalc", osc, dsamps, (rptr->waveptr << 16) + wave_size);
}

int
doc_read_c030(double dcycs)
{
	int	num;

	num = g_num_c030_fsamps;
	if(num >= MAX_C030_TIMES) {
		halt_printf("Too many clicks per vbl: %d\n", num);
		return 0;
	}

	c030_fsamps[num] = (float)(dcycs * g_dsamps_per_dcyc -
						g_last_sound_play_dsamp);
	g_num_c030_fsamps = num + 1;

	doc_printf("read c030, num this vbl: %04x\n", num);

	return float_bus(dcycs);
}

int
doc_read_c03c()
{
	return g_doc_sound_ctl;
}

int
doc_read_c03d(double dcycs)
{
	Doc_reg	*rptr;
	double	dsamps;
	int	osc, type, ret;

	ret = g_doc_saved_val;
	dsamps = dcycs * g_dsamps_per_dcyc;

	if(g_doc_sound_ctl & 0x40) {
		/* Read RAM */
		g_doc_saved_val = doc_ram[g_c03ef_doc_ptr];
	} else {
		/* Read DOC */
		g_doc_saved_val = 0;

		osc = g_c03ef_doc_ptr & 0x1f;
		type = (g_c03ef_doc_ptr >> 5) & 0x7;
		rptr = &(g_doc_regs[osc]);

		switch(type) {
		case 0x0:	/* freq lo */
			g_doc_saved_val = rptr->freq & 0xff;
			break;
		case 0x1:	/* freq hi */
			g_doc_saved_val = rptr->freq >> 8;
			break;
		case 0x2:	/* vol */
			g_doc_saved_val = rptr->vol;
			break;
		case 0x3:	/* data register */
			/* HACK: make this call sound_play sometimes */
			g_doc_saved_val = rptr->last_samp_val;
			break;
		case 0x4:	/* wave ptr register */
			g_doc_saved_val = rptr->waveptr;
			break;
		case 0x5:	/* control register */
			g_doc_saved_val = rptr->ctl;
			break;
		case 0x6:	/* control register */
			g_doc_saved_val = rptr->wavesize;
			break;
		case 0x7:	/* 0xe0-0xff */
			switch(osc) {
			case 0x00:	/* 0xe0 */
				g_doc_saved_val = doc_reg_e0;
				doc_printf("Reading doc 0xe0, ret: %02x\n",
							g_doc_saved_val);

				/* Clear IRQ on read of e0, if any irq pend */
				if((doc_reg_e0 & 0x80) == 0) {
					remove_sound_irq(doc_reg_e0 >> 1, 1);
				}
				break;
			case 0x01:	/* 0xe1 */
				g_doc_saved_val = (g_doc_num_osc_en - 1) << 1;
				break;
			case 0x02:	/* 0xe2 */
				g_doc_saved_val = 0x80;
#if 0
				halt_printf("Reading doc 0xe2, ret: %02x\n",
							g_doc_saved_val);
#endif
				break;
			default:
				g_doc_saved_val = 0;
				halt_printf("Reading bad doc_reg[%04x]: %02x\n",
					g_c03ef_doc_ptr, g_doc_saved_val);
			}
			break;
		default:
			g_doc_saved_val = 0;
			halt_printf("Reading bad doc_reg[%04x]: %02x\n",
					g_c03ef_doc_ptr, g_doc_saved_val);
		}
	}

	doc_printf("read c03d, doc_ptr: %04x, ret: %02x, saved: %02x\n",
		g_c03ef_doc_ptr, ret, g_doc_saved_val);

	DOC_LOG("read c03d", -1, dsamps, (g_c03ef_doc_ptr << 16) +
			(g_doc_saved_val << 8) + ret);

	if(g_doc_sound_ctl & 0x20) {
		g_c03ef_doc_ptr = (g_c03ef_doc_ptr + 1) & 0xffff;
	}


	return ret;
}

void
doc_write_c03c(int val, double dcycs)
{
	int	vol;

	vol = val & 0xf;
	if(g_doc_vol != vol) {
		/* don't bother playing sound..wait till next update */
		/* sound_play(dcycs); */

		g_doc_vol = vol;
		doc_printf("Setting doc vol to 0x%x at %f\n",
			vol, dcycs);
	}
	DOC_LOG("c03c write", -1, dcycs * g_dsamps_per_dcyc, val);

	g_doc_sound_ctl = val;
}

void
doc_write_c03d(int val, double dcycs)
{
	double	dsamps;
	Doc_reg	*rptr;
	int	osc, type, ctl, tmp;
	int	i;

	val = val & 0xff;

	dsamps = dcycs * g_dsamps_per_dcyc;
	doc_printf("write c03d, doc_ptr: %04x, val: %02x\n",
		g_c03ef_doc_ptr, val);

	DOC_LOG("write c03d", -1, dsamps, (g_c03ef_doc_ptr << 16) + val);

	if(g_doc_sound_ctl & 0x40) {
		/* RAM */
		doc_ram[g_c03ef_doc_ptr] = val;
	} else {
		/* DOC */
		osc = g_c03ef_doc_ptr & 0x1f;
		type = (g_c03ef_doc_ptr >> 5) & 0x7;
		rptr = &(g_doc_regs[osc]);
		ctl = rptr->ctl;
#if 0
		if((ctl & 1) == 0) {
			if(type < 2 || type == 4 || type == 6) {
				halt_printf("Osc %d is running, old ctl: %02x, "
					"but write reg %02x=%02x\n",
					osc, ctl, g_c03ef_doc_ptr & 0xff, val);
			}
		}
#endif

		switch(type) {
		case 0x0:	/* freq lo */
			if((rptr->freq & 0xff) == (word32)val) {
				break;
			}
			if((ctl & 1) == 0) {
				/* play through current status */
				DOC_LOG("flo_sound_play", osc, dsamps, val);
				sound_play(dsamps);
			}
			rptr->freq = (rptr->freq & 0xff00) + val;
			doc_recalc_sound_parms(osc, dsamps);
			break;
		case 0x1:	/* freq hi */
			if((rptr->freq >> 8) == (word32)val) {
				break;
			}
			if((ctl & 1) == 0) {
				/* play through current status */
				DOC_LOG("fhi_sound_play", osc, dsamps, val);
				sound_play(dsamps);
			}
			rptr->freq = (rptr->freq & 0xff) + (val << 8);
			doc_recalc_sound_parms(osc, dsamps);
			break;
		case 0x2:	/* vol */
			if(rptr->vol == (word32)val) {
				break;
			}
			if((ctl & 1) == 0) {
				/* play through current status */
				DOC_LOG("vol_sound_play", osc, dsamps, val);
				sound_play(dsamps);
#if 0
				halt_printf("vol_sound_play at %.1f osc:%d "
					"val:%d\n", dsamps, osc, val);
#endif
			}
			rptr->vol = val;
			break;
		case 0x3:	/* data register */
#if 0
			printf("Writing %02x into doc_data_reg[%02x]!\n",
				val, osc);
#endif
			break;
		case 0x4:	/* wave ptr register */
			if(rptr->waveptr == (word32)val) {
				break;
			}
			if((ctl & 1) == 0) {
				/* play through current status */
				DOC_LOG("wptr_sound_play", osc, dsamps, val);
				sound_play(dsamps);
			}
			rptr->waveptr = val;
			doc_recalc_sound_parms(osc, dsamps);
			break;
		case 0x5:	/* control register */
#if 0
			printf("doc_write ctl osc %d, val: %02x\n", osc, val);
#endif
			if(rptr->ctl == (word32)val) {
				break;
			}
			doc_write_ctl_reg(osc, val, dsamps);
			break;
		case 0x6:	/* wavesize register */
			if(rptr->wavesize == (word32)val) {
				break;
			}
			if((ctl & 1) == 0) {
				/* play through current status */
				DOC_LOG("wsz_sound_play", osc, dsamps, val);
				sound_play(dsamps);
			}
			rptr->wavesize = val;
			doc_recalc_sound_parms(osc, dsamps);
			break;
		case 0x7:	/* 0xe0-0xff */
			switch(osc) {
			case 0x00:	/* 0xe0 */
				doc_printf("writing doc 0xe0 with %02x, "
					"was:%02x\n", val, doc_reg_e0);
#if 0
				if(val != doc_reg_e0) {
					halt_printf("writing doc 0xe0 with "
						"%02x, was:%02x\n", val,
						doc_reg_e0);
				}
#endif
				break;
			case 0x01:	/* 0xe1 */
				doc_printf("Writing doc 0xe1 with %02x\n", val);
				tmp = val & 0x3e;
				tmp = (tmp >> 1) + 1;
				if(tmp < 1) {
					tmp = 1;
				}
				if(tmp > 32) {
					halt_printf("doc 0xe1: %02x!\n", val);
					tmp = 32;
				}
				g_doc_num_osc_en = tmp;
				UPDATE_G_DCYCS_PER_DOC_UPDATE(tmp);

				/* Stop any oscs that were running but now */
				/*  are disabled */
				for(i = g_doc_num_osc_en; i < 0x20; i++) {
					doc_write_ctl_reg(i,
						g_doc_regs[i].ctl | 1, dsamps);
				}

				break;
			default:
				/* this should be illegal, but Turkey Shoot */
				/* and apparently TaskForce, OOTW, etc */
				/*  writes to e2-ff, for no apparent reason */
				doc_printf("Writing doc 0x%x with %02x\n",
						g_c03ef_doc_ptr, val);
				break;
			}
			break;
		default:
			halt_printf("Writing %02x into bad doc_reg[%04x]\n",
				val, g_c03ef_doc_ptr);
		}
	}

	if(g_doc_sound_ctl & 0x20) {
		g_c03ef_doc_ptr = (g_c03ef_doc_ptr + 1) & 0xffff;
	}

	g_doc_saved_val = val;
}

void
doc_show_ensoniq_state()
{
	Doc_reg	*rptr;
	int	i;

	printf("Ensoniq state\n");
	printf("c03c doc_sound_ctl: %02x, doc_saved_val: %02x\n",
		g_doc_sound_ctl, g_doc_saved_val);
	printf("doc_ptr: %04x,    num_osc_en: %02x, e0: %02x\n",
		g_c03ef_doc_ptr, g_doc_num_osc_en, doc_reg_e0);

	for(i = 0; i < 32; i += 8) {
		printf("irqp: %02x: %04x %04x %04x %04x %04x %04x %04x %04x\n",
			i,
			g_doc_regs[i].has_irq_pending,
			g_doc_regs[i + 1].has_irq_pending,
			g_doc_regs[i + 2].has_irq_pending,
			g_doc_regs[i + 3].has_irq_pending,
			g_doc_regs[i + 4].has_irq_pending,
			g_doc_regs[i + 5].has_irq_pending,
			g_doc_regs[i + 6].has_irq_pending,
			g_doc_regs[i + 7].has_irq_pending);
	}

	for(i = 0; i < 32; i += 8) {
		printf("freq: %02x: %04x %04x %04x %04x %04x %04x %04x %04x\n",
			i,
			g_doc_regs[i].freq, g_doc_regs[i + 1].freq,
			g_doc_regs[i + 2].freq, g_doc_regs[i + 3].freq,
			g_doc_regs[i + 4].freq, g_doc_regs[i + 5].freq,
			g_doc_regs[i + 6].freq, g_doc_regs[i + 7].freq);
	}

	for(i = 0; i < 32; i += 8) {
		printf("vol: %02x: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			i,
			g_doc_regs[i].vol, g_doc_regs[i + 1].vol,
			g_doc_regs[i + 2].vol, g_doc_regs[i + 3].vol,
			g_doc_regs[i + 4].vol, g_doc_regs[i + 5].vol,
			g_doc_regs[i + 6].vol, g_doc_regs[i + 6].vol);
	}

	for(i = 0; i < 32; i += 8) {
		printf("wptr: %02x: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			i,
			g_doc_regs[i].waveptr, g_doc_regs[i + 1].waveptr,
			g_doc_regs[i + 2].waveptr, g_doc_regs[i + 3].waveptr,
			g_doc_regs[i + 4].waveptr, g_doc_regs[i + 5].waveptr,
			g_doc_regs[i + 6].waveptr, g_doc_regs[i + 7].waveptr);
	}

	for(i = 0; i < 32; i += 8) {
		printf("ctl: %02x: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			i,
			g_doc_regs[i].ctl, g_doc_regs[i + 1].ctl,
			g_doc_regs[i + 2].ctl, g_doc_regs[i + 3].ctl,
			g_doc_regs[i + 4].ctl, g_doc_regs[i + 5].ctl,
			g_doc_regs[i + 6].ctl, g_doc_regs[i + 7].ctl);
	}

	for(i = 0; i < 32; i += 8) {
		printf("wsize: %02x: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			i,
			g_doc_regs[i].wavesize, g_doc_regs[i + 1].wavesize,
			g_doc_regs[i + 2].wavesize, g_doc_regs[i + 3].wavesize,
			g_doc_regs[i + 4].wavesize, g_doc_regs[i + 5].wavesize,
			g_doc_regs[i + 6].wavesize, g_doc_regs[i + 7].wavesize);
	}

	show_doc_log();

	for(i = 0; i < 32; i++) {
		rptr = &(g_doc_regs[i]);
		printf("%2d: ctl:%02x wp:%02x ws:%02x freq:%04x vol:%02x "
			"ev:%d run:%d irq:%d sz:%04x\n", i,
			rptr->ctl, rptr->waveptr, rptr->wavesize, rptr->freq,
			rptr->vol, rptr->event, rptr->running,
			rptr->has_irq_pending, rptr->size_bytes);
		printf("    acc:%08x inc:%08x st:%08x end:%08x m:%08x\n",
			rptr->cur_acc, rptr->cur_inc, rptr->cur_start,
			rptr->cur_end, rptr->cur_mask);
		printf("    compl_ds:%f samps_left:%d ev:%f ev2:%f\n",
			rptr->complete_dsamp, rptr->samps_left,
			rptr->dsamp_ev, rptr->dsamp_ev2);
	}

#if 0
	for(osc = 0; osc < 32; osc++) {
		fmax = 0.0;
		printf("osc %d has %d samps\n", osc, g_fsamp_num[osc]);
		for(i = 0; i < g_fsamp_num[osc]; i++) {
			printf("%4d: %f\n", i, g_fsamps[osc][i]);
			fmax = MY_MAX(fmax, g_fsamps[osc][i]);
		}
		printf("osc %d, fmax: %f\n", osc, fmax);
	}
#endif
}
