const char rcsid_video_c[] = "@(#)$KmKId: video.c,v 1.174 2021-01-23 22:46:19+00 kentd Exp $";

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

#include <time.h>

#include "defc.h"

extern int Verbose;

int g_a2_line_stat[200];
int g_a2_line_left_edge[200];
int g_a2_line_right_edge[200];

int g_mode_line[200];

byte g_cur_border_colors[270];

word32	g_a2_screen_buffer_changed = (word32)-1;
word32	g_full_refresh_needed = (word32)-1;

word32 g_cycs_in_40col = 0;
word32 g_cycs_in_xredraw = 0;
word32 g_refresh_bytes_xfer = 0;

extern byte *g_slow_memory_ptr;
extern int g_fatal_log;

extern double g_cur_dcycs;

extern int g_line_ref_amt;

extern int g_c034_val;
extern int g_config_control_panel;
extern int g_halt_sim;

word32 slow_mem_changed[SLOW_MEM_CH_SIZE];

word32 g_a2font_bits[0x100][8];

word32 g_superhires_scan_save[256];

Kimage g_mainwin_kimage = { 0 };
Kimage g_debugwin_kimage = { 0 };
int g_debugwin_last_total = 0;

extern int g_debug_lines_total;

extern double g_last_vbl_dcycs;

double	g_video_dcycs_check_input = 0.0;
int	g_video_act_margin_left = BASE_MARGIN_LEFT;
int	g_video_act_margin_right = BASE_MARGIN_RIGHT;
int	g_video_act_margin_top = BASE_MARGIN_TOP;
int	g_video_act_margin_bottom = BASE_MARGIN_BOTTOM;
int	g_video_act_width = X_A2_WINDOW_WIDTH;
int	g_video_act_height = X_A2_WINDOW_HEIGHT;

word32	g_palette_change_cnt[16];
int	g_border_sides_refresh_needed = 1;
int	g_border_special_refresh_needed = 1;
int	g_border_line24_refresh_needed = 1;
int	g_status_refresh_needed = 1;

int	g_vbl_border_color = 0;
int	g_border_last_vbl_changes = 0;
int	g_border_reparse = 0;

int	g_use_dhr140 = 0;
int	g_use_bw_hires = 0;

int	g_a2_new_all_stat[200];
int	g_a2_cur_all_stat[200];
word32	g_a2_8line_changes[24];
int	g_new_a2_stat_cur_line = 0;
int	g_vid_update_last_line = 0;

int g_cur_a2_stat = ALL_STAT_TEXT | ALL_STAT_ANNUNC3 |
					(0xf << BIT_ALL_STAT_TEXT_COLOR);
extern int g_save_cur_a2_stat;		/* from config.c */

word32 g_palette_8to1624[256];
word32 g_a2palette_1624[16];

word32	g_saved_line_palettes[200][8];

word32 g_cycs_in_refresh_line = 0;
word32 g_cycs_in_refresh_ximage = 0;
word32 g_cycs_in_run_16ms = 0;

int	g_num_lines_superhires = 0;
int	g_num_lines_superhires640 = 0;
int	g_num_lines_prev_superhires = 0;
int	g_num_lines_prev_superhires640 = 0;

int	g_screen_redraw_skip_count = 0;
int	g_screen_redraw_skip_amt = -1;

word32	g_alpha_mask = 0;
word32	g_red_mask = 0xff;
word32	g_green_mask = 0xff;
word32	g_blue_mask = 0xff;
int	g_red_left_shift = 16;
int	g_green_left_shift = 8;
int	g_blue_left_shift = 0;
int	g_red_right_shift = 0;
int	g_green_right_shift = 0;
int	g_blue_right_shift = 0;

char	g_status_buf[MAX_STATUS_LINES][STATUS_LINE_LENGTH + 1];
char	*g_status_ptrs[MAX_STATUS_LINES] = { 0 };
word16	g_pixels_widened[128];

int	g_video_scale_algorithm = 0;

word16 g_dhires_convert[4096];	/* look up { next4, this4, prev 4 } */

const byte g_dhires_colors_16[] = {		// Convert dhires to lores color
		0x00,	/* 0x0 black */
		0x02,	/* 0x1 dark blue */
		0x04,	/* 0x2 dark green */
		0x06,	/* 0x3 medium blue */
		0x08,	/* 0x4 brown */
		0x0a,	/* 0x5 light gray */
		0x0c,	/* 0x6 green */
		0x0e,	/* 0x7 aquamarine */
		0x01,	/* 0x8 deep red */
		0x03,	/* 0x9 purple */
		0x05,	/* 0xa dark gray */
		0x07,	/* 0xb light blue */
		0x09,	/* 0xc orange */
		0x0b,	/* 0xd pink */
		0x0d,	/* 0xe yellow */
		0x0f	/* 0xf white */
};

const int g_lores_colors[] = {		// From IIgs Technote #63
		/* rgb */
		0x000,		/* 0x0 black */
		0xd03,		/* 0x1 deep red */
		0x009,		/* 0x2 dark blue */
		0xd2d,		/* 0x3 purple */
		0x072,		/* 0x4 dark green */
		0x555,		/* 0x5 dark gray */
		0x22f,		/* 0x6 medium blue */
		0x6af,		/* 0x7 light blue */
		0x850,		/* 0x8 brown */
		0xf60,		/* 0x9 orange */
		0xaaa,		/* 0xa light gray */
		0xf98,		/* 0xb pink */
		0x1d0,		/* 0xc green */
		0xff0,		/* 0xd yellow */
		0x4f9,		/* 0xe aquamarine */
		0xfff		/* 0xf white */
};

const byte g_hires_lookup[64] = {
// Indexed by { next_bit, this_bit, prev_bit, hibit, odd_byte, odd_col }.
//  Return lores colors: 0, 3, 6, 9, 0xc, 0xf
	0x00,			// 00,0000	// black: this and next are 0
	0x00,			// 00,0001
	0x00,			// 00,0010
	0x00,			// 00,0011
	0x00,			// 00,0100
	0x00,			// 00,0101
	0x00,			// 00,0110
	0x00,			// 00,0111
	0x00,			// 00,1000
	0x00,			// 00,1001
	0x00,			// 00,1010
	0x00,			// 00,1011
	0x00,			// 00,1100
	0x00,			// 00,1101
	0x00,			// 00,1110
	0x00,			// 00,1111

	0x03,			// 01,0000	// purple
	0x03,			// 01,0001	// purple
	0x0c,			// 01,0010	// green (odd column)
	0x0c,			// 01,0011	// green
	0x06,			// 01,0100	// blue
	0x06,			// 01,0101	// blue
	0x09,			// 01,0110	// orange
	0x09,			// 01,0111	// orange
	0x0f,			// 01,1000	// white: this and prev are 1
	0x0f,			// 01,1001
	0x0f,			// 01,1010
	0x0f,			// 01,1011
	0x0f,			// 01,1100
	0x0f,			// 01,1101
	0x0f,			// 01,1110
	0x0f,			// 01,1111

	0x00,			// 10,0000	// black
	0x00,			// 10,0001	// black
	0x00,			// 10,0010	// black
	0x00,			// 10,0011	// black
	0x00,			// 10,0100	// black
	0x00,			// 10,0101	// black
	0x00,			// 10,0110	// black
	0x00,			// 10,0111	// black
	0x0c,			// 10,1000	// green
	0x0c,			// 10,1001	// green
	0x03,			// 10,1010	// purple
	0x03,			// 10,1011	// purple
	0x09,			// 10,1100	// orange
	0x09,			// 10,1101	// orange
	0x06,			// 10,1110	// blue
	0x06,			// 10,1111	// blue

	0x0f,			// 11,0000	// white
	0x0f,			// 11,0001
	0x0f,			// 11,0010
	0x0f,			// 11,0011
	0x0f,			// 11,0100
	0x0f,			// 11,0101
	0x0f,			// 11,0110
	0x0f,			// 11,0111
	0x0f,			// 11,1000
	0x0f,			// 11,1001
	0x0f,			// 11,1010
	0x0f,			// 11,1011
	0x0f,			// 11,1100
	0x0f,			// 11,1101
	0x0f,			// 11,1110
	0x0f			// 11,1111
};

const int g_screen_index[] = {
		0x000, 0x080, 0x100, 0x180, 0x200, 0x280, 0x300, 0x380,
		0x028, 0x0a8, 0x128, 0x1a8, 0x228, 0x2a8, 0x328, 0x3a8,
		0x050, 0x0d0, 0x150, 0x1d0, 0x250, 0x2d0, 0x350, 0x3d0
};

byte g_font_array[256][8] = {
#include "kegsfont.h"
};

void
video_set_red_mask(word32 red_mask)
{
	video_set_mask_and_shift(red_mask, &g_red_mask, &g_red_left_shift,
							&g_red_right_shift);
}

void
video_set_green_mask(word32 green_mask)
{
	video_set_mask_and_shift(green_mask, &g_green_mask, &g_green_left_shift,
							&g_green_right_shift);
}

void
video_set_blue_mask(word32 blue_mask)
{
	video_set_mask_and_shift(blue_mask, &g_blue_mask, &g_blue_left_shift,
							&g_blue_right_shift);
}

void
video_set_alpha_mask(word32 alpha_mask)
{
	g_alpha_mask = alpha_mask;
	printf("Set g_alpha_mask=%08x\n", alpha_mask);
}

void
video_set_mask_and_shift(word32 x_mask, word32 *mask_ptr, int *shift_left_ptr,
						int *shift_right_ptr)
{
	int	shift;
	int	i;

	/* Shift until we find first set bit in mask, then remember mask,shift*/

	shift = 0;
	for(i = 0; i < 32; i++) {
		if(x_mask & 1) {
			/* we're done! */
			break;
		}
		x_mask = x_mask >> 1;
		shift++;
	}
	*mask_ptr = x_mask;
	*shift_left_ptr = shift;
	/* Now, calculate shift_right_ptr */
	shift = 0;
	x_mask |= 1;		// make sure at least one bit is set
	for(i = 0; i < 32; i++) {
		if(x_mask >= 0x80) {
			break;
		}
		shift++;
		x_mask = x_mask << 1;
	}

	*shift_right_ptr = shift;
}

void
video_set_palette()
{
	int	i;

	for(i = 0; i < 16; i++) {
		video_update_color_raw(i, g_lores_colors[i]);
		g_a2palette_1624[i] = g_palette_8to1624[i];
	}
}

void
video_set_redraw_skip_amt(int amt)
{
	if(g_screen_redraw_skip_amt < amt) {
		g_screen_redraw_skip_amt = amt;
		printf("Set g_screen_redraw_skip_amt = %d\n", amt);
	}
}

Kimage *
video_get_kimage(int win_id)
{
	if(win_id == 0) {
		return &g_mainwin_kimage;
	}
	if(win_id == 1) {
		return &g_debugwin_kimage;
	}
	printf("win_id: %d not supported\n", win_id);
	exit(1);
}

char *
video_get_status_ptr(int line)
{
	if(line < MAX_STATUS_LINES) {
		return g_status_ptrs[line];
	}
	return 0;
}

#if 0
int
video_get_x_refresh_needed(Kimage *kimage_ptr)
{
	int	ret;

	ret = kimage_ptr->x_refresh_needed;
	kimage_ptr->x_refresh_needed = 0;

	return ret;
}
#endif

void
video_set_x_refresh_needed(Kimage *kimage_ptr, int do_refresh)
{
	kimage_ptr->x_refresh_needed = do_refresh;
}

int
video_get_active(Kimage *kimage_ptr)
{
	return kimage_ptr->active;
}

void
video_set_active(Kimage *kimage_ptr, int active)
{
	kimage_ptr->active = active;
	if(kimage_ptr != &g_mainwin_kimage) {
		adb_nonmain_check();
	}
}

void
video_init(int mdepth)
{
	word32	col[4];
	word32	val0, val1, val2, val3, next_col, next2_col;
	word32	val, cur_col;
	int	i, j;
/* Initialize video system */

	for(i = 0; i < 200; i++) {
		g_a2_line_stat[i] = -1;
		g_a2_line_left_edge[i] = 0;
		g_a2_line_right_edge[i] = 0;
	}
	for(i = 0; i < 200; i++) {
		g_a2_new_all_stat[i] = 0;
		g_a2_cur_all_stat[i] = 1;
		for(j = 0; j < 8; j++) {
			g_saved_line_palettes[i][j] = (word32)-1;
		}
	}
	for(i = 0; i < 262; i++) {
		g_cur_border_colors[i] = -1;
	}
	for(i = 0; i < 24; i++) {
		g_a2_8line_changes[i] = 0;
	}
	for(i = 0; i < 128; i++) {
		val0 = i;
		val1 = 0;
		for(j = 0; j < 7; j++) {
			val1 = val1 << 2;
			if(val0 & 0x40) {
				val1 |= 3;
			}
			val0 = val0 << 1;
		}
		g_pixels_widened[i] = val1;
	}

	prepare_a2_font();

	g_new_a2_stat_cur_line = 0;

	vid_printf("Zeroing out video memory\n");

	for(i = 0; i < SLOW_MEM_CH_SIZE; i++) {
		slow_mem_changed[i] = (word32)-1;
	}

	/* create g_dhires_convert[] array */
	for(i = 0; i < 4096; i++) {
		/* Convert index bits 11:0 where 3:0 is the previous color */
		/*  and 7:4 is the current color to translate */
		/*  Bit 4 will be the first pixel displayed on the screen */
		for(j = 0; j < 4; j++) {
			cur_col = (i >> (1 + j)) & 0xf;
			next_col = (i >> (2 + j)) & 0xf;
			next2_col = (i >> (3 + j)) & 0xf;
			cur_col = (((cur_col << 4) + cur_col) >> (3 - j)) & 0xf;

			if((cur_col == 0xf) || (next_col == 0xf) ||
							(next2_col == 0xf)) {
				cur_col = 0xf;
				col[j] = cur_col;
			} else if((cur_col == 0) || (next_col == 0) ||
						(next2_col == 0)) {
				cur_col = 0;
				col[j] = cur_col;
			} else {
				col[j] = cur_col;
			}
		}
		if(g_use_dhr140) {
			for(j = 0; j < 4; j++) {
				col[j] = (i >> 4) & 0xf;
			}
		}
		val0 = g_dhires_colors_16[col[0] & 0xf];
		val1 = g_dhires_colors_16[col[1] & 0xf];
		val2 = g_dhires_colors_16[col[2] & 0xf];
		val3 = g_dhires_colors_16[col[3] & 0xf];
		val = val0 | (val1 << 4) | (val2 << 8) | (val3 << 12);
		g_dhires_convert[i] = val;
		if((i == 0x7bc) || (i == 0xfff)) {
			//printf("g_dhires_convert[%03x] = %04x\n", i, val);
		}
	}

	video_init_kimage(&g_mainwin_kimage, X_A2_WINDOW_WIDTH,
					X_A2_WINDOW_HEIGHT + 7*16 + 16);

	video_init_kimage(&g_debugwin_kimage, 80*8 + 8 + 8, 25*16 + 8 + 8);

	change_display_mode(g_cur_dcycs);
	video_reset();
	video_update_through_line(262);
	debugger_init();

	printf("g_mainwin_kimage created and init'ed\n");
	fflush(stdout);
}

void
video_init_kimage(Kimage *kimage_ptr, int width, int height)
{
	int	i;

	kimage_ptr->wptr = (word32 *)calloc(1, width * height * 4);
	kimage_ptr->a2_width_full = width;
	kimage_ptr->a2_height_full = height;
	kimage_ptr->a2_width = width;
	kimage_ptr->a2_height = height;
	kimage_ptr->x_width = width;
	kimage_ptr->x_height = height;
	kimage_ptr->x_refresh_needed = 1;
	kimage_ptr->active = 0;

	kimage_ptr->scale_width_to_a2 = 0x10000;
	kimage_ptr->scale_width_a2_to_x = 0x10000;
	kimage_ptr->scale_height_to_a2 = 0x10000;
	kimage_ptr->scale_height_a2_to_x = 0x10000;
	kimage_ptr->num_change_rects = 0;
	for(i = 0; i <= MAX_SCALE_SIZE; i++) {
		kimage_ptr->scale_width[i] = i;
		kimage_ptr->scale_height[i] = i;
	}
}

void
show_a2_line_stuff()
{
	int	i;

	for(i = 0; i < 200; i++) {
		printf("line: %d: stat: %04x, "
			"left_edge:%d, right_edge:%d\n",
			i, g_a2_line_stat[i],
			g_a2_line_left_edge[i],
			g_a2_line_right_edge[i]);
	}

	printf("new_a2_stat_cur_line: %d, cur_a2_stat:%04x\n",
		g_new_a2_stat_cur_line, g_cur_a2_stat);
	for(i = 0; i < 200; i++) {
		printf("cur_all[%d]: %03x new_all: %03x\n", i,
			g_a2_cur_all_stat[i], g_a2_new_all_stat[i]);
	}

}

int	g_flash_count = 0;

void
video_reset()
{
	int	stat;
	int	i;

	stat = ALL_STAT_TEXT | ALL_STAT_ANNUNC3 |
					(0xf << BIT_ALL_STAT_TEXT_COLOR);
	if(g_use_bw_hires) {
		stat |= ALL_STAT_COLOR_C021;
	}
	if(g_config_control_panel) {
		/* Don't update cur_a2_stat when in configuration panel */
		g_save_cur_a2_stat = stat;
	} else {
		g_cur_a2_stat = stat;
	}

	for(i = 0; i < 16; i++) {
		g_palette_change_cnt[i] = 0;
	}
}

word32	g_cycs_in_check_input = 0;

void
video_update()
{
	int	did_video;

	if(g_fatal_log > 0) {
		// NOT IMPLEMENTED YET
		//adb_all_keys_up();
		clear_fatal_logs();
	}

	g_screen_redraw_skip_count--;
	did_video = 0;
	if(g_screen_redraw_skip_count < 0) {
		did_video = 1;
		video_update_event_line(262);
		update_border_info();
		debugger_redraw_screen(&g_debugwin_kimage);
		g_screen_redraw_skip_count = g_screen_redraw_skip_amt;
	}

	/* update flash */
	g_flash_count++;
	if(g_flash_count >= 30) {
		g_flash_count = 0;
		g_cur_a2_stat ^= ALL_STAT_FLASH_STATE;
		change_display_mode(g_cur_dcycs);
	}

	if(did_video) {
		g_new_a2_stat_cur_line = 0;
		g_a2_new_all_stat[0] = g_cur_a2_stat;
		g_vid_update_last_line = 0;
		video_update_through_line(0);
	}
}

int
video_all_stat_to_line_stat(int line, int new_all_stat)
{
	int	page, color, dbl;
	int	st80, hires, annunc3, mix_t_gr;
	int	altchar, text_color, bg_color, flash_state;
	int	mode;

	st80 = new_all_stat & ALL_STAT_ST80;
	hires = new_all_stat & ALL_STAT_HIRES;
	annunc3 = new_all_stat & ALL_STAT_ANNUNC3;
	mix_t_gr = new_all_stat & ALL_STAT_MIX_T_GR;

	page = EXTRU(new_all_stat, 31 - BIT_ALL_STAT_PAGE2, 1) && !st80;
	color = EXTRU(new_all_stat, 31 - BIT_ALL_STAT_COLOR_C021, 1);
	dbl = EXTRU(new_all_stat, 31 - BIT_ALL_STAT_VID80, 1);

	altchar = 0; text_color = 0; bg_color = 0; flash_state = 0;

	if(new_all_stat & ALL_STAT_SUPER_HIRES) {
		mode = MODE_SUPER_HIRES;
		page = 0; dbl = 0; color = 0;
	} else {
		if(line >= 192) {
			mode = MODE_BORDER;
			page = 0; dbl = 0; color = 0;
		} else if((new_all_stat & ALL_STAT_TEXT) ||
						(line >= 160 && mix_t_gr)) {
			mode = MODE_TEXT;
			color = 0;
			altchar = EXTRU(new_all_stat,
					31 - BIT_ALL_STAT_ALTCHARSET, 1);
			text_color = EXTRU(new_all_stat,
					31 - BIT_ALL_STAT_TEXT_COLOR, 4);
			bg_color = EXTRU(new_all_stat,
					31 - BIT_ALL_STAT_BG_COLOR, 4);
			flash_state = EXTRU(new_all_stat,
					31 - BIT_ALL_STAT_FLASH_STATE, 1);
			if(altchar) {
				/* don't bother flashing if altchar on */
				flash_state = 0;
			}
		} else {
			/* obey the graphics mode */
			dbl = dbl && !annunc3;
			if(hires) {
				color = color | EXTRU(new_all_stat,
					31 - BIT_ALL_STAT_DIS_COLOR_DHIRES, 1);
				mode = MODE_HGR;
			} else {
				mode = MODE_GR;
			}
		}
	}

	return((text_color << 12) + (bg_color << 8) + (altchar << 7) +
		(mode << 4) + (flash_state << 3) + (page << 2) +
		(color << 1) + dbl);
}

void
change_display_mode(double dcycs)
{
	int	line, tmp_line;

	line = ((get_lines_since_vbl(dcycs) + 0xff) >> 8);
	if(line < 0) {
		line = 0;
		halt_printf("Line < 0!\n");
	}
	tmp_line = MY_MIN(199, line);

	dbg_log_info(dcycs, (g_cur_a2_stat << 12) | (line & 0xfff), 2);

	video_update_all_stat_through_line(tmp_line);

	if(line < 200) {
		g_a2_new_all_stat[line] = g_cur_a2_stat;
	}
	/* otherwise, g_cur_a2_stat is covered at the end of vbl */
}

void
video_update_all_stat_through_line(int line)
{
	int	start_line;
	int	prev_stat;
	int	max_line;
	int	i;

	start_line = g_new_a2_stat_cur_line;
	prev_stat = g_a2_new_all_stat[start_line];

	max_line = MY_MIN(199, line);

	for(i = start_line + 1; i <= max_line; i++) {
		g_a2_new_all_stat[i] = prev_stat;
	}
	g_new_a2_stat_cur_line = max_line;
}


#define MAX_BORDER_CHANGES	16384

STRUCT(Border_changes) {
	float	fcycs;
	int	val;
};

Border_changes g_border_changes[MAX_BORDER_CHANGES];
int	g_num_border_changes = 0;

void
change_border_color(double dcycs, int val)
{
	int	pos;

	pos = g_num_border_changes;
	g_border_changes[pos].fcycs = dcycs - g_last_vbl_dcycs;
	g_border_changes[pos].val = val;

	pos++;
	g_num_border_changes = pos;

	if(pos >= MAX_BORDER_CHANGES) {
		halt_printf("num border changes: %d\n", pos);
		g_num_border_changes = 0;
	}
}

void
update_border_info()
{
	double	dlines_per_dcyc;
	double	dcycs, dline, dcyc_line_start;
	int	offset;
	int	new_line_offset, last_line_offset;
	int	new_line;
	int	new_val;
	int	limit;
	int	color_now;
	int	i;

	/* to get this routine to redraw the border, change */
	/*  g_vbl_border_color, set g_border_last_vbl_changes = 1 */
	/*  and change the cur_border_colors[] array */

	color_now = g_vbl_border_color;

	dlines_per_dcyc = (double)(1.0 / 65.0);
	limit = g_num_border_changes;
	if(g_border_last_vbl_changes || limit || g_border_reparse) {
		/* add a dummy entry */
		g_border_changes[limit].fcycs = DCYCS_IN_16MS + 21.0;
		g_border_changes[limit].val = (g_c034_val & 0xf);
		limit++;
	}
	last_line_offset = ((word32)-1 << 8) + 44;
	for(i = 0; i < limit; i++) {
		dcycs = g_border_changes[i].fcycs;
		dline = dcycs * dlines_per_dcyc;
		new_line = (int)dline;
		dcyc_line_start = (double)new_line * 65.0;
		offset = ((int)(dcycs - dcyc_line_start)) & 0xff;

		/* here comes the tricky part */
		/* offset is from 0 to 65, where 0-3 is the right border of */
		/*  the previous line, 4-20 is horiz blanking, 21-24 is the */
		/*  left border and 25-64 is the main window */
		/* Convert this to a new notation which is 0-3 is the left */
		/*  border, 4-43 is the main window, and 44-47 is the right */
		/* basically, add -21 to offset, and wrap < 0 to previous ln */
		/* note this makes line -1 offset 44-47 the left hand border */
		/* for true line 261 on the screen */
		offset -= 21;
		if(offset < 0) {
			new_line--;
			offset += 64;
		}
		new_val = g_border_changes[i].val;
		new_line_offset = (new_line << 8) + offset;

		if((new_line_offset < -256) ||
					(new_line_offset > (262*256 + 0x80))) {
			printf("new_line_offset: %05x\n", new_line_offset);
			new_line_offset = last_line_offset;
		}
		while(last_line_offset < new_line_offset) {
			/* see if this will finish it */
			if((last_line_offset & -256)==(new_line_offset & -256)){
				update_border_line(last_line_offset,
						new_line_offset, color_now);
				last_line_offset = new_line_offset;
			} else {
				update_border_line(last_line_offset,
						(last_line_offset & -256) + 65,
						color_now);
				last_line_offset =(last_line_offset & -256)+256;
			}
		}

		color_now = new_val;
	}

#if 0
	if(g_num_border_changes) {
		printf("Border changes: %d\n", g_num_border_changes);
	}
#endif

	if(limit > 1) {
		g_border_last_vbl_changes = 1;
	} else {
		g_border_last_vbl_changes = 0;
	}

	g_num_border_changes = 0;
	g_border_reparse = 0;
	g_vbl_border_color = (g_c034_val & 0xf);
}

void
update_border_line(int st_line_offset, int end_line_offset, int color)
{
	int	st_offset, end_offset, left, right, width, mode, line;

	line = st_line_offset >> 8;
	if(line != (end_line_offset >> 8)) {
		halt_printf("ubl, %04x %04x %02x!\n", st_line_offset,
					end_line_offset, color);
	}
	if(line < -1 || line >= 262) {
		halt_printf("ubl-b, mod line is %d\n", line);
		line = 0;
	}
	if(line < 0 || line >= 262) {
		line = 0;
	}

	st_offset = st_line_offset & 0xff;
	end_offset = end_line_offset & 0xff;

	if((st_offset == 0) && (end_offset >= 0x41) && !g_border_reparse) {
		/* might be the same as last time, save some work */
		if(g_cur_border_colors[line] == color) {
			return;
		}
		g_cur_border_colors[line] = color;
	} else {
		g_cur_border_colors[line] = -1;
	}

	/* 0-3: left border, 4-43: main window, 44-47: right border */
	/* 48-65: horiz blanking */
	/* first, do the sides from line 0 to line 199 */
	if((line < 200) || (line >= 262)) {
		if(line >= 262) {
			line = 0;
		}
		if(st_offset < 4) {
			/* left side */
			left = st_offset;
			right = MY_MIN(4, end_offset);
			video_border_pixel_write(&g_mainwin_kimage,
				g_video_act_margin_top + 2*line, 2, color,
				(left * BORDER_WIDTH)/4,
				(right * BORDER_WIDTH) / 4);

			g_border_sides_refresh_needed = 1;
		}
		if((st_offset < 48) && (end_offset >= 44)) {
			/* right side */
			mode = (g_a2_line_stat[line] >> 4) & 7;
			width = BORDER_WIDTH;
			if(mode != MODE_SUPER_HIRES) {
				width += 80;
			}
			left = MY_MAX(0, st_offset - 44);
			right = MY_MIN(4, end_offset - 44);
			video_border_pixel_write(&g_mainwin_kimage,
				g_video_act_margin_top + 2*line, 2, color,
				X_A2_WINDOW_WIDTH - width +
						(left * width/4),
				X_A2_WINDOW_WIDTH - width +
						(right * width/4));
			g_border_sides_refresh_needed = 1;
		}
	}

	if((line >= 192) && (line < 200)) {
		if(st_offset < 44 && end_offset > 4) {
			left = MY_MAX(0, st_offset - 4);
			right = MY_MIN(40, end_offset - 4);
			video_border_pixel_write(&g_mainwin_kimage,
				g_video_act_margin_top + 2*line, 2, color,
				g_video_act_margin_left + (left * 640 / 40),
				g_video_act_margin_left + (right * 640 / 40));
			g_border_line24_refresh_needed = 1;
		}
	}

	/* now do the bottom, lines 200 to 215 */
	if((line >= 200) && (line < (200 + BASE_MARGIN_BOTTOM/2)) ) {
		line -= 200;
		left = st_offset;
		right = MY_MIN(48, end_offset);
		video_border_pixel_write(&g_mainwin_kimage,
			g_video_act_margin_top + 200*2 + 2*line, 2,
			color,
			(left * X_A2_WINDOW_WIDTH / 48),
			(right * X_A2_WINDOW_WIDTH / 48));
		g_border_special_refresh_needed = 1;
	}

	/* and top, lines 236 to 262 */
	if((line >= (262 - BASE_MARGIN_TOP/2)) && (line < 262)) {
		line -= (262 - BASE_MARGIN_TOP/2);
		left = st_offset;
		right = MY_MIN(48, end_offset);
		video_border_pixel_write(&g_mainwin_kimage, 2*line, 2, color,
			(left * X_A2_WINDOW_WIDTH / 48),
			(right * X_A2_WINDOW_WIDTH / 48));
		g_border_special_refresh_needed = 1;
	}
}

void
video_border_pixel_write(Kimage *kimage_ptr, int starty, int num_lines,
			int color, int st_off, int end_off)
{
	word32	*wptr, *wptr0;
	word32	pixel;
	int	width, width_full, offset;
	int	i, j;

	if(end_off <= st_off) {
		return;
	}

	width = end_off - st_off;
	width_full = kimage_ptr->a2_width_full;

	if(width > width_full) {
		halt_printf("border write but width %d > act %d\n", width,
				width_full);
		return;
	}
	if((starty + num_lines) > kimage_ptr->a2_height) {
		halt_printf("border write line %d, > act %d\n",
				starty+num_lines, kimage_ptr->a2_height);
		return;
	}
	pixel = g_a2palette_1624[color & 0xf];

	offset = starty * width_full;
	wptr0 = kimage_ptr->wptr;
	wptr0 += offset;
	for(i = 0; i < num_lines; i++) {
		wptr = wptr0 + st_off;
		for(j = 0; j < width; j++) {
			*wptr++ = pixel;
		}
		wptr0 += width_full;
	}
}


#define CH_SETUP_A2_VID(mem_ptr, ch_mask, reparse, do_clear)		\
	ch_ptr = &(slow_mem_changed[mem_ptr >> CHANGE_SHIFT]);		\
	bits_per_line = 40 >> SHIFT_PER_CHANGE;				\
	ch_shift_amount = (mem_ptr >> SHIFT_PER_CHANGE) & 0x1f;		\
	mask_per_line = (-(1 << (32 - bits_per_line)));			\
	mask_per_line = mask_per_line >> ch_shift_amount;		\
	ch_mask = *ch_ptr & mask_per_line;				\
	if(do_clear) {							\
		*ch_ptr = *ch_ptr & (~ch_mask);				\
	}								\
	ch_mask = ch_mask << ch_shift_amount;				\
									\
	if(reparse) {							\
		ch_mask = - (1 << (32 - bits_per_line));		\
	}

#define CH_LOOP_A2_VID(ch_mask, ch_tmp)					\
		ch_tmp = ch_mask & 0x80000000;				\
		ch_mask = ch_mask << 1;					\
		if(!ch_tmp) {						\
			continue;					\
		}

void
redraw_changed_text(int start_offset, int start_line, int reparse,
	word32 *in_wptr, int altcharset, word32 bg_pixel,
	word32 fg_pixel, int pixels_per_line, int dbl)
{
	byte	str_buf[81];
	word32	*ch_ptr;
	byte	*slow_mem_ptr;
	word32	ch_mask, line_mask, mask_per_line, mem_ptr, val0, val1;
	int	flash_state, y, bits_per_line, ch_shift_amount, st_line_mod8;
	int	x1, x2;

	// Redraws a single line, will be called 8 lines to finish one byte.
	//  To handle slow_mem_changed[], clear it on line 0, and then use
	//  g_a2_8line_changes[y] to remember changes to draw on lines 1-7
	st_line_mod8 = start_line & 7;

	y = start_line >> 3;
	line_mask = 1 << y;
	mem_ptr = 0x400 + g_screen_index[y] + start_offset;
	if((mem_ptr < 0x400) || (mem_ptr >= 0xc00)) {
		halt_printf("redraw_changed_text: mem_ptr: %08x, y:%d, "
			"start_offset:%04x\n", mem_ptr, y, start_offset);
		return;
	}

	CH_SETUP_A2_VID(mem_ptr, ch_mask, reparse, (st_line_mod8 == 0));
	/* avoid clearing changed bits unless we are line 0 (mod 8) */

	ch_mask |= g_a2_8line_changes[y];
	g_a2_8line_changes[y] |= ch_mask;
	if(ch_mask == 0) {
		return;
	}

	g_a2_screen_buffer_changed |= line_mask;
	x2 = 0;
	slow_mem_ptr = &(g_slow_memory_ptr[mem_ptr]);
	flash_state = -0x40;
	if(g_cur_a2_stat & ALL_STAT_FLASH_STATE) {
		flash_state = 0x40;
	}

	for(x1 = 0; x1 < 40; x1++) {
		val0 = slow_mem_ptr[0x10000];
		val1 = *slow_mem_ptr++;
		if(!altcharset) {
			if((val0 >= 0x40) && (val0 < 0x80)) {
				val0 += flash_state;
			}
			if((val1 >= 0x40) && (val1 < 0x80)) {
				val1 += flash_state;
			}
		}
		if(dbl) {
			str_buf[x2++] = val0;		// aux mem
		}
		str_buf[x2++] = val1;			// main mem
	}
	str_buf[x2] = 0;				// null terminate

	redraw_changed_string(&str_buf[0], start_line, ch_mask, in_wptr,
		bg_pixel, fg_pixel, pixels_per_line, dbl);
}

void
redraw_changed_string(byte *bptr, int start_line, word32 ch_mask,
	word32 *in_wptr, word32 bg_pixel,
	word32 fg_pixel, int pixels_per_line, int dbl)
{
	register word32 start_time, end_time;
	word32	*wptr;
	word32	val0, val1, val2, val3, pixel, ch_tmp;
	int	shift_per, left, right, st_line_mod8, offset, pos;
	int	x1, x2, j;

	left = 40;
	right = 0;

	GET_ITIMER(start_time);

	shift_per = (1 << SHIFT_PER_CHANGE);
	st_line_mod8 = start_line & 7;

	for(x1 = 0; x1 < 40; x1 += shift_per) {

		CH_LOOP_A2_VID(ch_mask, ch_tmp);

		left = MY_MIN(x1, left);
		right = MY_MAX(x1 + shift_per, right);
		offset = (start_line * 2 * pixels_per_line) + x1*14;
		pos = x1;
		if(dbl) {
			pos = pos * 2;
		}

		wptr = in_wptr + offset;

		for(x2 = 0; x2 < shift_per; x2++) {
			val0 = bptr[pos];
			if(dbl) {
				pos++;
			}
			val1 = bptr[pos++];
			val2 = g_a2font_bits[val0][st_line_mod8];
			val3 = g_a2font_bits[val1][st_line_mod8];
			// val2, [6:0] is 80-column character bits, and
			//  [21:8] are the 40-column char bits (double-wide)
			if(dbl) {
				val2 = (val3 << 7) | (val2 & 0x7f);
			} else {
				val2 = val3 >> 8;	// 40-column format
			}
			for(j = 0; j < 14; j++) {
				pixel = bg_pixel;
				if(val2 & 1) {		// LSB is first pixel
					pixel = fg_pixel;
				}
				wptr[pixels_per_line] = pixel;
				*wptr++ = pixel;
				val2 = val2 >> 1;
			}
		}
	}
	GET_ITIMER(end_time);
	if(start_line < 200) {
		g_a2_line_left_edge[start_line] = (left*14);
		g_a2_line_right_edge[start_line] = (right*14);
	}

	if((left >= right) || (left < 0) || (right < 0)) {
		printf("line %d, 40: left >= right: %d >= %d\n",
			start_line, left, right);
	}

	g_cycs_in_40col += (end_time - start_time);
}

void
redraw_changed_gr(int start_offset, int start_line, int reparse,
	word32 *in_wptr, int pixels_per_line, int dbl)
{
	word32	*ch_ptr, *wptr;
	byte	*slow_mem_ptr;
	word32	ch_mask, ch_tmp, line_mask, mask_per_line, mem_ptr, val0, val1;
	word32	pixel0, pixel1;
	int	y, bits_per_line, shift_per, ch_shift_amount;
	int	left, right, st_line_mod8, st_line, offset;
	int	x1, x2, i, j;

	st_line_mod8 = start_line & 7;
	st_line = start_line;

	y = start_line >> 3;
	line_mask = 1 << (y);
	mem_ptr = 0x400 + g_screen_index[y] + start_offset;
	if((mem_ptr < 0x400) || (mem_ptr >= 0xc00)) {
		halt_printf("redraw_changed_gr: mem_ptr: %08x, y:%d, "
			"start_offset:%04x\n", mem_ptr, y, start_offset);
		return;
	}

	CH_SETUP_A2_VID(mem_ptr, ch_mask, reparse, (st_line_mod8 == 0));
	/* avoid clearing changed bits unless we are line 0 (mod 8) */

	ch_mask |= g_a2_8line_changes[y];
	g_a2_8line_changes[y] |= ch_mask;
	if(ch_mask == 0) {
		return;
	}

	shift_per = (1 << SHIFT_PER_CHANGE);

	g_a2_screen_buffer_changed |= line_mask;

	left = 40;
	right = 0;

	for(x1 = 0; x1 < 40; x1 += shift_per) {

		CH_LOOP_A2_VID(ch_mask, ch_tmp);

		left = MY_MIN(x1, left);
		right = MY_MAX(x1 + shift_per, right);
		slow_mem_ptr = &(g_slow_memory_ptr[mem_ptr + x1]);
		offset = (st_line * 2 * pixels_per_line) + x1*14;

		wptr = in_wptr + offset;

		for(x2 = 0; x2 < shift_per; x2++) {
			val0 = slow_mem_ptr[0x10000];
			val1 = *slow_mem_ptr++;

			if(st_line_mod8 >= 4) {
				val0 = val0 >> 4;
				val1 = val1 >> 4;
			}
			if(dbl) {	// aux pixel is { [2:0],[3] }
				val0 = (val0 << 1) | ((val0 >> 3) & 1);
			} else {
				val0 = val1;
			}
			pixel0 = g_a2palette_1624[val0 & 0xf];
			pixel1 = g_a2palette_1624[val1 & 0xf];
			for(i = 0; i < 2; i++) {
				if(i) {
					pixel0 = pixel1;
				}
				for(j = 0; j < 7; j++) {
					wptr[pixels_per_line] = pixel0;
					*wptr++ = pixel0;
				}
			}
		}
	}
	g_a2_line_left_edge[st_line] = (left*14);
	g_a2_line_right_edge[st_line] = (right*14);

	if((left >= right) || (left < 0) || (right < 0)) {
		printf("line %d, 40: left >= right: %d >= %d\n",
			start_line, left, right);
	}
}

void
video_hgr_line_segment(byte *slow_mem_ptr, word32 *wptr, int x1,
		int monochrome, int dbl, int pixels_per_line, int st_line)
{
	word32	val0, val1, val2, prev_bits, val1_hi, dbl_step, pixel, color;
	int	shift_per, shift;
	int	x2, i;

	shift_per = (1 << SHIFT_PER_CHANGE);

	prev_bits = 0;
	if(x1) {
		prev_bits = (slow_mem_ptr[-1] >> 3) & 0xf;
		if(!dbl) {		// prev_bits is 4 bits, widen to 8
			prev_bits = g_pixels_widened[prev_bits] >> 4;
		}
		prev_bits = prev_bits & 0xf;
	}
	for(x2 = 0; x2 < shift_per; x2++) {
		val0 = slow_mem_ptr[0x10000];
		val1 = *slow_mem_ptr++;
		val2 = slow_mem_ptr[0x10000];	// next pixel, aux mem
		if((x1 + x2) >= 39) {
			val2 = 0;
		}
		val1_hi = ((val1 >> 5) & 4) | ((x2 & 1) << 1);
			// Hi-order bit in bit 2, odd pixel is in bit 0

		dbl_step = 3;
		if(dbl) { // aux+1[6:0], main[6:0], aux[6:0], prev[3:0]
			val0 = (val2 << 18) | ((val1 & 0x7f) << 11) |
							((val0 & 0x7f) << 4);
			if(!monochrome && (x2 & 1)) {	// Get 6 bits from prev
				val0 = (val0 << 2);
				dbl_step = 1;
			}
			val0 = val0 | prev_bits;
		} else if(monochrome) {
			val0 = g_pixels_widened[val1 & 0x7f] << 4;
		} else {			// color, normal hgr
			val2 = g_pixels_widened[*slow_mem_ptr & 0x7f];
			val0 = ((val1 & 0x7f) << 4) | prev_bits | (val2 << 11);
		}
#if 0
		if(st_line < 8) {
			printf("hgrl %d c:%d,d:%d, off:%03x val0:%02x 1:%02x\n",
				st_line, monochrome, dbl, x1 + x2, val0, val1);
		}
#endif
		for(i = 0; i < 14; i++) {
			color = 0;			// black
			if(monochrome) {
				if(val0 & 0x10) {
					color = 0xf;	// white
				}
				val0 = val0 >> 1;
			} else {			// color
				if(dbl) {
					color = g_dhires_convert[val0 & 0xfff];
					shift = (x2 + x2 + i) & 3;
					color = color >> (4 * shift);
					if((i & 3) == dbl_step) {
						val0 = val0 >> 4;
					}
				} else {
					val2 = (val0 & 0x38) ^ val1_hi ^(i & 3);
					color = g_hires_lookup[val2 & 0x7f];
					if(i & 1) {
						val0 = val0 >> 1;
					}
				}
			}
			pixel = g_a2palette_1624[color & 0xf];
			wptr[pixels_per_line] = pixel;
			*wptr++ = pixel;
		}
		if(dbl && ((x2 & 1) == 0)) {
			prev_bits = val0 & 0x3f;
		} else {
			prev_bits = val0 & 0xf;
		}
	}
}

void
redraw_changed_hgr(int start_offset, int start_line, int reparse,
	word32 *in_wptr, int pixels_per_line, int monochrome,
	int dbl)
{
	word32	*ch_ptr, *wptr;
	byte	*slow_mem_ptr;
	word32	ch_mask, ch_tmp, line_mask, mask_per_line, mem_ptr;
	int	y, bits_per_line, shift_per, ch_shift_amount;
	int	left, right, st_line_mod8, st_line, offset;
	int	x1;

	st_line_mod8 = start_line & 7;
	st_line = start_line;

	y = start_line >> 3;
	line_mask = 1 << (y);
	mem_ptr = 0x2000 + g_screen_index[y] + start_offset +
						(st_line_mod8 * 0x400);
	if((mem_ptr < 0x2000) || (mem_ptr >= 0x6000)) {
		halt_printf("redraw_changed_hgr: mem_ptr: %08x, y:%d, "
			"start_offset:%04x\n", mem_ptr, y, start_offset);
		return;
	}

	CH_SETUP_A2_VID(mem_ptr, ch_mask, reparse, 1);
	/* avoid clearing changed bits unless we are line 0 (mod 8) */

	ch_mask |= g_a2_8line_changes[y];
	g_a2_8line_changes[y] |= ch_mask;
	if(ch_mask == 0) {
		return;
	}
	// Hires depends on adjacent bits, so also reparse adjacent regions
	//  to handle redrawing of pixels on the boundaries
	ch_mask = ch_mask | (ch_mask >> 1) | (ch_mask << 1);

	shift_per = (1 << SHIFT_PER_CHANGE);

	g_a2_screen_buffer_changed |= line_mask;

	left = 40;
	right = 0;

	for(x1 = 0; x1 < 40; x1 += shift_per) {

		CH_LOOP_A2_VID(ch_mask, ch_tmp);

		left = MY_MIN(x1, left);
		right = MY_MAX(x1 + shift_per, right);
		slow_mem_ptr = &(g_slow_memory_ptr[mem_ptr + x1]);
		offset = (st_line * 2 * pixels_per_line) + x1*14;

		wptr = in_wptr + offset;
		video_hgr_line_segment(slow_mem_ptr, wptr, x1, monochrome,
					dbl, pixels_per_line, st_line);
	}
	g_a2_line_left_edge[st_line] = (left*14);
	g_a2_line_right_edge[st_line] = (right*14);

	if((left >= right) || (left < 0) || (right < 0)) {
		printf("line %d, 40: left >= right: %d >= %d\n",
			start_line, left, right);
	}
}

int
video_rebuild_super_hires_palette(word32 scan_info, int line, int reparse)
{
	word32	*word_ptr, *ch_ptr;
	byte	*byte_ptr;
	word32	ch_mask, mask_per_line, scan, old_scan, val0, val1;
	int	bits_per_line, diffs, ch_bit_offset, ch_word_offset, palette;
	int	j;

	palette = scan_info & 0xf;

	ch_ptr = &(slow_mem_changed[0x9e00 >> CHANGE_SHIFT]);
	ch_bit_offset = (palette << 5) >> SHIFT_PER_CHANGE;
	ch_word_offset = ch_bit_offset >> 5;
	ch_bit_offset = ch_bit_offset & 0x1f;
	bits_per_line = (0x20 >> SHIFT_PER_CHANGE);
	mask_per_line = -(1 << (32 - bits_per_line));
	mask_per_line = mask_per_line >> ch_bit_offset;

	ch_mask = ch_ptr[ch_word_offset] & mask_per_line;
	ch_ptr[ch_word_offset] &= ~mask_per_line;	/* clear the bits */

	old_scan = g_superhires_scan_save[line];
	scan = (scan_info & 0xfaf) + (g_palette_change_cnt[palette] << 12);
	g_superhires_scan_save[line] = scan;

#if 0
	if(line == 1) {
		word_ptr = (word32 *)&(g_slow_memory_ptr[0x19e00+palette*0x20]);
		printf("y1vrshp, ch:%08x, s:%08x,os:%08x %d = %08x %08x %08x "
			"%08x %08x %08x %08x %08x\n",
			ch_mask, scan, old_scan, reparse,
			word_ptr[0], word_ptr[1], word_ptr[2], word_ptr[3],
			word_ptr[4], word_ptr[5], word_ptr[6], word_ptr[7]);
	}
#endif

	diffs = reparse | ((scan ^ old_scan) & 0xf0f);
		/* we must do full reparse if palette changed for this line */

	if(!diffs && (ch_mask == 0) && (((scan ^ old_scan) & (~0xf0)) == 0)) {
		/* nothing changed, get out fast */
		return 0;
	}

	if(ch_mask) {
		/* indicates the palette has changed, and other scan lines */
		/*  using this palette need to do a full 32-byte compare to */
		/*  decide if they need to update or not */
		g_palette_change_cnt[palette]++;
	}

	word_ptr = (word32 *)&(g_slow_memory_ptr[0x19e00 + palette*0x20]);
	for(j = 0; j < 8; j++) {
		if(word_ptr[j] != g_saved_line_palettes[line][j]) {
			diffs = 1;
			break;
		}
	}

	if(diffs == 0) {
		return 0;
	}

	/* first, save this word_ptr into saved_line_palettes */
	byte_ptr = (byte *)word_ptr;
	for(j = 0; j < 8; j++) {
		g_saved_line_palettes[line][j] = word_ptr[j];
	}

	byte_ptr = (byte *)word_ptr;
	/* this palette has changed */
	for(j = 0; j < 16; j++) {
		val0 = *byte_ptr++;
		val1 = *byte_ptr++;
		video_update_color_raw(palette*16 + j, (val1<<8) + val0);
	}

	return 1;
}

word32
redraw_changed_super_hires_oneline(word32 *in_wptr, int pixels_per_line,
				int y, int scan, word32 ch_mask)
{
	word32	*palptr, *wptr;
	byte	*slow_mem_ptr;
	word32	mem_ptr, val0, ch_tmp, pal, pix0, pix1, pix2, pix3, save_pix;
	int	offset, shift_per, left, right;
	int	x1, x2;

	mem_ptr = 0x12000 + (0xa0 * y);

	shift_per = (1 << SHIFT_PER_CHANGE);
	pal = (scan & 0xf);

	save_pix = 0;
	if(scan & 0x20) {			// Fill mode
		ch_mask = -1;
	}

	palptr = &(g_palette_8to1624[pal * 16]);
	left = 160;
	right = 0;

	for(x1 = 0; x1 < 0xa0; x1 += shift_per) {

		CH_LOOP_A2_VID(ch_mask, ch_tmp);

		left = MY_MIN(x1, left);
		right = MY_MAX(x1 + shift_per, right);

		slow_mem_ptr = &(g_slow_memory_ptr[mem_ptr + x1]);
		offset = y*2*pixels_per_line + x1*4;

		wptr = in_wptr + offset;

		for(x2 = 0; x2 < shift_per; x2++) {
			val0 = *slow_mem_ptr++;

			if(scan & 0x80) {		// 640 mode
				pix0 = (val0 >> 6) & 3;
				pix1 = (val0 >> 4) & 3;
				pix2 = (val0 >> 2) & 3;
				pix3 = val0 & 3;
				pix0 = palptr[pix0 + 8];
				pix1 = palptr[pix1 + 12];
				pix2 = palptr[pix2 + 0];
				pix3 = palptr[pix3 + 4];
			} else {	/* 320 mode */
				pix0 = (val0 >> 4);
				pix2 = (val0 & 0xf);
				if(scan & 0x20) {	// Fill mode
					if(!pix0) {	// 0 = repeat last color
						pix0 = save_pix;
					}
					if(!pix2) {
						pix2 = pix0;
					}
					save_pix = pix2;
				}
				pix0 = palptr[pix0];
				pix1 = pix0;
				pix2 = palptr[pix2];
				pix3 = pix2;
			}
			wptr[pixels_per_line] = pix0;
			*wptr++ = pix0;
			wptr[pixels_per_line] = pix1;
			*wptr++ = pix1;
			wptr[pixels_per_line] = pix2;
			*wptr++ = pix2;
			wptr[pixels_per_line] = pix3;
			*wptr++ = pix3;
		}
	}

	return (left << 16) | (right & 0xffff);
}

void
redraw_changed_super_hires(int start_offset, int start_line, int reparse,
	word32 *wptr, int pixels_per_line)
{
	word32	*ch_ptr;
	word32	mask_per_line, check0, check1, mask0, mask1;
	word32	this_check, tmp, scan, old_scan;
	int	y, bits_per_line, left, right, st_line, check_bit_pos;
	int	check_word_off, ret;

	st_line = start_line;

	ch_ptr = &(slow_mem_changed[(0x2000) >> CHANGE_SHIFT]);
	bits_per_line = 160 >> SHIFT_PER_CHANGE;
	mask_per_line = -(1 << (32 - bits_per_line));

	if(SHIFT_PER_CHANGE != 3) {
		halt_printf("SHIFT_PER_CHANGE must be 3!\n");
		return;
	}

	check0 = 0;
	check1 = 0;
	y = st_line;
	scan = g_slow_memory_ptr[0x19d00 + y];
	check_bit_pos = bits_per_line * y;
	check_word_off = check_bit_pos >> 5;	/* 32 bits per word */
	check_bit_pos = check_bit_pos & 0x1f;	/* 5-bit bit_pos */
	check0 = ch_ptr[check_word_off];
	check1 = ch_ptr[check_word_off+1];
	mask0 = mask_per_line >> check_bit_pos;
	mask1 = 0;
	this_check = check0 << check_bit_pos;
				/* move indicated bit to MSbit position */
	if((check_bit_pos + bits_per_line) > 32) {
		this_check |= (check1 >> (32 - check_bit_pos));
		mask1 = mask_per_line << (32 - check_bit_pos);
	}

	ch_ptr[check_word_off] = check0 & ~mask0;
	ch_ptr[check_word_off+1] = check1 & ~mask1;

	this_check = this_check & mask_per_line;
	old_scan = g_superhires_scan_save[y];

	ret = video_rebuild_super_hires_palette(scan, y, reparse);
	if(ret || reparse || ((scan ^ old_scan) & 0xa0)) {
					/* 0x80 == mode640, 0x20 = fill */
		this_check = -1;
	}

	if(!this_check) {
		return;			// Nothing to do, get out
	}

	if(scan & 0x80) {			// 640 mode
		g_num_lines_superhires640++;
	}

	if((scan >> 5) & 1) {		// fill mode--redraw whole line
		this_check = -1;
	}

	g_a2_screen_buffer_changed |= (1 << (start_line >> 3));
	tmp = redraw_changed_super_hires_oneline(wptr, pixels_per_line, y,
							scan, this_check);
	left = tmp >> 16;
	right = tmp & 0xffff;

	g_a2_line_left_edge[st_line] = 4*left;
	g_a2_line_right_edge[st_line] = 4*right;

	if((left >= right) || (left > 160) || (right > 160)) {
		printf("line %d, shr left:%d right:%d\n", start_line, left,
								right);
	}
}

void
video_update_event_line(int line)
{
	int	new_line;

	video_update_through_line(line);

	new_line = line + g_line_ref_amt;
	if(new_line < 200) {
		if(!g_config_control_panel && !g_halt_sim) {
			add_event_vid_upd(new_line);
		}
	} else if(line >= 262) {
		video_update_through_line(0);
		if(!g_config_control_panel && !g_halt_sim) {
			add_event_vid_upd(1);	/* add event for new screen */
		}
	}
}

void
video_update_through_line(int line)
{
	register word32 start_time;
	register word32 end_time;
	word32	mask, xor_stat;
	int	last_line, must_reparse, new_all_stat, prev_all_stat, new_stat;
	int	prev_stat;
	int	i;

#if 0
	vid_printf("\nvideo_upd for line %d, lines: %06x\n", line,
				get_lines_since_vbl(g_cur_dcycs));
#endif

	GET_ITIMER(start_time);

	video_update_all_stat_through_line(line);

	last_line = MY_MIN(200, line+1); /* go through line, but not past 200 */

	new_stat = -2;
	new_all_stat = -2;
	must_reparse = 0;
	for(i = g_vid_update_last_line; i < last_line; i++) {
		prev_all_stat = new_all_stat;
		prev_stat = new_stat;
		new_all_stat = g_a2_new_all_stat[i];
		new_stat = g_a2_line_stat[i];
		xor_stat = new_all_stat ^ g_a2_cur_all_stat[i];
		if(xor_stat) {
			/* regen line_stat for this line */
			if((xor_stat & ALL_STAT_SUPER_HIRES) &&
				!(new_all_stat & ALL_STAT_SUPER_HIRES)) {
				g_border_reparse = 1;	// Redraw right border
			}
			g_a2_cur_all_stat[i] = new_all_stat;
			if((new_all_stat == prev_all_stat) && (i & 31)) {
				/* save a lookup, not line 160, 192 */
				new_stat = prev_stat;
			} else {
				new_stat = video_all_stat_to_line_stat(i,
								new_all_stat);
			}
			if(new_stat != g_a2_line_stat[i]) {
				/* status changed */
				g_a2_line_stat[i] = new_stat;
				if(g_mode_line[i] != new_stat) {
					must_reparse = 1;
					g_mode_line[i] = new_stat;
				}
				mask = 1 << (line >> 3);
				g_full_refresh_needed |= mask;
				g_a2_screen_buffer_changed |= mask;
			}
		}

#if 0
		if(i == 10) {
			printf("Refresh line 10 new_stat:%08x prev_stat:%08x, "
				"must_reparse:%d\n", new_stat, prev_stat,
				must_reparse);
		}
#endif
		video_refresh_line(i, must_reparse);
		must_reparse = 0;
	}

	g_vid_update_last_line = last_line;

	/* deal with border and forming rects for xdriver.c to use */
	if(line >= 262) {
		if(g_num_lines_prev_superhires != g_num_lines_superhires) {
			/* switched in/out from superhires--refresh borders */
			g_border_sides_refresh_needed = 1;
		}

		video_form_change_rects();
		g_num_lines_prev_superhires = g_num_lines_superhires;
		g_num_lines_prev_superhires640 = g_num_lines_superhires640;
		g_num_lines_superhires = 0;
		g_num_lines_superhires640 = 0;
		for(i = 0; i < 24; i++) {
			g_a2_8line_changes[i] = 0;
		}
	}
	GET_ITIMER(end_time);
	g_cycs_in_refresh_line += (end_time - start_time);
}

void
video_refresh_line(int line, int must_reparse)
{
	word32	*wptr;
	word32	fg_pixel, bg_pixel;
	int	stat, mode, dbl, page, monochrome, altchar, bg_color;
	int	fg_color, pixels_per_line, offset;

	stat = g_a2_line_stat[line];
	wptr = g_mainwin_kimage.wptr;
	pixels_per_line = g_mainwin_kimage.a2_width_full;
	offset = (pixels_per_line * g_video_act_margin_top) +
						g_video_act_margin_left;
	wptr = wptr + offset;

	g_a2_line_left_edge[line] = 640;
	g_a2_line_right_edge[line] = 0;
	/* all routs force in new left/right when there are screen changes */

	dbl = stat & 1;
	monochrome = (stat >> 1) & 1;
	page = (stat >> 2) & 1;
	mode = (stat >> 4) & 7;

#if 0
	printf("refresh line: %d, stat: %04x, mode:%d\n", line, stat, mode);
#endif

	switch(mode) {
	case MODE_TEXT:
		altchar = (stat >> 7) & 1;
		bg_color = (stat >> 8) & 0xf;
		fg_color = (stat >> 12) & 0xf;
		bg_pixel = g_a2palette_1624[bg_color];
		fg_pixel = g_a2palette_1624[fg_color];
		redraw_changed_text(0x000 + page*0x400, line, must_reparse,
			wptr, altchar, bg_pixel, fg_pixel,
			pixels_per_line, dbl);
		break;
	case MODE_GR:
		redraw_changed_gr(0x000 + page*0x400, line, must_reparse,
			wptr, pixels_per_line, dbl);
		break;
	case MODE_HGR:
		redraw_changed_hgr(0x000 + page*0x2000, line, must_reparse,
			wptr, pixels_per_line, monochrome, dbl);
		break;
	case MODE_SUPER_HIRES:
		g_num_lines_superhires++;
		redraw_changed_super_hires(0, line, must_reparse, wptr,
							pixels_per_line);
		break;
	case MODE_BORDER:
		if(line < 192) {
			halt_printf("Border line not 192: %d\n", line);
		}
		g_a2_line_left_edge[line] = 0;
		g_a2_line_right_edge[line] = 560;
		if(g_border_line24_refresh_needed) {
			g_border_line24_refresh_needed = 0;
			g_a2_screen_buffer_changed |= (1 << 24);
		}
		break;
	default:
		halt_printf("refresh screen: mode: 0x%02x unknown!\n", mode);
		exit(7);
	}
}

void
prepare_a2_font()
{
	word32	val0, val1, val2;
	int	i, j, k;

	// Prepare g_a2font_bits[char][line] where each entry indicates the
	//  set pixels in this line of the character.  Bits 6:0 are an
	//  80-column character, and bits 21:8 are  the 14 expanded bits of a
	//  40-columns character, both with the first visible bit at the
	//  rightmost bit address.  But g_font_array[] is in big-endian bit
	//  order, which is less useful
	for(i = 0; i < 256; i++) {
		for(j = 0; j < 8; j++) {
			val0 = g_font_array[i][j] >> 1;
			val1 = 0;		// 80-column bits
			val2 = 0;		// 40-column bits (doubled)
			for(k = 0; k < 7; k++) {
				val1 = val1 << 1;
				val2 = val2 << 2;
				if(val0 & 1) {
					val1 |= 1;
					val2 |= 3;
				}
				val0 = val0 >> 1;
			}
			g_a2font_bits[i][j] = (val2 << 8) | val1;
		}
	}
}

void
video_add_rect(Kimage *kimage_ptr, int x, int y, int width, int height)
{
	int	pos;

	pos = kimage_ptr->num_change_rects++;
	if(pos >= MAX_CHANGE_RECTS) {
		return;			// This will be handled later
	}
	kimage_ptr->change_rect[pos].x = x;
	kimage_ptr->change_rect[pos].y = y;
	kimage_ptr->change_rect[pos].width = width;
	kimage_ptr->change_rect[pos].height = height;
#if 0
	printf("Add rect %d, x:%d y:%d, w:%d h:%d\n", pos, x, y, width, height);
#endif
}

void
video_add_a2_rect(int start_line, int end_line, int left_pix, int right_pix)
{
	int	srcy;

	if(left_pix >= right_pix || left_pix < 0 || right_pix <= 0) {
		halt_printf("video_push_lines: lines %d to %d, pix %d to %d\n",
			start_line, end_line, left_pix, right_pix);
		printf("a2_screen_buf_ch:%08x, g_full_refr:%08x\n",
			g_a2_screen_buffer_changed, g_full_refresh_needed);
		return;
	}

	srcy = 2*start_line;

	video_add_rect(&g_mainwin_kimage, g_video_act_margin_left + left_pix,
			g_video_act_margin_top + srcy,
			(right_pix - left_pix), 2*(end_line - start_line));
}

void
video_form_change_rects()
{
	Kimage	*kimage_ptr;
	register word32 start_time;
	register word32 end_time;
	word32	mask;
	int	start, line, left_pix, right_pix, left, right, line_div8;
	int	x, y, width, height;

	kimage_ptr = &g_mainwin_kimage;
	if(g_border_sides_refresh_needed) {
		g_border_sides_refresh_needed = 0;
		// Add left side border
		video_add_rect(kimage_ptr, 0, g_video_act_margin_top,
						BORDER_WIDTH, A2_WINDOW_HEIGHT);

		// Add right-side border.  Resend x from 560 through
		//  X_A2_WINDOW_WIDTH
		x = g_video_act_margin_left + 560;
		width = X_A2_WINDOW_WIDTH - x;
		video_add_rect(kimage_ptr, x, g_video_act_margin_top, width,
							A2_WINDOW_HEIGHT);
	}
	if(g_border_special_refresh_needed) {
		g_border_special_refresh_needed = 0;

		// Do top border
		width = g_video_act_width;
		height = g_video_act_margin_top;
		video_add_rect(kimage_ptr, 0, 0, width, height);

		// Do bottom border
		height = g_video_act_margin_bottom;
		y = g_video_act_margin_top + A2_WINDOW_HEIGHT;
		video_add_rect(kimage_ptr, 0, y, width, height);
	}
	if(g_status_refresh_needed) {
		g_status_refresh_needed = 0;
		width = g_mainwin_kimage.a2_width;
		y = g_video_act_margin_top + A2_WINDOW_HEIGHT +
						g_video_act_margin_bottom;
		height = g_mainwin_kimage.a2_height - y;
		video_add_rect(kimage_ptr, 0, y, width, height);
	}

	if(g_a2_screen_buffer_changed == 0) {
		return;
	}

	GET_ITIMER(start_time);

	start = -1;

	left_pix = 640;
	right_pix = 0;

	for(line = 0; line < 200; line++) {
		line_div8 = line >> 3;
		mask = 1 << (line_div8);
		if((g_full_refresh_needed & mask) != 0) {
			left = 0;
			right = 640;
		} else {
			left = g_a2_line_left_edge[line];
			right = g_a2_line_right_edge[line];
		}

		if(!(g_a2_screen_buffer_changed & mask) || (left >= right)) {
			/* No need to update this line */
			/* Refresh previous chunks of lines, if any */
			if(start >= 0) {
				video_add_a2_rect(start, line, left_pix,
								right_pix);
				start = -1;
				left_pix = 640;
				right_pix = 0;
			}
		} else {
			/* Need to update this line */
			if(start < 0) {
				start = line;
			}
			left_pix = MY_MIN(left, left_pix);
			right_pix = MY_MAX(right, right_pix);
		}
	}

	if(start >= 0) {
		video_add_a2_rect(start, 200, left_pix, right_pix);
	}

	g_a2_screen_buffer_changed = 0;
	g_full_refresh_needed = 0;

	GET_ITIMER(end_time);

	g_cycs_in_xredraw += (end_time - start_time);
}

int
video_get_a2_width(Kimage *kimage_ptr)
{
	return kimage_ptr->a2_width;
}

int
video_get_a2_height(Kimage *kimage_ptr)
{
	return kimage_ptr->a2_height;
}

// video_out_query: return 0 if no screen drawing at all is needed.
//  returns 1 or the number of change_rects if any drawing is needed
int
video_out_query(Kimage *kimage_ptr)
{
	int	num_change_rects, x_refresh_needed;

	num_change_rects = kimage_ptr->num_change_rects;
	x_refresh_needed = kimage_ptr->x_refresh_needed;
	if(x_refresh_needed) {
		return 1;
	}
	return num_change_rects;
}

// video_out_done: used by specialize xdriver platform code which needs to
//  clear the num_change_rects=0.
void
video_out_done(Kimage *kimage_ptr)
{
	kimage_ptr->num_change_rects = 0;
	kimage_ptr->x_refresh_needed = 0;
}

// Called by xdriver.c to copy KEGS's kimage data to the vptr buffer
int
video_out_data(void *vptr, Kimage *kimage_ptr, int out_width_act,
		Change_rect *rectptr, int pos)
{
	word32	*out_wptr, *wptr;
	int	a2_width, a2_width_full, width, a2_height, height, x, eff_y;
	int	x_width, x_height, num_change_rects, x_refresh_needed;
	int	i, j;

	// Copy from kimage_ptr->wptr to vptr
	num_change_rects = kimage_ptr->num_change_rects;
	x_refresh_needed = kimage_ptr->x_refresh_needed;
	if(((pos >= num_change_rects) || (pos >= MAX_CHANGE_RECTS)) &&
							!x_refresh_needed) {
		kimage_ptr->num_change_rects = 0;
		return 0;
	}
	a2_width = kimage_ptr->a2_width;
	a2_width_full = kimage_ptr->a2_width_full;
	a2_height = kimage_ptr->a2_height;
	if((num_change_rects >= MAX_CHANGE_RECTS) || x_refresh_needed) {
		// Table overflow, just copy everything in one go
		kimage_ptr->x_refresh_needed = 0;
		if(pos >= 1) {
			kimage_ptr->num_change_rects = 0;
			return 0;		// No more to do
		}
		// Force full update
		rectptr->x = 0;
		rectptr->y = 0;
		rectptr->width = a2_width;
		rectptr->height = a2_height;
	} else {
		*rectptr = kimage_ptr->change_rect[pos];	// Struct copy
	}
#if 0
	printf("video_out_data, %p rectptr:%p, pos:%d, x:%d y:%d w:%d h:%d, "
		"wptr:%p\n", vptr, rectptr, pos, rectptr->x,
		rectptr->y, rectptr->width, rectptr->height,
		kimage_ptr->wptr);
#endif

	width = rectptr->width;
	height = rectptr->height;
	x = rectptr->x;
	x_width = kimage_ptr->x_width;
	x_height = kimage_ptr->x_height;
	if((a2_width != x_width) || (a2_height != x_height)) {
			// HACK!
#if 0
		printf("a2_width:%d, x_width:%d, a2_height:%d, x_height:"
			"%d\n", a2_width, x_width, a2_height, x_height);
#endif
		return video_out_data_scaled(vptr, kimage_ptr, out_width_act,
								rectptr);
	} else {
		out_wptr = (word32 *)vptr;
		for(i = 0; i < height; i++) {
			eff_y = rectptr->y + i;
			wptr = kimage_ptr->wptr + (eff_y * a2_width_full) + x;
			out_wptr = ((word32 *)vptr) +
						(eff_y * out_width_act) + x;
			for(j = 0; j < width; j++) {
				*out_wptr++ = *wptr++;
			}
		}
	}

	return 1;
}


int
video_out_data_intscaled(void *vptr, Kimage *kimage_ptr, int out_width_act,
					Change_rect *rectptr)
{
	word32	*out_wptr, *wptr;
	word32	pos_scale, alpha_mask;
	int	a2_width_full, eff_y, src_y, x, y, new_x, out_x, out_y;
	int	out_width, out_height, max_x, max_y, out_max_x, out_max_y, pos;
	int	i, j;

	// Faster scaling routine which does simple pixel replication rather
	//  than blending.  Intended for scales >= 3.0 (or so) since at
	//  these scales, replication looks fine.
	x = rectptr->x;
	y = rectptr->y;
	max_x = rectptr->width + x;
	max_y = rectptr->height + y;
	max_x = MY_MIN(kimage_ptr->a2_width_full, max_x + 1);
	max_y = MY_MIN(kimage_ptr->a2_height, max_y + 1);
	x = MY_MAX(0, x - 1);
	y = MY_MAX(0, y - 1);
	a2_width_full = kimage_ptr->a2_width_full;

	out_x = (x * kimage_ptr->scale_width_a2_to_x) >> 16;
	out_y = (y * kimage_ptr->scale_height_a2_to_x) >> 16;
	out_max_x = (max_x * kimage_ptr->scale_width_a2_to_x + 65535) >> 16;
	out_max_y = (max_y * kimage_ptr->scale_height_a2_to_x + 65535) >> 16;
	out_max_x = MY_MIN(out_max_x, out_width_act);
	out_max_y = MY_MIN(out_max_y, kimage_ptr->x_height);
	out_width = out_max_x - out_x;
	out_height = out_max_y - out_y;
	out_wptr = (word32 *)vptr;
	rectptr->x = out_x;
	rectptr->y = out_y;
	rectptr->width = out_width;
	rectptr->height = out_height;
	alpha_mask = g_alpha_mask;
	for(i = 0; i < out_height; i++) {
		eff_y = out_y + i;
		pos_scale = kimage_ptr->scale_height[eff_y];
		src_y = pos_scale >> 16;
		wptr = kimage_ptr->wptr + (src_y * a2_width_full);
		out_wptr = ((word32 *)vptr) + (eff_y * out_width_act) + out_x;
		for(j = 0; j < out_width; j++) {
			new_x = j + out_x;
			pos_scale = kimage_ptr->scale_width[new_x];
			pos = pos_scale >> 16;
			*out_wptr++ = wptr[pos] | alpha_mask;
		}
	}
	rectptr->width = kimage_ptr->x_width - rectptr->x;

	return 1;
}

int
video_out_data_scaled(void *vptr, Kimage *kimage_ptr, int out_width_act,
					Change_rect *rectptr)
{
	word32	*out_wptr, *wptr;
	word32	comp0a, comp0b, comp1a, comp1b, val0a, val0b, val1a, val1b;
	word32	scale, scale_y, comp0, comp1, comp, new_val, pos_scale;
	word32	alpha_mask;
	int	a2_width_full, eff_y, src_y, x, y, new_x, out_x, out_y;
	int	out_width, out_height, max_x, max_y, out_max_x, out_max_y, pos;
	int	i, j, shift;

	if((kimage_ptr->scale_width_a2_to_x >= 0x30000) ||
			(kimage_ptr->scale_height_a2_to_x >= 0x30000)) {
		return video_out_data_intscaled(vptr, kimage_ptr,
						out_width_act, rectptr);
	}
	x = rectptr->x;
	y = rectptr->y;
	max_x = rectptr->width + x;
	max_y = rectptr->height + y;
	max_x = MY_MIN(kimage_ptr->a2_width_full, max_x + 1);
	max_y = MY_MIN(kimage_ptr->a2_height, max_y + 1);
	x = MY_MAX(0, x - 1);
	y = MY_MAX(0, y - 1);
	a2_width_full = kimage_ptr->a2_width_full;

	out_x = (x * kimage_ptr->scale_width_a2_to_x) >> 16;
	out_y = (y * kimage_ptr->scale_height_a2_to_x) >> 16;
	out_max_x = (max_x * kimage_ptr->scale_width_a2_to_x + 65535) >> 16;
	out_max_y = (max_y * kimage_ptr->scale_height_a2_to_x + 65535) >> 16;
	out_max_x = MY_MIN(out_max_x, out_width_act);
	out_max_y = MY_MIN(out_max_y, kimage_ptr->x_height);
	out_width = out_max_x - out_x;
	out_height = out_max_y - out_y;
#if 0
	printf("scaled: in %d,%d %d,%d becomes %d,%d %d,%d\n", x, y, width,
		height, out_x, out_y, out_width, out_height);
#endif
	out_wptr = (word32 *)vptr;
	rectptr->x = out_x;
	rectptr->y = out_y;
	rectptr->width = out_width;
	rectptr->height = out_height;
	alpha_mask = g_alpha_mask;
	for(i = 0; i < out_height; i++) {
		eff_y = out_y + i;
		pos_scale = kimage_ptr->scale_height[eff_y];
		src_y = pos_scale >> 16;
		scale_y = pos_scale & 0xffff;
		wptr = kimage_ptr->wptr + (src_y * a2_width_full);
		out_wptr = ((word32 *)vptr) + (eff_y * out_width_act) + out_x;
		for(j = 0; j < out_width; j++) {
			new_x = j + out_x;
			pos_scale = kimage_ptr->scale_width[new_x];
			pos = pos_scale >> 16;
			scale = pos_scale & 0xffff;
			val0a = wptr[pos];
			val0b = wptr[pos + 1];
			val1a = wptr[pos + a2_width_full];
			val1b = wptr[pos + 1 + a2_width_full];
			new_val = 0;
			for(shift = 0; shift < 32; shift += 8) {
				comp0a = (val0a >> shift) & 0xff;
				comp0b = (val0b >> shift) & 0xff;
				comp1a = (val1a >> shift) & 0xff;
				comp1b = (val1b >> shift) & 0xff;
				comp0 = ((65536 - scale) * comp0a) +
							(scale * comp0b);
				comp1 = ((65536 - scale) * comp1a) +
							(scale * comp1b);
				comp0 = comp0 >> 16;
				comp1 = comp1 >> 16;
				comp = ((65536 - scale_y) * comp0) +
							(scale_y * comp1);
				comp = comp >> 16;
				new_val |= (comp << shift);
			}
			*out_wptr++ = new_val | alpha_mask;
#if 0
			if((pos >= 640+32+30) && (eff_y == 100)) {
				printf("x:%d pos:%d %08x.  %08x,%08x pos_sc:"
					"%08x\n", new_x, pos, new_val, val0a,
					val0b, pos_scale);
			}
#endif
		}
	}
	rectptr->width = kimage_ptr->x_width - rectptr->x;
#if 0
	for(i = 0; i < kimage_ptr->x_height; i++) {
		out_wptr = ((word32 *)vptr) + (i * out_width_act) +
						kimage_ptr->x_width - 1;
		*out_wptr = 0x00ff00ff;
# if 0
		for(j = 0; j < 10; j++) {
			if(*out_wptr != 0) {
				printf("out_wptr:%p is %08x at %d,%d\n",
					out_wptr, *out_wptr,
					out_width_act - 1 - j, i);
			}
			out_wptr--;
		}
# endif
	}
#endif

	return 1;
}

word32
video_scale_calc_frac(int pos, int out_max, word32 frac_inc,
							word32 frac_inc_inv)
{
	word32	frac, frac_to_next, new_frac;

	frac = pos * frac_inc;
	if(pos >= (out_max - 1)) {
		return (g_mainwin_kimage.a2_width - 1) << 16;
							// Clear frac bits
	}
	if(g_video_scale_algorithm == 2) {
		return frac & -65536;			// nearest neighbor
	}
	if(g_video_scale_algorithm == 1) {
		return frac;				// bilinear interp
	}
	// Do proper scaling.  fraction=0 means 100% this pixel, fraction=ffff
	//  means 99.99% the next pixel
	frac_to_next = frac_inc + (frac & 0xffff);
	if(frac_to_next < 65536) {
		frac_to_next = 0;
	}
	frac_to_next = (frac_to_next & 0xffff) * frac_inc_inv;
	frac_to_next = frac_to_next >> 16;
	new_frac = (frac & -65536) | (frac_to_next & 0xffff);
#if 0
	if((frac >= (30 << 16)) && (frac < (38 << 16))) {
		printf("scale %d (%02x) -> %08x (was %08x) %08x %08x\n",
			pos, pos, new_frac, frac, frac_inc, frac_inc_inv);
	}
#endif
	return new_frac;
}

void
video_update_scale(Kimage *kimage_ptr, int out_width, int out_height)
{
	word32	frac_inc, frac_inc_inv, new_frac;
	int	a2_width, a2_height, exp_width, exp_height;
	int	i;

	if(out_width > MAX_SCALE_SIZE) {
		out_width = MAX_SCALE_SIZE;
	}
	if(out_height > MAX_SCALE_SIZE) {
		out_height = MAX_SCALE_SIZE;
	}
	a2_width = kimage_ptr->a2_width;
	a2_height = kimage_ptr->a2_height;

	// Handle aspect ratio.  Calculate height/width based on the other's
	//  aspect ratio, and pick the smaller value
	exp_width = (a2_width * out_height) / a2_height;
	exp_height = (a2_height * out_width) / a2_width;
	if(exp_width < out_width) {
		out_width = exp_width;
	}
	if(exp_height < out_height) {
		out_height = exp_height;
	}

	// See if anything changed.  If it's unchanged, don't do anything
	if((kimage_ptr->x_width == out_width) &&
			(kimage_ptr->x_height == out_height)) {
		return;
	}
	kimage_ptr->x_width = out_width;
	kimage_ptr->x_height = out_height;
	kimage_ptr->x_refresh_needed = 1;

	// the per-pixel inc = a2_width / out_width.  Scale by 65536
	frac_inc = (a2_width * 65536UL) / out_width;
	kimage_ptr->scale_width_to_a2 = frac_inc;
	frac_inc_inv = (out_width * 65536UL) / a2_width;
	kimage_ptr->scale_width_a2_to_x = frac_inc_inv;
	printf("scale_width_to_a2: %08x, a2_to_x:%08x, is_debugwin:%d\n",
		kimage_ptr->scale_width_to_a2, kimage_ptr->scale_width_a2_to_x,
		(kimage_ptr == &g_debugwin_kimage));
	for(i = 0; i < out_width + 1; i++) {
		new_frac = video_scale_calc_frac(i, out_width, frac_inc,
								frac_inc_inv);
		kimage_ptr->scale_width[i] = new_frac;
	}

	frac_inc = (a2_height * 65536UL) / out_height;
	kimage_ptr->scale_height_to_a2 = frac_inc;
	frac_inc_inv = (out_height * 65536UL) / a2_height;
	kimage_ptr->scale_height_a2_to_x = frac_inc_inv;
	printf("scale_height_to_a2: %08x, a2_to_x:%08x\n",
		kimage_ptr->scale_height_to_a2,
		kimage_ptr->scale_height_a2_to_x);
	for(i = 0; i < out_height + 1; i++) {
		new_frac = video_scale_calc_frac(i, out_height, frac_inc,
								frac_inc_inv);
		kimage_ptr->scale_height[i] = new_frac;
	}
}

int
video_scale_mouse_x(Kimage *kimage_ptr, int raw_x, int x_width)
{
	int	x;

	// raw_x is in output coordinates.  Scale down to a2 coordinates
	if(x_width == 0) {
		x = (kimage_ptr->scale_width_to_a2 * raw_x) / 65536;
	} else {
		// Scale raw_x using x_width
		x = (raw_x * kimage_ptr->a2_width_full) / x_width;
	}
	x = x - BASE_MARGIN_LEFT;
	return x;
}

int
video_scale_mouse_y(Kimage *kimage_ptr, int raw_y, int y_height)
{
	int	y;

	// raw_y is in output coordinates.  Scale down to a2 coordinates
	if(y_height == 0) {
		y = (kimage_ptr->scale_height_to_a2 * raw_y) / 65536;
	} else {
		// Scale raw_y using y_height
		y = (raw_y * kimage_ptr->a2_height_full) / y_height;
	}
	y = y - BASE_MARGIN_TOP;
	return y;
}

int
video_unscale_mouse_x(Kimage *kimage_ptr, int a2_x, int x_width)
{
	int	x;

	// Convert a2_x to output coordinates
	x = a2_x + BASE_MARGIN_LEFT;
	if(x_width == 0) {
		x = (kimage_ptr->scale_width_a2_to_x * x) / 65536;
	} else {
		// Scale a2_x using x_width
		x = (x * x_width) / kimage_ptr->a2_width_full;
	}
	return x;
}

int
video_unscale_mouse_y(Kimage *kimage_ptr, int a2_y, int y_height)
{
	int	y;

	// Convert a2_y to output coordinates
	y = a2_y + BASE_MARGIN_TOP;
	if(y_height == 0) {
		y = (kimage_ptr->scale_height_a2_to_x * y) / 65536;
	} else {
		// Scale a2_y using y_height
		y = (y * y_height) / kimage_ptr->a2_height_full;
	}
	return y;
}

void
video_update_color_raw(int col_num, int a2_color)
{
	word32	tmp;
	int	red, green, blue, newred, newgreen, newblue;

	if(col_num >= 256 || col_num < 0) {
		halt_printf("video_update_color_raw: col: %03x\n", col_num);
		return;
	}

	red = (a2_color >> 8) & 0xf;
	green = (a2_color >> 4) & 0xf;
	blue = (a2_color) & 0xf;
	red = ((red << 4) + red);
	green = ((green << 4) + green);
	blue = ((blue << 4) + blue);

	newred = red >> g_red_right_shift;
	newgreen = green >> g_green_right_shift;
	newblue = blue >> g_blue_right_shift;

	tmp = ((newred & g_red_mask) << g_red_left_shift) +
			((newgreen & g_green_mask) << g_green_left_shift) +
			((newblue & g_blue_mask) << g_blue_left_shift);
	g_palette_8to1624[col_num] = tmp;
}

void
video_update_status_line(int line, const char *string)
{
	byte	a2_str_buf[STATUS_LINE_LENGTH+1];
	word32	*wptr;
	char	*buf;
	const char *ptr;
	int	start_line, c, pixels_per_line, offset;
	int	i;

	if(line >= MAX_STATUS_LINES || line < 0) {
		printf("update_status_line: line: %d!\n", line);
		exit(1);
	}

	ptr = string;
	buf = &(g_status_buf[line][0]);
	g_status_ptrs[line] = buf;
	for(i = 0; i < STATUS_LINE_LENGTH; i++) {
		if(*ptr) {
			c = *ptr++;
		} else {
			c = ' ';
		}
		buf[i] = c;
		a2_str_buf[i] = c | 0x80;
	}

	buf[STATUS_LINE_LENGTH] = 0;
	a2_str_buf[STATUS_LINE_LENGTH] = 0;
	start_line = (200 + 2*8) + line*8;
	pixels_per_line = g_mainwin_kimage.a2_width_full;
	offset = (pixels_per_line * g_video_act_margin_top);
	wptr = g_mainwin_kimage.wptr;
	wptr += offset;
	for(i = 0; i < 8; i++) {
		redraw_changed_string(&(a2_str_buf[0]), start_line + i, -1L,
			wptr, 0, 0x00ffffff, pixels_per_line, 1);
	}
	video_add_a2_rect(start_line, start_line + 8, 0, 640);
}

void
video_show_debug_info()
{
	word32	tmp1;

	printf("g_cur_dcycs: %f, last_vbl: %f\n", g_cur_dcycs,
							g_last_vbl_dcycs);
	tmp1 = get_lines_since_vbl(g_cur_dcycs);
	printf("lines since vbl: %06x\n", tmp1);
	printf("Last line updated: %d\n", g_vid_update_last_line);
}

word32
float_bus(double dcycs)
{
	word32	val;
	int	lines_since_vbl, line, eff_line, line24, all_stat, byte_offset;
	int	hires, page2, addr;

	lines_since_vbl = get_lines_since_vbl(dcycs);

/* For floating bus, model hires style: Visible lines 0-191 are simply the */
/* data being displayed at that time.  Lines 192-255 are lines 0 - 63 again */
/*  and lines 256-261 are lines 58-63 again */
/* For each line, figure out starting byte at -25 mod 128 bytes from this */
/*  line's start */
/* This emulates an Apple II style floating bus.  A real IIgs does not */
/*  drive anything meaningful during the 25 horizontal blanking cycles, */
/*  nor during veritical blanking.  The data seems to be 0 or related to */
/*  the instruction fetches on a real IIgs during blankings */

	line = lines_since_vbl >> 8;
	byte_offset = lines_since_vbl & 0xff;
	// byte offset is from 0 through 64, where the visible screen is drawn
	//  from 25 through 64

	eff_line = line;
	if((line >= 192) || (byte_offset < 25)) {
		return 0;		// Don't do anything during blanking
	}
	all_stat = g_cur_a2_stat;
	hires = all_stat & ALL_STAT_HIRES;
	if((all_stat & ALL_STAT_MIX_T_GR) && (line >= 160)) {
		hires = 0;
	}
	page2 = EXTRU(all_stat, 31 - BIT_ALL_STAT_PAGE2, 1);
	if(all_stat & ALL_STAT_ST80) {
		page2 = 0;
	}

	line24 = (eff_line >> 3) & 0x1f;
	addr = g_screen_index[line24] & 0x3ff;
	addr = (addr & 0x380) + (((addr & 0x7f) - 25 + byte_offset) & 0x7f);
	if(hires) {
		addr = 0x2000 + addr + ((eff_line & 7) << 10) + (page2 << 13);
	} else {
		addr = 0x400 + addr + (page2 << 10);
	}

	val = g_slow_memory_ptr[addr];
#if 0
	printf("For %04x (%d) addr=%04x, val=%02x, dcycs:%9.2f\n",
		lines_since_vbl, eff_line, addr, val, dcycs - g_last_vbl_dcycs);
#endif
	return val;
}
