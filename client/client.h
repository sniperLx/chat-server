#ifndef CLIENT_H
#define CLIENT_H

#include <gtk/gtk.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../protocol.h"

int build_socket(const char *serv_ip);
void close_connection(void);
int connection_is_active(void);
int connection_is_logged_in(void);
int send_login(const char *name);
int send_func(const char *text);

void send_text(GtkWidget *button, gpointer data);
void get_ip(GtkWidget *button, gpointer data);
void quit_win(GtkWidget *widget, gpointer data);
void update_login_state_ui(int logged_in);
void request_login_name(void);

void clean_text(void);
void show_err(const gchar *text);
void show_remote_text(const char *text);

#endif
