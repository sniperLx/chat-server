/*
 * client.c
 * GTK chat client: display local/remote messages and persist them locally.
 */

#include "client.h"

#include <time.h>

GtkWidget *window,
	  *rec_text, *snd_text,
	  *space_label,
	  *link_button, *snd_button, *quit_button,
	  *hbox, *vbox,
	  *rec_scrolled, *snd_scrolled;
GtkTextBuffer *rec_buffer, *snd_buffer;

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static int append_log(const char *prefix, const char *text)
{
	FILE *out;
	time_t now;
	char timestamp[32];
	int result = 1;

	pthread_mutex_lock(&log_mutex);
	out = fopen("chatlog.db", "a");
	if (out == NULL) {
		pthread_mutex_unlock(&log_mutex);
		return 0;
	}

	now = time(NULL);
	if (ctime_r(&now, timestamp) != NULL)
		timestamp[strcspn(timestamp, "\n")] = '\0';
	else
		strcpy(timestamp, "unknown time");

	if (fprintf(out, "[%s] %s%s\n", timestamp, prefix, text) < 0)
		result = 0;
	if (fclose(out) != 0)
		result = 0;

	pthread_mutex_unlock(&log_mutex);
	return result;
}

static void show_local_text(const char *text)
{
	GtkTextIter start, end;

	gtk_text_buffer_get_bounds(rec_buffer, &start, &end);
	gtk_text_buffer_insert(rec_buffer, &end, "Me:", -1);
	gtk_text_buffer_insert(rec_buffer, &end, text, -1);
	gtk_text_buffer_insert(rec_buffer, &end, "\n", 1);
}

void clean_text(void)
{
	GtkTextIter start, end;

	gtk_text_buffer_get_bounds(snd_buffer, &start, &end);
	gtk_text_buffer_delete(snd_buffer, &start, &end);
}

void show_err(const gchar *text)
{
	GtkTextIter start, end;

	gtk_text_buffer_get_bounds(rec_buffer, &start, &end);
	gtk_text_buffer_insert(rec_buffer, &end, text, -1);
}

void send_text(GtkWidget *button, gpointer data)
{
	GtkTextIter start, end;
	gchar *text;
	size_t length;

	(void)button;
	(void)data;

	if (!connection_is_active()) {
		show_err("Not connected\n");
		return;
	}

	gtk_text_buffer_get_bounds(snd_buffer, &start, &end);
	text = gtk_text_buffer_get_text(snd_buffer, &start, &end, FALSE);
	if (text == NULL) {
		show_err("Failed to read message\n");
		return;
	}

	length = strlen(text);
	if (length == 0) {
		show_err("Message cannot be empty.\n");
	} else if (length > CHAT_MAX_TEXT) {
		show_err("Message is too long. Shorten it and try again.\n");
	} else if (send_func(text) != 0) {
		show_err("Send failed. Connection closed.\n");
	} else {
		clean_text();
		show_local_text(text);
		if (!append_log("Me:", text))
			show_err("Failed to save chat history.\n");
	}

	g_free(text);
}

void get_ip(GtkWidget *button, gpointer data)
{
	int result;

	(void)button;
	(void)data;

	result = build_socket("127.0.0.1");
	if (result == 1)
		show_err("Invalid IP address.\n");
	else if (result == -1)
		show_err("Connection failed.\n");
	else if (result == -2)
		show_err("Already connected to the server.\n");
	else
		show_err("Connected successfully.\n");
}

void show_remote_text(const char *text)
{
	GtkTextIter start, end;

	gtk_text_buffer_get_bounds(rec_buffer, &start, &end);
	gtk_text_buffer_insert(rec_buffer, &end, text, -1);
	gtk_text_buffer_insert(rec_buffer, &end, "\n", 1);

	if (!append_log("", text))
		show_err("Failed to save chat history.\n");
}

void quit_win(GtkWidget *widget, gpointer data)
{
	(void)widget;
	(void)data;

	close_connection();
	gtk_main_quit();
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "Linux Chat");
	gtk_container_set_border_width(GTK_CONTAINER(window), 5);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 300);

	space_label = gtk_label_new("                  ");
	link_button = gtk_button_new_with_label("Connect");
	snd_button = gtk_button_new_with_label("Send");
	quit_button = gtk_button_new_with_label("Quit");

	rec_text = gtk_text_view_new();
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(rec_text), FALSE);
	rec_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(rec_text));
	gtk_text_view_set_editable(GTK_TEXT_VIEW(rec_text), FALSE);

	snd_text = gtk_text_view_new();
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(snd_text), TRUE);
	snd_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(snd_text));
	gtk_text_view_set_editable(GTK_TEXT_VIEW(snd_text), TRUE);

	rec_scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rec_scrolled),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);
	snd_scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(snd_scrolled),
				       GTK_POLICY_AUTOMATIC,
				       GTK_POLICY_AUTOMATIC);

	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(rec_scrolled),
					      rec_text);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(snd_scrolled),
					      snd_text);

	hbox = gtk_hbox_new(FALSE, 2);
	vbox = gtk_vbox_new(FALSE, 2);

	gtk_box_pack_start(GTK_BOX(hbox), link_button, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), space_label, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), quit_button, FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(hbox), snd_button, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(vbox), rec_scrolled, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(vbox), snd_scrolled, TRUE, TRUE, 2);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 2);

	gtk_container_add(GTK_CONTAINER(window), vbox);

	g_signal_connect(GTK_OBJECT(link_button), "clicked",
			 G_CALLBACK(get_ip), NULL);
	g_signal_connect(GTK_OBJECT(snd_button), "clicked",
			 G_CALLBACK(send_text), NULL);
	g_signal_connect(GTK_OBJECT(quit_button), "clicked",
			 G_CALLBACK(quit_win), NULL);
	g_signal_connect(GTK_OBJECT(window), "destroy",
			 G_CALLBACK(quit_win), NULL);

	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
