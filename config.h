/* modifier 0 means no modifier */
static char *useragent      = "Mozilla/5.0 (X11; U; Unix; en-US) "
	"AppleWebKit/537.36 (KHTML, like Gecko) Chrome/37.0.2062.120 "
	"Safari/537.36 Surf/"VERSION;
static char *stylefile      = "~/.surf/style.css";
static char *scriptfile     = "~/.surf/script.js";
static char *historyfile    = "~/.surf/history.txt";

static Bool kioskmode       = FALSE; /* Ignore shortcuts */
static Bool showindicators  = TRUE;  /* Show indicators in window title */
static Bool zoomto96dpi     = FALSE;  /* Zoom pages to always emulate 96dpi */
static Bool runinfullscreen = FALSE; /* Run in fullscreen mode by default */
static Bool openbar         = FALSE; /* Open address bar upon launch */

static guint defaultfontsize = 16;   /* Default font size */
static gfloat zoomlevel = 1.0;       /* Default zoom level */

/* Soup default features */
static char *cookiefile     = "~/.surf/cookies.txt";
static char *cookiepolicies = "A@a"; /* A: accept all; a: accept nothing,
                                        @: accept no third party */
static Bool strictssl      = TRUE; /* Refuse untrusted SSL connections */

/* Webkit default features */
static Bool enablefavicons = TRUE;
static Bool enablescrollbars = TRUE;
static Bool enablespatialbrowsing = FALSE;
static Bool enableplugins = FALSE;
static Bool enablescripts = TRUE;
static Bool enableinspector = TRUE;
static Bool loadimages = TRUE;
static Bool allowgeolocation = FALSE;

#define SETPROP(p) { .v = (char *[]){ "/bin/sh", "-c", "~/.surf/surf.sh $0 $1 $2", p, winid, NULL } }

/* DOWNLOAD(URI, referer) */
#define DOWNLOAD(d, r) { \
	.v = (char *[]){ "/bin/sh", "-c", "download.sh \"$0\" \"$1\" \"$2\" \"$3\" \"$4\"", \
		d, useragent, r, cookiefile, NULL \
	} \
}

#define MODKEY GDK_CONTROL_MASK

/* hotkeys */
/*
 * If you use anything else but MODKEY and GDK_SHIFT_MASK, don't forget to
 * edit the CLEANMASK() macro.
 */
static Key keys[] = {
    /* modifier	            keyval      function    arg             Focus */
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_r,      reload,     { .b = TRUE } },
    { MODKEY,               GDK_KEY_r,      reload,     { .b = FALSE } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_p,      print,      { 0 } },

    { MODKEY,               GDK_KEY_p,      clipboard,  { .b = TRUE } },
    { MODKEY,               GDK_KEY_y,      clipboard,  { .b = FALSE } },

    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_j,      zoom,       { .i = -1 } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_k,      zoom,       { .i = +1 } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_q,      zoom,       { .i = 0  } },
    { MODKEY,               GDK_KEY_minus,  zoom,       { .i = -1 } },
    { MODKEY,               GDK_KEY_plus,   zoom,       { .i = +1 } },

    { MODKEY,               GDK_KEY_l,      navigate,   { .i = +1 } },
    { MODKEY,               GDK_KEY_h,      navigate,   { .i = -1 } },

    { MODKEY,               GDK_KEY_j,      scroll_v,   { .i = +1 } },
    { MODKEY,               GDK_KEY_k,      scroll_v,   { .i = -1 } },
    { MODKEY,               GDK_KEY_b,      scroll_v,   { .i = -10000 } },
    { MODKEY,               GDK_KEY_space,  scroll_v,   { .i = +10000 } },
    { MODKEY,               GDK_KEY_i,      scroll_h,   { .i = +1 } },
    { MODKEY,               GDK_KEY_u,      scroll_h,   { .i = -1 } },

    { 0,                    GDK_KEY_F11,    fullscreen, { 0 } },
    { MODKEY,               GDK_KEY_Escape, stop,       { 0 } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_o,      inspector,  { 0 } },

    { MODKEY,               GDK_KEY_g,      spawn,      SETPROP("_SURF_URI") },
    { MODKEY,               GDK_KEY_f,      spawn,      SETPROP("_SURF_FIND") },
    { MODKEY,               GDK_KEY_slash,  spawn,      SETPROP("_SURF_FIND") },

    { MODKEY,               GDK_KEY_n,      find,       { .b = TRUE } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_n,      find,       { .b = FALSE } },

    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_c,      toggle,     { .v = "enable-caret-browsing" } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_i,      toggle,     { .v = "auto-load-images" } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_s,      toggle,     { .v = "enable-javascript" } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_v,      toggle,     { .v = "enable-plugins" } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_a,      togglecookiepolicy, { 0 } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_m,      togglestyle, { 0 } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_b,      togglescrollbars, { 0 } },
    { MODKEY|GDK_SHIFT_MASK,GDK_KEY_l,      togglegeolocation, { 0 } },

    // my shotcuts
    { MODKEY, GDK_KEY_b, spawn, SETPROP("_SURF_BMARK") },
    { MODKEY|GDK_SHIFT_MASK, GDK_KEY_d, spawn, SETPROP("_SURF_INFO") },
    { MODKEY|GDK_SHIFT_MASK, GDK_KEY_g, spawn, SETPROP("_SURF_URI_RAW") },
    { 0,               GDK_KEY_Forward,   navigate,   { .i = +1 } },
    { 0,               GDK_KEY_Back,      navigate,   { .i = -1 } },
};

