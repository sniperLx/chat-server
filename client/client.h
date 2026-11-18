#ifndef CLIENT_H
#define CLIENT_H

#include <gtk/gtk.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../protocol.h"

/*
 * Connection API is thread-safe. build_socket starts the receiver thread;
 * incoming packets are later delivered to GTK on the main thread.
 *
 * build_socket returns 0 on success, 1 for an invalid IPv4 address, -1 for an
 * I/O/thread setup failure, and -2 when a connection is already active.
 */
int build_socket(const char *serv_ip);
void close_connection(void);
int connection_is_active(void);
int connection_is_logged_in(void);
int send_login(const char *name);
int send_func(const char *text);

/* These callbacks and helpers must be called from the GTK main thread. */
void send_text(GtkWidget *button, gpointer data);
void get_ip(GtkWidget *button, gpointer data);
void quit_win(GtkWidget *widget, gpointer data);
void update_login_state_ui(int logged_in);
void request_login_name(void);

void clean_text(void);
void show_err(const gchar *text);
void show_remote_text(const char *text);

#endif
