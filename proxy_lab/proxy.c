#include <stdio.h>
#include <time.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_THREAD 50
#define CACHE_SIZE 3

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


typedef struct Cache_file Cache_file;
struct Cache_file{
    char uri[MAXLINE];
    int valid;
    int length;
    Cache_file* prev;
    Cache_file* next;
};

sem_t LOG, CACHE;
Cache_file* CACHE_FILE;

void client_error(int fd, char *cause, char*errnum, char* shortmsg, char* longmsg);
int connect_to_endserver(char* hostname, int port);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
void format_log_entry(char *logstring, int conn_fd, char *uri, int size);
void* proxy(void* conn_fd);
int check_cache(char *uri);
void change_cache(char* uri, int length);


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
    sem_init(&LOG, 0, 1);
    sem_init(&CACHE, 0, 1);
    //cache opeartion
    CACHE_FILE = malloc(CACHE_SIZE*sizeof(Cache_file));
    for(int i = 0; i < CACHE_SIZE-1; ++i){
        CACHE_FILE[i].valid=-1; // -1 means not used
        if(i == 0){
            CACHE_FILE[i].prev = &CACHE_FILE[CACHE_SIZE-1];
            CACHE_FILE[CACHE_SIZE-1].next = &CACHE_FILE[i];
        }
        CACHE_FILE[i].next = &CACHE_FILE[i+1];
        CACHE_FILE[i+1].prev = &CACHE_FILE[i];
    }


    while(1){
        client_len = sizeof(client_addr);
        conn_fd = Accept(listen_fd, (SA* )&client_addr, &client_len);
        Getnameinfo((SA*)&client_addr, client_len, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        pthread_t tid;
        Pthread_create(&tid, NULL, proxy, &conn_fd);
    }
    //printf("%s", user_agent_hdr);
    return 0;
}

void* proxy(void* fd){
    // Io variables
    size_t n;
    int conn_fd = *((int *)fd);
    Pthread_detach(pthread_self());
    rio_t rio, server_rio; // server_rio to receive the bytes from end server
    char buf[MAXLINE];
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];


    Rio_readinitb(&rio, conn_fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, url, version);

    if(strcasecmp(method, "GET")){
        client_error(conn_fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return (void *)-1;
    }

    //cache operation
    int cached = -1, length = 0;
    char save_path[MAXLINE] = "./cache";


    char hostname[MAXLINE], pathname[MAXLINE], end_server_http_header[MAXLINE];
    int proxy_conn_fd, port;
    parse_uri(url, hostname, pathname, &port);
    build_http_header(end_server_http_header, hostname, pathname, port, &rio);
    strcat(save_path, pathname);
    cached = check_cache(save_path);
    if(cached){
        Cache_file* tmp = CACHE_FILE;
        P(&CACHE);
        for(; tmp->next != CACHE_FILE; tmp = tmp->next){
            if(!strcmp(tmp->uri, save_path)){
                FILE* cached_file = Fopen(save_path, "r");
                while((n=Fread(buf, sizeof(char), MAXLINE, cached_file)) > 0){
                    Rio_writen(conn_fd, buf, (int)n);
                }
                // Linked list change
                tmp->prev->next = tmp->next;
                tmp->next->prev = tmp->prev;
                CACHE_FILE->prev->next = tmp;
                CACHE_FILE->prev = tmp;
                tmp->next = CACHE_FILE;
                tmp->prev = CACHE_FILE->prev;
                CACHE_FILE = tmp;
                break;
            }
        }
        V(&CACHE);
    }else{


        FILE* save;
        printf("The pathname is %s\n", save_path);
        save = Fopen(save_path, "a");

        if((proxy_conn_fd = connect_to_endserver(hostname, port)) < 0){
            fprintf(stderr, "proxy connect error");
            return (void *)-2;
        }
        Rio_writen(proxy_conn_fd,end_server_http_header,strlen(end_server_http_header));
        char key[MAXLINE], value[MAXLINE], content_length[MAXLINE];
        Rio_readinitb(&server_rio, proxy_conn_fd);
        //Read the status line
        if((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0){
            Rio_writen(conn_fd, buf, strlen(buf));
            Fwrite(buf, sizeof(char), (int)n, save);
        }
        //Read and transmit the response header
        while((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0 && strcmp(buf, "\r\n")){
            sscanf(buf, "%s %s", key, value);
            if(!strcmp(key, "Content-length:")){
                strcpy(content_length, value);
                length = atoi(content_length);
            }
            Rio_writen(conn_fd, buf, strlen(buf));
            Fwrite(buf, sizeof(char), (int)n, save);
        }
        Rio_writen(conn_fd, buf, strlen(buf)); // write /r/n to the conn_fd
        Fwrite(buf, sizeof(char), (int)n, save);

        while((n = Rio_readlineb(&server_rio, buf, MAXBUF)) > 0){
            //printf("Proxy server receive %d bytes\n", (int)n);
            Rio_writen(conn_fd, buf, (int)n);
            Fwrite(buf, sizeof(char), (int)n, save);
        }
        Fclose(save);

        P(&CACHE);
        change_cache(save_path, length);
        V(&CACHE);
    }

    char log_string[MAXLINE];
    format_log_entry(log_string, conn_fd, url, length);

    //record log
    P(&LOG);
    FILE* log = Fopen("proxy.log", "a");
    Fwrite(log_string, sizeof(char), strlen(log_string), log);
    Fclose(log);
    V(&LOG);



    printf("%s", log_string);
    Close(conn_fd);
    return (void *)0;
}



void change_cache(char* uri, int length){
    struct Cache_file* tmp = CACHE_FILE;
    P(&CACHE);
    for(; tmp->next != CACHE_FILE; tmp = tmp->next){
        if(tmp->valid == -1){
            // Not used cache space
            strcpy(tmp->uri, uri);
            tmp->length = length;
            tmp->valid = 1;
            // Linked list change
            tmp->prev->next = tmp->next;
            tmp->next->prev = tmp->prev;
            CACHE_FILE->prev->next = tmp;
            CACHE_FILE->prev = tmp;
            tmp->next = CACHE_FILE;
            tmp->prev = CACHE_FILE->prev;
            CACHE_FILE = tmp;
            break;
        }
    }
    //Operation for the least recently used element
    if(tmp->valid == -1){
        //Not used
        strcpy(tmp->uri, uri);
        tmp->length = length;
        tmp->valid = 1;

        tmp->prev->next = tmp->next;
        tmp->next->prev = tmp->prev;
        CACHE_FILE->prev->next = tmp;
        CACHE_FILE->prev = tmp;
        tmp->next = CACHE_FILE;
        tmp->prev = CACHE_FILE->prev;
        CACHE_FILE = tmp;
    }else{
        //used and eviction
        remove(tmp->uri);
        strcpy(tmp->uri, uri);
        tmp->length = length;
        CACHE_FILE = tmp;
    }

    V(&CACHE);
}

int check_cache(char* uri){
    struct Cache_file* tmp = CACHE_FILE;
    P(&CACHE);
    for(; tmp->next !=CACHE_FILE; tmp = tmp->next){
        if(!strcmp(tmp->uri, uri)){
            V(&CACHE);
            return 1;
        }
    }
    if(!strcmp(tmp->uri, uri)) {
        V(&CACHE);
        return 1;
    }
    V(&CACHE);
    return 0;
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

void format_log_entry(char *logstring, int conn_fd, char *uri, int size){
    time_t now;
    char time_str[MAXLINE];
    // /unsigned long host;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
     struct sockaddr_in addr;
     socklen_t addr_size = sizeof(struct sockaddr_in);
     getpeername(conn_fd, (struct sockaddr *)&addr, &addr_size);
     char clientip[100];
     strcpy(clientip, inet_ntoa(addr.sin_addr));

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %s %s %d\n", time_str, clientip, uri, size);
    return;
}
