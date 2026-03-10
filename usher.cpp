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
//#include "smdb.h"
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

GtkWidget *window;
GtkWidget *GWinum;	// IMDB number - only editable if didn't get it from *.nfo or _tt
GtkWidget *GWinam;	// The "standard default" name of the movie as gotten by IMDB api
GtkWidget *GWunam;	// User override MovieName (will be stored in _ttNNNNN) - IGNORED IF ALREADY IN DATABASE!
					// GWunam doesn't usually include (YEAR), but it CAN be used to override tmdb mis-dating
GtkWidget *GWSearchText;	// Foldername preceded by IMDB to use for Cut&Paste&Lookup
GtkWidget *GWok;
GtkWidget *GWscale;
GtkAdjustment *GWadj;
GtkWidget *GWPartWatched;
GtkAdjustment *GWadj2;
GtkWidget *GWwatchLabel;	// Label above "Partwatch" spin control RED if non-zero
GtkWidget *GWSearch;	// copy contents of GWSearchText into clipboard to paste into browser search
GtkWidget *GWprv;		// label showing previous WatchDate + Rating if present, else blank
GtkWidget *GWtags;	// Checkbox for "Update metadata Title & Publisher tags"
GtkWidget *GWNotes;	// Button to open "Edit Notes" dialog box

void set_cursor(bool busy)
{
if (window==NULL) return;  // (must be running console_test)
GdkWindow *gdk_window = gtk_widget_get_window(window);
GdkDisplay *display = gdk_display_get_default();
if (busy)
   {
   GdkCursor *cursor = gdk_cursor_new_from_name(display, "wait");
   gdk_window_set_cursor(gdk_window, cursor);
   g_object_unref(cursor); // Free the cursor after setting it
   }
else gdk_window_set_cursor(gdk_window, NULL);
while (gtk_events_pending()) gtk_main_iteration();
}

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

char *fmt_name_year(char *s, OMZ *oz, bool escape_ampersand)
{
*s=0;
if (oz->year)
    strfmt(s,"%s (%d)",oz->title,oz->year);
else
   crash("fuck");
if (escape_ampersand)
   for (; *s; s++)
      if (*s=='&' && !SAME4BYTES(&s[1],"amp;"))
         strins(&s[1],"amp;");
return(s);
}

static void show_inam(const char *colour)
{
char w[256];
strfmt(w,"<span foreground=\"%s\" size=\"x-large\" background=\"white\">", colour);
fmt_name_year(strend(w), &mv->omz, true);	// THIS call is for gtk display, so escape any ampersands
strcat(w,"</span>");
gtk_label_set_markup(GTK_LABEL(GWinam), (const gchar*) w);
}

static void show_unam(void)
{
const gchar *text = gtk_entry_get_text(GTK_ENTRY(GWunam));
const char *style="highlighted-entry";
GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(GWunam));
if (*text != 0 && strcmp(mv->omz.title, text) != 0)
	gtk_style_context_add_class(context,style);
else
	gtk_style_context_remove_class(context,style);
}

static void show_unam_css(void)
{
GtkCssProvider *provider = gtk_css_provider_new();
gtk_css_provider_load_from_data(provider, ".highlighted-entry { font-weight: bold; color: green; }", -1, NULL);
gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
g_object_unref(provider);
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

// Hide GWtags UNLESS inp_state==2 AND metadata "title" of biggest video file doesn't match _tt Title+Year
static void widget_init(void)
{
static bool inp_state_was_1=false;
char w[256];
gint pos=0;
show_scale(NO);
gtk_widget_set_visible(GWtags, false);          // EQV gtk_widget_hide(widget)
show_unam();
if (mv->inp_state==1)	// Didn't get imno from _tt or *.nfo, so gtk_widget_hideuser can enter / override it
	{
	inp_state_was_1 = true;
	if (mv->omz.k.imno!=0)
		{
		show_inam("red");
		strfmt(w,"tt%d",mv->omz.k.imno);
		gtk_entry_set_text(GTK_ENTRY(GWinum), (const gchar*)w);
		}
	gtk_button_set_label((GtkButton*)GWok, "Identify");	
	gtk_widget_set_visible(GWPartWatched, false);
	gtk_widget_set_visible(GWwatchLabel, false);
	return;
	}
if (mv->inp_state==2) // - at least one file/folder needs renaming (and/or _ttNNNNN needs to be created) 		
	{
//	if (!inp_state_was_1) show_inam("red");
	show_inam(inp_state_was_1?"red":"blue");
	gtk_entry_set_text(GTK_ENTRY(GWinum), (const gchar*)strfmt(w,"tt%d",mv->omz.k.imno));
	gtk_editable_set_editable((GtkEditable*)GWinum, FALSE);
	gtk_button_set_label((GtkButton*)GWok, "Rename Files");
	gtk_widget_set_visible(GWPartWatched, false);
	gtk_widget_set_visible(GWwatchLabel, false);

// SHOW GWtags widget, but NOT SENSITIVE UNLESS TAG NEEDS TO BE UPDATED
   gtk_widget_set_visible(GWtags, true);
   bool already_got_tag=mv->tag_present();
   gtk_widget_set_sensitive(GWtags,!already_got_tag);
   gtk_button_set_label((GtkButton*)GWtags, already_got_tag?"Tag ALREADY SET":"Update Tag");	
	return;
	}
/// inp_state==3 - set movie rating
show_inam("green");
gtk_widget_set_visible(GWSearchText, false);
gtk_widget_set_visible(GWinum, false);
gtk_widget_set_visible(GWunam, false);

if (*mv->get_prv_txt(w))
	gtk_label_set_text(GTK_LABEL(GWprv), (const gchar*) w);

show_scale(YES);

gtk_widget_set_visible(GWwatchLabel,true);
gtk_widget_set_visible(GWPartWatched,true);		// added 6/8/24 - NEEDED!
gtk_widget_set_sensitive(GWPartWatched,true);
gtk_button_set_label((GtkButton*)GWok, "Set Rating");	
}


static void set_button_image(GtkWidget *button, const char *file_path, int width, int height)
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
if (widget==GWinam)
	{
	if (txt==NULL) txt=mv->get_tooltip_text();
	if (txt==NULL) return(false);
	gtk_tooltip_set_text(tooltip, txt);
	}
else
	{
	OMDB1 om1(true);
	OM1_KEY k;
	bool got=om1.get_om1(k.imno=mv->omz.k.imno,&k);
	if (!got) return(false);
	std::string sstr=om1.get_notes(k.imno);
	const char *cstr=sstr.c_str();
	if (cstr==NULL || *cstr==0) return(false);
	gtk_tooltip_set_text(tooltip, sstr.c_str());
	}
return true;
}

GET_SET_GEOMETRY *global_gsg;

static int run_main(int argc, char *argv[])
{
gtk_init(&argc, &argv);
char exe_path[256], *we;
int sz=readlink("/proc/self/exe", exe_path, sizeof(exe_path));
if (sz<10) throw(88);
exe_path[sz]=0;
strncpy(we=strrchr(exe_path,'/'),"/usher.glade",20); // https://stackoverflow.com/questions/13237716/splash-screen-in-gtk
GtkBuilder *builder = gtk_builder_new_from_file(exe_path);
window = GTK_WIDGET(gtk_builder_get_object(builder, "window"));
g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
gtk_builder_connect_signals(builder, NULL);
GWinum = GTK_WIDGET(gtk_builder_get_object(builder, "GWinum"));
GWinam = GTK_WIDGET(gtk_builder_get_object(builder, "GWinam"));
GWunam = GTK_WIDGET(gtk_builder_get_object(builder, "GWunam"));
GWSearchText = GTK_WIDGET(gtk_builder_get_object(builder, "GWSearchText"));
GWprv = GTK_WIDGET(gtk_builder_get_object(builder, "GWprv"));
GWscale = GTK_WIDGET(gtk_builder_get_object(builder, "GWscale"));
GWadj = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "GWadj"));
GWok = GTK_WIDGET(gtk_builder_get_object(builder, "GWok"));
GWPartWatched = GTK_WIDGET(gtk_builder_get_object(builder, "GWPartWatched"));
GWadj2 = GTK_ADJUSTMENT(gtk_builder_get_object(builder, "GWadj2"));
GWwatchLabel = GTK_WIDGET(gtk_builder_get_object(builder, "GWwatchLabel"));
GWtags = GTK_WIDGET(gtk_builder_get_object(builder, "GWtags"));
GWNotes = GTK_WIDGET(gtk_builder_get_object(builder, "GWNotes"));

strcpy(we,"/lens.png");
GWSearch = GTK_WIDGET(gtk_builder_get_object(builder, "GWSearch"));
set_button_image(GWSearch, exe_path, 25, 25); // Adjust width and height as needed
show_unam_css();
gtk_widget_set_has_tooltip(GWinam, TRUE);
g_signal_connect(GWinam, "query-tooltip", G_CALLBACK(on_query_tooltip), NULL);
gtk_widget_set_has_tooltip(GWNotes, TRUE);
g_signal_connect(GWNotes, "query-tooltip", G_CALLBACK(on_query_tooltip), NULL);
 
// - should call valid_folder() here, not in the original main()
mv=new MVDIR(argv[1]);        // HERE is where we should chdir into the specified folder

char wrk[256], *basen=Basename(argv[1]);
strcpy(wrk,basen);      // grab just the final foldername containing the movie files
if (memcmp(argv[0],"/home",5)==0) strins(wrk,"(DEV) ");
gtk_window_set_title((GtkWindow*)window, wrk);
gtk_label_set_text(GTK_LABEL(GWSearchText), (const gchar*) strfmt(wrk,"IMDB %s",basen));

//gtk_widget_show(window);
GET_SET_GEOMETRY gsg("usher", window); // Create gsg on the stack
global_gsg=&gsg;
widget_init();
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


static void google_search(const char *srch)
{
char browser[128], s[256];
strfmt(s,"%s%s","https://www.google.com/search?q=",srch);
int i;
while ((i=stridxc(SPACE,s))!=NOTFND) s[i]='+';
execute(parm_str("browser",browser, "xdg-open"),s);
}

static int valid_folder(const char *p)
{
struct stat sb;
char path[PATH_MAX];
drfullpath(path,p);			// Fully expand passed path
if (stat(path,&sb)) return(-500);	// some kind of error (copied to system global 'errno')
return(NO);	// No error
}

static int console_test(const char *p1, const char *p2);       // pre-declare

static int test(const char *fn)
{
int64_t bytes=dr_foldersize(fn);
short szA=bytes/100000000;
int szB=(((bytes>>20)) + 5)/10;
char wrkA[64], wrkB[64];
strfmt(wrkA,"%1.1f",0.1*szA);
strfmt(wrkB,"%1.1f",0.1*szB);
printf("bytes:%s A:%s B:%s\n",str_size64(bytes),wrkA,wrkB);
return(0);
}

int main(int argc, char *argv[])
{
//return(test("/media/steve/03Magnum/Films03/Fruitvale Station (2013)"));
int err=0;
if (argc==3) return(console_test(argv[1], argv[2]));
if (argc==2)
	{
	char *p=argv[1];
	if (SAME2BYTES(p,"--")) p++;
	if (p[0]=='-') return(process_cli_flag(&p[1]));
	}
//if (argc==2 && !is_mount(argv[1])) crash("Path not mounted [%s]",argv[1]);  // 24/07/25  - Why is this here????
try
	{
//      leak_tracker(YES);
	err=valid_folder(argv[1]);
	if (!err) err=run_main(argc,argv);
//   leak_tracker(NO);
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

// If OMZ.title not set, get it from the passed api-returned json buffer
void retrieve_api_title(OMZ *oz, char *buf8k)
{
JBLOB_READER jb(buf8k);
const char *ptr=jb.get("Title");
strncpy(oz->title,ptr,sizeof(oz->title));
if (!valid_movie_year(oz->year))	// 04/01/26 trying to fix Super Troopers 2001 as well as Rats 2024
   {
   ptr=jb.get("Released");
   int len=strlen(ptr);
   if (len>=4) ptr+=(len-4); else ptr=jb.get("Year");
   if (oz->title[0] && ISDIGIT(ptr[0]))
   oz->year=a2i(ptr,4);
   }
}

// Use API with ImdbNo for title + Year - called by read_nfo(MV.e) AND on_GWok_clicked(eEe)
bool api_name_from_number(OMZ *oz)
{
char buf8k[8192]; // Allow PLENTY of space for the ibmdb API call
if (oz->k.imno==0) m_finish("impossible 301225");         // added 6/1/25 - avoid "bad tmno" when there's no chance of finding it
if (!tmdb_all_from_number(oz,buf8k))
	return(false);

if (oz->title[0]==0 || oz->year==0)		// Added 24/11/25 for "Rats!"
	retrieve_api_title(oz,buf8k);
str_slash2dash(oz->title);
return(*oz->title!=0 && valid_movie_year(oz->year));    // ok - imdb number is valid - EM_KEY name+Year filled in
}

 
extern "C"
void  on_GWok_clicked(GtkButton *b)			// HERE is the on click Ok routine
{
global_gsg->save();
if (mv->inp_state==3)
	{
	mv->omz.k.partwatch=gtk_adjustment_get_value(GWadj2);
	if (mv->omz.k.rating) mv->set_rating();
	gtk_window_close((GtkWindow*)window);
	return;
	}
mv->check_GWunam();	//  Added 22/11/25 for "Rats 2024 1080p AMZN WEB-DL"
if (mv->inp_state==1)	// need to confirm identity (auto- or user input imdb number)
	{
   char w[32];	// 19/12/24 after disable on_GWinum_changed
   strcpy(w,gtk_editable_get_chars((GtkEditable*)GWinum,0,NOTFND));
   mv->omz.k.imno=str2imno(w);
	if (mv->omz.k.imno==0 || !api_name_from_number(&mv->omz)) {MessageBox("Invalid ImdbNO"); return;}
	mv->inp_state=2;
	}
else						// Must be (mv->inp_state==2)	// (we know some renaming is required)
	{
	mv->rename(true); 
	mv->inp_state=3;

	static guint timeout_id = 0;
	if (timeout_id) g_source_remove(timeout_id);
	timeout_id = g_timeout_add_seconds(30, [](gpointer) -> gboolean
         {
		   gtk_main_quit();
		   return FALSE; // Ensure the timeout runs only once
	      },
      NULL);

	}
widget_init();
}

extern "C"
void  on_GWinum_changed(GtkEditable *editable)
{
char w[32];
strcpy(w,gtk_editable_get_chars(editable,0,NOTFND));
int prv=mv->omz.k.imno;		// 14/12/24 re-validate changed _tt (don't know if needed)
mv->omz.k.imno=str2imno(w);
if (mv->omz.k.imno!=prv)
	{
	mv->inp_state=1;
	if (mv->omz.k.imno==0 || !api_name_from_number(&mv->omz))
      MessageBox("Invalid ImdbNO"); 
   else
      w[30]=0;
	}
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

extern "C"
void on_GWSearch_clicked(void)
{
const char *txt;
txt = gtk_label_get_text(GTK_LABEL(GWSearchText));        // string created by program from filename (NOT user input)
google_search(txt);
}

extern "C" void on_GWNotes_clicked(GtkButton *button, gpointer user_data)
{
GtkWidget *dialog = gtk_dialog_new_with_buttons("Notes", GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
    (GtkDialogFlags)(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
	"Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_ACCEPT, NULL);
GtkWidget *textview = gtk_text_view_new();
GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));
GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
gtk_container_add(GTK_CONTAINER(scrolled_window), textview);
gtk_widget_set_size_request(scrolled_window, 400, 300);

USRTXT ut(mv->omz.k.imno);		// Opens omdb.mst and reads any user notes record
std::string txt = ut.get();
gtk_text_buffer_set_text(buffer, txt.c_str(), -1);

GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
gtk_box_pack_start(GTK_BOX(content_area), scrolled_window, TRUE, TRUE, 0);
gtk_widget_show_all(dialog);

gint response = gtk_dialog_run(GTK_DIALOG(dialog));
if (response == GTK_RESPONSE_ACCEPT)
	{
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    char *new_text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    ut.put(new_text);
    g_free(new_text);
	}
gtk_widget_destroy(dialog);
}

extern "C"
void on_GWtags_toggled()
{
mv->update_tags_do_it=gtk_toggle_button_get_active((GtkToggleButton*)GWtags);
}


static int console_test(const char *p1, const char *p2)  // Do we actually need this?
{
if (!SAME2BYTES(p1,"-c")) m_finish("Bad parameters");
p1+=2;
int imno=0;
if (*p1!=0 && (imno=str2imno(p1))==0) m_finish("Bad console test imno");
MVDIR mv(p2);
mv.update_tags_do_it=true;
if (mv.inp_state==1)
	{
   mv.omz.k.imno=imno;
	if (imno==0 || !api_name_from_number(&mv.omz)) m_finish("imno NEEDED!");
	mv.inp_state=2;
	}
if (mv.inp_state!=2) m_finish("console test no rename needed");
mv.rename(true);
return(0);  // no error
}
