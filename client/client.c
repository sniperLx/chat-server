/*
 Client.c          
 功能: 创建客户端窗口     
*/

#include "client.h"
#include <gtk/gtk.h>
#include <time.h>
/*声明全局变量*/
GtkWidget *window,
	  *rec_text,*snd_text,*ip_text,
	  *space_label,
	  *link_button,*snd_button,*quit_button,
	  *hbox,*vbox,
	  *rec_scrolled,*snd_scrolled;
GtkTextBuffer *rec_buffer,*snd_buffer;

int issucceed=-1;// build socked with server or not
void get_ip(GtkWidget *,gpointer);
void quit_win(GtkWidget *,gpointer);

int main(int argc,char **argv)
{
	

	gtk_init(&argc,&argv);

	window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window),"client");/*设置主窗口标题*/
        gtk_container_set_border_width(GTK_CONTAINER(window),5);/*设置主窗口边框*/
	gtk_window_set_position(GTK_WINDOW(window),GTK_WIN_POS_CENTER);
	gtk_window_set_default_size(GTK_WINDOW(window),400,300);
	
	space_label=gtk_label_new("                  ");
        

	/* 设置按钮 */
	link_button=gtk_button_new_with_label("连接");
	snd_button=gtk_button_new_with_label("发送");
	quit_button=gtk_button_new_with_label("退出");


        /* 设置接收文本框区*/
	rec_text=gtk_text_view_new();
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(rec_text),FALSE);//光标不可见

        rec_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(rec_text));
        gtk_text_view_set_editable(GTK_TEXT_VIEW(rec_text),FALSE);//不可编辑
        
        /* 设置发送文本框区*/
	snd_text=gtk_text_view_new();
        gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(snd_text),TRUE);

        snd_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(snd_text));
        gtk_text_view_set_editable(GTK_TEXT_VIEW(snd_text),TRUE);
      		   
	/*创建滚动窗口盒构件*/
	rec_scrolled=gtk_scrolled_window_new(NULL,NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(rec_scrolled),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	snd_scrolled=gtk_scrolled_window_new(NULL,NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(snd_scrolled),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	
	/* 创建文本盒构件*/
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(rec_scrolled),rec_text);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(snd_scrolled),snd_text);
	
	hbox=gtk_hbox_new(FALSE,2);/*生成一个水平排布的盒子*/
	vbox=gtk_vbox_new(FALSE,2);/*生成一个垂直排布的盒子*/
	
	/*包装所有盒构件*/
	
	gtk_box_pack_start(GTK_BOX(hbox),link_button,FALSE,FALSE,2);
	gtk_box_pack_start(GTK_BOX(hbox),space_label,TRUE,TRUE,2);
        gtk_box_pack_start(GTK_BOX(hbox),quit_button,FALSE,FALSE,2);
	gtk_box_pack_start(GTK_BOX(hbox),snd_button,FALSE,FALSE,2);

	gtk_box_pack_start(GTK_BOX(vbox),rec_scrolled,TRUE,TRUE,2);
	gtk_box_pack_start(GTK_BOX(vbox),snd_scrolled,TRUE,TRUE,2);
	gtk_box_pack_start(GTK_BOX(vbox),hbox,FALSE,FALSE,2);

	gtk_container_add(GTK_CONTAINER(window),vbox);/*将垂直盒构件包装到主盒中*/

	/*连接回调函数*/
	g_signal_connect(GTK_OBJECT(link_button),"clicked",GTK_SIGNAL_FUNC(get_ip),NULL);/* 点击"连接"回调函数get_ip */
	g_signal_connect(GTK_OBJECT(snd_button),"clicked",GTK_SIGNAL_FUNC(send_text),NULL);/*点击“发送”回调函数send_text*/
	g_signal_connect(GTK_OBJECT(quit_button),"clicked",GTK_SIGNAL_FUNC(quit_win),NULL);/* 点击“退出”回调函数quit_win*/
	g_signal_connect(GTK_OBJECT(window),"destroy",GTK_SIGNAL_FUNC(quit_win),NULL);
	
	gtk_widget_show_all(window);
	gtk_main();
	return 0;
}
/* 显示输入本文 */
void show_local_text(const gchar* text)
{
	GtkTextIter start,end;
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(rec_buffer),&start,&end);/*获得缓冲区开始和结束位置的Iter*/
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(rec_buffer),&end,"Me:",-1);/*插入文本到缓冲区*/
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(rec_buffer),&end,text,strlen(text));/*插入文本到缓冲区*/
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(rec_buffer),&end,"\n",1);/*插入文本到缓冲区*/
}
/* 清空输入文本框 */
void clean_text()
{
	GtkTextIter start,end;
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(snd_buffer),&start,&end);/*获得缓冲区开始和结束位置的Iter*/
	gtk_text_buffer_delete(GTK_TEXT_BUFFER(snd_buffer),&start,&end);/*插入到缓冲区*/
}
/* 获取输入文本并发送 */
void send_text()
{
	GtkTextIter start,end;
	gchar *text;
 	if(issucceed==-1){ /* 未创建套接字 */
 		show_err("未连接\n");
	}
	else
	{ /* 套接字创建成功并发送信息 */
		text = (gchar *)malloc(MAXSIZE);
		if(text==NULL)
		{
			printf("内存分配失败!\n");
			exit(1);
		}
		/* 获取文本 */
		gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(snd_buffer),&start,&end);
		text = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(snd_buffer),&start,&end,FALSE);
		/* 如果没有输入，直接返回 */
		if(strcmp(text,"")!=0)
		{
			send_func(text);
			clean_text();
			show_local_text(text);
		}
		else show_err("消息不能为空！\n");
		FILE *out = fopen("chatlog.db","a");//保存聊天记录

		time_t timep;
		time (&timep);
		fprintf(out,"\n%s",ctime(&timep));

		fprintf(out,"Me:%s\n",text);
		fclose(out); 
		free(text);
	}
}
/* 示错 */
void show_err(char *err)
{
	GtkTextIter start,end;
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(rec_buffer),&start,&end);/*获得缓冲区开始和结束位置的Iter*/
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(rec_buffer),&end,err,strlen(err));
}
/* 获取IP地址并创建套接字 */
void get_ip(GtkWidget *button,gpointer ip_text)
{
	gchar *serv_ip="127.0.0.1";
	int res;
	res=build_socket(serv_ip);
	if(res==1)
		show_err("IP地址无效\n");
	else if(res==-1)
		show_err("连接失败！\n");/*插入文本到缓冲区*/
	else{
		show_err("连接成功！\n");
		issucceed=0;
	}
}
/* 显示接收信息 */
void show_remote_text(char rcvd_mess[])
{
	GtkTextIter start,end;
	gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(rec_buffer),&start,&end);/*获得缓冲区开始和结束位置的Iter*/
	//gtk_text_buffer_insert(GTK_TEXT_BUFFER(rec_buffer),&end,"Other:",-1);/*插入文本到缓冲区*/
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(rec_buffer),&end,rcvd_mess,strlen(rcvd_mess));/*插入文本到缓冲区*/
	gtk_text_buffer_insert(GTK_TEXT_BUFFER(rec_buffer),&end,"\n",1);/*插入换行到缓冲区*/

	FILE *out = fopen("chatlog.db","a");//保存聊天记录
	fprintf(out,"%s\n",rcvd_mess);	
	fclose(out); 
}
/* 关闭主窗口 */
void quit_win(GtkWidget *window,gpointer data)
{
	gtk_main_quit();
}
