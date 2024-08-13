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

#include "pdef.h"
#include "cal.h"
#include "str.h"
#include "memgive.h"
#include "parm.h"
#include "smdb.h"
#include "omdb1.h"
#include "flopen.h"
#include "drinfo.h"
#include "dirscan.h"
#include "log.h"
#include "imdb.h"
#include "imdbf.h"
#include "exec.h"
#include "scan.h"
#include <cstdarg>
#include "qblob.h"
#include "my_json.h"
#include "tmdbc.h"

#include "usher.h"

char *fmt_name_year(char *s, OMZ *oz);	// (in usher.cpp)
bool api_name_from_number(OMZ *zz);

void crash(const char *fmt,...)	// pop up a YAD notification using formatted string as TITLE
{
char ss[1024];
va_list va;
va_start(va,fmt);
strnfmt(ss,sizeof(ss)-1,fmt,va);
Sjhlog("Fatal error!\r\n%s",ss);
char cmd[1024], buf[256];
exec_cmd(strfmt(cmd,"notify-send -u critical %c%s%c",CHR_QTDOUBLE,ss,CHR_QTDOUBLE),buf,sizeof(buf));
throw(99);
}

// Sort DIRTBL elements by filename. If renaming would have 'lost' duplicate OUTPUT filenames,
// they can be be preserved by appending cd2, cd3,...
int _cdecl cp_fi(const void *a, const void *b)
{return(strcmp(((const FILEINFO*)a)->name,((const FILEINFO*)b)->name));}

DIRTBL::DIRTBL(const char *pth):DYNAG(sizeof(FILEINFO))
{
DIRSCAN ds(pth);
FILEINFO fi;
struct dirent *entry;
while ((entry=ds.next(&fi))!=NULLPTR)
	{
//	if ((entry->d_type&DT_DIR)!=0) continue;
	strcpy(fi.name,entry->d_name);  // Make it JUST filename - not FULLPATH, as returned by ds.next()
//if (!fn_cdx(fi.name))	// Don't store "....cd2 / cd3" files (of ANY type)
   DYNAG::put(&fi);
	}
qsort(get(0),ct,sizeof(FILEINFO),cp_fi);		// Assume this puts cd1, cd2, cd3,... in the right sequence
}


static char *delete_square_bracket_text(char *n)
{
char *p=strchr(n,'['), *q;
if (p!=NULL && (q=strchr(p,']'))!=NULL)
	strdel(p,q-p+1);
const char *unwanted_prefix[] = {"www.Torrenting.com", "www.torrenting.org", NULL};
int i,j;
for (i=0;unwanted_prefix[i];i++)
	if (!strncasecmp(n,unwanted_prefix[i],j=strlen(unwanted_prefix[i])))
	{
		Sjhlog("delete prefix: %s",n);
		strdel(n,j);
	}
while (*n==SPACE || *n=='-') strdel(n,1);
return(n);
}

// p points to 4 characters before close bracket in foldername. Check if it's a valid year
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


int moviename_in_foldername(char *fn, OMZ *oz)
{
int i=strlen(fn)-7, j;
if ((j=stridxc('(',fn))!=NOTFND && valid_year(&fn[j], oz))
	{strancpy(oz->title,fn,j); return(YES);}
if (i<0) return(NO);		// (just in case it's a very short folder name)
for(i=j=0; (i=stridxc('.',&fn[j]))!=NOTFND && i<sizeof(oz->title)-5; j+=(i+1))
	{
	if (valid_year1(&fn[j+i+1], oz) && (fn[j+i+5]=='.' || !fn[j+i+5]))
		{strancpy(oz->title,fn,j+i+1); return(YES);}
	}
return(NO);
}


class APOSTROPHIZER {			// A class to dynamically allocate space for any number
public:
APOSTROPHIZER(const char *_ttl);
const char *get(void);
char	*fiddle(char *moviename);
//~APOSTROPHIZER();			// nothing for destructor to do!
private:
char ttl[80];
int	again, prv_added;
bool apostrophe_already_present;
};

// this fn REPLICATED in q3:window for user-not-steve Rating changes  // NOT ANY MORE!!! (JAN 2024)
char* APOSTROPHIZER::fiddle(char *mn)		// Fiddle with passed movie name to get round CLI and API quirks
{
strxlt(mn,SPACE,'+');
char *p, ins[5]={TAB,BACKSLASH,TAB,TAB,0};	// Allow for MULTIPLE single QT1 marks, so TEMPORARILY
															// use TABs (that can all be xlt'd to QT1's in one fell swoop)
while ((p=strchr(mn,CHR_QTSINGLE))!=NULL) strins(strdel(p,1),ins);
strxlt(mn,TAB,CHR_QTSINGLE);
char ins3[6]={'%','2','6',0};
while ((p=strchr(mn,AMPERSAND))!=NULL) strins(strdel(p,1),ins3);
return(mn);
}

APOSTROPHIZER::APOSTROPHIZER(const char *_ttl)
{
fiddle(strcpy(ttl,_ttl));
again=prv_added=0;
apostrophe_already_present=(stridxc(CHR_QTSINGLE,ttl)!=NOTFND);
}

// Return offset of last letter of first word ending in 's' in passed string
// Caller adds previously-returned offset to passed address, so it's effectively NEXT apostrophizable word
static int apostrophizable(const char *p)
{
int letters_stepped_over=0, i, c;
for (i=0;!ISALPHA(p[i]);i++) {;}	// skip past any initial non-letters
for (i=0;(c=p[i])!=0;i++)	//  now find the next "word" (sequence of ALPHA) ending in 's' 
	{
	if (c=='s' && letters_stepped_over>0 && (p[i+1]==0 || (p[i+1]==PLUS)))
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
	char add[4]={'%','2','7',0};
	strins(&ttl[prv_added+pa],add);
	prv_added+=(pa);	// +1 so NEXT call gets pointer to terminating 's of this call
	}
return(ttl);
}


static void retrieve_api_title(OMZ *zz, char *buf)
{
if (!*zz->title)
	{
	JBLOB_READER jb(buf);
	char *str=jb.get("Title");
	strncpy(zz->title,fix_colon(str),sizeof(zz->title));
//sjhlog("retrieve_api_title I%d [%s] %d",zz->k.imno,zz->title,zz->year);
	}
}


// Call API with title+Year to get ImdbNo
static int api_number_from_name(const char *fn, OMZ *omz)	// oz, not e (param)
{
char fixfn[256];
delete_square_bracket_text(strcpy(fixfn,fn));
if (moviename_in_foldername(fixfn,omz))
	{
	const char *ttl;
	APOSTROPHIZER ap(omz->title);
	while ((ttl=ap.get())!=NULLPTR)
		{
		char *p, buf8k[8192];
		if (api_all_from_name(ttl, omz->year, buf8k) && *(p=strquote(buf8k,"imdbID"))!=0
		    && SAME2BYTES(p,"tt") && (omz->k.imno=a2l(&p[2],0))!=0)
			{
			*omz->title=0;	// so next line will copy definitive MovieName into omz
			retrieve_api_title(omz,buf8k);	// omz.Year not updated, 'cos it MUST be correct in order for lookup to work!
	    	return(omz->k.imno);
			}
		}
	}
return(omz->k.imno=0);	// zeroise in case we put (invalid) value in there above
}

int32_t MVDIR::read_nfo(const char *fn)
{
char s[512];
int i, len, num=0;
HDL f=flopen(strfmt(s,"%s/%s",path,fn),"r");
while (num==0 && (len=flgetln(s,sizeof(s)-1,f))>=0)   // Look for   <uniqueid type="imdb">2788716</uniqueid>
    {
    if ((i=stridxs("<uniqueid type=",s))!=NOTFND && s[i+15]==34
    &&  SAME4BYTES(&s[i+16],"imdb") && s[i+20]==34 && s[i+21]=='>')
        num=a2l(&s[i+22],0);
    else if ((i=stridxs("www.imdb.com/title/tt",s))!=NOTFND)
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
//     Biggest (video), and latest of any *.srt files must match Foldername excluding "(YYYY)"
// IF *.nfo exists (max 1 such file)
//     if _ttNNNNN also exists, AND Ino specified in *.nfo, the numbers must match
// IF multiple *.srt files exist, only consider the latest-dated one
MVDIR::MVDIR(char *pth)		// MVDIR constructor
{
if (pth==NULL || strlen(pth)>=sizeof(path)) crash("Movie folder path invalid or missing");
strcpy(path,pth);
dt = new DIRTBL(path);
foldername=(char*)strrchr(path,'/')+1;		// point to final folder in passed path
memset(&omz,0,sizeof(OMZ));
biggest_vid_sz=0;
int i, num;
FILEINFO *fi;
char *fn;
for (fi=(FILEINFO*)dt->get(i=0);i++ < dt->ct;fi++)
	{
	fn=fi->name;
	if ((fi->attr&DT_DIR)!=0)	// is it a directory?
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
		biggest_vid_sz=fi->size;
	}
if (biggest_vid_sz==0) crash("No video files");
if (omz.k.imno==0)
	{
	api_number_from_name(foldername,&omz);		// TRUE api got match for movie name+year from dirnam
	api_name_from_number(&omz);
	inp_state=1;
	return;
	}
api_name_from_number(&omz);
inp_state=(rename(false)?2:3);
}

MVDIR::~MVDIR()
{
memtake(tooltip_text);
strquote(0,0);
delete dt;
}




static void update_api_api(OMZ *k)	// Update imdb.api (ImdbNo record contains ENTIRE text returned by API) 
{												// k points to private oK within MV
IMDB_API ia;
if (ia.get(k->k.imno,0)==NULL)
	{
	char buf8k[8192];    // Allow PLENTY of space for the ENTIRE ibmdb API call
	tmdb_all_from_number(k,buf8k);
	ia.put(k->k.imno,buf8k);
	}
}
static void update_api_dbf(int32_t imno)	// Update imdb.dbf (optimised storage for only the fields Q3 needs)
{
IMDB_FLD imf;
if (imf.exists(imno)) return;					// sjhLog("tt%d shouldn't exist",imno); 
char buf[8192];    // Allow PLENTY of space for the ENTIRE imdb API call
IMDB_API ia;
const char *ptr=ia.get(imno,0);
if (ptr==NULL) m_finish("Impossible!");		// Should have just Added IA.rec if not already present
imf.put(imno,ptr);
}

static int32_t update_now;		// Set when Put_rating() starts to ensure exactly constant value

static void align_watch_history_mst(int32_t imno, OM1_KEY *k)	// Ensure smdb.mst correctly reflects this imno+partwatch pair
{
USRTXT ut(imno);
DYNAG *d=ut.extract("prv");	// get table of all existing {prv=...} subrecs in notes 
char w[32];
if (k->rating!=0 && ((short_bd(update_now) - short_bd(k->seen)))>31)	// best (no dup of CURRENT NEW rating)
	d->in_or_add(strendfmt(calfmt(w,"%3.3M %4C",k->seen)," %1.1f",0.1*k->rating));
ut.insert(d);
delete d;
}

static void watch_history_update(const char *pfn, int32_t imno)
{
OMDB1 om1(true);
OM1_KEY k;
if (!om1.get_om1(imno,&k)) m_finish("wtf1");
align_watch_history_mst(imno, &k);
}

// this is the only place where a new record is ADDED to smdb.mst
void MVDIR::update_om2(bool setting_rating)	// could pass/populate optional non-null WATCHED ptr before overwriting
{
OMDB1 om1(true);
OM1_KEY k;
memset(&k,0,sizeof(OM1_KEY));
if (!om1.get_om1(k.imno=omz.k.imno,&k))
	{
	IMDB_API ia;
	k.sz=biggest_vid_sz/100000000;
	if (wh.sseen) {k.added=short_bd(k.seen=wh.sseen); k.rating=wh.rating;}
	else k.added=short_bd(update_now);
	const char *inam=ia.get(k.imno,get_fld_name(FID_TITLE));
	if (inam && *inam && !same_alnum(inam,omz.title))
		{k.mytitle=om1.str2rh(omz.title);}
	k.partwatch=omz.k.partwatch;
	k.tmno=omz.k.tmno;
	k.tv=omz.k.tv;
	om1.put(&k);
	}
if (!setting_rating || !omz.k.rating) return;
k.partwatch=omz.k.partwatch;
om1.put_rating(k.imno,omz.k.rating, &k); 
RECENT recent;
recent.put(k.imno);
#ifdef KEEP_TMDB_SYNCHED		// maintain TMdb online ratings in real time
if (!tmdb_update_rating(authenticate, k.tmno, k.imno, k.tv, 0,0, k.rating))
	sjhlog("Error setting tmdb rating:%d on I:%d",k.imno);
#endif
}



void MVDIR::update_imdb(bool setting_rating)
{
update_api_api(&omz);	// pass &oK to have TMdb+tv set
update_api_dbf(omz.k.imno);
update_om2(setting_rating);	// UniqCall. set_watch_history=FALSE when adding NEW movie
}


// Create "definitive" _ttNNNNN containing my preferred MovieName
static void write_tt_file(const char *folder, OMZ *omz)
{
char fn[FNAMSIZ];
HDL f=flopen(strfmt(fn,"%s/_tt%d", folder, omz->k.imno),"w");
if (f==NULL) crash("Can't write %s (read-only access?)",fn);
if (omz->k.mytitle)				// If non-zero it's the rhdl of user-override title to store in _ttNNNNN
	{
	fmt_name_year(fn,omz);		// Don't write actual title unless it's different to imdb name from api
	flputln(fn,f);
	}
flclose(f);
}


// Rename fully-qualified file or folder. Return YES if successful, else NO (fatal error?)
static bool exec_rename(char *from, char *to)
{
char cmd[512], buf[256];
strfmt(cmd,"%s %c%s%c %c%s%c", "mv", CHR_QTDOUBLE,from,CHR_QTDOUBLE, CHR_QTDOUBLE,to,CHR_QTDOUBLE);
int err=exec_cmd(cmd,buf,sizeof(buf));
if (err) crash("Error renaming %s to %s",from,to);
return(!err);
}


bool MVDIR::rename_file(const char *fn, bool do_it)	// Rename avi / srt / nfo FILE in 'Dt' to match preferred MovieName
{
int len=strlen(fn)-4;
if (len==strlen(omz.title) && !strncmp(omz.title,fn,len)) return(false);	// No need to Rename
if (do_it)
	{
	char from[FNAMSIZ], to[FNAMSIZ];
	strfmt(from,"%s/%s",path,fn);
	int cd=0;		// default assumption is there's only going to be ONE video file
	do	{
		char cdnum[8]; strfmt(cdnum," cd%d",++cd);
		strfmt(to,"%s/%s%s%s",path,omz.title,(cd<2)?"":cdnum,drext(fn));
		} while (drattrget(to, NULL));
	exec_rename(from,to);
	}
return(true);
}


bool MVDIR::rename_folder(bool do_it)	// Rename Movie FOLDER to match preferred MovieName + (YEAR)
{
char fn[FNAMSIZ], to[FNAMSIZ];
if (!strcmp(foldername, fmt_name_year(fn,&omz))) return(do_it);	// No need to Rename
if (do_it)
	{
	strcpy(&strcpy(to,path)[foldername-path], fn);
	//int p;
	//while ((p=stridxs("&amp;",to))!=NOTFND) *strdel(&to[p],4)='&';
	exec_rename((char*)path,to);
	foldername=(char*)strrchr(strcpy(path,to),'/')+1;		// point to final folder in passed path
	}
return(true);
}

bool MVDIR::omdb1_rec_exists(bool do_it)
{
OMDB1 *om1 = new OMDB1(true);
OM1_KEY k;
bool got=om1->get_om1(k.imno=omz.k.imno,&k);
delete om1;
if (do_it && !got)
	{
	extern GtkWidget *GWunam;	// User override MovieName (will be stored in _ttNNNNN)
	const gchar *text = gtk_entry_get_text(GTK_ENTRY(GWunam));	// User-defined MovieName - NOT INCLUDING (YEAR)
	if (*text) strancpy(omz.title,text,sizeof(omz.title));
	if (update_now==0) update_now=calnow();
	update_imdb(false);
	}
return(got);
}


void MVDIR::rarbg_del(void)	// Delete any files in MovieFolder starting with (case-insensitive) "rarbg"
{
for (int i=0;i<dt->ct;i++)
	{
	FILEINFO *fi=(FILEINFO*)dt->get(i);
	char fn[FNAMSIZ];
	if (unwanted_filename(fi->name))
		{
		if (unlink(strfmt(fn,"%s/%s",path,fi->name))) Sjhlog("Error deleting %s",fn);
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

// do_it=FALSE when called from constructor (does anything need to be done?), TRUE when called by app code
bool MVDIR::rename(bool do_it)
{
bool tt_exists=false;
if (!omdb1_rec_exists(do_it))
	if (!do_it) return(true);						// we WILL need to add rec to *.dbf when do_it=true
if (do_it) rarbg_del();				// delete dross torrent files (and remove from DIRTBL before doing renames)
for (int i=0;i<dt->ct;i++)
	{
	FILEINFO *fi=(FILEINFO*)dt->get(i);
	int megabytes=(fi->size >> 20);
	const char *fn = fi->name;
	if (SAME3BYTES(fn,"_tt")) tt_exists=true;
	const char *ext=drext(fn);
	if (do_it && SAME4BYTES(ext,".srt") && !latest_srt(dt,i)) continue;
	if ((drisvid(ext) && megabytes>300) || stridxs(ext, ".srt.jpg.nfo")!=NOTFND)
		if (rename_file(fn,do_it) && !do_it) return(true);	// No need to continue if even ONE Rename needed
	}
if (!tt_exists)
	{
	if (!do_it) return(true);		// adding missing _tt file counts as "renaming"
	write_tt_file(path,&omz);
	}
rename_folder(do_it);		// If FOLDER is renamed, 'path' and 'foldername' are adjusted accordingly
return(true);
}

static void align_partwatch_on_disc(const char *path, char partwatch)
{
bool unchanged=NO;
int i;
DIRSCAN ds(path, "watched*");
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
strfmt(to,"%s/watched 0.%c",path,partwatch+'0');
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
strfmt(fn,"%s/_%1.1f",path, 0.1 * omz.k.rating);   // The FULL name of required (NEW) Rating folder
if (prv<=0)	// no existing rating folder, so make new one
	{ if (mkdir(fn, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) !=0) crash("Error writing %s Rating folder",fn); }
else			// change dttm of existing folder
	{
	if (prv!=omz.k.rating) exec_rename(strfmt(old_fn,"%s/_%1.1f",path, 0.1 * prv), fn);
	utime(fn,NULL);	// after possible Rename, set DateLastModified to 'now'
	}
partwatch_update(fn,&omz.k);		// Add, Rename, or delete "watched 0.n" in dbf AND rating folder
if (prv!=omz.k.rating || ((short_bd(update_now) - short_bd(omz.k.seen)))>31)
	omz.k.seen=update_now;		// only update 'seen' if > 1 nmonth since last rating, OR changed rating value
watch_history_update(fn,omz.k.imno);
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

char* MVDIR::get_tooltip_text(void)
{
if (tooltip_text==0) // tooltip_text=(char*)memgive(100);
	{
	char buf2k[2048]; // Allow PLENTY of space for the text
	*buf2k=0;
	if (omz.k.imno)
		{
		IMDB_API ia;
		const char *p;
		if ((p=ia.get(omz.k.imno,"Director"))!=0) strfmt(buf2k,"Directed by %s",p);
		if ((p=ia.get(omz.k.imno,"Actors"))!=0) strendfmt(buf2k,"\n%s",p);
		if ((p=ia.get(omz.k.imno,"plot"))!=0) strendfmt(buf2k,"\n\n%s",p);
		}
	tooltip_text=stradup(buf2k);
	}
return(tooltip_text);
}

