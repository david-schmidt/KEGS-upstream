#ifdef INCLUDE_RCSID_C
const char rcsid_iwm_h[] = "@(#)$KmKId: iwm.h,v 1.30 2021-08-10 13:47:06+00 kentd Exp $";
#endif

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

#define MAX_TRACKS	(2*80)
#define MAX_C7_DISKS	32

#define NIB_LEN_525		0x1900		/* 51072 bits per track */
#define NIBS_FROM_ADDR_TO_DATA	20

// image_type settings.  0 means unknown type
#define DSK_TYPE_PRODOS		1
#define DSK_TYPE_DOS33		2
#define DSK_TYPE_DYNAPRO	3
#define DSK_TYPE_NIB		4
#define DSK_TYPE_WOZ		5

STRUCT(Trk) {
	byte	*raw_bptr;
	byte	*sync_ptr;
	dword64	dunix_pos;
	word32	unix_len;
	word32	track_qbits;
};

STRUCT(Woz_info) {
	byte	*wozptr;
	word32	woz_size;
	int	version;
	int	bad;
	int	meta_size;
	int	trks_size;
	byte	*tmap_bptr;
	byte	*trks_bptr;
	byte	*info_bptr;
	byte	*meta_bptr;
};

typedef struct Dynapro_map_st Dynapro_map;

STRUCT(Dynapro_file) {
	Dynapro_file *next_ptr;
	Dynapro_file *parent_ptr;
	Dynapro_file *subdir_ptr;
	char	*unix_path;
	byte	*buffer_ptr;
	byte	prodos_name[17];	// First byte is len, nul at end
	word32	dir_byte;		// Byte address of this file's dir ent
	word32	eof;
	word32	blocks_used;
	word32	creation_time;
	word32	lastmod_time;
	word16	upper_lower;		// Version/Min_version: lowercase flags
	word16	key_block;
	word16	aux_type;
	word16	header_pointer;
	word16	map_first_block;
	byte	file_type;
	byte	modified_flag;
	byte	damaged;
};

struct Dynapro_map_st {
	Dynapro_file *file_ptr;
	word16	next_map_block;
	word16	modified;
};

STRUCT(Dynapro_info) {
	char	*root_path;
	Dynapro_file *volume_ptr;
	Dynapro_map *block_map_ptr;
	int	damaged;
};

STRUCT(Disk) {
	double	dcycs_last_read;
	byte	*raw_data;
	Woz_info *wozinfo_ptr;
	Dynapro_info *dynapro_info_ptr;
	char	*name_ptr;
	char	*partition_name;
	int	partition_num;
	int	fd;
	int	dynapro_size;
	dword64	raw_dsize;
	dword64	dimage_start;
	dword64	dimage_size;
	int	smartport;
	int	disk_525;
	int	drive;
	int	cur_qtr_track;
	int	image_type;
	int	vol_num;
	int	write_prot;
	int	write_through_to_unix;
	int	disk_dirty;
	int	just_ejected;
	int	last_phase;
	word32	cur_qbit_pos;
	word32	cur_track_qbits;
	Trk	*cur_trk_ptr;
	int	num_tracks;
	Trk	*trks;
};

STRUCT(Iwm) {
	Disk	drive525[2];
	Disk	drive35[2];
	Disk	smartport[MAX_C7_DISKS];
	double	dcycs_last_fastemul_read;
	int	motor_on;
	int	motor_off;
	int	motor_off_vbl_count;
	int	motor_on35;
	int	head35;
	int	last_sel35;
	int	step_direction35;
	int	iwm_phase[4];
	int	iwm_mode;
	int	drive_select;
	int	q6;
	int	q7;
	int	enable2;
	int	reset;
	word32	write_val;
	word32	qbit_wr_start;
	word32	qbit_wr_last;
	word32	forced_sync_qbit;
};

STRUCT(Driver_desc) {
	word16	sig;
	word16	blk_size;
	word32	blk_count;
	word16	dev_type;
	word16	dev_id;
	word32	data;
	word16	drvr_count;
};

STRUCT(Part_map) {
	word16	sig;
	word16	sigpad;
	word32	map_blk_cnt;
	word32	phys_part_start;
	word32	part_blk_cnt;
	char	part_name[32];
	char	part_type[32];
	word32	data_start;
	word32	data_cnt;
	word32	part_status;
	word32	log_boot_start;
	word32	boot_size;
	word32	boot_load;
	word32	boot_load2;
	word32	boot_entry;
	word32	boot_entry2;
	word32	boot_cksum;
	char	processor[16];
	char	junk[128];
};

