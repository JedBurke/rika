// dragon - very lightweight DnD file source/target
// Copyright 2014 Michael Homer.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

void selectable_set_active(bool state) {
  for (int i = 0; i < MAX_SIZE; i++) {
    SelectableItem* item = selectable_items[i];

    if (item != NULL) {
      gtk_toggle_button_set_active((GtkToggleButton*)item->ref, state);
    }
  }
}

void selectable_next(bool desired_state) {
  // How many items should be selected. Every item matching `desired_state`
  // will increment the value by one.
  int select_count = 0;

  // Whether an item currently matching `desired_state` has been found.
  bool gathering = false;

  // Where to start processing the items which need to have their active state
  // set to `desired_state`. Its value should be the first item not matching
  // `desired_state` while `gathering` is true.
  int offset = 0;

  for (int gather_index = 0; gather_index < MAX_SIZE; gather_index++) {
    SelectableItem* item = selectable_items[gather_index];

    if (item == NULL)
      break;

    if (!gathering && item->selected == desired_state) {
      gathering = true;
    }

    if (gathering) {
      if (item->selected == desired_state) {
        gtk_toggle_button_set_active((GtkToggleButton*)item->ref, !desired_state);
        select_count++;

      } else {
        // Stop processing, since the chain has been broken.
        offset = gather_index;
        break;

      }
    }
  }

  // Set the desired state after the items have been gathered and the offset
  // has been determined.
  for (int set_index = offset; set_index < offset + select_count; set_index++)
  {
    SelectableItem* item = selectable_items[set_index];

    if (item == NULL)
      break;

    // Have the widget explicitly grab focus when the command bar is enabled,
    // since it will otherwise scroll off the screen.
    if (command_bar_enabled)
      gtk_widget_grab_focus((GtkWidget*)item->ref);

    gtk_toggle_button_set_active((GtkToggleButton*)item->ref, desired_state);
  }
}

bool handle_keys(GtkWidget* self, GdkEventKey *event, gpointer user_data) {
  switch (event->keyval) {
    // Select all.
    case GDK_KEY_a:
      selectable_set_active(true);
      return true;

    // Deselect all.
    case GDK_KEY_d:
      selectable_set_active(false);
      return true;

    // Previous set.
    case GDK_KEY_k:
      return false;

    // Next set.
    case GDK_KEY_j:
      selectable_next(true);
      return true;

    default:
      return false;
  }

}

GtkWidget* init_command_bar() {
  GtkWidget* bar = gtk_action_bar_new();

  GtkWidget *next_set;
  next_set = gtk_button_new_from_icon_name("go-bottom", GTK_ICON_SIZE_SMALL_TOOLBAR);

  gtk_action_bar_pack_start((GtkActionBar*)bar, next_set);

  return bar;
}

void add_target_button();

void do_quit(GtkWidget *widget, gpointer data) {
    exit(0);
}

void button_clicked(GtkWidget *widget, gpointer user_data) {
    struct draggable_thing *dd = (struct draggable_thing *)user_data;
    if (0 == fork()) {
        execlp("xdg-open", "xdg-open", dd->uri, NULL);
    }
}

// Updates the active (selected) status of the file in `selectable_items` when toggled.
void button_toggled(GtkWidget *widget, gpointer user_data) {
  for (int i = 0; i < MAX_SIZE; i++) {
    SelectableItem* item = selectable_items[i];

    if (item != NULL && item->ref == widget) {
      bool selected = gtk_toggle_button_get_active((GtkToggleButton*)widget);
      item->selected = selected;

      break;
    }
  }
}

void drag_data_get(GtkWidget    *widget,
               GdkDragContext   *context,
               GtkSelectionData *data,
               guint             info,
               guint             time,
               gpointer          user_data) {

    struct draggable_thing *dd = (struct draggable_thing *)user_data;

    if (info == TARGET_TYPE_URI) {
        char** uris;
        char* single_uri_data[2] = {dd->uri, NULL};

        if (drag_all) {
            uri_collection[uri_count] = NULL;
            uris = uri_collection;
        } else {
          // Handle the --single-select option here.
          if (single_select) {
            uris = single_uri_data;

          } else {
            // Reinitialize the selected items array.
            if (selected_items != NULL)
              free(selected_items);

            selected_items = malloc(sizeof(char*) * (MAX_SIZE  + 1));

            // Go through each of the items in `selectable_items`, storing it
            // as `item` and checking if the `selected` field is true. If so,
            // store the `uri` field in the `selected_items` array.
            int selected_index = 0;

            for (int item_index = 0; item_index < MAX_SIZE; item_index++) {
              SelectableItem* item = selectable_items[item_index];

              if (item != NULL && item->selected == true)
                selected_items[selected_index++] = item->uri;

            }

            // Check if any items have been selected. If not, return the one
            // which is being dragged regardless of its status.
            if (selected_index == 0) {
              uris = single_uri_data;

            } else {
              selected_items[selected_index] = NULL;
              uris = selected_items;

            }
          }
        }
        if (verbose) {
            if (drag_all)
                fputs("Sending all as URI\n", stderr);
            else
                fprintf(stderr, "Sending as URI: %s\n", dd->uri);
        }

        gtk_selection_data_set_uris(data, uris);
        g_signal_stop_emission_by_name(widget, "drag-data-get");
    } else if (info == TARGET_TYPE_TEXT) {
        if (verbose)
            fprintf(stderr, "Sending as TEXT: %s\n", dd->text);
        gtk_selection_data_set_text(data, dd->text, -1);
    } else {
        fprintf(stderr, "Error: bad target type %i\n", info);
    }
}

void drag_end(GtkWidget *widget, GdkDragContext *context, gpointer user_data) {
    if (verbose) {
        gboolean succeeded = gdk_drag_drop_succeeded(context);
        GdkDragAction action = gdk_drag_context_get_selected_action (context);
        char* action_str;
        switch (action) {
            case GDK_ACTION_COPY:
                action_str = "COPY"; break;
            case GDK_ACTION_MOVE:
                action_str = "MOVE"; break;
            case GDK_ACTION_LINK:
                action_str = "LINK"; break;
            case GDK_ACTION_ASK:
                action_str = "ASK"; break;
            default:
                action_str = malloc(sizeof(char) * 20);
                snprintf(action_str, 20, "invalid (%d)", action);
                break;
        }
        fprintf(stderr, "Selected drop action: %s; Succeeded: %d\n", action_str, succeeded);
        if (action_str[0] == 'i')
            free(action_str);
    }
    if (and_exit)
        gtk_main_quit();
}

void add_uri(char *uri) {
    if (uri_count < MAX_SIZE) {
        uri_collection[uri_count] = uri;
        uri_count++;
    } else {
        fprintf(stderr, "Exceeded maximum number of files for drag_all (%d)\n", MAX_SIZE);
    }
}

GtkButton *add_button(char *label, struct draggable_thing *dragdata, int type) {
    GtkWidget *button;

    if (icons_only) {
        button = gtk_toggle_button_new();
    } else {
        button = gtk_toggle_button_new_with_label(label);
    }

    gtk_toggle_button_set_active((GtkToggleButton*)button, default_active_state);

    // Show a tooltip with the filename. Should perhaps show the full path.
    gtk_widget_set_tooltip_text(button, label);

    GtkTargetList *targetlist = gtk_drag_source_get_target_list(GTK_WIDGET(button));
    if (targetlist)
        gtk_target_list_ref(targetlist);
    else
        targetlist = gtk_target_list_new(NULL, 0);
    if (type == TARGET_TYPE_URI)
        gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);
    else
        gtk_target_list_add_text_targets(targetlist, TARGET_TYPE_TEXT);

    gtk_drag_source_set(GTK_WIDGET(button), GDK_BUTTON1_MASK, NULL, 0,
            GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK);
    gtk_drag_source_set_target_list(GTK_WIDGET(button), targetlist);
    g_signal_connect(GTK_WIDGET(button), "drag-data-get",
            G_CALLBACK(drag_data_get), dragdata);
    /* g_signal_connect(GTK_WIDGET(button), "clicked", */
    /*         G_CALLBACK(button_clicked), dragdata); */

    g_signal_connect(GTK_WIDGET(button), "toggled", G_CALLBACK(button_toggled), NULL);

    g_signal_connect(GTK_WIDGET(button), "drag-end",
            G_CALLBACK(drag_end), dragdata);

    gtk_container_add(GTK_CONTAINER(vbox), button);

    SelectableItem *current_item = malloc(sizeof(SelectableItem));
    current_item->ref = button;
    current_item->uri = dragdata->uri;
    current_item->selected = default_active_state;

    selectable_items[selectable_index++] = current_item;

    if (drag_all)
      add_uri(dragdata->uri);
    else
      uri_count++;

    return (GtkButton *)button;
}

void left_align_button(GtkButton *button) {
    GList *child = g_list_first(
            gtk_container_get_children(GTK_CONTAINER(button)));
    if (child)
        gtk_widget_set_halign(GTK_WIDGET(child->data), GTK_ALIGN_START);
}

GtkIconInfo* icon_info_from_content_type(char *content_type) {
    GIcon *icon = g_content_type_get_icon(content_type);
    return gtk_icon_theme_lookup_by_gicon(icon_theme, icon, 48, 0);
}

void add_file_button(GFile *file) {
    char *filename;

    // When the filename only option is set, only the file's basename including
    // extenion is displayed, not the entire path.
    if (filename_only) {
      char *path = g_file_get_path(file);
      filename = g_path_get_basename(path);
    } else {
      filename = g_file_get_path(file);
    }

    if(!g_file_query_exists(file, NULL)) {
        fprintf(stderr, "The file `%s' does not exist.\n",
                filename);
        exit(1);
    }
    char *uri = g_file_get_uri(file);
    if (all_compact) {
      add_uri(uri);
      return;
    }
    struct draggable_thing *dragdata = malloc(sizeof(struct draggable_thing));
    dragdata->text = filename;
    dragdata->uri = uri;

    GtkButton *button = add_button(filename, dragdata, TARGET_TYPE_URI);
    GdkPixbuf *pb = gdk_pixbuf_new_from_file_at_size(filename, thumb_size, thumb_size, NULL);
    if (pb) {
        GtkWidget *image = gtk_image_new_from_pixbuf(pb);
        gtk_button_set_always_show_image(button, true);
        gtk_button_set_image(button, image);
        gtk_button_set_always_show_image(button, true);
    } else {
        GFileInfo *fileinfo = g_file_query_info(file, "*", 0, NULL, NULL);
        GIcon *icon = g_file_info_get_icon(fileinfo);
        GtkIconInfo *icon_info = gtk_icon_theme_lookup_by_gicon(icon_theme,
                icon, 48, 0);

        // Try a few fallback mimetypes if no icon can be found
        if (!icon_info)
            icon_info = icon_info_from_content_type("application/octet-stream");
        if (!icon_info)
            icon_info = icon_info_from_content_type("text/x-generic");
        if (!icon_info)
            icon_info = icon_info_from_content_type("text/plain");

        if (icon_info) {
            GtkWidget *image = gtk_image_new_from_pixbuf(
                    gtk_icon_info_load_icon(icon_info, NULL));
            gtk_button_set_image(button, image);
            gtk_button_set_always_show_image(button, true);
        }
    }

    if (!icons_only)
        left_align_button(button);
}

void add_filename_button(char *filename) {
    GFile *file = g_file_new_for_path(filename);
    add_file_button(file);
}

void add_uri_button(char *uri) {
    if (all_compact) {
      add_uri(uri);
      return;
    }
    struct draggable_thing *dragdata = malloc(sizeof(struct draggable_thing));
    dragdata->text = uri;
    dragdata->uri = uri;
    GtkButton *button = add_button(uri, dragdata, TARGET_TYPE_URI);
    left_align_button(button);
}

bool is_uri(char *uri) {
    for (int i=0; uri[i]; i++)
        if (uri[i] == '/')
            return false;
        else if (uri[i] == ':' && i > 0)
            return true;
        else if (!(    (uri[i] >= 'a' && uri[i] <= 'z')
                    || (uri[i] >= 'A' && uri[i] <= 'Z')
                    || (uri[i] >= '0' && uri[i] <= '9' && i > 0)
                    || (i > 0 && (uri[i] == '+' || uri[i] == '.' || uri[i] == '-'))
                  )) // RFC3986 URI scheme syntax
            return false;
    return false;
}

bool is_file_uri(char *uri) {
    char *prefix = "file:";
    return strncmp(prefix, uri, strlen(prefix)) == 0;
}

gboolean drag_drop (GtkWidget *widget,
               GdkDragContext *context,
               gint            x,
               gint            y,
               guint           time,
               gpointer        user_data) {
    GtkTargetList *targetlist = gtk_drag_dest_get_target_list(widget);
    GList *list = gdk_drag_context_list_targets(context);
    if (list) {
        while (list) {
            GdkAtom atom = (GdkAtom)g_list_nth_data(list, 0);
            if (gtk_target_list_find(targetlist,
                        GDK_POINTER_TO_ATOM(g_list_nth_data(list, 0)), NULL)) {
                gtk_drag_get_data(widget, context, atom, time);
                return true;
            }
            list = g_list_next(list);
        }
    }
    gtk_drag_finish(context, false, false, time);
    return true;
}

void update_all_button() {
    sprintf(file_num_label, "%d files", uri_count);
    gtk_button_set_label((GtkButton *)all_button, file_num_label);
}

void
drag_data_received (GtkWidget          *widget,
                    GdkDragContext     *context,
                    gint                x,
                    gint                y,
                    GtkSelectionData   *data,
                    guint               info,
                    guint               time) {
    gchar **uris = gtk_selection_data_get_uris(data);
    unsigned char *text = gtk_selection_data_get_text(data);
    if (!uris && !text)
        gtk_drag_finish (context, FALSE, FALSE, time);
    if (uris) {
        if (verbose)
            fputs("Received URIs\n", stderr);
        gtk_container_remove(GTK_CONTAINER(vbox), widget);
        for (; *uris; uris++) {
            if (is_file_uri(*uris)) {
                GFile *file = g_file_new_for_uri(*uris);
                if (print_path) {
                    char *filename = g_file_get_path(file);
                    printf("%s\n", filename);
                } else
                    printf("%s\n", *uris);
                if (keep)
                    add_file_button(file);

            } else {
                printf("%s\n", *uris);
                if (keep)
                    add_uri_button(*uris);
            }
        }
        if (all_compact)
            update_all_button();
        add_target_button();
        gtk_widget_show_all(window);
    } else if (text) {
        if (verbose)
            fputs("Received Text\n", stderr);
        printf("%s\n", text);
    } else if (verbose)
        fputs("Received nothing\n", stderr);
    gtk_drag_finish (context, TRUE, FALSE, time);
    if (and_exit)
        gtk_main_quit();
}

void add_target_button() {
    GtkWidget *label = gtk_button_new();
    gtk_button_set_label(GTK_BUTTON(label), "Drag something here...");
    gtk_container_add(GTK_CONTAINER(vbox), label);
    GtkTargetList *targetlist = gtk_drag_dest_get_target_list(GTK_WIDGET(label));
    if (targetlist)
        gtk_target_list_ref(targetlist);
    else
        targetlist = gtk_target_list_new(NULL, 0);
    gtk_target_list_add_text_targets(targetlist, TARGET_TYPE_TEXT);
    gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);
    gtk_drag_dest_set(GTK_WIDGET(label),
            GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_HIGHLIGHT, NULL, 0,
            GDK_ACTION_COPY);
    gtk_drag_dest_set_target_list(GTK_WIDGET(label), targetlist);
    g_signal_connect(GTK_WIDGET(label), "drag-drop",
            G_CALLBACK(drag_drop), NULL);
    g_signal_connect(GTK_WIDGET(label), "drag-data-received",
            G_CALLBACK(drag_data_received), NULL);
}

void target_mode() {
    add_target_button();
    gtk_widget_show_all(window);
    gtk_main();
}

void make_btn(char *filename) {
    if (!is_uri(filename)) {
        add_filename_button(filename);
    } else if (is_file_uri(filename)) {
        GFile *file = g_file_new_for_uri(filename);
        add_file_button(file);
    } else {
        add_uri_button(filename);
    }
}

static void readstdin(void) {
    char *write_pos = stdin_files, *newline;
    size_t max_size = BUFSIZ * 2, cur_size = 0;
    // read each line from stdin and add it to the item list
    while (fgets(write_pos, BUFSIZ, stdin)) {
            if (write_pos[0] == '-')
                    continue;
            if ((newline = strchr(write_pos, '\n')))
                    *newline = '\0';
            else
                    break;
            make_btn(write_pos);
            cur_size = newline - stdin_files + 1;
            if (max_size < cur_size + BUFSIZ) {
                    if (!(stdin_files = realloc(stdin_files, (max_size += BUFSIZ))))
                            fprintf(stderr, "%s: cannot realloc %lu bytes.\n", progname, max_size);
                    newline = stdin_files + cur_size - 1;
            }
            write_pos = newline + 1;
    }
}

void create_all_button() {
    sprintf(file_num_label, "%d files", uri_count);
    all_button = gtk_button_new_with_label(file_num_label);

    GtkTargetList *targetlist = gtk_target_list_new(NULL, 0);
    gtk_target_list_add_uri_targets(targetlist, TARGET_TYPE_URI);

    // fake uri to avoid segfault when callback deference it
    fake_dragdata.uri = file_num_label;

    gtk_drag_source_set(GTK_WIDGET(all_button), GDK_BUTTON1_MASK, NULL, 0,
            GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_ASK);
    gtk_drag_source_set_target_list(GTK_WIDGET(all_button), targetlist);
    g_signal_connect(GTK_WIDGET(all_button), "drag-data-get",
            G_CALLBACK(drag_data_get), &fake_dragdata);
    g_signal_connect(GTK_WIDGET(all_button), "drag-end",
            G_CALLBACK(drag_end), &fake_dragdata);

    gtk_container_add(GTK_CONTAINER(vbox), all_button);
}

int main (int argc, char **argv) {
    bool from_stdin = false;
    stdin_files = malloc(BUFSIZ * 2);
    progname = argv[0];
    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            mode = MODE_HELP;
            printf("%s - lightweight DnD source/target\n", APPLICATION);
            printf("Usage: %s [OPTION] [FILENAME]\n", progname);
            printf("  --and-exit,    -x  exit after a single completed drop\n");
            printf("  --target,      -t  act as a target instead of source\n");
            printf("  --keep,        -k  with --target, keep files to drag out\n");
            printf("  --print-path,  -p  with --target, print file paths"
                    " instead of URIs\n");
            printf("  --all,         -a  drag all files at once\n");
            printf("  --all-compact, -A  drag all files at once, only displaying"
                    " the number of files\n");
            printf("  --single       -S  drag only the current file, regardless"
                   " of how many are selected\n");
            printf("  --select-all   -G  select all files at start\n");
            printf("  --icon-only,   -i  only show icons in drag-and-drop"
                    " windows\n");
            printf("  --name-only,   -f  only show the file's basename and"
                   " not the full path\n");
            printf("  --on-top,      -T  make window always-on-top\n");
            printf("  --stdin,       -I  read input from stdin\n");
            printf("  --thumb-size,  -s  set thumbnail size (default 96)\n");
            printf("  --verbose,     -v  be verbose\n");
            printf("  --help            show help\n");
            printf("  --version         show version details\n");
            exit(0);
        } else if (strcmp(argv[i], "--version") == 0) {
            mode = MODE_VERSION;
            puts("dragon " VERSION);
            puts("Copyright (C) 2014-2022 Michael Homer and contributors");
            puts("This program comes with ABSOLUTELY NO WARRANTY.");
            puts("See the source for copying conditions.");
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0
                || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-t") == 0
                || strcmp(argv[i], "--target") == 0) {
            mode = MODE_TARGET;
        } else if (strcmp(argv[i], "-x") == 0
                || strcmp(argv[i], "--and-exit") == 0) {
            and_exit = true;
        } else if (strcmp(argv[i], "-k") == 0
                || strcmp(argv[i], "--keep") == 0) {
            keep = true;
        } else if (strcmp(argv[i], "-p") == 0
                || strcmp(argv[i], "--print-path") == 0) {
            print_path = true;
        } else if (strcmp(argv[i], "-a") == 0
                || strcmp(argv[i], "--all") == 0) {
            drag_all = true;
        } else if (strcmp(argv[i], "-A") == 0
                || strcmp(argv[i], "--all-compact") == 0) {
            drag_all = true;
            all_compact = true;
        } else if (strcmp(argv[i], "-S") == 0
                || strcmp(argv[i], "--single") == 0) {
            single_select = true;
        } else if (strcmp(argv[i], "-G") == 0
                || strcmp(argv[i], "--select-all") == 0) {
            default_active_state = true;
        } else if (strcmp(argv[i], "-i") == 0
                || strcmp(argv[i], "--icon-only") == 0) {
            icons_only = true;
        } else if (strcmp(argv[i], "-f") == 0
                || strcmp(argv[i], "--name-only") == 0) {
            filename_only = true;
        } else if (strcmp(argv[i], "-T") == 0
                || strcmp(argv[i], "--on-top") == 0) {
            always_on_top = true;
        } else if (strcmp(argv[i], "-I") == 0
                || strcmp(argv[i], "--stdin") == 0) {
            from_stdin = true;
        } else if (strcmp(argv[i], "-s") == 0
                || strcmp(argv[i], "--thumb-size") == 0) {
            if (argv[++i] == NULL || (thumb_size = atoi(argv[i])) <= 0) {
                fprintf(stderr, "%s: error: bad argument for %s `%s'.\n",
                        progname, argv[i-1], argv[i]);
                exit(1);
            }
            argv[i][0] = '\0';
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: error: unknown option `%s'.\n",
                    progname, argv[i]);
        }
    }
    setvbuf(stdout, NULL, _IOLBF, BUFSIZ);

    GtkAccelGroup *accelgroup;
    GClosure *closure;

    gtk_init(&argc, &argv);

    icon_theme = gtk_icon_theme_get_default();

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

    // Scrolling Window
    panel = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(panel, true);
    gtk_widget_set_hexpand(panel, true);

    closure = g_cclosure_new(G_CALLBACK(do_quit), NULL, NULL);
    accelgroup = gtk_accel_group_new();
    gtk_accel_group_connect(accelgroup, GDK_KEY_Escape, 0, 0, closure);
    closure = g_cclosure_new(G_CALLBACK(do_quit), NULL, NULL);
    gtk_accel_group_connect(accelgroup, GDK_KEY_q, 0, 0, closure);
    gtk_window_add_accel_group(GTK_WINDOW(window), accelgroup);

    gtk_window_set_title(GTK_WINDOW(window), "Run");
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    gtk_window_set_keep_above(GTK_WINDOW(window), always_on_top);

    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    gtk_widget_add_events(window, GDK_KEY_PRESS_MASK);
    g_signal_connect(G_OBJECT(window), "key_press_event", G_CALLBACK(handle_keys), NULL);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, ITEM_SPACING);


    GtkWidget *outer_panel;
    outer_panel = gtk_grid_new();
    gtk_orientable_set_orientation((GtkOrientable*)outer_panel, GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_vexpand(outer_panel, true);
    gtk_widget_set_hexpand(outer_panel, true);

    gtk_container_add(GTK_CONTAINER(panel), vbox);
    gtk_container_add(GTK_CONTAINER(outer_panel), panel);

    if (command_bar_enabled) {
      action_bar = init_command_bar();
      gtk_container_add(GTK_CONTAINER(outer_panel), action_bar);
    }

    gtk_container_add(GTK_CONTAINER(window), outer_panel);

    gtk_window_set_title(GTK_WINDOW(window), APPLICATION);

    if (all_compact)
        create_all_button();

    if (mode == MODE_TARGET) {
        if (drag_all)
            uri_collection = malloc(sizeof(char*) * (MAX_SIZE  + 1));
        target_mode();
        exit(0);
    }

    if (from_stdin)
        uri_collection = malloc(sizeof(char*) * (MAX_SIZE  + 1));
    else if (drag_all)
        uri_collection = malloc(sizeof(char*) * ((argc > MAX_SIZE ? argc : MAX_SIZE) + 1));

    for (int i=1; i<argc; i++) {
        if (argv[i][0] != '-' && argv[i][0] != '\0')
           make_btn(argv[i]);
    }
    if (from_stdin)
        readstdin();

    if (!uri_count) {
        printf("Usage: %s [OPTIONS] FILENAME\n", progname);
        exit(0);
    }

    if (all_compact)
        update_all_button();

    gtk_widget_show_all(window);

    gtk_main();

    return 0;
}
