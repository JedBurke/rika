#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 500
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#define APPLICATION "rika"
#define VERSION "1.2.0"

// Defines the spacing between each item in the container (vbox).
#define ITEM_SPACING 6

// Top-level window.
GtkWidget *window;

// Container providing the scrolling.
GtkWidget *panel;

// Child of panel, where all the children are added.
GtkWidget *vbox;

GtkWidget *action_bar;

GtkIconTheme *icon_theme;

char *progname;
bool verbose = false;
int mode = 0;
int thumb_size = 96;
bool and_exit;
bool keep;
bool print_path = false;
bool icons_only = false;
bool filename_only = false;
bool always_on_top = false;

static char *stdin_files;

#define MODE_HELP 1
#define MODE_TARGET 2
#define MODE_VERSION 4

#define TARGET_TYPE_TEXT 1
#define TARGET_TYPE_URI 2

struct draggable_thing {
    char *text;
    char *uri;
};

typedef struct {
    GtkWidget* ref;
    char* uri;
    bool selected;
} SelectableItem;

// MODE_ALL
#define MAX_SIZE 1000
char** uri_collection;
int uri_count = 0;
bool drag_all = false;
bool all_compact = false;
char file_num_label[10];
struct draggable_thing fake_dragdata;
GtkWidget *all_button;

// Whether the action bar which, holds buttons relating to relecting files
// should be enabled.
bool command_bar_enabled = false;

// ---

// How many items which have been selected and will be dragged.

// Determines whether the items will be active (selected) by default or not.
// Can be set with the `-G` or `--select-all` switches.
bool default_active_state = false;

// Whether only the item that is being dragged should be processed and not all
// the other selected items.
bool single_select = false;

// A list of the selected items to be processed.
char** selected_items = NULL;

// A SelectableItem pointer array meant to hold references of the files added
// to the instance.
SelectableItem* selectable_items[MAX_SIZE];

// The current index of the selectable_items array. Only to be incremented when
// a new item is being added.
int selectable_index = 0;


void selectable_set_active(bool state);



