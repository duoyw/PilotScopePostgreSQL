/*-------------------------------------------------------------------------
 *
 * http.h
 *	  prototypes for http.c.
 *
 *-------------------------------------------------------------------------
 */
#ifndef _HTTP_TCPCLIENT_
#define _HTTP_TCPCLIENT_
#include <netinet/in.h>

// http client
typedef struct _http_tcpclient{
	int 	socket;
	int 	remote_port;
	char 	remote_ip[16];
	struct sockaddr_in _addr; 
	int 	connected;
} http_tcpclient;

// global function
extern http_tcpclient t_client;
extern int init_http_conn();
extern int send_data(http_tcpclient *pclient,char *string_of_pilottransdata);
extern int recv_data(http_tcpclient* pclient,char* string_of_pilottransdata,char** response);

#endif