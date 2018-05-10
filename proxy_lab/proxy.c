#include <stdio.h>
#include <time.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void client_error(int fd, char *cause, char*errnum, char* shortmsg, char* longmsg);
int connect_to_endserver(char* hostname, int port);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void proxy(int conn_fd);


int main(int argc, char *argv[])
{
    if(argc != 2){
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int listen_fd, conn_fd;
    socklen_t client_len;
    struct sockaddr_storage client_addr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    if((listen_fd = open_listenfd(argv[1])) < 0){
        fprintf(stderr, "Open listen port error:");
        exit(EXIT_FAILURE);
    }
    while(1){
        client_len = sizeof(client_addr);
        conn_fd = Accept(listen_fd, (SA* )&client_addr, &client_len);
        Getnameinfo((SA*)&client_addr, client_len, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        proxy(conn_fd);
        Close(conn_fd);
    }

    //printf("%s", user_agent_hdr);
    return 0;
}

void proxy(int conn_fd){
    // Io variables
    size_t n;
    rio_t rio, server_rio; // server_rio to receive the bytes from end server
    char buf[MAXLINE];
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];


    Rio_readinitb(&rio, conn_fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, url, version);

    if(strcasecmp(method, "GET")){
        client_error(conn_fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }


    char hostname[MAXLINE], pathname[MAXLINE], end_server_http_header[MAXLINE];
    int proxy_conn_fd, port;
    parse_uri(url, hostname, pathname, &port);
    printf("Hostname: %s, port:%d, pathname: %s\n", hostname, port, pathname);
    build_http_header(end_server_http_header, hostname, pathname, port, &rio);

    if((proxy_conn_fd = connect_to_endserver(hostname, port)) < 0){
        fprintf(stderr, "proxy connect error");
        return;
    }

    Rio_writen(proxy_conn_fd,end_server_http_header,strlen(end_server_http_header));

    Rio_readinitb(&server_rio, proxy_conn_fd);
    while((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0){
        printf("Proxy server receive %d bytes\n", (int)n);
        Rio_writen(conn_fd, buf, strlen(buf));
    }

    return;

}

inline int connect_to_endserver(char* hostname, int port){
    char buf[100];
    sprintf(buf, "%d", port);
    return open_clientfd(hostname, buf);
}

void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio){
    char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];
    /*request line*/
    sprintf(request_hdr,requestlint_hdr_format,path);
    /*get other request header for client rio and change it */
    while(Rio_readlineb(client_rio,buf,MAXLINE)>0)
    {
        if(strcmp(buf,endof_hdr)==0) break;/*EOF*/

        if(!strncasecmp(buf,host_key,strlen(host_key)))/*Host:*/
        {
            strcpy(host_hdr,buf);
            continue;
        }

        if(!strncasecmp(buf,connection_key,strlen(connection_key))
                &&!strncasecmp(buf,proxy_connection_key,strlen(proxy_connection_key))
                &&!strncasecmp(buf,user_agent_key,strlen(user_agent_key)))
        {
            strcat(other_hdr,buf);
        }
    }
    if(strlen(host_hdr)==0)
    {
        sprintf(host_hdr,host_hdr_format,hostname);
    }
    sprintf(http_header,"%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            user_agent_hdr,
            other_hdr,
            endof_hdr);

    return ;
}

void parse_uri(char *uri,char *hostname,char *path,int *port){
    *port = 80;
    char* pos = strstr(uri,"//");

    pos = pos!=NULL? pos+2:uri;

    char*pos2 = strstr(pos,":");
    if(pos2!=NULL)
    {
        *pos2 = '\0';
        sscanf(pos,"%s",hostname);
        sscanf(pos2+1,"%d%s",port,path);
    }
    else
    {
        pos2 = strstr(pos,"/");
        if(pos2!=NULL)
        {
            *pos2 = '\0';
            sscanf(pos,"%s",hostname);
            *pos2 = '/';
            sscanf(pos2,"%s",path);
        }
        else
        {
            sscanf(pos,"%s",hostname);
        }
    }
    return;
}

void client_error(int fd, char *cause, char*errnum, char* shortmsg, char* longmsg){
    char buf[MAXLINE], body[MAXBUF];

    sprintf(body, "<html><title>Tiny error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n",body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The tiny web proxy</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s \r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\n\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
		      char *uri, int size){
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}
