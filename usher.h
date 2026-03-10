void crash(const char *fmt,...);	// pop up a YAD notification using formatted string as TITLE

struct WATCH_HISTORY {int32_t sseen; char rating;};

// Create/validate _ttNNNNNN, Rename folder/files, Manage _N.N MovieRating folder
class MVDIR {
public:
MVDIR(const char *pth);
//bool	get_numnam(void);	// YES = bulletproof (values confirmed by presence of _ttNNNNN)
void	set_numnam(OMZ *usr);
void 	set_rating(void);
bool  rename(bool do_it);     // Regularize folder/filenames, and ADD TO DATABASE if not already present
char  *get_prv_txt(char *buf);   // return most recent {prv=...} entry in passed buf, or empty string
char  *get_tooltip_text(void);
bool  tag_present(void);
~MVDIR();
int   inp_state;  // 1=confirm ImdbNo, 2=Rename and/or add to dbf, 3=Update Rating
bool  update_tags_do_it=false;
void  check_GWunam(void);
OMZ omz;
private:
void	update_om2(bool set_watch_history);
//void  update_watch_history(OMDB1 *om1, int32_t imno);
bool  rename_file(const char *fn, bool do_it);
bool  rename_folder(bool do_it);
bool  update_tags(void);
void  update_imdb(bool set_watch_history);
int32_t read_nfo(const char *fn);
bool  omdb1_rec_exists(bool do_it);
void	rarbg_del(void);
char  *prvname(const char *fn);      // (unpathed) fn of first video file being renamed
char	Path[1024];  // Initially, FULLpath passed to constructor - final Foldername MAY BE CHANGED BY RENAME LATER
char	*Foldername;	// ptr -> BaseName (within Path) of the current folder, which contains ONE MOVIE
char  *tooltip_text=0;
char  vidext[4];  /// ".mkv" or whatever. INCLUDES the dot, but NOT EOS nullbyte
DIRTBL   *dt;		// Table of (unpathed) FILENAMES found in the FOLDER passed to constructor
//int64_t biggest_vid_sz;
char biggest_vid_fn[256];
WATCH_HISTORY wh={0,0};
};

int process_cli_flag(const char *parm);   // (in usherx.cpp) Special CLI processing / maintenance facilities

char *fmt_name_year(char *s, OMZ *oz, bool escape_ampersand=false);	  // (in usher.cpp) bool TRUE = gtk display  
bool api_name_from_number(OMZ *zz);	            // (in usher.cpp)
void retrieve_api_title(OMZ *zz, char *buf);	   // (in usher.cpp)
void set_cursor(bool busy);	                  // (in usher.cpp)

void update_api_both(OMZ *omz);      // update both imdb.api (all api results) AND imdb.fld (fast-load compressed version)


