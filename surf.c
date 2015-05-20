/* See LICENSE file for copyright and license details.
 *
 * To understand surf, start reading main().
 */

#include <signal.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <webkit2/webkit2.h>
#include <glib/gstdio.h>
#include <JavaScriptCore/JavaScript.h>
#include <sys/file.h>
#include <libgen.h>
#include <stdarg.h>

#include "arg.h"

char *argv0;

#define LENGTH(x)               (sizeof x / sizeof x[0])
#define CLEANMASK(mask)         (mask & (MODKEY|GDK_SHIFT_MASK))

enum { AtomFind, AtomGo, AtomUri, AtomLast };

typedef union Arg Arg;
union Arg {
	gboolean b;
	gint i;
	const void *v;
};

typedef struct Client {
	GtkWidget *win, *scroll, *vbox, *pane;
	WebKitWebView *view;
	WebKitWebInspector *inspector;
	char *title;
	const char *needle, *linkhover;
	gint progress;
	struct Client *next;
	gboolean zoomed, fullscreen, isinspecting, sslfailed, userstyle;
} Client;

typedef struct {
	guint mod;
	guint keyval;
	void (*func)(Client *c, const Arg *arg);
	const Arg arg;
} Key;

static Display *dpy;
static Atom atoms[AtomLast];
static Client *clients = NULL;
static Window embed = 0;
static gboolean showxid = FALSE;
static char winid[64];
static gboolean usingproxy = 0;
static char togglestat[8];
static char pagestat[3];
static int policysel = 0;

static void addaccelgroup(Client *c);
static char *buildpath(const char *path);
static void cleanup(void);
static void clipboard(Client *c, const Arg *arg);
static WebKitCookieAcceptPolicy cookiepolicy_get(void);
static char cookiepolicy_set(const WebKitCookieAcceptPolicy p);
static char *copystr(char **str, const char *src);
static WebKitWebView *createwindow(WebKitWebView *v, WebKitNavigationAction *a,
		Client *c);
static gboolean decidepolicy (WebKitWebView *v, WebKitPolicyDecision *d,
		WebKitPolicyDecisionType t, Client *c);
static gboolean permisssionrequested(WebKitWebView *v, WebKitPermissionRequest *r,
		Client *c);
static void destroyclient(Client *c);
static void destroywin(GtkWidget* w, Client *c);
static void die(const char *errstr, ...);
static void eval(Client *c, const Arg *arg);
static void faviconchange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void find(Client *c, const Arg *arg);
static void fullscreen(Client *c, const Arg *arg);
static const char *getatom(Client *c, int a);
static void gettogglestat(Client *c);
static void getpagestat(Client *c);
static char *geturi(Client *c);
static gboolean initdownload(WebKitURIRequest *r, Client *c);

static void inspector(Client *c, const Arg *arg);
static gboolean inspector_show(WebKitWebInspector *i, Client *c);
static gboolean inspector_close(WebKitWebInspector *i, Client *c);

static gboolean keypress(GtkAccelGroup *group,
		GObject *obj, guint key, GdkModifierType mods,
		Client *c);
static void mousetargetchange(WebKitWebView *v, WebKitHitTestResult *r,
		guint modifiers, Client *c);
static void loadstatuschange(WebKitWebView *view, WebKitLoadEvent e,
		Client *c);
static void loaduri(Client *c, const Arg *arg);
static void navigate(Client *c, const Arg *arg);
static Client *newclient(void);
static void newwindow(Client *c, const Arg *arg, gboolean noembed);
static void pasteuri(GtkClipboard *clipboard, const char *text, gpointer d);
static gboolean contextmenu(WebKitWebView *v, WebKitContextMenu *menu,
		GdkEvent *e, WebKitHitTestResult *r, Client *c);
static void menuactivate(GtkAction *gaction, Client *c);
static void print(Client *c, const Arg *arg);
static GdkFilterReturn processx(GdkXEvent *xevent, GdkEvent *event,
		gpointer d);
static void progresschange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void reload(Client *c, const Arg *arg);
static void scroll_h(Client *c, const Arg *arg);
static void scroll_v(Client *c, const Arg *arg);
static void scroll(GtkAdjustment *a, const Arg *arg);
static void setatom(Client *c, int a, const char *v);
static void setup(void);
static void sigchld(int unused);
static void spawn(Client *c, const Arg *arg);
static void stop(Client *c, const Arg *arg);
static void titlechange(WebKitWebView *view, GParamSpec *pspec, Client *c);
static void toggle(Client *c, const Arg *arg);
static void togglecookiepolicy(Client *c, const Arg *arg);
static void togglegeolocation(Client *c, const Arg *arg);
static void togglescrollbars(Client *c, const Arg *arg);
static void togglestyle(Client *c, const Arg *arg);
static void updatetitle(Client *c);
static void updatewinid(Client *c);
static void usage(void);
static void zoom(Client *c, const Arg *arg);

/* configuration, allows nested code to access above variables */
#include "config.h"

static void
addaccelgroup(Client *c) {
	int i;
	GtkAccelGroup *group = gtk_accel_group_new();
	GClosure *closure;

	for(i = 0; i < LENGTH(keys); i++) {
		closure = g_cclosure_new(G_CALLBACK(keypress), c, NULL);
		gtk_accel_group_connect(group, keys[i].keyval, keys[i].mod,
				0, closure);
	}
	gtk_window_add_accel_group(GTK_WINDOW(c->win), group);
}

static char *
buildpath(const char *path) {
	char *apath, *p;
	FILE *f;

	/* creating directory */
	if(path[0] == '/') {
		apath = g_strdup(path);
	} else if(path[0] == '~') {
		if(path[1] == '/') {
			apath = g_strconcat(g_get_home_dir(), &path[1], NULL);
		} else {
			apath = g_strconcat(g_get_home_dir(), "/",
					&path[1], NULL);
		}
	} else {
		apath = g_strconcat(g_get_current_dir(), "/", path, NULL);
	}

	if((p = strrchr(apath, '/'))) {
		*p = '\0';
		g_mkdir_with_parents(apath, 0700);
		g_chmod(apath, 0700); /* in case it existed */
		*p = '/';
	}
	/* creating file (gives error when apath ends with "/") */
	if((f = fopen(apath, "a"))) {
		g_chmod(apath, 0600); /* always */
		fclose(f);
	}

	return apath;
}

static void
cleanup(void) {
	while(clients)
		destroyclient(clients);
	g_free(cookiefile);
	g_free(historyfile);
	g_free(scriptfile);
	g_free(stylefile);
}

static WebKitCookieAcceptPolicy
cookiepolicy_get(void) {
	switch(cookiepolicies[policysel]) {
	case 'a':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
	case '@':
		return WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
	case 'A':
	default:
		break;
	}

	return WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
}

static char
cookiepolicy_set(const WebKitCookieAcceptPolicy ep) {
	switch(ep) {
	case WEBKIT_COOKIE_POLICY_ACCEPT_NEVER:
		return 'a';
	case WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY:
		return '@';
	case WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS:
	default:
		break;
	}

	return 'A';
}

static void
evalscript(JSGlobalContextRef js, char *script, char* scriptname) {
	JSStringRef jsscript, jsscriptname;
	JSValueRef exception = NULL;

	jsscript = JSStringCreateWithUTF8CString(script);
	jsscriptname = JSStringCreateWithUTF8CString(scriptname);
	JSEvaluateScript(js, jsscript, JSContextGetGlobalObject(js),
			jsscriptname, 0, &exception);
	JSStringRelease(jsscript);
	JSStringRelease(jsscriptname);
}

static void
clipboard(Client *c, const Arg *arg) {
	gboolean paste = *(gboolean *)arg;

	if(paste) {
		gtk_clipboard_request_text(
				gtk_clipboard_get(GDK_SELECTION_PRIMARY),
				pasteuri, c);
	} else {
		gtk_clipboard_set_text(
				gtk_clipboard_get(GDK_SELECTION_PRIMARY),
				c->linkhover ? c->linkhover : geturi(c), -1);
	}
}

static char *
copystr(char **str, const char *src) {
	char *tmp;
	tmp = g_strdup(src);

	if(str && *str) {
		g_free(*str);
		*str = tmp;
	}
	return tmp;
}

static WebKitWebView *
createwindow(WebKitWebView  *v, WebKitNavigationAction *a, Client *c) {
	Client *n = newclient();
	return n->view;
}

static gboolean
decidepolicy (WebKitWebView *v, WebKitPolicyDecision *d,
		WebKitPolicyDecisionType t, Client *c)
{
	WebKitNavigationAction *a;
	WebKitURIRequest *r;
	Arg arg;

	switch (t) {
	case WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION: ;
		a =	webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(d));
		r = webkit_navigation_action_get_request (a);

		if(webkit_navigation_action_get_navigation_type(a) ==
				WEBKIT_NAVIGATION_TYPE_LINK_CLICKED) {
			if(webkit_navigation_action_get_mouse_button(a) == 2 ||
					(webkit_navigation_action_get_mouse_button(a) == 1 && CLEANMASK(webkit_navigation_action_get_modifiers (a)) == CLEANMASK(MODKEY))) {
				webkit_policy_decision_ignore(d);
				arg.v = (void*)webkit_uri_request_get_uri(r);
				newwindow(NULL, &arg, webkit_navigation_action_get_modifiers(a) & GDK_CONTROL_MASK);
				return TRUE;
			}
		}
		return FALSE;

	case WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION: ;
		a =	webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(d));
		r = webkit_navigation_action_get_request (a);

		if(webkit_navigation_action_get_navigation_type(a) ==
				WEBKIT_NAVIGATION_TYPE_LINK_CLICKED) {
			webkit_policy_decision_ignore(d);
			arg.v = (void *)webkit_uri_request_get_uri(r);
			newwindow(NULL, &arg, 0);
			return TRUE;
		}
		return FALSE;

	case WEBKIT_POLICY_DECISION_TYPE_RESPONSE: ;
		r = webkit_response_policy_decision_get_request(WEBKIT_RESPONSE_POLICY_DECISION(d));

		if(!webkit_response_policy_decision_is_mime_type_supported(WEBKIT_RESPONSE_POLICY_DECISION (d))) {
			webkit_policy_decision_ignore(d);
			initdownload(r, c);
			return TRUE;
		}
		return FALSE;

	default:
		return FALSE;
	}
	
}

static void
destroyclient(Client *c) {
	Client *p;

	webkit_web_view_stop_loading(c->view);
	gtk_widget_destroy(GTK_WIDGET(c->view));
	gtk_widget_destroy(c->scroll);
	gtk_widget_destroy(c->vbox);
	gtk_widget_destroy(c->win);

	for(p = clients; p && p->next != c; p = p->next);
	if(p) {
		p->next = c->next;
	} else {
		clients = c->next;
	}
	free(c);
	if(clients == NULL)
		gtk_main_quit();
}

static void
destroywin(GtkWidget* w, Client *c) {
	destroyclient(c);
}

static void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static void
find(Client *c, const Arg *arg) {
	const char *s;
	WebKitFindController *f;

	f = webkit_web_view_get_find_controller(c->view);
	s = getatom(c, AtomFind);
	gboolean forward = *(gboolean *)arg;
	webkit_find_controller_search (f, s,
		WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE | WEBKIT_FIND_OPTIONS_WRAP_AROUND |
		(forward ? WEBKIT_FIND_OPTIONS_NONE : WEBKIT_FIND_OPTIONS_BACKWARDS),
		G_MAXUINT);
}

static void
fullscreen(Client *c, const Arg *arg) {
	if(c->fullscreen) {
		gtk_window_unfullscreen(GTK_WINDOW(c->win));
	} else {
		gtk_window_fullscreen(GTK_WINDOW(c->win));
	}
	c->fullscreen = !c->fullscreen;
}

static gboolean
permisssionrequested(WebKitWebView *v, WebKitPermissionRequest *r,
		Client *c) {
	if (!WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(r)) {
		return FALSE;
	}

	if(allowgeolocation) {
		webkit_permission_request_allow(r);
	} else {
		webkit_permission_request_deny(r);
	}
	return TRUE;
}

static const char *
getatom(Client *c, int a) {
	static char buf[BUFSIZ];
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *p = NULL;

	XGetWindowProperty(dpy, GDK_WINDOW_XID(gtk_widget_get_window(GTK_WIDGET(c->win))),
			atoms[a], 0L, BUFSIZ, False, XA_STRING,
			&adummy, &idummy, &ldummy, &ldummy, &p);
	if(p) {
		strncpy(buf, (char *)p, LENGTH(buf)-1);
	} else {
		buf[0] = '\0';
	}
	XFree(p);

	return buf;
}

static char *
geturi(Client *c) {
	char *uri;

	if(!(uri = (char *)webkit_web_view_get_uri(c->view)))
		uri = "about:blank";
	return uri;
}

static gboolean
initdownload(WebKitURIRequest *r, Client *c) {
	Arg arg;

	updatewinid(c);
	arg = (Arg)DOWNLOAD((char *)webkit_uri_request_get_uri(r), geturi(c));
	spawn(c, &arg);
	return FALSE;
}

static void
inspector(Client *c, const Arg *arg) {
	if(c->isinspecting) {
		webkit_web_inspector_close(c->inspector);
	} else {
		webkit_web_inspector_show(c->inspector);
	}
}

static gboolean
inspector_show(WebKitWebInspector *i, Client *c) {
	c->isinspecting = true;
	return FALSE;
}

static gboolean
inspector_close(WebKitWebInspector *i, Client *c) {
	c->isinspecting = false;
	return FALSE;
}

static gboolean
keypress(GtkAccelGroup *group, GObject *obj,
		guint key, GdkModifierType mods, Client *c) {
	guint i;
	gboolean processed = FALSE;

	mods = CLEANMASK(mods);
	key = gdk_keyval_to_lower(key);
	updatewinid(c);
	for(i = 0; i < LENGTH(keys); i++) {
		if(key == keys[i].keyval
				&& mods == keys[i].mod
				&& keys[i].func) {
			keys[i].func(c, &(keys[i].arg));
			processed = TRUE;
		}
	}

	return processed;
}

static void
mousetargetchange(WebKitWebView *v, WebKitHitTestResult *r,
		guint modifiers, Client *c) {
	if(webkit_hit_test_result_context_is_link(r)) {
		c->linkhover = webkit_hit_test_result_get_link_uri(r);
	} else if(c->linkhover) {
		c->linkhover = NULL;
	}
	updatetitle(c);
}

static void
loadstatuschange(WebKitWebView *v, WebKitLoadEvent e, Client *c) {
	GTlsCertificateFlags errors;
	char *uri;

	switch(e) {
	case WEBKIT_LOAD_COMMITTED:
		uri = geturi(c);
		if(webkit_web_view_get_tls_info(v, NULL, &errors)) {
			c->sslfailed = errors ? TRUE : FALSE;
		}
		setatom(c, AtomUri, uri);

		/* write to history */
		FILE *f;
		f = fopen(historyfile, "a+");
		fprintf(f, "h: %s\n", uri);
		fclose(f);
		break;
	case WEBKIT_LOAD_FINISHED:
		c->progress = 100;
		updatetitle(c);
		break;
	default:
		break;
	}
}

static void
loaduri(Client *c, const Arg *arg) {
	char *u = NULL, *rp;
	const char *uri = (char *)arg->v;
	Arg a = { .b = FALSE };
	struct stat st;

	if(strcmp(uri, "") == 0)
		return;

	/* In case it's a file path. */
	if(stat(uri, &st) == 0) {
		rp = realpath(uri, NULL);
		u = g_strdup_printf("file://%s", rp);
		free(rp);
	} else {
		u = g_strrstr(uri, "://") ? g_strdup(uri)
			: g_strdup_printf("http://%s", uri);
	}

	setatom(c, AtomUri, uri);

	/* prevents endless loop */
	if(strcmp(u, geturi(c)) == 0) {
		reload(c, &a);
	} else {
		webkit_web_view_load_uri(c->view, u);
		c->progress = 0;
		c->title = copystr(&c->title, u);
		updatetitle(c);
	}
	g_free(u);
}

static void
navigate(Client *c, const Arg *arg) {
	WebKitBackForwardList *l = webkit_web_view_get_back_forward_list(c->view);
	int steps = *(int *)arg;
	WebKitBackForwardListItem *i = webkit_back_forward_list_get_nth_item(l, steps);

	if(WEBKIT_IS_BACK_FORWARD_LIST_ITEM(i))
		webkit_web_view_go_to_back_forward_list_item(c->view, i);
}

static Client *
newclient(void) {
	Client *c;
	WebKitSettings *settings;
	WebKitUserContentManager *usercontent;
	WebKitUserScript *script;
	WebKitUserStyleSheet *style;
	GdkGeometry hints = { 1, 1 };
	GdkScreen *screen;
	GdkWindow *window;
	gdouble dpi;
	char *ua, *source;

	if(!(c = calloc(1, sizeof(Client))))
		die("Cannot malloc!\n");

	c->title = NULL;
	c->progress = 100;

	/* Window */
	if(embed) {
		c->win = gtk_plug_new(embed);
	} else {
		c->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

		/* TA:  20091214:  Despite what the GNOME docs say, the ICCCM
		 * is always correct, so we should still call this function.
		 * But when doing so, we *must* differentiate between a
		 * WM_CLASS and a resource on the window.  By convention, the
		 * window class (WM_CLASS) is capped, while the resource is in
		 * lowercase.   Both these values come as a pair.
		 */
		gtk_window_set_wmclass(GTK_WINDOW(c->win), "surf", "Surf");

		/* TA:  20091214:  And set the role here as well -- so that
		 * sessions can pick this up.
		 */
		gtk_window_set_role(GTK_WINDOW(c->win), "Surf");
	}

	gtk_widget_realize(GTK_WIDGET(c->win));
	window = gtk_widget_get_window(GTK_WIDGET(c->win));

	gtk_window_set_default_size(GTK_WINDOW(c->win), 800, 600);
	g_signal_connect(G_OBJECT(c->win),
			"destroy",
			G_CALLBACK(destroywin), c);

	if(!kioskmode)
		addaccelgroup(c);

	/* Pane */
	c->pane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

	/* VBox */
	c->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_paned_pack1(GTK_PANED(c->pane), c->vbox, TRUE, TRUE);

	/* Webview */
	usercontent = webkit_user_content_manager_new();
	c->view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_user_content_manager(usercontent));
	g_clear_object(&usercontent);

	g_signal_connect(G_OBJECT(c->view),
			"notify::title", /* good */
			G_CALLBACK(titlechange), c);
	g_signal_connect(G_OBJECT(c->view),
			"notify::favicon", /* good */
			G_CALLBACK(faviconchange), c);
	g_signal_connect(G_OBJECT(c->view),
			"mouse-target-changed", /* new */
			G_CALLBACK(mousetargetchange), c);
	g_signal_connect(G_OBJECT(c->view),
			"permission-request", /* new */
			G_CALLBACK(permisssionrequested), c);
	g_signal_connect(G_OBJECT(c->view),
			"create", /* new */
			G_CALLBACK(createwindow), c);
	g_signal_connect(G_OBJECT(c->view),
			"decide-policy", /* new */
			G_CALLBACK(decidepolicy), c);
	g_signal_connect(G_OBJECT(c->view),
			"load-changed", /* new */
			G_CALLBACK(loadstatuschange), c);
	g_signal_connect(G_OBJECT(c->view),
			"notify::estimated-load-progress", /* new */
			G_CALLBACK(progresschange), c);
	g_signal_connect(G_OBJECT(c->view),
			"context-menu", /* new */
			G_CALLBACK(contextmenu), c);

	/* Scrolled Window */
	c->scroll = gtk_scrolled_window_new(NULL, NULL);

	if(!enablescrollbars) {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	} else {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	}

	/* Arranging */
	gtk_container_add(GTK_CONTAINER(c->scroll), GTK_WIDGET(c->view));
	gtk_container_add(GTK_CONTAINER(c->win), c->pane);
	gtk_container_add(GTK_CONTAINER(c->vbox), c->scroll);

	/* Setup */
	gtk_box_set_child_packing(GTK_BOX(c->vbox), c->scroll, TRUE,
			TRUE, 0, GTK_PACK_START);
	gtk_widget_show(c->pane);
	gtk_widget_show(c->vbox);
	gtk_widget_show(c->scroll);
	gtk_widget_show(GTK_WIDGET(c->view));
	gtk_widget_show(c->win);
	gtk_widget_grab_focus(GTK_WIDGET(c->view));
	gtk_window_set_geometry_hints(GTK_WINDOW(c->win), NULL, &hints,
			GDK_HINT_MIN_SIZE);
	gdk_window_set_events(window, GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(window, processx, c);

	settings = webkit_web_view_get_settings(c->view);
	if(!(ua = getenv("SURF_USERAGENT")))
		ua = useragent;
	g_object_set(G_OBJECT(settings), "user-agent", ua, NULL); /* good */
	g_object_set(G_OBJECT(settings), "auto-load-images", loadimages,
			NULL); /* good */
	g_object_set(G_OBJECT(settings), "enable-plugins", enableplugins,
			NULL); /* good */
	g_object_set(G_OBJECT(settings), "enable-javascript", enablescripts,
			NULL); /* new */
	g_object_set(G_OBJECT(settings), "enable-spatial-navigation",
			enablespatialbrowsing, NULL); /* good */
	g_object_set(G_OBJECT(settings), "enable-developer-extras",
			enableinspector, NULL); /* good */
	g_object_set(G_OBJECT(settings), "default-font-size",
			defaultfontsize, NULL); /* good */
	g_object_set(G_OBJECT(settings), "enable-resizable-text-areas",
			1, NULL); /* new */
	g_object_set(G_OBJECT(settings), "zoom-text-only",
			0, NULL); /* new */

	/* custom config */
	g_object_set(G_OBJECT(settings), "enable-accelerated-2d-canvas",
			TRUE, NULL);
	g_object_set(G_OBJECT(settings), "enable-mediasource",
			TRUE, NULL);
	g_object_set(G_OBJECT(settings), "enable-webaudio",
			TRUE, NULL);
	g_object_set(G_OBJECT(settings), "enable-webgl",
			TRUE, NULL);

	/* stylefile and scriptfile */
	usercontent = webkit_web_view_get_user_content_manager(c->view);
	if(g_file_get_contents(scriptfile, &source, NULL, NULL)) {
		script = webkit_user_script_new(
				source, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
				WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END,
				NULL, NULL);
		webkit_user_content_manager_add_script(usercontent, script);
		g_free(source);
		webkit_user_script_unref(script);
	}
	if(g_file_get_contents(stylefile, &source, NULL, NULL)) {
		style = webkit_user_style_sheet_new(
				source, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
				WEBKIT_USER_STYLE_LEVEL_USER,
				NULL, NULL);
		webkit_user_content_manager_add_style_sheet(usercontent, style);
		g_free(source);
		webkit_user_style_sheet_unref(style);
	}
	c->userstyle = true;

	/*
	 * While stupid, CSS specifies that a pixel represents 1/96 of an inch.
	 * This ensures websites are not unusably small with a high DPI screen.
	 * It is equivalent to firefox's "layout.css.devPixelsPerPx" setting.
	 */
	if(zoomto96dpi) {
		screen = gdk_window_get_screen(window);
		dpi = gdk_screen_get_resolution(screen);
		if(dpi != -1) {
			webkit_web_view_set_zoom_level(c->view, dpi/96);
		}
	}
	/* This might conflict with _zoomto96dpi_. */
	if(zoomlevel != 1.0)
		webkit_web_view_set_zoom_level(c->view, zoomlevel);

	if(enableinspector) {
		c->inspector = webkit_web_view_get_inspector(c->view);
		g_signal_connect(G_OBJECT(c->inspector), "attach",
				G_CALLBACK(inspector_show), c);
		g_signal_connect(G_OBJECT(c->inspector), "closed",
				G_CALLBACK(inspector_close), c);
		c->isinspecting = false;
	}

	if(runinfullscreen) {
		c->fullscreen = 0;
		fullscreen(c, NULL);
	}

	setatom(c, AtomFind, "");
	setatom(c, AtomUri, "about:blank");

	/* optional startup action
		todo: config in config.h */
	if(openbar) {
		openbar = FALSE;
		updatewinid(c);
		Arg v = SETPROP("_SURF_URI");
		spawn(c, &v);
	}

	c->next = clients;
	clients = c;

	if(showxid) {
		gdk_display_sync(gtk_widget_get_display(c->win));
		printf("%u\n",
			(guint)GDK_WINDOW_XID(window));
		fflush(NULL);
				if (fclose(stdout) != 0) {
			die("Error closing stdout");
				}
	}

	return c;
}

static void
newwindow(Client *c, const Arg *arg, gboolean noembed) {
	guint i = 0;
	const char *cmd[16], *uri;
	const Arg a = { .v = (void *)cmd };
	char tmp[64];

	cmd[i++] = argv0;
	cmd[i++] = "-a";
	cmd[i++] = cookiepolicies;
	if(!enablescrollbars)
		cmd[i++] = "-b";
	if(embed && !noembed) {
		cmd[i++] = "-e";
		snprintf(tmp, LENGTH(tmp), "%u\n", (int)embed);
		cmd[i++] = tmp;
	}
	if(!loadimages)
		cmd[i++] = "-i";
	if(kioskmode)
		cmd[i++] = "-k";
	if(!enableplugins)
		cmd[i++] = "-p";
	if(!enablescripts)
		cmd[i++] = "-s";
	if(showxid)
		cmd[i++] = "-x";
	cmd[i++] = "-c";
	cmd[i++] = cookiefile;
	cmd[i++] = "--";
	uri = arg->v ? (char *)arg->v : c->linkhover;
	if(uri)
		cmd[i++] = uri;
	cmd[i++] = NULL;
	spawn(NULL, &a);
}

static gboolean
contextmenu(WebKitWebView *v, WebKitContextMenu *menu,
		GdkEvent *e, WebKitHitTestResult *r, Client *c) {
	GList *items = webkit_context_menu_get_items(menu);

	if(kioskmode)
		return TRUE;

	for(GList *l = items; l; l = l->next) {
		if (!webkit_context_menu_item_is_separator(l->data))
			g_signal_connect(webkit_context_menu_item_get_action(l->data),
				"activate", G_CALLBACK(menuactivate), c);
	}
	if (webkit_hit_test_result_context_is_image(r))
		c->needle = webkit_hit_test_result_get_image_uri(r);

	return FALSE;
}

static void
menuactivate(GtkAction *gaction, Client *c) {
	/*
	 * context-menu-action-2000	open link
	 * context-menu-action-1	open link in window
	 * context-menu-action-2	download linked file
	 * context-menu-action-3	copy link location
	 * context-menu-action-7	copy image address
	 * context-menu-action-13	reload
	 * context-menu-action-10	back
	 * context-menu-action-11	forward
	 * context-menu-action-12	stop
	 */

	const char *name, *uri;

	name = gtk_action_get_name(gaction);
	uri = NULL;
	if(!g_strcmp0(name, "context-menu-action-3"))
		uri = c->linkhover;
	else if(!g_strcmp0(name, "context-menu-action-7"))
		uri = c->needle;

	if(uri)
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY),
				uri, -1);
}

static void
pasteuri(GtkClipboard *clipboard, const char *text, gpointer d) {
	Arg arg = {.v = text };
	if(text != NULL)
		loaduri((Client *) d, &arg);
}

static void
print(Client *c, const Arg *arg) {
	WebKitPrintOperation *p = webkit_print_operation_new(c->view);
	webkit_print_operation_run_dialog(p, GTK_WINDOW(c->win));
	g_clear_object(&p);
}

static GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer d) {
	Client *c = (Client *)d;
	XPropertyEvent *ev;
	Arg arg;

	if(((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if(ev->state == PropertyNewValue) {
			if(ev->atom == atoms[AtomFind]) {
				arg.b = TRUE;
				find(c, &arg);

				return GDK_FILTER_REMOVE;
			} else if(ev->atom == atoms[AtomGo]) {
				arg.v = getatom(c, AtomGo);
				loaduri(c, &arg);

				return GDK_FILTER_REMOVE;
			}
		}
	}
	return GDK_FILTER_CONTINUE;
}

static void
progresschange(WebKitWebView *v, GParamSpec *pspec, Client *c) {
	c->progress = webkit_web_view_get_estimated_load_progress(v) * 100;
	updatetitle(c);
}

static void
reload(Client *c, const Arg *arg) {
	gboolean nocache = *(gboolean *)arg;
	if(nocache) {
		 webkit_web_view_reload_bypass_cache(c->view);
	} else {
		 webkit_web_view_reload(c->view);
	}
}

static void
scroll_h(Client *c, const Arg *arg) {
	scroll(gtk_scrolled_window_get_hadjustment(
				GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

static void
scroll_v(Client *c, const Arg *arg) {
	scroll(gtk_scrolled_window_get_vadjustment(
				GTK_SCROLLED_WINDOW(c->scroll)), arg);
}

static void
scroll(GtkAdjustment *a, const Arg *arg) {
	gdouble v;

	v = gtk_adjustment_get_value(a);
	switch(arg->i) {
	case +10000:
	case -10000:
		v += gtk_adjustment_get_page_increment(a) *
			(arg->i / 10000);
		break;
	case +20000:
	case -20000:
	default:
		v += gtk_adjustment_get_step_increment(a) * arg->i;
	}

	v = MAX(v, 0.0);
	v = MIN(v, gtk_adjustment_get_upper(a) -
			gtk_adjustment_get_page_size(a));
	gtk_adjustment_set_value(a, v);
}

static void
setatom(Client *c, int a, const char *v) {
	XSync(dpy, False);
	XChangeProperty(dpy, GDK_WINDOW_XID(gtk_widget_get_window(GTK_WIDGET(c->win))),
			atoms[a], XA_STRING, 8, PropModeReplace,
			(unsigned char *)v, strlen(v) + 1);
}

static void
setup(void) {
	WebKitWebContext *c;
	WebKitCookieManager *cm;

	/* clean up any zombies immediately */
	sigchld(0);
	gtk_init(NULL, NULL);

	dpy = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

	/* atoms */
	atoms[AtomFind] = XInternAtom(dpy, "_SURF_FIND", False);
	atoms[AtomGo] = XInternAtom(dpy, "_SURF_GO", False);
	atoms[AtomUri] = XInternAtom(dpy, "_SURF_URI", False);

	/* dirs and files */
	cookiefile = buildpath(cookiefile);
	historyfile = buildpath(historyfile);
	scriptfile = buildpath(scriptfile);
	stylefile = buildpath(stylefile);

	/* request handler */
	c = webkit_web_context_get_default();

	/* caching */
	webkit_web_context_set_cache_model(c, WEBKIT_CACHE_MODEL_WEB_BROWSER);

	/* cookies */
	cm = webkit_web_context_get_cookie_manager(c);
	webkit_cookie_manager_set_persistent_storage(cm,
			cookiefile, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
	webkit_cookie_manager_set_accept_policy(cm, cookiepolicy_get());

	/* ssl policy */
	webkit_web_context_set_tls_errors_policy (c,
			strictssl ? WEBKIT_TLS_ERRORS_POLICY_FAIL : WEBKIT_TLS_ERRORS_POLICY_IGNORE);

	/* enable favicons */
	if (enablefavicons)
		webkit_web_context_set_favicon_database_directory(c, NULL);
}

static void
sigchld(int unused) {
	if(signal(SIGCHLD, sigchld) == SIG_ERR)
		die("Can't install SIGCHLD handler");
	while(0 < waitpid(-1, NULL, WNOHANG));
}

static void
spawn(Client *c, const Arg *arg) {
	if(fork() == 0) {
		if(dpy)
			close(ConnectionNumber(dpy));
		setsid();
		execvp(((char **)arg->v)[0], (char **)arg->v);
		fprintf(stderr, "surf: execvp %s", ((char **)arg->v)[0]);
		perror(" failed");
		exit(0);
	}
}

static void
eval(Client *c, const Arg *arg) {
	evalscript(webkit_web_view_get_javascript_global_context(c->view),
			((char **)arg->v)[0], "");
}

static void
stop(Client *c, const Arg *arg) {
	webkit_web_view_stop_loading(c->view);
}

static void
faviconchange(WebKitWebView *view, GParamSpec *pspec, Client *c) {
	cairo_surface_t *favicon = webkit_web_view_get_favicon(view);
	GdkPixbuf *pixbuf;
	int width;
	int height;

	if(!favicon) {
		pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default (),
				"web-browser-symbolic", 16, GTK_ICON_LOOKUP_GENERIC_FALLBACK, NULL);
	} else {
		width = cairo_image_surface_get_width(favicon);
		height = cairo_image_surface_get_height(favicon);
		pixbuf = gdk_pixbuf_get_from_surface(favicon, 0, 0, width, height);
	}
	gtk_window_set_icon(GTK_WINDOW(c->win), pixbuf);
	g_object_unref(pixbuf);
}

static void
titlechange(WebKitWebView *view, GParamSpec *pspec, Client *c) {
	const char *t = webkit_web_view_get_title(view);
	if (t) {
		c->title = copystr(&c->title, t);
		updatetitle(c);
	}
}

static void
toggle(Client *c, const Arg *arg) {
	WebKitSettings *settings;
	char *name = (char *)arg->v;
	gboolean value;
	Arg a = { .b = FALSE };

	settings = webkit_web_view_get_settings(c->view);
	g_object_get(G_OBJECT(settings), name, &value, NULL);
	g_object_set(G_OBJECT(settings), name, !value, NULL);

	reload(c, &a);
}

static void
togglecookiepolicy(Client *c, const Arg *arg) {
	WebKitWebContext *context;
	WebKitCookieManager *cm;

	policysel++;
	if(policysel >= strlen(cookiepolicies))
		policysel = 0;

	context = webkit_web_context_get_default();
	cm = webkit_web_context_get_cookie_manager(context);
	webkit_cookie_manager_set_accept_policy(cm, cookiepolicy_get());

	updatetitle(c);
	/* Do not reload. */
}

static void
togglegeolocation(Client *c, const Arg *arg) {
	Arg a = { .b = FALSE };

	allowgeolocation ^= 1;

	reload(c, &a);
}

static void
twitch(Client *c, const Arg *arg) {
	GtkAdjustment *a;
	gdouble v;

	a = gtk_scrolled_window_get_vadjustment(
			GTK_SCROLLED_WINDOW(c->scroll));

	v = gtk_adjustment_get_value(a);

	v += arg->i;

	v = MAX(v, 0.0);
	v = MIN(v, gtk_adjustment_get_upper(a) -
			gtk_adjustment_get_page_size(a));
	gtk_adjustment_set_value(a, v);
}

static void
togglescrollbars(Client *c, const Arg *arg) {
	GtkPolicyType vspolicy;
	Arg a;

	gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(c->scroll), NULL, &vspolicy);

	if(vspolicy == GTK_POLICY_AUTOMATIC) {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_NEVER, GTK_POLICY_NEVER);
	} else {
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(c->scroll),
				GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
		a.i = +1;
		twitch(c, &a);
		a.i = -1;
		twitch(c, &a);
	}
}

static void
togglestyle(Client *c, const Arg *arg) {
	WebKitUserContentManager *usercontent;
	WebKitUserStyleSheet *style;
	char *source;

	usercontent = webkit_web_view_get_user_content_manager(c->view);
	if(c->userstyle) {
		webkit_user_content_manager_remove_all_style_sheets(usercontent);
		c->userstyle = false;
	} else {
		if(g_file_get_contents(stylefile, &source, NULL, NULL)) {
			style = webkit_user_style_sheet_new(
					source, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
					WEBKIT_USER_STYLE_LEVEL_USER,
					NULL, NULL);
			webkit_user_content_manager_add_style_sheet(usercontent, style);
			g_free(source);
			webkit_user_style_sheet_unref(style);
		}
		c->userstyle = true;
	}

	updatetitle(c);
}

static void
gettogglestat(Client *c) {
	gboolean value;
	int p = 0;
	WebKitSettings *settings = webkit_web_view_get_settings(c->view);

	togglestat[p++] = cookiepolicy_set(cookiepolicy_get());

	g_object_get(G_OBJECT(settings), "enable-caret-browsing",
			&value, NULL);
	togglestat[p++] = value? 'C': 'c';

	togglestat[p++] = allowgeolocation? 'G': 'g';

	g_object_get(G_OBJECT(settings), "auto-load-images", &value, NULL);
	togglestat[p++] = value? 'I': 'i';

	g_object_get(G_OBJECT(settings), "enable-javascript", &value, NULL);
	togglestat[p++] = value? 'S': 's';

	g_object_get(G_OBJECT(settings), "enable-plugins", &value, NULL);
	togglestat[p++] = value? 'V': 'v';

	togglestat[p++] = c->userstyle ? 'M': 'm';

	togglestat[p] = '\0';
}

static void
getpagestat(Client *c) {
	const char *uri = geturi(c);

	if(strstr(uri, "https://") == uri) {
		pagestat[0] = c->sslfailed ? 'U' : 'T';
	} else {
		pagestat[0] = '-';
	}

	pagestat[1] = usingproxy ? 'P' : '-';
	pagestat[2] = '\0';

}

static void
updatetitle(Client *c) {
	char *t;

	if(showindicators) {
		gettogglestat(c);
		getpagestat(c);

		if(c->linkhover) {
			t = g_strdup_printf("%s:%s | %s", togglestat,
					pagestat, c->linkhover);
		} else if(c->progress != 100) {
			t = g_strdup_printf("[%i%%] %s:%s | %s", c->progress,
					togglestat, pagestat,
					(c->title == NULL)? "" : c->title);
		} else {
			t = g_strdup_printf("%s:%s | %s", togglestat, pagestat,
					(c->title == NULL)? "" : c->title);
		}

		gtk_window_set_title(GTK_WINDOW(c->win), t);
		g_free(t);
	} else {
		gtk_window_set_title(GTK_WINDOW(c->win),
				(c->title == NULL)? "" : c->title);
	}
}

static void
updatewinid(Client *c) {
	snprintf(winid, LENGTH(winid), "%u",
			(int)GDK_WINDOW_XID(gtk_widget_get_window(GTK_WIDGET(c->win))));
}

static void
usage(void) {
	die("usage: %s [-bBfFgGiIkKnNopPsSvx]"
		" [-a cookiepolicies ] "
		" [-c cookiefile] [-e xid] [-r scriptfile]"
		" [-t stylefile] [-u useragent] [-z zoomlevel]"
		" [uri]\n", basename(argv0));
}

static void
zoom(Client *c, const Arg *arg) {
	c->zoomed = TRUE;
	if(arg->i < 0) {
		/* zoom out */
		webkit_web_view_set_zoom_level(c->view, 
				webkit_web_view_get_zoom_level(c->view) - 0.1);
	} else if(arg->i > 0) {
		/* zoom in */
		webkit_web_view_set_zoom_level(c->view, 
				webkit_web_view_get_zoom_level(c->view) + 0.1);
	} else {
		/* reset */
		c->zoomed = FALSE;
		webkit_web_view_set_zoom_level(c->view, zoomlevel);
	}
}

int
main(int argc, char *argv[]) {
	Arg arg;
	Client *c;

	memset(&arg, 0, sizeof(arg));

	/* command line args */
	ARGBEGIN {
	case 'a':
		cookiepolicies = EARGF(usage());
		break;
	case 'b':
		enablescrollbars = 0;
		break;
	case 'B':
		enablescrollbars = 1;
		break;
	case 'c':
		cookiefile = EARGF(usage());
		break;
	case 'e':
		embed = strtol(EARGF(usage()), NULL, 0);
		break;
	case 'f':
		runinfullscreen = 1;
		break;
	case 'F':
		runinfullscreen = 0;
		break;
	case 'g':
		allowgeolocation = 0;
		break;
	case 'G':
		allowgeolocation = 1;
		break;
	case 'i':
		loadimages = 0;
		break;
	case 'I':
		loadimages = 1;
		break;
	case 'k':
		kioskmode = 0;
		break;
	case 'K':
		kioskmode = 1;
		break;
	case 'n':
		enableinspector = 0;
		break;
	case 'N':
		enableinspector = 1;
		break;
	case 'o':
		openbar = 1;
		break;
	case 'p':
		enableplugins = 0;
		break;
	case 'P':
		enableplugins = 1;
		break;
	case 'r':
		scriptfile = EARGF(usage());
		break;
	case 's':
		enablescripts = 0;
		break;
	case 'S':
		enablescripts = 1;
		break;
	case 't':
		stylefile = EARGF(usage());
		break;
	case 'u':
		useragent = EARGF(usage());
		break;
	case 'v':
		die("surf-"VERSION", ©2009-2014 surf engineers, "
				"see LICENSE for details\n");
	case 'x':
		showxid = TRUE;
		break;
	case 'z':
		zoomlevel = strtof(EARGF(usage()), NULL);
		break;
	default:
		usage();
	} ARGEND;
	if(argc > 0)
		arg.v = argv[0];

	setup();
	c = newclient();
	if(arg.v) {
		loaduri(clients, &arg);
	} else {
		updatetitle(c);
	}

	gtk_main();
	cleanup();

	return EXIT_SUCCESS;
}

