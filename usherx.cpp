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

static int check_ascii(const char *p)
{
bool pause=false;
int32_t imno = str2imno(p);
char str[256];
int i;
bool again=false, chg;
OM1_KEY k;
OMDB1 om(true);
IMDB_API ia;
int ct=0;
while (om.scan_all(&k,&again))
	{
   if (imno!=0) pause = (k.imno==imno);
	char *inam=strcpy(str,ia.get(k.imno,get_fld_name(FID_TITLE)));     // FID_TITLE FID_CAST
   char output[128];
if (pause)
i=99;         // set break point to THIS LINE for debugger to see context before conversion
//    convert_to_plain_ascii_char(inam, output, sizeof(output));
//   ret=myconv(inam, output);
   chg=uxlt(strcpy(output,inam));
   if (!chg && pause==0) continue;
   printf("UTF-8 error %s\n%s   tt%07d\n\n",inam,output,k.imno);
   sjhlog("UTF-8 error %s\n%s   tt%07d",inam,output,k.imno);
   ct++;
   if (ct>3) break;     // report a maximum of 4 conversion failures
	}
if (ct>0) printf("%d utf-8 encoded title (or cast) errors logged\n",ct);
return(0);	// No error
}




static char *str_unquote(char *s)
{
int len=strlen(s);
if (len>=2 && s[0]==QTDOUBLE && s[len-1]==QTDOUBLE)
	{s[len-1]=0; strdel(s,1);}
return(s);
}

static char *str_quote_if_commas(char *s)
{
if (stridxc(COMMA,s)==NOTFND) return(str_unquote(s));
strendfmt(s,"%c",QTDOUBLE);
strinsc(s,QTDOUBLE);
return(s);
}

static char *space_after_comma(char *s)
{
for (int i=1;s[i];i++)
	if (s[i]==COMMA && s[i+1]!=SPACE) strinsc(&s[i+1],SPACE);
return(s);
}


struct KEYCT {char key[32]; short ct;};

// 4 values stored as Plot s/b plot
static int view_all_json_keys(void)
{
int i, j, k;
IMDB_API ia;
DYNAG *im=ia.get_tbl();
int32_t *imno=(int32_t*)(im->get(0));
DYNTBL all(sizeof(KEYCT),(PFI_v_v)cp_str);
KEYCT *kc;
for (i=0; i<im->ct;i++)
	{
   const char *buf=ia.get(imno[i],NULL);
   JBLOB_READER jb(buf);
   DYNAG *keys=jb.get();
   for (j=0; j<keys->ct; j++)
      {
      const char *key=(const char*)(keys->get(j));
      KEYCT kc0;
      strcpy(kc0.key,key);
      kc0.ct=0;
      int k=all.in_or_add(&kc0);
      kc=(KEYCT*)all.get(k);
      kc->ct++;
      }
   SCRAP(keys)
//   if (i>10) break;
	}
SCRAP(im)
printf("all-ct=%d\n\n",all.ct);
kc=(KEYCT*)all.get(k);
for (i=0;i<all.ct;i++)
   printf("%s ct=%d\n",kc[i].key, kc[i].ct);
return(0);	// No error
}

static const char *api_fld[]={
   "Actors", "Director", "Genre", "Runtime", "Title", "Year", "imdbID",
   "imdbRating", "imdbVotes", "plot", "tmdbID", "tmdbTV", NULL};

static int list_missing_keys(void)  // usher -vk   list ALL missing / superfluous cached api_fields in imdb.api
{
int i, j, k;
IMDB_API ia;
DYNAG *im=ia.get_tbl();
int32_t *imno=(int32_t*)(im->get(0));
DYNTBL want(0,(PFI_v_v)cp_str);
for (i=0;api_fld[i]!=NULL;i++) want.put(api_fld[i]);
const char *key;
char txt[256], missing[128], excess[128];
for (i=0; i<im->ct;i++)
	{
   *missing=*excess=0;
   const char *buf=ia.get(imno[i],NULL);
   JBLOB_READER jb(buf);
   DYNAG *keys=jb.get();
   for (j=0; j<keys->ct; j++)
      if (want.in(key=(const char*)keys->get(j))==NOTFND) strendfmt(excess,"%s%s",(*excess?",":""),key);
   for (j=0; j<want.ct; j++)
      if (keys->in(key=(const char*)want.get(j))==NOTFND) strendfmt(missing,"%s%s",(*missing?",":""),key);
   SCRAP(keys)
   if (*missing==0 && *excess==0) continue;
   if (*excess==0 && !strcmp(missing,"imdbRating,imdbVotes")) continue;
   strfmt(txt,"imno:%d ",imno[i]);
   if (*excess) strendfmt(txt,"  HAVE:%s",excess);
   if (*missing) strendfmt(txt,"  LACK:%s",missing);
   printf("%s\n",txt);
	}
SCRAP(im)
return(0);	// No error
}

static int add_missing_api_rec(int32_t imno)
{
OMDB1 om1(true);
OMZ oz;
if (!om1.get_om1(imno,&oz.k))
   {
   printf("IMDB Number not in library!\n");
   return(1);
   }
printf("tt%07d added missing API data to imdb.api and imdb.fld\n", imno);
update_api_both(&oz);
return(NO); // No error
}

static int view(const char *p)
{
if (!p[0]) return(view_all_json_keys());
if (SAME2BYTES(p,"k")) return(list_missing_keys());
int32_t imno = str2imno(p);
const char *str;
   {                    // these brackets are just so database is closed before possible add_missing_api_rec() call
   IMDB_API ia;
   str=ia.get(imno,0);
   }
if (str==NULL) return(add_missing_api_rec(imno));
printf("\n%s\n",str);
return(0);
}


static int orphans(void)		// list any movies in imdb.Api but not smdb.mst
{									// imdb.Api = every movie usherette ever looked up, maybe deleted / not added to database
IMDB_API ia;					// smdb.mst = my actual movie database
DYNAG *du=ia.get_tbl();
OMDB1   sjh(true);
OM1_KEY om1;
bool again=false;
DYNAG *dq=new DYNAG(sizeof(int32_t));
while (sjh.scan_all(&om1,&again))
    dq->put(&om1.imno);
int i,j;
char wrk[128];
strfmt(wrk, "Record counts - %s:%d",Basename(ia.filename()),du->ct);
printf("%s  %s:%d\n",wrk,Basename(sjh.filename()),dq->ct);
printf("Movies in %s but not %s...\n",ia.filename(),Basename(sjh.filename()));
for (i=0;i<du->ct;i++)
    if (in_table(&j,du->get(i),dq->get(0),dq->ct,sizeof(int32_t),cp_long)==NOTFND)
        {
        int32_t imno=*(int32_t*)(du->get(i));
        strcpy(wrk,ia.get(imno,"Title"));
        if (!wrk[0]) strcpy(wrk,"MISSING");
        printf("%-8d  %s\n",imno, wrk);
        }
printf("Movies in %s but not %s...\n",sjh.filename(),Basename(ia.filename()));
for (i=0;i<dq->ct;i++)
    if (in_table(&j,dq->get(i),du->get(0),du->ct,sizeof(int32_t),cp_long)==NOTFND)
        {
        int32_t imno=*(int32_t*)(dq->get(i));
        sjh.get_om1(imno,&om1);
        strcpy(wrk,"?");
        sjh.rh2str(om1.mytitle,wrk);    // only non-blank if there's a custom name for movie
        printf("%-8d  %s\n",imno, wrk);
        }
delete du;
delete dq;
return(0);
}

static bool get_votes(int32_t imno, char *rating, char *votes) // Not really used (21/2/25)
{
char buf8k[8192];    // Allow PLENTY of space for the ENTIRE ibmdb API call
if (!omdb_all_from_number(imno, buf8k)) m_finish("api fail 1!");
JBLOB_READER jb(buf8k);
strcpy(rating,jb.get("imdbRating"));
strcpy(votes,jb.get("imdbVotes"));
return(*rating!=0 && *votes!=0);
}

static void copyjb(JBLOB_READER *dst, JBLOB_READER *src, const char *name)
{
const char *value=src->get(name);
if (!*value) value="n/a";
dst->put(name,value);
}

static void add_api_rating(int32_t imno, JBLOB_READER *jb)
{
char buf8k[8192];
OMZ oz;
oz.k.imno=imno;
bool ok=tmdb_all_from_number(&oz, buf8k);
if (!ok) m_finish("tmdb_all_from_number FAIL imno:%d",imno);
JBLOB_READER jb1(buf8k);
copyjb(jb,&jb1,"imdbRating");
copyjb(jb,&jb1,"imdbVotes");
}

// Update no more than 900 in one pass refreshing "cached api results" records in imdb.api
// ...for some hard-coded specific metric that might not be in all records (currently "imdbRating")
static int update_api_cache(void)
{
IMDB_API ia;
DYNAG *tbl=ia.get_tbl();
int i;
int32_t imno;
for (i=0;i<tbl->ct;i++)       // remove imno's that already have imdbRating in api cache database
   {
   JBLOB_READER jb(ia.get(imno=*(int32_t*)(tbl->get(i)),NULL));
   if (*jb.get("imdbRating")) tbl->del(i--);
   }
for (i=0;i<tbl->ct;i++)
   {
   JBLOB_READER jb(ia.get(imno=*(int32_t*)(tbl->get(i)),NULL));
   if (i>900) break;
   add_api_rating(imno, &jb);
   ia.put(imno,jb.get(NULL));
   printf("Updated %d of %d   imno:%d\n",i,tbl->ct,imno);
}
delete tbl;
return(0);
}

struct NMG {char nm[30]; short ct;};


static int list_genre(void)
{
char str[256], s[128], imno[16];
int i;
bool again=false;
OM1_KEY k;
OMDB1 om(true);
IMDB_API ia;
DYNTBL nmt(sizeof(NMG),(PFI_v_v)cp_str);
NMG nmg, *_n;
while (om.scan_all(&k,&again))
	{
	char genall[128], *_g;
	strcpy(genall,ia.get(k.imno,get_fld_name(FID_GENRE)));
   strcat(genall,",");
	strxlt(genall,COMMA,TAB);
	for (i=0; (_g=vb_field(genall,i))!=NULL && *strtrim(strcpy(nmg.nm,_g));i++)
		{
		if ((_n=(NMG*)nmt.find(nmg.nm))!=NULL) _n->ct++;
		else {nmg.ct=1; nmt.put(&nmg);}
		}
	}
for (i=0;i<nmt.ct;i++)
	{
	_n=(NMG*)nmt.get(i);
	printf("%-5d  %s\n",_n->ct, _n->nm);
	}
return(0);	// No error
}


static int rebuild_dbf_cache(void)  // rebuild imdb.fld optimised cache of MY data fields (not API fields)
{
printf("Updating imdb.dbf...\r\n");
IMDB_API ia;
DYNAG *d=ia.get_tbl();
IMDB_FLD im;
int i=im.recct();
if (i) m_finish("imdb.fld already contains %d records! Can't rebuild!",i);
for (i=0;i<d->ct;i++)
	{
	int32_t imno=*((int32_t*)d->get(i));
	const char *buf=ia.get(imno, NULL);
	im.put(imno,buf);
	}
printf("Wrote %d records\r\n",d->ct);
delete d;
return(0);     // No error
}


struct MIR {int32_t id; char rating_sjh; short year; char title[64];};
static MIR *fill_mir(char *str, MIR *m)
{
char *s;
if (!SAME2BYTES(str,"tt")) crash("Err1 line:%s",str);
if ((m->id=a2l(&str[2],0))==0) crash("Err2 line:%s",str);
if (a2err_char!=TAB) crash("Err3 line:%s",str);
m->rating_sjh=a2i(vb_field(str,1),0);
if (m->rating_sjh<1 || m->rating_sjh>10)  crash("Err3 line:%s",str);
m->year=a2i(vb_field(str,9),0);
if (!valid_movie_year(m->year))  crash("Err4 line:%s",str);
strcpy(m->title,vb_field(str,3));
return(m);
}
#define IMDBCSV "/home/steve/Downloads/imdb.csv"
// GET CSV FROM https://www.imdb.com/exports/?ref_=rt
// go to "Your Ratings", select "Export" and...
// check above page to see when finished exporting
static DYNTBL *missing_imdb_ratings1(void)
{
char  str[512];
MIR   m, _m;
DYNTBL tbl(sizeof(MIR),cp_long);
printf("Check %s for missing ratings\n",IMDBCSV);
HDL f=flopen(IMDBCSV,"r");
if (f==NULL) m_finish("%s not found! Export 'My ratings' from IMDB",IMDBCSV);
int ct;
for (ct=0; flgetln(str,500,f)>1; ct++)
   if (ct==0 && SAME4BYTES(str,"Cons")) ct=NOTFND;
   else tbl.put(fill_mir(tabify(str), &m));
flclose(f);
bool again=false;
OM1_KEY k;
OMDB1 om(true);
IMDB_API ia;
DYNTBL *tbl1 = new DYNTBL(sizeof(MIR),cp_long);
while (om.scan_all(&k,&again))
   if (k.rating!=0 && tbl.in(&k.imno)==NOTFND)
      {
//if (k.imno==3486626) sjhlog("%d",k.imno);
      m.id=k.imno;
      const char *ptr=ia.get(k.imno,"Year");
      if (ptr==NULL) m_finish("Missing imdb.api rec! Fix with usher -v%d", k.imno);
      m.year=a2i(ptr,0);		// TODO - this might not work for "Late Bloomers" 2023 or 2024
      m.rating_sjh=k.rating;
      strcpy(m.title,ia.get(k.imno,"Title"));
      tbl1->put(&m);
      }
return(tbl1);
}

static int rating2imdb(int rating)
{
if (rating>=93) return(10);
if (rating>=84) return(9);
if (rating==0) return(0);
if (rating<10) rating=10;
return((rating+5)/10);
}


static char *makeline(char *str, MIR *m, bool hi)
{return(strfmt(str,"tt%07d %s%d%s  %s (%d)",m->id,
   hi?"\033[1;37m":"",rating2imdb(m->rating_sjh),hi?"\033[0m":"",m->title,m->year));}
static int missing_imdb_ratings(const char *p)
{
int32_t done_up_to = str2imno(p), i, ch;
char str[256];
DYNTBL *t=missing_imdb_ratings1();
printf("%d listed ratings not in IMDB\nPress <Esc> to quit, any other key to visit IMDB and update rating\n",t->ct);
for (i=0;i<t->ct;i++)
   {
   MIR *m=(MIR*)t->get(i);
   if (m->id<=done_up_to) continue;
   printf("%s\n",makeline(str,m,true));
   do ch=keypress_waiting(); while (ch==0);
   printf("%s%s\n","\033[F",makeline(str,m,false));
   if (ch==ESC) break;
   visit_imdb_webpage(m->id);
   }
printf("%d of %d to go...\r\n",t->ct-i, t->ct);
return(0);
}


static int list_mytitle(void)
{
char str[256], s[128], imno[16];
int ct=0, cc=0;
bool again=false;
OM1_KEY k;
OMDB1 om(true);
IMDB_API ia;
while (om.scan_all(&k,&again))
	{
	if (k.mytitle==0) continue;
	const char *inam=ia.get(k.imno,get_fld_name(FID_TITLE));
	strfmt(imno,"tt%07d",k.imno);
	om.rh2str(k.mytitle,str);
	printf("%-11.11s%s\n%11.11s%s\n",imno,str,"(imdb)  ",inam);
	if (!stricmp(inam,str)) {cc++; printf("#############################\n");}
	ct++;
	}
printf("Listed %d movies with 'non-imdb' names (%d case-only)\n",ct,cc);
return(0);	// No error
}



static int32_t get_bd(const char *p)
{
int32_t bd=caljoin(a2i(p,4),a2i(&p[5],2),a2i(&p[8],2),0,0,0);  // convert string to binary date
char s[32];
calfmt(s, "%4C-%02O-%02D",bd);   // write binary date as a string
if (strcmp(p,s)) return(0);      // ERROR if string not EQ original 
return(bd);
}


static int fix_added(const char *pp)	// if added=0 set to watched date
{
int32_t imno=str2imno(pp), bd;
if (imno<9999 || a2err_char!=COMMA || (bd=get_bd(&pp[stridxc(COMMA,pp)+1]))==0)
	m_finish("Bad 'Date Added' parameter - expected -a[imno],YYYY-MM-DD");
OM1_KEY k;
OMDB1 om1(true);
if (!om1.get_om1(imno,&k)) m_finish("Error1 changing Date Added");
k.added=short_bd(bd);
if (!om1.upd(&k)) m_finish("Error2 changing Date Added");
printf("IMNO:TT%d Date Added changed to %s\n",imno,dmy_stri(short_bd(bd)));
return(0);	// No error
}

#include "mvdb.h"

static int size_check(void)   // one-off update to smdb.mst::foldersize where moovie.dbf has since indexed a BIGGER copy
{
int i, j;
DYNTBL timsz(sizeof(IMSZ),cp_long);
IMSZ im, *_im, *tim;
MVDB mvdb;
OMDB1 om(true);
OM1_KEY omk;
BL_CARGO blc;
for (blc.number=0; mvdb.get(BK_GE,&blc,NULL); blc.number++)
   {
   DYNTBL imsz(sizeof(IMSZ),cp_long);                 // table of all imno's on this disk number
   if (!mvdb.get(BK_EQ,&blc,&imsz)) m_finish("bums!");
   for (_im=(IMSZ*)imsz.get(i=0);i++<imsz.ct;_im++)
      {
      j=timsz.in_or_add(_im);
      tim=(IMSZ*)timsz.get(j);
      if (_im->sz > tim->sz) _im->sz=tim->sz;
      }
   }
for (omk.imno=0; om.get_ge(&omk); omk.imno++)
   {
   _im=(IMSZ*)timsz.find(&omk.imno);
   if (_im==NULL) {continue;}
   ushort prv_sz=omk.sz;
   omk.sz=_im->sz /100000000;	// (100M) stores foldersize in "tenths of a Gb"
   if (omk.sz != prv_sz)
      {
      if (!om.upd(&omk)) m_finish("Error2 updating filesize");
      char wrk[32];
      strfmt(wrk,"%1.1f",0.1*omk.sz);
      SJHLOG("tt%07d size:%s updated",omk.imno,wrk);
      }
   }
return(0);
}

static int delete_imno(const char *parm)
{
int32_t deli = str2imno(parm);
OMDB1 om1(true);
const char *fn=om1.filename();
if (om1.del(deli)) printf("\nDeleted imno:%d from %s\n",deli,fn);
else printf("\nFailed to delete imno:%d from %s\n",deli,fn);

IMDB_API ia;
fn=ia.filename();
if (ia.del(deli)) printf("\nDeleted imno:%d from %s\n",deli,fn);
else printf("\nFailed to delete imno:%d from %s\n",deli,fn);

IMDB_FLD imf;
fn=imf.filename();
if (imf.exists(deli) && imf.del(deli)) printf("\nDeleted imno:%d from %s\n",deli,fn);
else printf("\nFailed to delete imno:%d from %s\n",deli,fn);
return(0);
}

static int update_title_all_utf8(void)
{
int i, ct;
printf("Press y to convert all UTF-8 titles in imdb.api to Ascii\n");
do i=keypress_waiting(); while (i!='y');
printf("\nUpdating...\n");
IMDB_API ia;
DYNAG *im=ia.get_tbl();
int32_t *imno=(int32_t*)(im->get(0));
for (i=ct=0;i<im->ct;i++)
   {
   const char *ttl=ia.get(imno[i],get_fld_name(FID_TITLE));
   if (ttl==NULL) m_finish("no api title!");
   char new_ttl[80];
//   myconv(ttl,new_ttl);
//   if (strcmp(ttl,new_ttl))
   if (uxlt(strcpy(new_ttl,ttl)))  // IF UTF8 transliterator "normalised" any "extended" characters to plain Ascii
      {
      printf("tt%07d\n%s\n%s\n\n", imno[i],ttl,new_ttl);
      const char *buf=ia.get(imno[i],NULL);
      JBLOB_READER jb(buf);
      jb.put(get_fld_name(FID_TITLE),new_ttl);
      ia.put(imno[i],jb.get(NULL));
      ct++;
      }
   }
printf("%d UTF-8 titles reduced to Ascii\n",ct);
return(0);
}

static int update_title(const char *p)
{
int32_t imno = str2imno(p);
char mst_ttl[80], new_ttl[80], api_ttl[80];
int len;
if (imno==0) return(update_title_all_utf8());
printf("IMDB Number:tt%07d\n",imno);
	{
	OMDB1 om1(true);
	OM1_KEY k;
	if (!om1.get_om1(imno,&k)) m_finish("imdb number not in main smdb.mst database");
   if (k.mytitle==0) strcpy(mst_ttl,"(No title override)");
	else om1.rh2str(k.mytitle,mst_ttl);
	}
	{
	IMDB_API ia;
	strcpy(api_ttl,ia.get(imno,"Title"));
	}
printf("MST title:%s\n",mst_ttl);
printf("API title:%s\n",api_ttl);
printf("New title:");
fgets(new_ttl, 64, stdin);
printf("\n");
len=strlen(new_ttl);
if (len>0 && new_ttl[len-1]=='\n') new_ttl[--len]=0; // Should always end in (unwanted) newline
if (len==0) return(0);     // do nothing
int year_override=0;
if (new_ttl[len-1]==')' && new_ttl[len-6]=='(')
   if (valid_movie_year(year_override=a2i(&new_ttl[len-5],4))) new_ttl[len-7]=0;
   else m_finish("Bad year override");
strtrim(new_ttl);
if (len>63)
   m_finish("Bad title");
	{
	IMDB_FLD imf;
	if (!imf.upd_title(imno, new_ttl)) m_finish("Title update failed");
	}
	{
   bool ok=false;
	OMDB1 om1(true);
	OM1_KEY k;
	if (!om1.get_om1(imno,&k)) m_finish("this cant happen!");
	if (strcmp(new_ttl,api_ttl))
		ok=om1.upd_title(imno,new_ttl);
	else
		ok=om1.upd_title(imno,NULL);
   if (!ok) m_finish("om1.upd failed!");
	}

if (year_override)
   {
   IMDB_API ia;
   JBLOB_READER jb(ia.get(imno,0));
   char wrk[16];
   jb.put("Year",strfmt(wrk,"%d",year_override));
   ia.put(imno,jb.get(NULL));
   printf("\nDelete imdb.fld and rebuild with 'usher -i' after YEAR update\n\n");
   }

printf("Title %supdated\n", (year_override==0)?"":"and Year ");
return(0);  // means 'ok'
}

static int backup(void)
{
OMDB1 om1(true);
om1.backup();
return(0);
}

// manage <name>+<value> entries for ad-hoc variables stored in notes
// If no <value> specified after '=' delete any existing setting for <name>
static int notes_name_value(const char *p)
{
bool ok=true;
int imno=str2imno(p);
if (imno<1000 || a2err_char!=COMMA) ok=false;
const char *f=p+stridxc(COMMA,p)+1;           // point to start of fieldname after the comma
int flen=stridxc('=',f);
char wrk[64];
if (!ok || flen<2) m_finish("Expected ',<name>=<value>' after imno");
memmove(wrk,f,flen);
wrk[flen]=0;
char value[64];
strcpy(value,f+flen+1);
OMDB1 om1(true);
OM1_KEY k;
if (!om1.get_om1(imno,&k)) m_finish("imno not in smdb.mst");
USRTXT ut(imno);
DYNAG *d=ut.extract(wrk);	// get table of any existing {<name>=...} subrec(s) in notes
while (d->ct) d->del(0);   // delete any existing setting (NOT for 'prv', which can have multiple entries)
if (*value) d->in_or_add(value);
ut.insert(d);
printf("\nIMNO tt%07d  %s set to [%s]\n\n",imno, (char*)d->cargo(NULL), value);
delete d;
return(0);  // no error
}

int process_cli_flag(const char *parm)
{
int err;
char subx=*parm++;
leak_tracker(YES);
switch (subx)
	{
	case 'a': err=(fix_added(parm)); break;
   case 'b': err=backup(); break;
   case 'c': err=check_ascii(parm); break;
   case 'd': err=delete_imno(parm); break;
   case 'g': err=list_genre(); break;
   case 'i': err=rebuild_dbf_cache(); break;    // DELETE imdb.fld before running this!
   case 'l': err=list_mytitle(); break;
   case 'm': err=missing_imdb_ratings(parm); break;
   case 'n': err=notes_name_value(parm); break;
   case 'o': err=orphans(); break;
   case 's': err=size_check(); break;
   case 't': err=update_title(parm); break;
   case 'u': err=update_api_cache(); break;
   case 'v': err=view(parm); break;
   default:  printf("\nBad flag %s\n",parm); err=99;
   }
if (err==0) err=leak_tracker(NO); 
return(err);
}

// extra line xxxxxZZZZPP
