/************************************************************************/
/*			KEGS: Apple //gs Emulator			*/
/*			Copyright 2002-2022 by Kent Dickey		*/
/*									*/
/*	This code is covered by the GNU GPL v3				*/
/*	See the file COPYING.txt or https://www.gnu.org/licenses/	*/
/*	This program is provided with no warranty			*/
/*									*/
/*	The KEGS web page is kegs.sourceforge.net			*/
/*	You may contact the author at: kadickey@alumni.princeton.edu	*/
/************************************************************************/

// $KmKId: protos_windriver.h,v 1.10 2022-02-12 01:31:42+00 kentd Exp $

/* END_HDR */

/* windriver.c */
void x_dialog_create_kegs_conf(const char *str);
int x_show_alert(int is_fatal, const char *str);
int win_update_mouse(int x, int y, int button_states, int buttons_valid);
void win_event_mouse(WPARAM wParam, LPARAM lParam);
void win_event_key(HWND hwnd, UINT raw_vk, BOOL down, int repeat, UINT flags);
void win_event_quit(HWND hwnd);
void win_event_redraw(void);
LRESULT CALLBACK win_event_handler(HWND hwnd, UINT umsg, WPARAM wParam, LPARAM lParam);
int main(int argc, char **argv);
void check_input_events(void);
void win_video_init(int mdepth);
void xdriver_end(void);
void x_update_display(void);
void x_hide_pointer(int do_hide);
int opendir_int(DIR *dirp, const char *in_filename);
DIR *opendir(const char *in_filename);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
int lstat(const char *path, struct stat *bufptr);


/* win32snd_driver.c */
void win32snd_init(word32 *shmaddr);
void win32snd_shutdown(void);

