//#include <gtkmm.h>

#include <gtk/gtk.h>
#include <gtk/gtkx.h>

#include <string>

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

MVDIR *mv;

GtkBuilder *builder;
GtkWidget *window;
GtkWidget *GWinum;	// IMDB number - only editable if didn't get it from *.nfo or _tt
GtkWidget *GWinam;	// The "standard default" name of the movie as gotten by IMDB api
GtkWidget *GWunam;	// User override MovieName (will be stored in _ttNNNNN) - IGNORED IF ALREADY IN DATABASE!
GtkWidget *GWlabel;	// Foldername preceded by IMDB to use for Cut&Paste&Lookup
GtkWidget *GWok;
GtkWidget *GWscale;
GtkAdjustment *GWadj;
GtkWidget *GWPartWatched;
GtkAdjustment *GWadj2;
GtkWidget *GWwatchLabel;	// Label above "Partwatch" spin control RED if non-zero
GtkWidget *GWcopy;	// copy contents of GWlabel into clipboard to paste into browser search
GtkWidget *GWprv;		// label showing previous WatchDate + Rating if present, else blank

static void MessageBox(const char *txt)
{
GtkDialogFlags flags = GTK_DIALOG_DESTROY_WITH_PARENT;
GtkWidget *dialog;
dialog = gtk_message_dialog_new (GTK_WINDOW(window), flags, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", txt);
gtk_dialog_run (GTK_DIALOG (dialog));
gtk_widget_destroy(dialog);
}

static bool get_session_gui(char *session_id)
{
char wrk[256], request_token[64];
bool ok=get_request_token_url(wrk, request_token);

GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                           GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
                                           "Authenticate request_TOKEN by visiting...");
gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Authentication Confirmed", 1, NULL);
GtkWidget *content_area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
GtkWidget *label = gtk_label_new(NULL); // Create the label without setting text directly
char *markup = g_markup_printf_escaped("<a href='%s'>%s</a>", wrk, wrk);
gtk_label_set_markup(GTK_LABEL(label), markup);
g_free(markup); // Free the markup string after setting it
gtk_container_add(GTK_CONTAINER(content_area), label);
gtk_widget_show_all(dialog);
if (gtk_dialog_run(GTK_DIALOG(dialog)) == 1) ok=true;
gtk_widget_destroy(dialog);

return(authenticate_request_token(request_token, session_id));
}

bool authenticate(char *session_id)
{
if (session_id_works(session_id)) return(true);
if (!get_session_gui(session_id)) return(false);
return(save_new_session(session_id));
}


static void escape_ampersand(char *s)	// replace every occurence of "&" with "&amp;"
{
int p;
while ((p=stridxc('&',s))!=NOTFND) s[p]='\t';
while ((p=stridxc('\t',s))!=NOTFND) strins(strdel(&s[p],1),"&amp;");
} 

char *fmt_name_year(char *s, OMZ *oz)
{
*s=0;
if (oz->year)
    strfmt(s,"%s (%d)",oz->title,oz->year);
else sjhlog("fuck");
return(s);
}

static int inum_unknown=NO;
static void show_inam(const char *colour)
{
char w[256], *e;
strfmt(w,"<span foreground=\"%s\" size=\"x-large\">", colour);
fmt_name_year(e=strend(w), &mv->omz);	// Allow ampersand		// was Usr
escape_ampersand(e); 
strcat(w,"</span>");
gtk_label_set_markup(GTK_LABEL(GWinam), (const gchar*) w);
}


static void show_watchlabel(int val)
{
char w[256];
bool bigger = (val!=0);
const char* colour=(bigger?"red":"grey");
strfmt(w,"<span foreground=\"%s\" size=\"%s\">", colour, bigger?"larger":"smaller");
strcat(w,"PartWatched");
strcat(w,"</span>");
gtk_label_set_markup(GTK_LABEL(GWwatchLabel), (const gchar*) w);
gtk_widget_set_visible(GWwatchLabel,true);
}

static void activate_partwatch_if_wanted(void)
{
bool can_partwatch=false;
if (gtk_adjustment_get_value(GWadj)>0) can_partwatch=true;
gtk_widget_set_sensitive(GWPartWatched,can_partwatch);
}

static void show_scale(bool show)
{
if (show)
	{
	char w[256];
	for (int i=0;i<10;i++)
		gtk_scale_add_mark((GtkScale*)GWscale, i, GTK_POS_TOP, (const char*)strfmt(w,"%d",i));
	gdouble gd=0.1 * mv->omz.k.rating;
	gtk_adjustment_set_value (GWadj, gd);
	if (!mv->omz.k.partwatch)
		{
		USRTXT ut(mv->omz.k.imno);		// Opens omdb.mst and reads any user notes record
		DYNAG *d=ut.extract("watched");
		if (d->ct>1) m_finish("multiple partwatched subrecs");
		if (d->ct>0)
			{
			int i=dot2i((char*)d->get(0));
			if (i==NOTFND) m_finish("Bad partwatch in notes");
			mv->omz.k.partwatch=i;
			}
		delete d;
		}
	gtk_adjustment_set_value (GWadj2, mv->omz.k.partwatch);
	show_watchlabel(mv->omz.k.partwatch);
	}
gtk_widget_set_visible(GWscale,show);
activate_partwatch_if_wanted();
}

static void widget_init(void)
{
char w[256];
gint pos=0;
show_scale(NO);
if (mv->inp_state==1)	// Didn't get imno from _tt or *.nfo, so user can enter / override it
	{
	gtk_label_set_text(GTK_LABEL(GWlabel), (const gchar*) strfmt(w,"IMDB %s",mv->foldername));
	if (mv->omz.k.imno!=0)
		{
		show_inam("red");
		strfmt(w,"tt%d",mv->omz.k.imno);
		gtk_entry_set_text(GTK_ENTRY(GWinum), (const gchar*)w);
		}
	gtk_button_set_label((GtkButton*)GWok, "Identify");	
	gtk_widget_hide(GWPartWatched);
	gtk_widget_hide(GWwatchLabel);
	return;
	}
if (mv->inp_state==2) // - at least one file/folder needs renaming (and/or _ttNNNNN needs to be created) 		
	{
	show_inam("blue");
	gtk_entry_set_text(GTK_ENTRY(GWinum), (const gchar*)strfmt(w,"tt%d",mv->omz.k.imno));
	gtk_editable_set_editable((GtkEditable*)GWinum, FALSE);
	gtk_button_set_label((GtkButton*)GWok, "Rename Files");
	gtk_widget_hide(GWPartWatched);
	gtk_widget_hide(GWwatchLabel);
	return;
	}
/// inp_state==3 - set movie rating
show_inam("green");
gtk_widget_hide(GWlabel);
gtk_widget_hide(GWinum);
gtk_widget_hide(GWunam);

if (*mv->get_prv_txt(w))
	gtk_label_set_text(GTK_LABEL(GWprv), (const gchar*) w);

show_scale(YES);

gtk_widget_set_visible(GWwatchLabel,true);
gtk_widget_set_visible(GWPartWatched,true);		// added 6/8/24 - NEEDED!
gtk_widget_set_sensitive(GWPartWatched,true);
gtk_button_set_label((GtkButton*)GWok, "Set Rating");	
}


void set_button_image(GtkWidget *button, const char *file_path, int width, int height)
{
GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(file_path, width, height, true, NULL);
if (!pixbuf) crash("Error loading image: %s\n", file_path);
GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
g_object_unref(pixbuf); // Release the pixbuf after setting the image
gtk_button_set_image(GTK_BUTTON(button), image);
}


gboolean on_query_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard_mode, GtkTooltip *tooltip, gpointer user_data)
{
static const char *txt=0;
if (txt==0) txt=mv->get_tooltip_text();
if (!*txt) return(false);
gtk_tooltip_set_text(tooltip, txt);
return true;
}


static int run_main(int argc, char *argv[])
{
gtk_init(&argc, &argv);
char w[256], *we;
int sz=readlink("/proc/self/exe", w, sizeof(w));
if (sz<10) throw(88);
w[sz]=0;
strncpy(we=strrchr(w,'/'),"/usher.glade",20); // https://stackoverflow.com/questions/13237716/splash-screen-in-gtk
builder = gtk_builder_new_from_file(w);
window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
gtk_builder_connect_signals(builder, NULL);
GWinum = GTK_WIDGET(gtk_builder_get_object(builder, "GWinum"));
GWinam = GTK_WIDGET(gtk_builder_get_object(builder, "GWinam"));
GWunam = GTK_WIDGET(gtk_builder_get_object(builder, "GWunam"));
GWlabel = GTK_WIDGET(gtk_builder_get_object(builder, "GWlabel"));
GWprv = GTK_WIDGET(gtk_builder_get_object(builder, "GWprv"));
GWscale = GTK_WIDGET(gtk_builder_get_object(builder, "GWscale"));
GWadj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "GWadj"));
GWok = GTK_WIDGET(gtk_builder_get_object(builder, "GWok"));
GWPartWatched = GTK_WIDGET(gtk_builder_get_object(builder, "GWPartWatched"));
GWadj2 = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "GWadj2"));
GWwatchLabel = GTK_WIDGET(gtk_builder_get_object(builder, "GWwatchLabel"));

strcpy(we,"/lens.png");
//sjhlog("file [%s]", w);
GWcopy = GTK_WIDGET(gtk_builder_get_object(builder, "GWcopy"));
set_button_image(GWcopy, w, 25, 25); // Adjust width and height as needed

gtk_widget_set_has_tooltip(GWinam, TRUE);
g_signal_connect(GWinam, "query-tooltip", G_CALLBACK(on_query_tooltip), NULL);


strcpy(w,argv[1]);
mv=new MVDIR(w);
//if (memcmp(argv[0],"/home",5)==0) strins(w,"(DEV) ");
gtk_window_set_title((GtkWindow*) window, w);
widget_init();
gtk_widget_show(window);
gtk_main();
delete mv;
return EXIT_SUCCESS;
}

static void notify_error(int err)
{
const char *x="";
if (err==-500) x=" - Invalid folder path";
if (err==-393) x=" - Database system not active";
sjhLog("Failed with error %d%s",err,x);
printf("Failed with error %d%s\r\n",err,x);
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
int32_t imno=tt_number_from_str(pp), bd;
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

static int del(char *p)
{
int32_t deli = tt_number_from_str(&p[2]);
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


struct NMG {char nm[30]; short ct;};
int _cdecl cp_str(char *a, char *b)
{
int cmp=strcmp(a,b);
if (cmp<0) return(-1);
if (cmp>0) return(1);
return(0);
}

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


static void rebuild_dbf_cache(void)
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
}


static int list_mytitle(void)
{
char str[256], s[128], imno[16];
int ct=0;
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
	ct++;
	}
printf("Listed %d movies with 'non-imdb' names\n",ct);
return(0);	// No error
}

static int orphans(void)		// list any movies in imdb.api but not smdb.mst
{									// imdb.api = every movie usherette ever looked up, maybe deleted / not added to database
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
    if (in_table(&j,du->get(i),dq->get(0),dq->ct,sizeof(int32_t),cp_int32_t)==NOTFND)
        {
        int32_t imno=*(int32_t*)(du->get(i));
        strcpy(wrk,ia.get(imno,"Title"));
        if (!wrk[0]) strcpy(wrk,"MISSING");
        printf("%-8d  %s\n",imno, wrk);
        }
printf("Movies in %s but not %s...\n",sjh.filename(),Basename(ia.filename()));
for (i=0;i<dq->ct;i++)
    if (in_table(&j,dq->get(i),du->get(0),du->ct,sizeof(int32_t),cp_int32_t)==NOTFND)
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


static void update_api_cache(const char *p)	// -u param may be followed by NNNN-NNNNN range of imddID's if not ALL
{
if (!*p) p="1-999999999";
OM1_KEY omk;
omk.imno = tt_number_from_str(p);
int32_t hi;
if (a2err && a2err_char=='-') hi=tt_number_from_str(&p[stridxc('-',p)+1]); else hi=omk.imno;
bool one=(omk.imno==hi);	// crash if only looking for ONE imno, and it's NOT in the cache database
IMDB_API ia;
int ct, tmr_ct=0;	// ct to prevent hitting the rate limit, tmr_ct for occasional screen update
HDL	tmr=tmrist(100);
int then=calnow();
OMDB1 om1(true);
for (ct=0; ++ct<800 && omk.imno<=hi && om1.get_ge(&omk); omk.imno++)
	{
	const char *p=ia.get(omk.imno,0);
	if (p==NULL)
		{
		char buf8k[8192];    // Allow PLENTY of space for the ENTIRE ibmdb API call
		if ((++tmr_ct & 3)==0)
			{
			if (tmr_ct>100) break;
			while (tmrelapsed(tmr)<0) usleep(100000);
			tmrreset(tmr,100);
			}
		if (!api_all_from_number(omk.imno, buf8k)) break;
		ia.put(omk.imno,buf8k);
		if ((tmr_ct&15)==0) printf("imno:%d ADD %zu bytes TOT=%d\n",omk.imno,strlen(buf8k),tmr_ct);
		}
	else if (one) sjhlog("tt%07d [%s]",omk.imno,p);
	}
tmrrls(tmr);
int took=calnow()-then;
printf("took %d\r\n",took);
}

static int view(char *p)
{
int32_t imno = tt_number_from_str(&p[2]);
IMDB_API ia;
const char *str=ia.get(imno,0);
printf("\n%s\n",str);
return(0);
}


static char *str_unquote(char *s)
{
int len=strlen(s);
if (len>=2 && s[0]==CHR_QTDOUBLE && s[len-1]==CHR_QTDOUBLE)
	{s[len-1]=0; strdel(s,1);}
return(s);
}


static char *str_quote_if_commas(char *s)
{
if (stridxc(COMMA,s)==NOTFND) return(str_unquote(s));
strendfmt(s,"%c",CHR_QTDOUBLE);
strinsc(s,CHR_QTDOUBLE);
return(s);
}

static char *space_after_comma(char *s)
{
for (int i=1;s[i];i++)
	if (s[i]==COMMA && s[i+1]!=SPACE) strinsc(&s[i+1],SPACE);
return(s);
}



//Const,Your Rating,Date Rated,Title,URL,Title Type,IMDb Rating,Runtime (mins),Year,Genres,Num Votes,Release Date,Directors
//tt0100024,9,2016-03-28,Life Is Sweet,https://www.imdb.com/title/tt0100024/,movie,7.4,103,1990,"Comedy, Drama",11025,1990-11-15,Mike Leigh
static int xport(void)	// Update format of 'Watch History' subrecord within in Notes field
{
char str[256], s[128], imno[16];
int ct=0;
bool again=false;
OM1_KEY k;
OMDB1 om(true);
SCAN_ALL ia;
HDL f=flopen("/home/steve/Downloads/myratings.csv","w");
flputln(strcpy(str,"Const,Your Rating,Date Rated,Title,URL,Title Type,"
	"IMDb Rating,Runtime (mins),Year,Genres,Num Votes,Release Date,Directors"),f);
//flputln(strcpy(str,"imdbID,Rating10,WatchedDate"),f);
while (om.scan_all(&k,&again))
	{
int32_t ww=NO, want[]={3316948,1131729,100046,100140,100150,0};
for (int w=0;!ww && want[w];w++) ww=(want[w]==k.imno);
//if (!ww) continue;

	strfmt(imno,"tt%07d",k.imno);
//printf("%s\n",imno);
	if (k.rating<10) continue;		// hkmsrda7
	int rat=(k.rating+5)/10;
	if (k.rating>=93) rat=10;
	if (k.rating>=84 && k.rating<=89) rat=9;

rat=(rating2tmdb(k.rating)+5)/10;

	if (rat<1 || rat>10)
		m_finish("%s duff rating!", imno);
	if (k.seen==0) m_finish("%s duff date seen!",imno);
	strfmt(str,"%s,%d",imno,rat);
	calfmt(strend(str),",%4C-%02O-%02D",k.seen);
	ia.get(k.imno,FID_TITLE,s);
	strendfmt(str,",%s",str_quote_if_commas(s));
	strendfmt(str,",https://www.imdb.com/title/tt%07d/,movie,5.5",k.imno);
	ia.get(k.imno,FID_RUNTIME,s);
if ((ww=stridxc(':',s))==NOTFND) continue;
int mm=(a2i(s,0)*60) + a2i(&s[ww+1],0);
if (mm<60) continue; // ignore short movies 
	strendfmt(str,",%d",mm);
	ia.get(k.imno,FID_YEAR,s);
	int yr=a2i(s,4);
	strendfmt(str,",%s",s);
	ia.get(k.imno,FID_GENRE,s);
	strendfmt(str,",%s,12345",str_quote_if_commas(space_after_comma(s)));
strendfmt(str,",%4d-01-01",yr);
	ia.get(k.imno,FID_DIRECTOR,s);
	strendfmt(str,",%s",str_quote_if_commas(s));
	flputln(str,f);
	ct++;
	}
printf("Exported %d ratings\n",ct);
flclose(f);
return(0);	// No error
}





static int valid_folder(const char *p)
{
struct stat sb;
char path[PATH_MAX];
drfullpath(path,p);			// Fully expand passed path
if (stat(path,&sb)) return(-500);	// some kind of error (copied to system global 'errno')
return(NO);	// No error
}

static bool is_mount(char *p)
{
PATHDYNAG pd(p);
return(pd.is_mount());
}

int main(int argc, char *argv[])
{
int err=0;
if (argc==2)
	{
	char *p=argv[1];
	if (SAME2BYTES(p,"--")) p++;
	if (SAME2BYTES(p,"-a")) return(fix_added(&p[2]));
	if (SAME2BYTES(p,"-d")) return(del(p));
	if (SAME2BYTES(p,"-g")) return(list_genre());
	if (SAME2BYTES(p,"-i")) {leak_tracker(YES); rebuild_dbf_cache(); leak_tracker(NO); return(0);}
	if (SAME2BYTES(p,"-l")) return(list_mytitle());
	if (SAME2BYTES(p,"-o")) return(orphans());
	if (SAME2BYTES(p,"-u")) {update_api_cache(&p[2]); return(0);}
	if (SAME2BYTES(p,"-v")) return(view(p));
	if (SAME2BYTES(p,"-x")) return(xport());
	}
if (argc==2 && !is_mount(argv[1])) crash("Path not mounted [%s]",argv[1]);
try
	{
	err=valid_folder(argv[1]);
	if (!err) err=run_main(argc,argv);
   }
catch (int e)
	{
	err=e;
	}
if (err)
	{
	if (argc>1) printf("\n[%s\n\n]",argv[1]);
	notify_error(err);
	}
return(err);
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

// Use API with ImdbNo for title + Year - called by read_nfo(MV.e) AND on_GWok_clicked(eEe)
bool api_name_from_number(OMZ *zz)
{
char buf8k[8192]; // Allow PLENTY of space for the ibmdb API call
api_all_from_number(zz->k.imno,buf8k);
retrieve_api_title(zz,buf8k);
zz->year=a2i(strquote(buf8k,"Year"),4);
return(*zz->title!=0 && valid_movie_year(zz->year));    // ok - imdb number is valid - EM_KEY name+Year filled in
}


extern "C"
void  on_GWok_clicked(GtkButton *b)			// HERE is the on click Ok routine
{
if (mv->inp_state==3)
	{
	mv->omz.k.partwatch=gtk_adjustment_get_value(GWadj2);
	if (mv->omz.k.rating) mv->set_rating();
	gtk_window_close((GtkWindow*)window);	// Only close if Rating i/p (not if Num or Nam changed)
	return;
	}
if (mv->inp_state==1)	// need to confirm identity (auto- or user input imdb number)
	{
   if (!mv->omz.k.imno) {MessageBox("Missing ImdbNO"); return;}
	if (!api_name_from_number(&mv->omz)) {MessageBox("Invalid ImdbNO"); return;}
	mv->inp_state=2;
	}
else						// Must be (mv->inp_state==2)	// (we know some renaming is required)
	{
	mv->rename(true);
	mv->inp_state=3;
	}
widget_init();
}

extern "C"
void  on_GWinum_changed(GtkEditable *editable)
{
char w[32];
strcpy(w,gtk_editable_get_chars(editable,0,NOTFND));
mv->omz.k.imno=tt_number_from_str(w);
}

extern "C"
void  on_GWadj_value_changed(GtkAdjustment *a)
{
gdouble gd;
gd=gtk_adjustment_get_value (a);
int i=(int)(gd*10);
mv->omz.k.rating=(char)i;
activate_partwatch_if_wanted();
}

extern "C"
void  on_GWadj2_value_changed(GtkAdjustment *a)
{
int i=gtk_adjustment_get_value (a);
show_watchlabel(i);
}

static void google_search(const char *srch)
{
char browser[128], s[256];
strfmt(s,"%s%s","https://www.google.com/search?q=",srch);
int i;
while ((i=stridxc(SPACE,s))!=NOTFND) s[i]='+';
sjhlog("[%s]",s);
execute(parm_str("browser",browser, "xdg-open"),s);
}

extern "C"
void on_GWcopy_clicked(void)
{
const char *txt;
GtkClipboard *clipboard;
txt = gtk_label_get_text(GTK_LABEL(GWlabel));
clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
gtk_clipboard_set_text(clipboard, txt, -1);
google_search(txt);
}
