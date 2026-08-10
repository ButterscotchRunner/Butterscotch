#include <gtk/gtk.h>

void show_error_box(const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(
        NULL,
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_OK,
        "%s",
        message
    );

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}