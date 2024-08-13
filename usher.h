void crash(const char *fmt,...);	// pop up a YAD notification using formatted string as TITLE

// Simple class to load directory contents into a DYNTBL
class DIRTBL : public DYNAG	 {
public:
DIRTBL	(const char *pth);
private:
};


struct WATCH_HISTORY {int32_t sseen; char rating;};

// Create/validate _ttNNNNNN, Rename folder/files, Manage _N.N MovieRating folder
class MVDIR {
public:
MVDIR(char *pth);
//bool	get_numnam(void);	// YES = bulletproof (values confirmed by presence of _ttNNNNN)
void	set_numnam(OMZ *usr);
void 	set_rating(void);
bool  rename(bool do_it);     // Regularize folder/filenames, and ADD TO DATABASE if not already present
char  *get_prv_txt(char *buf);   // return most recent {prv=...} entry in passed buf, or empty string
char  *get_tooltip_text(void);
~MVDIR();
char	*foldername;	// BaseName (excluding path) of the current folder, which contains ONE MOVIE
int   inp_state;  // 1=confirm ImdbNo, 2=Rename and/or add to dbf, 3=Update Rating
OMZ omz;
private:
void	update_om2(bool set_watch_history);
void update_watch_history(OMDB1 *om1, int32_t imno);
bool  rename_file(const char *fn, bool do_it);
bool  rename_folder(bool do_it);
void update_imdb(bool set_watch_history);
int32_t read_nfo(const char *fn);
bool omdb1_rec_exists(bool do_it);
void	rarbg_del(void);
char	path[1024];  // Initially, FULLpath passed to constructor - MAY BE CHANGED BY RENAME LATER
char  *tooltip_text=0;
DIRTBL   *dt;		// Table of (unpathed) FILENAMES found in the FOLDER passed to constructor
int64_t biggest_vid_sz;
WATCH_HISTORY wh={0,0};
};

