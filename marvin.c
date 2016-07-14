/*
    This is the marvin chat client
    Hassan Tahir
*/

#include <stdio.h>
#include <stdlib.h> //for itoa
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include "util.h"
#include "parse.h"
#include "chatsvr.h"

struct client {
    int fd;
    char buf[MAXMESSAGE+16];  /* partial line(s) */
    int bytes_in_buf;  /* how many data bytes in buf (after nextpos) */
    char *nextpos;  /* if non-NULL, move this down to buf[0] before reading */
};

void setupSocket(char *hostname, int port, struct client *p);
char *myreadline(struct client *p);

int main(int argc, char **argv) {
    struct client p;
    int op=0; // For select return value
    char *msg;
    char reply[MAXMESSAGE]="";
    fd_set fdlist;
    
    if (argc > 3 || argc <= 1) {
        fprintf(stderr, "usage: %s host [port]\n", argv[0]);
        return(1);
    }
    
    /* SOCKET Connection */
    setupSocket(argv[1], atoi(argv[2]) ? atoi(argv[2]) : 1234, &p);
    
    /* Name of bot */
    write(p.fd, "Marvin\r\n", 8);
    
    /* clinet struct property for reading */
    p.bytes_in_buf = 0;
    p.nextpos = NULL;
    
    /* CHATSVR ID Verify */
    
    while (strcmp(myreadline(&p), CHATSVR_ID_STRING)) {
        if (p.bytes_in_buf > sizeof CHATSVR_ID_STRING) {
            fprintf(stderr, "Invalid CHATSVR ID String\n");
            return(1);
        }
    }
    
    int maxfd = p.fd;
    do
    {
        if( op == -1 ) {
            perror("select");
            exit(1);
        }
        
        if (op && FD_ISSET(0, &fdlist)) {
            fgets(reply, MAXMESSAGE-2, stdin);
            write(p.fd, reply, strlen(reply));
        }
        
        if(op && FD_ISSET(p.fd, &fdlist)) {
            if((msg = myreadline(&p))) {
                char *temp=strchr (msg, ':');
                strncpy(reply, "Hey ", 4);
                if(temp){
                    strncat( reply, msg, ( temp - msg ));
                    temp=strstr(temp, "Hey Marvin,");
                    if(temp) {
                        temp=strchr (msg, ',');
                        temp++;
                        struct expr *e = parse(temp);
                        if (e) {
                            strncpy(reply, ", ", 2);
                            int length = snprintf( NULL, 0, "%d", evalexpr(e) );
                            char* result = malloc( length + 1 );
                            snprintf(result, length + 1, "%d", evalexpr(e) );
                            strncpy(reply, result, length + 1);
                            free(result);
                            freeexpr(e);
                        } else {
                           strncat( reply, ", I don't like that.\r\n", 21);
                        }
                        write(p.fd, reply, strlen(reply));
                    }
                }
                printf("%s\n",msg);
            }
        }
        
        FD_ZERO(&fdlist);
        FD_SET(p.fd, &fdlist);
        FD_SET(0, &fdlist);
    } while ((op = select(maxfd+1, &fdlist, NULL, NULL, NULL)));  

    return(0);
}

void setupSocket(char *hostname, int port, struct client *p) {
    struct hostent *hp;
    struct sockaddr_in r;
    
    /* Init SOCKET */
    
    if ((p->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");  
        exit(1);
    }
    
    /* HOSTNAME Lookup */
    if ((hp = gethostbyname(hostname)) == NULL) {
        fprintf(stderr, "%s: no such host\n", hostname);
        exit(1);
    }
    if (hp->h_addr_list[0] == NULL || hp->h_addrtype != AF_INET) {
        fprintf(stderr, "%s: not an internet protocol host name\n", hostname);
        exit(1);
    }

    /* SOCKET Properties */
    memset(&r, '\0', sizeof r);
    memcpy(&r.sin_addr, hp->h_addr_list[0], hp->h_length);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(port);
    
    /* SOCKET Connect */
    if (connect(p->fd, (struct sockaddr *)&r, sizeof r)) {
        perror("connect");
        exit(1);
    }
}

char *myreadline(struct client *p) {
    int nbytes;

    /* move the leftover data to the beginning of buf */
    if (p->bytes_in_buf && p->nextpos)
	memmove(p->buf, p->nextpos, p->bytes_in_buf);

    /* If we've already got another whole line, return it without a read() */
    if ((p->nextpos = extractline(p->buf, p->bytes_in_buf))) {
	p->bytes_in_buf -= (p->nextpos - p->buf);
	return(p->buf);
    }

    /*
     * Ok, try a read().  Note that we _never_ fill the buffer, so that there's
     * always room for a \0.
     */
    nbytes = read(p->fd, p->buf + p->bytes_in_buf, sizeof p->buf - p->bytes_in_buf - 1);
    if (nbytes < 0) {
	    perror("read()");
    } else if (nbytes > 0) {

	   p->bytes_in_buf += nbytes;

        /* So, _now_ do we have a whole line? */
        if ((p->nextpos = extractline(p->buf, p->bytes_in_buf))) {
            p->bytes_in_buf -= (p->nextpos - p->buf);
            return(p->buf);
        }

        /*
         * Don't do another read(), to avoid the possibility of blocking.
         * However, if we've hit the maximum message size, we should call
         * it all a line.
         */
        if (p->bytes_in_buf >= MAXMESSAGE) {
            p->buf[p->bytes_in_buf] = '\0';
            p->bytes_in_buf = 0;
            p->nextpos = NULL;
            return(p->buf);
        }

    }

    /* If we get to here, we don't have a full input line yet. */
    return(NULL);
}