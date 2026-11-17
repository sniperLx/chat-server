/************************************************
 * se_sock.c                                  		     *
 * function:                                  		     *
 *    create a socket                        		     *
 *    create a new thread,for receive function  *
 *    send message                             	      *
 ************************************************/
#include "server.h"

#define N 10
socklen_t clen;//客户端地址结构体长度

int s_sockfd; //server socket fd
int c_sockfd[N];//clients socket fd
int c_num = 0; //clients number
pthread_t recv_thread[N]; //save subthread pid
struct sockaddr_in saddr,caddr;//saddr表示本地服务器地址信息，caddr用于保存向本服务器发送信息的主机地址信息
int build_socket()
{
	int res;
	int reuse = 0; //reuse port
	pthread_attr_t thread_attr;
	memset(c_sockfd,-1,sizeof(c_sockfd));

	/* Set status of thread */
	res=pthread_attr_init(&thread_attr);
	if(res!=0)
	{
		printf("Setting detached attribute failed");
		exit(EXIT_FAILURE);
	}
	/* Create a socket */
	if((s_sockfd=socket(AF_INET,SOCK_STREAM,0)) < 0)
	{
		perror("Socket Error");
		exit(1);
	}
	/*avoid error: address already in use(when server down, we can reuse port immediately)*/
 	if (setsockopt(s_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
       	{
                perror("setsockopet error");
                return -1;
       	}
	
	bzero(&saddr,sizeof(struct sockaddr));
	saddr.sin_family=AF_INET;
	saddr.sin_addr.s_addr=htonl(INADDR_ANY);
	saddr.sin_port=htons(PORT);
	if(bind(s_sockfd,(struct sockaddr *)&saddr,sizeof(struct sockaddr_in))==-1)
	{
		perror("Bind Error");
		exit(1);
	}
	if((res = listen(s_sockfd,5)) == -1)
		perror("listen failed");
	signal(SIGCHLD,SIG_IGN);

	/* Set the status of thread,don't wait for return of the subthread */
	res=pthread_attr_setdetachstate(&thread_attr,PTHREAD_CREATE_DETACHED);
	if(res!=0)
	{
		printf("Setting detached attribute failed");
		exit(EXIT_FAILURE);
	}
	
	while(1){
		int clientfd;
		clen = sizeof(caddr);
		printf("Server waiting connection...\n");
		clientfd = accept(s_sockfd, (struct sockaddr *)&caddr, &clen);
		printf("in main thread,connection with %s:%d(fd = %d) builded\n",inet_ntoa(caddr.sin_addr),ntohs(caddr.sin_port),clientfd);
		/* Create a sub thread,call recv_func() */
		res = pthread_create(&recv_thread[c_num],&thread_attr,(void *)recv_func,(void *)&clientfd); 

		c_sockfd[c_num] = clientfd;
		//printf("c_sockfd[%d]:%d\n",c_num,c_sockfd[c_num]);
		c_num++;

		if(res != 0)
		{
			printf("Thread create error");
			exit(EXIT_FAILURE);
		}
		
	}
	/* Callback the attribute of thread */
	(void)pthread_attr_destroy(&thread_attr);
	return 0;
}
/* Receive function */
void *recv_func(int *_clientfd)
{
		
	int clientfd = *_clientfd;
	int i,j;
	int loc = -1;
	int n_recv; //received bit num	
	char recv_text[MAXSIZE],temp[MAXSIZE];
	for(j = 0; j < c_num; j++) //find current client's fd index in fd array
	{
		if(c_sockfd[j] == clientfd)
		{
			loc = j;
			break;
		}
			
	}
	//printf("in recv_func,clientfd:%d\n",clientfd); //for test
	while(1)
	{	/* To Receive message from client and get the address infomation */
		//printf("server waiting for receiving data...\n");
		n_recv = recv(clientfd,recv_text,MAXSIZE,0);
		if( n_recv == 0)  
		{
			printf("client(fd = %d) exit\n",clientfd);
			c_sockfd[loc] = -1;
			pthread_exit(NULL);
			
		}
		strcpy(temp,recv_text);
		sprintf(recv_text,"fd_%d:",clientfd);
		//strcpy(recv_text,name);
		//strcat(name,recv_text);
		strcat(recv_text,temp);
		printf("in subthread,recv text from client(fd = %d):%s\n",clientfd,recv_text);
		for(i = 0; i < c_num; i++)
		{
			if(c_sockfd[i] != clientfd && c_sockfd[i] != -1)
				if(send(c_sockfd[i],recv_text,MAXSIZE,0) == 0){
					printf("send to client %d(fd = %d) zero bit\n",i,c_sockfd[i]);
					perror("send");
				}
		}
	}
}

int main()
{
	build_socket();
	return 0;
}

