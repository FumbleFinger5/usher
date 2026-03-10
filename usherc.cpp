#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <utime.h>
#include <cassert>
#include <cstdarg>

#include "pdef.h"
#include "cal.h"
#include "str.h"
#include "memgive.h"
#include "parm.h"
#include "omdb1.h"
#include "flopen.h"
#include "drinfo.h"
#include "dirscan.h"
#include "log.h"
#include "imdb.h"
#include "imdbf.h"
#include "exec.h"
#include "scan.h"
#include "qblob.h"
#include "my_json.h"
#include "tmdbc.h"

#include "usher.h"

void crash(const char *fmt,...)	// pop up a YAD notification using formatted string as TITLE
{
char ss[1024];
va_list va;
va_start(va,fmt);
strnfmt(ss,sizeof(ss)-1,fmt,va);
Sjhlog("Fatal error!\r\n%s",ss);
char cmd[1024], buf[256];
exec_cmd(strfmt(cmd,"notify-send -u critical %c%s%c",QTDOUBLE,ss,QTDOUBLE),buf,sizeof(buf));
throw(99);
}

static char *delete_junk_in_foldername(char *n)
{
char *p=strchr(n,'['), *q;
if (p!=NULL && (q=strchr(p,']'))!=NULL)
	strdel(p,q-p+1);
int i,j;
if (SAME4BYTES(n,"www.") && (i=stridxc(SPACE,n))!=NOTFND)
   strdel(n,i);
while (*n==SPACE || *n=='-') strdel(n,1);
return(n);
}

// p points to 4 characters before close bracket in foldername. Check if it's a valid Year
static bool valid_year1(char *p, OMZ *oz)
{
if (valid_movie_year(oz->year=a2i(p,4)) && !a2err) return(true);
return(false);
}

// p points to open bracket in foldername. Check if there's a following CLOSE bracket preceded by CCYY
static bool valid_year(char *p, OMZ *oz)
{
int cb=stridxc(')',p);
if (cb>4 && valid_year1(&p[cb-4],oz)) return(true);
return(false);
}

// TODO - just add eos nullbyte before (Year) in passed buff and return true (caller can check for '/' or ':' ) 
static int moviename_in_foldername(const char *foldername, OMZ *oz)
{
char fn[256];
delete_junk_in_foldername(strcpy(fn,foldername));
int i=strlen(fn)-7, j;
if ((j=stridxc('(',fn))!=NOTFND && valid_year(&fn[j], oz))
	{strancpy(oz->title,fn,j); return(YES);}
if (i<0) return(NO);		// (just in case it's a very short folder name)
char sep[3]={'.',SPACE,0}, cc;
for (int cs=0; (cc=sep[cs])!=0; cs++)
   for(i=j=0; (i=stridxc(cc,&fn[j]))!=NOTFND && i<sizeof(oz->title)-5; j+=(i+1))
	   {
	   if (valid_year1(&fn[j+i+1], oz) && (fn[j+i+5]==cc || !fn[j+i+5]))
		   {strancpy(oz->title,fn,j+i+1); return(YES);}
   	}
return(NO);
}

class APOSTROPHIZER {				// Initialized with ORIGINAL title (MovieFolderNanme), which get() returns FIRST
public:									// subsequent get() calls return all possibly missing apostrophe version 
APOSTROPHIZER(const char *_ttl);
const char *get(void);
//char	*fiddle(char *moviename);
private:
char ttl[80];
int	again, prv_added;
bool apostrophe_already_present;
};

APOSTROPHIZER::APOSTROPHIZER(const char *_ttl)
{
strcpy(ttl,_ttl);
apostrophe_already_present=(stridxc(QTSINGLE,_ttl)!=NOTFND);
again=prv_added=0;
}

// Return offset of last letter of first word ending in 's' in passed string
// Caller adds previously-returned offset to passed address, so it's effectively NEXT Apostrophizable word
static int apostrophizable(const char *p)
{
int letters_stepped_over=0, i, c;
for (i=0;!ISALPHA(p[i]);i++) {;}	// skip past any initial non-letters
for (i=0;(c=p[i])!=0;i++)	//  now find the next "word" (sequence of ALPHA) ending in 's' 
	{
	if (c=='s' && letters_stepped_over>0 && (p[i+1]==0 || (p[i+1]==PLUS) || (p[i+1]==SPACE)))
		return(i);
	if (ISALPHA(c)) letters_stepped_over++;
	else letters_stepped_over=0;
	}
return(NOTFND);
}

const char* APOSTROPHIZER::get(void)
{
if (again++)
	{
	if (apostrophe_already_present || again>4) return(NULL);
	if (prv_added>1) strdel(&ttl[prv_added],3);
	prv_added++;
	int pa=apostrophizable(&ttl[prv_added]);
	if (pa==NOTFND) return(NULL);
//	char add[4]={'%','2','7',0};
	char add[4]={'\'',0};
	strins(&ttl[prv_added+pa],add);
	prv_added+=(pa);	// +1 so NEXT call gets pointer to terminating 's of this call
	}
return(ttl);
}

// Call API with tItle+Year to get ImdbNo
static void api_number_from_name(const char *foldername, OMZ *oz)	// oz, not e (param)
{
if (moviename_in_foldername(foldername,oz))
	{
   char buf8k[8192];
	const char *ttl;
	APOSTROPHIZER ap(oz->title);
	while ((ttl=ap.get())!=NULLPTR)
		if (omdb_all_from_name(ttl, oz->year, buf8k))
			{
			JBLOB_READER jb(buf8k);
			const char *p=jb.get("imdbID");
			if (SAME2BYTES(p,"tt") && (oz->k.imno=a2l(&p[2],0))!=0)
				{	// 24/11/25 added oz->title[0]=oz->year=0 for "Rats!", but that fucked Super.Troopers.2001
				oz->title[0]=0;
            p=jb.get("Year");
            if (a2l(p,4)!=oz->year)		// test added 04/01/26 for Super.Troopers.2001
               oz->year=0;
				retrieve_api_title(oz,buf8k);
    			return;
				}
			}
	}
oz->title[0]=0;   // 30/12/25 - don't keep name if we haven't DEFINITELY tied it to an imno
oz->k.imno=0;     // (from long ago) zeroise in case we set imno to a spurious non-zero value above
}

int32_t MVDIR::read_nfo(const char *fn)
{
char s[512];
int i, len, num=0;
HDL f=flopen(strfmt(s,"%s/%s",Path,fn),"r");
while (num==0 && (len=flgetln(s,sizeof(s)-1,f))>=0)   // Look for   <uniqueid type="imdb">2788716</uniqueid>
    {
    if ((i=stridxs("<uniqueid type=",s))!=NOTFND && s[i+15]==34
    &&  SAME4BYTES(&s[i+16],"imdb") && s[i+20]==34 && s[i+21]=='>')
        num=a2l(&s[i+22],0);
    else if ((i=stridxs("www.imdb.com/title/tt",s))!=NOTFND)   // *.URL contains ptr->imdb movie webpage
        {num=a2l(&s[i+21],0);}
    }
flclose(f);
if (num!=0 && omz.k.imno!=0 && num!=omz.k.imno)
	crash("ImdbNo in *.nfo (%d) doesn't match _tt%d",num,omz.k.imno);
return(num);			// could be 0 if this *.nfo format doesn't contain imdbNo
}

// Constructor performs a range of validation checks on the passed folder & contents thereof...
// Biggest file must be video > 100Mb
// IF _ttNNNNN exists (max 1 such file)
//     first line MUST match folder name including "(YYYY)"
//     Biggest (video), and latest of any *.srt files must match Foldernam excluding "(YYYY)"
// IF *.nfo exists (max 1 such file)
//     if _ttNNNNN also exists, AND Ino specified in *.nfo, the numbers must match
// IF multiple *.srt files exist, only consider the latest-dated one
MVDIR::MVDIR(const char *pth)		// MVDIR constructor
{
if (pth==NULL || strlen(pth)>=sizeof(Path)) crash("Movie folder path invalid or missing");
strcpy(Path,pth);
dt = new DIRTBL(Path);
Foldername=(char*)strrchr(Path,'/')+1;		// point to final folder in passed path
memset(&omz,0,sizeof(OMZ));
int64_t biggest_vid_sz=0;
tooltip_text=NULL;
int i, num;
FILEINFO *fi;
char *fn;
for (fi=(FILEINFO*)dt->get(i=0);i++ < dt->ct;fi++)
	{
	fn=fi->name;
	if ((fi->attr&DT_DIR)!=0)	// must be folder, not file
		{
		if (fn[0]=='_' && (num=dot2i(&fn[1]))!=NOTFND)
			{
			if (wh.rating) crash("Multiple Rating folders!");
			if (!num) crash("Bad Rating folder!");
			wh.rating=num;
			wh.sseen= fi->dttm;
			}
		continue;
		}
	num=0;
	if (SAME3BYTES(fn,"_tt") && ((num=a2l(&fn[3],0))<10000 || a2err))
		m_finish("Invalid _tt file %s",fn);
	if (SAME4BYTES(drext(fn),".nfo")) num=read_nfo(fn);
	if (num!=0)
		{
		if (omz.k.imno==0) omz.k.imno=num;
		if (num!=omz.k.imno) m_finish("Conflicting _tt / *.nfo files");
		}
	if (fi->size>biggest_vid_sz && drisvid(drext(fn)))
		{biggest_vid_sz=fi->size; MOVE4BYTES(vidext,drext(fn)); strcpy(biggest_vid_fn,fn);}
	}
if (biggest_vid_sz==0) crash("No video files");
if (omz.k.imno==0)
	{
	api_number_from_name(Foldername,&omz);		// TRUE api got match for movie Name+Year from dirnam
   if (omz.k.imno!=0)
   	api_name_from_number(&omz);
	inp_state=1;
	return;
	}
api_name_from_number(&omz);
if (rename(false)) inp_state=2;     // 2 = User needs to confirm renaming
else inp_state=3;                   // 3 = Renaming not required - we're now asking for UserRating
}

MVDIR::~MVDIR()
{
memtake(tooltip_text);
delete dt;
}

static bool GWunam_year_override=false;

static void update_api_api(OMZ *oz)	// Update imdb.Api (ImdbNo record contains ENTIRE text returned by API) 
{												// oz points to private oK within MV
IMDB_API ia;
if (ia.get(oz->k.imno,0)==NULL)
	{
	char buf8k[8192];    // Allow PLENTY of space for the ENTIRE ibmdb API call
	if (!tmdb_all_from_number(oz,buf8k)) crash("Update_api_api failed! imno:%d",oz->k.imno);
	if (GWunam_year_override)
		{
		JBLOB_READER jb(buf8k);								// once jb is instantiated, we can...
		jb.put("Year",strfmt(buf8k,"%4d",oz->year));	// scribble over buf8k to format string Year text
		strcpy(buf8k,jb.get(NULL));						// re-retrieved buf8k now contains Year override
		}
	ia.put(oz->k.imno,buf8k);
	}
}
static void update_api_dbf(int32_t imno)	// Update imdb.dbf (optimised storage for only the fields Q3 needs)
{
IMDB_FLD imf;
if (imf.exists(imno)) return;					// sjhLog("tt%d shouldn't exist",imno); 
IMDB_API ia;
const char *ptr=ia.get(imno,0);
if (ptr==NULL) m_finish("Impossible!");		// Should have just Added IA.rec if not already present
imf.put(imno,ptr);
}

static int32_t update_now;		// Set when Put_rating() starts to ensure exactly constant value

static void watch_history_update(int32_t imno)
{
OMDB1 om1(true);
OM1_KEY k;
if (!om1.get_om1(imno,&k)) m_finish("wtf1");
// Ensure smdb.mst correctly reflects this imno+partwatch pair
USRTXT ut(imno);
DYNAG *d=ut.extract("prv");	// get table of all existing {prv=...} subrecs in notes 
char w[32];
if (k.rating!=0 && ((short_bd(update_now) - short_bd(k.seen)))>31)	// best (no dup of CURRENT NEW rating)
	d->in_or_add(strendfmt(calfmt(w,"%3.3M %4C",k.seen)," %1.1f",0.1*k.rating));
ut.insert(d);
delete d;
}

// this is the ONLY place where a new record is ADDED to smdb.mst
void MVDIR::update_om2(bool setting_rating)	// could pass/populate optional non-null WATCHED ptr before overwriting
{
OMDB1 om1(true);
OM1_KEY k;
memset(&k,0,sizeof(OM1_KEY));
if (GWunam_year_override && omz.year==0) m_finish("fuckfuck year override error imno:%d",omz.k.imno);
bool added=false;
if (!om1.get_om1(k.imno=omz.k.imno,&k))
	{
	IMDB_API ia;
	k.sz=dr_foldersize(Path) /100000000;	// (100M) stores foldersize in "tenths of a Gb"
	if (wh.sseen) {k.added=short_bd(k.seen=wh.sseen); k.rating=wh.rating;}
	else k.added=short_bd(update_now);
	const char *inam=ia.get(k.imno,get_fld_name(FID_TITLE));
	if (inam && *inam && strcmp(inam,omz.title)!=0)
		{k.mytitle=om1.str2rh(omz.title);}
	k.partwatch=omz.k.partwatch;
	k.tmno=omz.k.tmno;
	k.tv=omz.k.tv;
	om1.put(&k);
   added=true;
	}
#ifdef UNUSED
// definitely not used after 24/11/2025
if (GWunam_year_override && omz.year!=0)
   {
   char wrk[32];
   USRTXT ut(k.imno,&om1);
   DYNAG d(0,1);
   d.cargo("year",5);
   d.put(strfmt(wrk,"%d",omz.year));
   ut.insert(&d);
   }
#endif
   {RECENT recent; recent.put(k.imno);}
if (!setting_rating || !omz.k.rating) return;
k.partwatch=omz.k.partwatch;
om1.put_rating(k.imno,omz.k.rating, &k); 
#ifdef KEEP_TMDB_SYNCHED		// maintain TMdb online ratings in real time
if (!tmdb_set_rating(authenticate, k.tmno, k.imno, k.tv, 0,0, k.rating))
	sjhlog("Error setting tmdb rating:%d on I:%d",k.imno);
#endif
}


void update_api_both(OMZ *oz)      // update both imdb.api (all api results) AND imdb.fld (fast-load compressed version)
{
update_api_api(oz);
update_api_dbf(oz->k.imno);
}

void MVDIR::update_imdb(bool setting_rating)
{
update_api_both(&omz);
update_om2(setting_rating);	// UniqCall. set_watch_history=FALSE when adding NEW movie
}


// Create "definitive" _ttNNNNN containing my preferred MovieName
static void write_tt_file(const char *folder, OMZ *oz)
{
char fn[FNAMSIZ];
HDL f=flopen(strfmt(fn,"%s/_tt%d", folder, oz->k.imno),"w");
if (f==NULL) crash("Can't write %s (read-only access?)",fn);
if (oz->k.mytitle)				// If non-zero it's the rhdl of user-override tItle to store in _ttNNNNN
	{
	fmt_name_year(fn,oz);		// Don't write actual tItle unless it's different to imdb name from api
	flputln(fn,f);
	}
flclose(f);
}

char* MVDIR::prvname(const char *fn)
{
static char *saved_prvname=NULL;
if (fn==NULL)
   return(saved_prvname);
char prv[FNAMSIZ];
strancpy(prv,fn,strlen(fn)-4+1);      // +1 for EOS
if (strcmp(Basename(Path),prv))
   {
   strins(prv,"/");
   strins(prv,Basename(Path));
   }
saved_prvname=stradup(prv);
return(0);  // not interested in return value here
}

bool MVDIR::rename_file(const char *fn, bool do_it)	// Rename avi / srt / nfo FILE in 'Dt' to match preferred MovieName
{
int len=strlen(fn)-4;
if (len==strlen(omz.title) && !strncmp(omz.title,fn,len))   // 26/10/24 if there's no need to Rename...
   return(do_it);    // ...returns FALSE for "Rename needed?" TRUE for "Rename done!" (maybe earlier)
if (do_it)
	{
	char from[FNAMSIZ], to[FNAMSIZ];
   static bool again=false;
   if (!again && drisvid(drext(fn)))
      {/*MOVE4BYTES(vidext,drext(fn));*/ prvname(fn); again=true;}
	strfmt(from,"%s/%s",Path,fn);
	int cd=0;		// default assumption is there's only going to be ONE video file
	do	{
		char cdnum[8]; strfmt(cdnum," cd%d",++cd);
		strfmt(to,"%s/%s%s%s",Path,omz.title,(cd<2)?"":cdnum,drext(fn));
		} while (drattrget(to, NULL));
	exec_rename(from,to);
	}
return(true);		// We WILL (or DID) Rename this file
}

bool MVDIR::rename_folder(bool do_it)	// Rename Movie FOLDER to match preferred MovieName + (YEAR)
{
char fn[FNAMSIZ], to[FNAMSIZ];
if (!strcmp(Foldername, fmt_name_year(fn,&omz)))   // 26/10/24 if there's no need to Rename...
   return(do_it);	// ...returns FALSE for "Rename needed?" TRUE for "Rename done!" (maybe earlier)
if (do_it)
	{
	strcpy(&strcpy(to,Path)[Foldername-Path], fn);
	exec_rename((char*)Path,to);
	Foldername=(char*)strrchr(strcpy(Path,to),'/')+1;		// point to final folder in path
	}
return(true);		// We WILL (or DID) Rename the folder
}

void MVDIR::check_GWunam(void)
{
extern GtkWidget *GWunam;	// User override MovieName (will be stored in _ttNNNNN)
const gchar *text = gtk_entry_get_text(GTK_ENTRY(GWunam));	// title & possible Year user override
if (*text)
	{
	char wrk[sizeof(omz.title)+7];
	strancpy(wrk,text,sizeof(wrk));
	int len=strlen(wrk), yr;
	if (len>=8 && wrk[len-6]=='(' && valid_movie_year(yr=a2i(&wrk[len-5],0)) && a2err_char==')')
		{wrk[len-6]=0; strtrim(wrk); GWunam_year_override=true; omz.year=yr;}
	strancpy(omz.title,wrk,sizeof(omz.title));
	}
}

bool MVDIR::omdb1_rec_exists(bool do_it)
{
OMDB1 *om1 = new OMDB1(true);
OM1_KEY k;
bool got=om1->get_om1(k.imno=omz.k.imno,&k);
delete om1;
if (do_it && !got)
	{
	if (update_now==0) update_now=calnow();
	update_imdb(false);			// ADD this movie to imdb.api + imdb.fld (but DON'T set Rating)
	}
return(got);
}


void MVDIR::rarbg_del(void)	// Delete any files in MovieFolder starting with (case-insensitive) "rarbg"
{
for (int i=0;i<dt->ct;i++)
	{
	FILEINFO *fi=(FILEINFO*)dt->get(i);
	char fn[FNAMSIZ];
	if (megabytes(fi->size)<75 && unwanted_filename(fi->name))
		{
		if (unlink(strfmt(fn,"%s/%s",Path,fi->name))) Sjhlog("Error deleting %s",fn);
		else dt->del(i--);
		}
	}
}

static bool latest_srt(DYNAG *dt, int subscript)	// return TRUE unless there's a LATER *.srt file in table
{
FILEINFO *fi=(FILEINFO*)dt->get(subscript);
int32_t dttm=fi->dttm;
for (int i=0; i<dt->ct; i++)
	{
	fi=(FILEINFO*)dt->get(i);
	if ((i!=subscript) && SAME4BYTES(drext(fi->name), ".srt"))
		if (fi->dttm > dttm)
			return(false);
	}
return(true);	// Didn't find a later *.srt file, so this is the one to be renamed
}

static char *mytrim(char *s)
{
int len;
while ((len=strlen(s))>1 && s[len-1]<=SPACE) s[len-1]=0;
return(s);
}

static bool title_tag_matches(const char *fnam, const char *title)
{
char cmd[512], buf[256];
strcpy(cmd,"ffprobe -v error -show_entries format_tags=title -of default=noprint_wrappers=1:nokey=1");
strendfmt(cmd," \"%s\"",fnam);
int err=exec_cmd(cmd,buf,sizeof(buf));
mytrim(buf);
return(err==0 && strcmp(buf,title)==0);      // true if tag matches, else false
}

bool MVDIR::tag_present(void)
{
char fnam[256];
strfmt(fnam,"%s/%s",Path,biggest_vid_fn);
char title[80];
strfmt(title,"%s (%d)",omz.title,omz.year);
return(title_tag_matches(fnam,title));
}

bool MVDIR::update_tags(void)
{
int pass=0;   // pass:0 check existing file for tags, pass:1 basic addtag, pass:2 aggressive addtag
char cmd[512], *_prvnam;
char buf8k[8192];
char rvf[128];       // (probably) rvf = "renamed video file"
strfmt(rvf,"%s%4.4s",omz.title,vidext);
const char *fnam=rvf;
const char *basen=strrchr(Path,'/')+1; // Just the name of parent (movie Name+Year), not fullpath
set_cursor(true);
assert(chdir(Path)==0);
_prvnam=prvname(NULL);
while (!title_tag_matches(fnam,basen) && ++pass<=2)
   {
   strcpy(cmd,"ffmpeg -y");        // 11/1/25 - ffmpeg needs "file:" before filename if it contains a semicolon
   if (pass==2) strcat(cmd," -fflags +genpts");
#ifdef pre23_5_25
   strendfmt(cmd," -i \"file:%s\" -codec copy -metadata title=\"%s\"",rvf,basen);
   if (_prvnam!=NULL) strendfmt(cmd," -metadata publisher=\"%s\"",_prvnam);
#else
// Update avoids trying to copy subtitle streams, in case they're "unrecognised format".
// BUT maybe should just Insert   -map 0:v:0 -map 0:a:0  into previous (keeping double quotes and "file:" component)
   strendfmt(cmd," -i \"file:%s\" -codec copy  -map 0:v:0 -map 0:a:0  -metadata title=\"%s\"",rvf,basen);
   if (_prvnam!=NULL) strendfmt(cmd," -metadata publisher=\"%s\"",_prvnam);
#endif
   strendfmt(cmd," \"%s\"",fnam="temp.mkv");
   int err=exec_cmd_wait(cmd);      // 7/2/25  make sure ffmpeg finished!
   if (err!=0)
      {
      sjhlog("\n%s\n",cmd);
      crash("Tagging command failed:\n%s",cmd);
      }
   }
if (pass>0 && pass<=2)    // then we did create temp.mkv containing the required tags
   {
   char to[256];
   strfmt(to,"ORG_%s",rvf);
   exec_rename(rvf,to);
   if (!SAME4BYTES(drext(rvf),".mkv"))
      MOVE4BYTES(&rvf[strlen(rvf)-4],".mkv");
   exec_rename("temp.mkv", rvf);
   }
memtake(_prvnam);
set_cursor(false);
return(true);
}

// do_it=FALSE when called from constructor (does anything need to be done?), TRUE when called by app code
bool MVDIR::rename(bool do_it)
{
bool tt_exists=false;
if (!omdb1_rec_exists(do_it))						// Count as "renaming" if no existing record in *.dbf 
	if (!do_it) return(true);						// we WILL need to add rec to *.dbf when do_it=true
GWunam_year_override=false;		// probably unnecessary, but shouldn't do any harm
if (do_it) rarbg_del();				// delete dross torrent files (and remove from DIRTBL before doing renames)
for (int i=0;i<dt->ct;i++)
	{
	FILEINFO *fi=(FILEINFO*)dt->get(i);
	const char *fn = fi->name;
	if (SAME3BYTES(fn,"_tt")) tt_exists=true;
	const char *ext=drext(fn);
	if (do_it && SAME4BYTES(ext,".srt") && !latest_srt(dt,i)) continue;
   if (SAME4BYTES(fn,"ORG_")) continue;      // Don't Rename this one again - it's ALREADY renamed!
	if ((drisvid(ext) && megabytes(fi->size)>300) || stridxs(ext, ".srt.jpg.nfo")!=NOTFND)
		if (rename_file(fn,do_it) && !do_it) return(true);	// No need to continue if even ONE Rename needed
	}
if (!tt_exists)
	{
	if (!do_it) return(true);		// adding missing _tt file counts as "renaming"
	write_tt_file(Path,&omz);
	}
bool rf=rename_folder(do_it);		// If FOLDER is renamed, 'path' and 'foldername' are adjusted accordingly
if (do_it && update_tags_do_it)
   rf=update_tags();
return(rf);
}

static void align_partwatch_on_disc(const char *pth, char partwatch)
{
bool unchanged=NO;
int i;
DIRSCAN ds(pth, "watched*");
FILEINFO fi;
struct dirent *entry;
DYNAG d(sizeof(FILEINFO));  // Store FULLPATH as returned by ds.next(), not just filename in fi
while ((entry=ds.next(&fi))!=NULLPTR)
	{
	if ((entry->d_type&DT_DIR)!=0) continue;
    if (d.ct) m_finish("multiple PartWatch!");
    char str[32];
    strancpy(str,&entry->d_name[7],30);
    if (str[0]==SPACE) strdel(str,1);
    if (str[0]=='0') strdel(str,1);
    if (str[0]=='.') strdel(str,1);
    if (!ISDIGIT(str[0])) m_finish("Bad PartWatch file:%s",fi.name);
    unchanged = ((str[0]-'0')==partwatch);
    d.put(&fi);
	}
if (unchanged || (d.ct==0 && partwatch==0)) return;
char from[FNAMSIZ], to[FNAMSIZ];
strfmt(to,"%s/watched 0.%c",pth,partwatch+'0');
if (d.ct)
    {
    memmove(&fi,d.get(0),sizeof(FILEINFO));
    strcpy(from,fi.name);
    if (partwatch) exec_rename(from,to);
    else drunlink(from);
    return;
    }
// If we get here, there's no existing file, but partWatch is non-zero, so write a new file
HDL f=flopen(to,"w");
flclose(f);
}

static void align_partwatch_mst(int32_t imno, char partwatch)	// Ensure smdb.mst correctly reflects this imno+partwatch pair
{
USRTXT ut(imno);
DYNAG *d=ut.extract("watched");
while (d->ct>0) d->del(0);			// there should be AT MOST ONE existing partwatch subrec, but play safe!
char w[8]; 
if (partwatch) d->put(strfmt(w,"0.%d",partwatch));
ut.insert(d);
delete d;
}

static void partwatch_update(const char *pfn, OM1_KEY *om)
{
align_partwatch_on_disc(pfn, om->partwatch);
align_partwatch_mst(om->imno, om->partwatch);
}


void MVDIR::set_rating(void)
{
if (update_now==0) update_now=calnow();
OM1_KEY k;
char prv=wh.rating;
char	fn[256], old_fn[256];
strfmt(fn,"%s/_%1.1f",Path, 0.1 * omz.k.rating);   // The FULL name of required (NEW) Rating folder
if (prv<=0)	// no existing rating folder, so make new one
	{ if (mkdir(fn, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) !=0) crash("Error writing %s Rating folder",fn); }
else			// change dttm of existing folder
	{
	if (prv!=omz.k.rating) exec_rename(strfmt(old_fn,"%s/_%1.1f",Path, 0.1 * prv), fn);
	utime(fn,NULL);	// after possible Rename, set DateLastModified to 'now'
	}
partwatch_update(fn,&omz.k);		// Add, Rename, or delete "watched 0.n" in dbf AND rating folder
if (prv!=omz.k.rating || ((short_bd(update_now) - short_bd(omz.k.seen)))>31)
	omz.k.seen=update_now;		// only update 'seen' if > 1 nmonth since last rating, OR changed rating value
watch_history_update(omz.k.imno);
update_imdb(true);				// This call updates RATING for movie in old OmDB.dbf and new imdb.dbf
}



char* MVDIR::get_prv_txt(char *buf)
{
WATCH_HISTORY _wh; memmove(&_wh,&wh,sizeof(WATCH_HISTORY));
if (!_wh.sseen)
	{
	OMDB1 om1(true);
	OM1_KEY k;
	if (om1.get_om1(omz.k.imno,&k))
		{_wh.sseen=k.seen; _wh.rating=k.rating;}
	}
if (_wh.sseen) strendfmt(calfmt(buf,"Last watched %3.3M %4C, Rated",_wh.sseen)," %1.1f",0.1*_wh.rating);
else strcpy(buf,"no prv");
return(buf);
}


static void single_shot_tooltip(OMZ *oz, char *buf8k)
{
static int32_t prv=0;
if (prv==oz->k.imno) return;
OMZ _omz;                        // kludge stops Tmdb_all_from_number() from updating what should be "const" omz
memcpy(&_omz,oz,sizeof(OMZ));    // (I think only possible update is if the api call ends up assigning to tmdb:TV)
prv=_omz.k.imno=oz->k.imno;
if (!tmdb_all_from_number(oz, buf8k)) return;
JBLOB_READER jb(buf8k);
const char *p;
if ((p=jb.get("Director"))!=0) strfmt(buf8k,"Director: %s",p);
if ((p=jb.get("Actors"))!=0) strendfmt(buf8k,"\n%s",p);
if ((p=jb.get("plot"))!=0) strendfmt(buf8k,"\n\n%s",p);
}

char* MVDIR::get_tooltip_text(void)
{
if (tooltip_text==NULL) // pointer initially NULL - not updated UNLESS valid imno leads to valid movie details
	{
	char buf8k[8096]; // Allow PLENTY of space for the text
	*buf8k=0;
	if (omz.k.imno)
		{
		IMDB_API ia;      // TODO - get EVERYTHING from ia into buf8k. IF we got nothing (new imno not yet in system) THEN...
		const char *p;    // ...call actual api's in single_shot_tooltip, and share code below for formatting tooltip text
		if ((p=ia.get(omz.k.imno,"Director"))!=0 && *p) strfmt(buf8k,"Directed by %s",p);
		if ((p=ia.get(omz.k.imno,"Actors"))!=0 && *p) strendfmt(buf8k,"\n%s",p);
		if ((p=ia.get(omz.k.imno,"plot"))!=0 && *p) strendfmt(buf8k,"\n\n%s",p);
      if (buf8k[0]==0) single_shot_tooltip(&omz,buf8k);
      if (*buf8k) return tooltip_text=stradup(buf8k);
		}
	}
return(NULL);
}

