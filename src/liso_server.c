/******************************************************************************
* echo_server.c                                                               *
*                                                                             *
* Description: This file contains the C source code for an echo server.  The  *
*              server runs on a hard-coded port and simply write back anything*
*              sent to it by connected clients.  It does not support          *
*              concurrent clients.                                            *
*                                                                             *
* Authors: Athula Balachandran <abalacha@cs.cmu.edu>,                         *
*          Wolf Richter <wolf@cs.cmu.edu>                                     *
*                                                                             *
*******************************************************************************/

#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <parse.h>
#include <time.h>

#define ECHO_PORT 9999
#define BUF_SIZE 163840

int close_socket(int sock)
{
    if (close(sock))
    {
        fprintf(stderr, "Failed closing socket.\n");
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
    int sock, client_sock;
    int readret;
    socklen_t cli_size;
    struct sockaddr_in addr, cli_addr;
    
	char tot_recv_buf[BUF_SIZE];
	char recv_buf[BUF_SIZE], send_buf[BUF_SIZE];

    fprintf(stdout, "----- Liso Server -----\n");
    
    /* all networked programs must create a socket */
    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1)
    {
        fprintf(stderr, "Failed creating socket.\n");
        return EXIT_FAILURE;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(ECHO_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

	int optval = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
		fprintf(stderr, "Error setting SO_REUSEADDR option\n");
		close_socket(sock);
		return 1;
	}

    /* servers bind sockets to ports---notify the OS they accept connections */
    if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)))
    {
        close_socket(sock);
        fprintf(stderr, "Failed binding socket.\n");
        return EXIT_FAILURE;
    }

    if (listen(sock, 100))
    {
        close_socket(sock);
        fprintf(stderr, "Error listening on socket.\n");
        return EXIT_FAILURE;
    }

	fd_set fds, allfds;
	FD_ZERO(&allfds);
	FD_SET(sock, &allfds);
	int clients[FD_SETSIZE], client_cnt = 0;

	int fail_cnt = 0;
	int connected = 0;
    while (1)
    {
		struct timeval timeout;
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;

		fds = allfds;
		int ready_num = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
		printf("ready_num = %d\n", ready_num);

		if(ready_num == 0)
		{
			if(++fail_cnt == 5 && connected)break;
			continue;
		}
		
		fail_cnt = 0;
		if(FD_ISSET(sock, &fds))
		{
			connected = 1;
			cli_size = sizeof(cli_addr);
			if ((client_sock = accept(sock, (struct sockaddr *) &cli_addr, &cli_size)) == -1)
			{
				close(sock);
				fprintf(stderr, "Error accepting connection.\n");
				return EXIT_FAILURE;
			}
			clients[client_cnt++] = client_sock;
			FD_SET(client_sock, &allfds);
			printf("new connection: client %d\n", client_sock);
		}
		else
		{
			for(int i = 0; i < client_cnt; ++i)
			{
				client_sock = clients[i];
				if(FD_ISSET(client_sock, &fds))
				{
					printf("client %d is available\n", client_sock);

					memset(tot_recv_buf, 0, BUF_SIZE);
					readret = recv(client_sock, tot_recv_buf, BUF_SIZE, 0);

					if(readret == 0)
					{
						printf("client %d is off\n", client_sock);
						FD_CLR(client_sock, &allfds);
						continue;
					}

					int lst = 0;

					printf("readret=%d\n",readret);
					while(lst < readret)
					{
						// matching for "\r\n\r\n"
						int now = lst, cnt = 0;
						while(now<readret && cnt<4)
						{
							if(cnt==0 && tot_recv_buf[now]=='\r')++cnt;
							else if(cnt==1 && tot_recv_buf[now]=='\n')++cnt;
							else if(cnt==2 && tot_recv_buf[now]=='\r')++cnt;
							else if(cnt==3 && tot_recv_buf[now]=='\n')++cnt;
							else cnt=0;
							++now;
						}
						if(cnt<4)break;

						memset(recv_buf, 0, BUF_SIZE);
						memset(send_buf, 0, BUF_SIZE);
						memcpy(recv_buf, tot_recv_buf+lst, now-lst);
						printf("lst=%d now=%d\n",lst,now);
						lst=now;

						Request* req;
						req = parse(recv_buf, readret, client_sock);
						printf("recv_buf:\n%s\n", recv_buf);

						if(req != NULL)
						{
							printf("http_version = %s\n",req->http_version);
							printf("http_method = %s\n",req->http_method);
							printf("http_uri = %s\n",req->http_uri);
						}

						if (req == NULL)
						{
							// malformed
							sprintf(send_buf, "HTTP/1.1 400 Bad request\r\n\r\n");
						}
						else if (strcmp(req->http_version, "HTTP/1.1") != 0)
						{
							// not supported version
							sprintf(send_buf, "HTTP/1.1 505 HTTP Version not supported\r\n\r\n");
						}
						else if (strcmp(req->http_method, "GET") == 0 || strcmp(req->http_method, "HEAD") == 0)
						{
							if(strcmp(req->http_uri, "/") == 0)
							{
								sprintf(req->http_uri, "./static_site/index.html");
							}
							
							struct stat *file_stat = (struct stat *)malloc(sizeof(struct stat));
							if (stat(req->http_uri, file_stat) == -1)
							{
								// not found
								sprintf(send_buf, "HTTP/1.1 404 Not Found\r\n\r\n");
							}
							else
							{
								// found
								char date_now[1000], date_modified[1000];
								time_t now = time(0);

								struct tm *gmt_now = gmtime(&now);
								strftime(date_now, sizeof(date_now), "%a, %d %b %Y %H:%M:%S %Z", gmt_now);

								struct tm *gmt_modified = gmtime(&(file_stat->st_mtime));
								strftime(date_modified, sizeof(date_modified), "%a, %d %b %Y %H:%M:%S %Z", gmt_modified);

								sprintf(send_buf, 
										"HTTP/1.1 200 OK\r\n"
										"Server: Liso/1.0\r\n"
										"Date: %s\r\n"
										"Content-Length: %ld\r\n"
										"Content-Type: text/html\r\n"
										"Last-modified: %s\r\n"
										"Connection: keep-alive\r\n\r\n",
										date_now, (long)file_stat->st_size, date_modified);
								
								if (strcmp(req->http_method, "GET") == 0)
								{
									FILE *file = fopen(req->http_uri, "r");
									if (file == NULL)
									{
										close_socket(client_sock);
										close_socket(sock);
										fprintf(stderr, "Error opening file.\n");
										return EXIT_FAILURE;
									}

									char file_buf[BUF_SIZE];
									size_t read_size;
									while ((read_size = fread(file_buf, 1, BUF_SIZE, file)) > 0)
									{
										strcat(send_buf, file_buf);
									}
									fclose(file);
								}
							}
						}
						else if (strcmp(req->http_method, "POST") == 0)
						{
							// post
							sprintf(send_buf, "%s", recv_buf);
						}
						else
						{
							// not implemented
							sprintf(send_buf, "HTTP/1.1 501 Not Implemented\r\n\r\n");
						}

						printf("send_buf:\n%s\n", send_buf);
						int sendret;
						if ((sendret=send(client_sock, send_buf, strlen(send_buf), 0)) != strlen(send_buf))
						{
							close_socket(client_sock);
							close_socket(sock);
							fprintf(stderr, "Error sending to client.\n");
							return EXIT_FAILURE;
						}
//						printf("sendret=%d\n",sendret);

					}
				}
			}
		}
    }

	for(int i = 0; i < client_cnt; ++i)
		close_socket(clients[i]);
    close_socket(sock);

    return EXIT_SUCCESS;
}
