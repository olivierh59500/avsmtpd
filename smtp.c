
/*
 *   $Id: smtp.c,v 1.5 2003-02-23 12:00:59 alexd Exp $
 *
 *   $Log: smtp.c,v $
 *   Revision 1.5  2003-02-23 12:00:59  alexd
 *   smtp_putline: add flush, check for buffer len
 *
 *   Revision 1.4  2003/02/23 07:24:32  alexd
 *   replace sock_ I/O functions with vsock_
 *
 *   Revision 1.3  2003/02/22 18:39:59  alexd
 *   add dmalloc.h
 *   free malloc'ed memory after use
 *
 *   Revision 1.2  2003/02/17 01:55:37  alexd
 *   some lint cleanup
 *
 *   Revision 1.1  2003/02/17 01:22:48  alexd
 *   moved some functions to smtp.c sock.c
 *
 *
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include "log.h"
#include "sock.h"
#include "smtp.h"

#include "config.h"

#ifdef WITH_DMALLOC
#include "dmalloc.h"
#endif

#define DAY_MIN         (24 * HOUR_MIN) /* minutes in a day */
#define HOUR_MIN        60              /* minutes in an hour */
#define MIN_SEC         60              /* seconds in a minute */

char *mail_date ( time_t when ) { 

    static char buf[1500];
    char b2[10];
    char b3[10];

    struct tm *lt; 
    struct tm gmt; 
    int     gmtoff;

    gmt = *gmtime(&when); 
    lt = localtime(&when); 
    
    gmtoff = (lt->tm_hour - gmt.tm_hour) * HOUR_MIN + lt->tm_min - gmt.tm_min; 
    if (lt->tm_year < gmt.tm_year) 
        gmtoff -= DAY_MIN; 
    else if (lt->tm_year > gmt.tm_year) 
        gmtoff += DAY_MIN; 
    else if (lt->tm_yday < gmt.tm_yday) 
        gmtoff -= DAY_MIN; 
    else if (lt->tm_yday > gmt.tm_yday) 
        gmtoff += DAY_MIN; 
    if (lt->tm_sec <= gmt.tm_sec - MIN_SEC) 
        gmtoff -= 1; 
    else if (lt->tm_sec >= gmt.tm_sec + MIN_SEC) 
        gmtoff += 1;

    strftime( buf, sizeof(buf), "%a, %e %b %Y %H:%M:%S ", lt );
    debug("strftime. %s", buf);
    snprintf( b2, sizeof(b2), "%+03d%02d", (int) (gmtoff / HOUR_MIN),
                                       (int) (abs(gmtoff) % HOUR_MIN) );
    strftime( b3, sizeof(b3), " (%Z)", lt );

    strcat( buf , b2);
    strcat( buf , b3);

    return buf;

}

void free_smtp_resp( struct smtp_resp *resp ) {
    if ( resp != NULL  && resp->text != NULL) { 
        free( resp->text );
    }
}

void free_smtp_cmd ( struct smtp_cmd  *cmd ) {
    int i;

    if ( cmd->command != NULL )
        free( cmd->command );
    if ( cmd != NULL && cmd->argv != NULL ) {
        for ( i = 0; cmd->argv[i] != NULL ; i++) 
            free(cmd->argv[i]);
        free( cmd->argv);
    }
}

void free_mem_chunks( struct mem_chunk *root ) {
    struct mem_chunk *prev;
    while ( root != NULL ) {
        if ( root->size > 0 && root->b != NULL ) {
            free( root->b);
        }
        prev = root;
        root = root->next;
        free( prev );
    }
}

int smtp_readln( int s, char *buf, size_t len ) {

    char *buf_start = buf;
    char  c, lc;

    static char *bp;
    static char b[1500]; /* Typical packet size */
    static int  cnt = 0; /* number of read bytes in b */

    Perror("smtp_readln deprecated");

    while( len > 0 ) { /* still need more bytes */
        if ( cnt <= 0 ) { /* no data in internal buffer */

#if 0
            cnt = recv( s, b, sizeof(b), 0);
#else
            cnt = sock_read( s, b, sizeof(b), 120000 );
#endif
            if ( cnt < 0) { /* can't read */
                if ( errno == EINTR ) 
                    continue;
                debug("recv: %s", strerror(errno) );
                return cnt;
            }
            if ( cnt == 0 ) /* no data to read */
                return 0;
            debug("smtp_readln: recv %d bytes", cnt );
            /* b[cnt] = '\0'; */
            /* debug( "recv: <%s>", b); */
            bp = b; /* reset head to begin of b */
        }
        c = *bp; /* next char */ 

        if ( c == '\n' && lc == '\r') { 
            buf--; cnt--;
            *buf = '\0'; 
            bp++; 
            return buf - buf_start; 
        }


        if ( *bp == '\r' && cnt > 1 && len > 1 && *(bp+1) == '\n' ) {
            *buf = '\0';
            cnt -= 2;
            bp += 2;
            return buf - buf_start;
            
        }
        *buf = c; /* store to caller buffer */
        cnt--; len--; bp++; buf++; /* */
    }
/*    set_errno( EMSGSIZE ); */
    error("smtp_readln: MSG too big");
    return -1;
}


int smtp_putline( vsock_t *s, char *b ) {
    int rc;
    size_t len;

    if ( b == NULL ) {
        error( "smtp_putline: bad argument" );
        return -1;
    }

    len = strlen( b );

    debug("smtp_putline: <%s>", b);

    if ( len > 0 ) {
        rc = vsock_write( s, b, len);
        if ( rc < 0 ) {
            Perror("write");
            return rc;
        }
    }

    rc = vsock_write( s, "\r\n", 2);
    if ( rc < 0 ) {
        Perror("write");
        return rc;
    }
    vsock_oflush( s );
    return rc;
}

int smtp_printf( vsock_t *s, const char *fmt, ... ) {

    char b[1500];

    va_list ap;
    va_start(ap, fmt );
    

    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);

    return smtp_putline( s, b);
}

int smtp_putreply( vsock_t *s, int code, const char *text, int cont ) {

    char *b;
    size_t need;
    int rc;

    /* FIXME */
    need = strlen( text ) + 10;

    b = malloc( need );

    snprintf( b, need, "%d%c%s", code, cont ? '-' : ' '  ,text );

    debug("smtp_putreply: %s", b);
    
    rc = smtp_putline( s, b ) <= 0;
    free( b );
    return rc;
}


struct smtp_cmd *
smtp_readcmd( vsock_t *vsock ) {
    static struct smtp_cmd cmd;
    
    char *buf;
    char *p = buf;
    char *ap = buf;
    int  i, rc;

    bzero( &cmd, sizeof(struct smtp_cmd) );

    buf = smtp_get( vsock );
    if ( buf == NULL ) {
        return NULL;
    }

    p = buf;

    ap = strsep( &p , " \t");

    cmd.command = malloc( strlen(ap) + 1 );
    if ( cmd.command == NULL ) {
        Perror("malloc");
        free( buf );
        return NULL;
    }
    strcpy( cmd.command, ap );

    while( p != NULL && *p == ' ')
        p++;
    
    if ( p == NULL || *p == '\0' ) {
        free( buf );
        return &cmd;
    }

    i = sizeof( char *) * 10;

    cmd.argv = malloc( sizeof ( cmd.argv ) * 10);
    bzero( cmd.argv, 10 * sizeof ( cmd.argv )  );

    for ( i = 0;  p != NULL && (ap = strsep( &p, " \t")) != NULL ; i++ ) {
        if ( i > 9 ) { 
            error("too many args");
            break;
        }
        if ( ap == NULL )
            break;
        cmd.argv[i] = malloc( strlen(ap) + 1 );
        if ( cmd.argv[i] == NULL ) {
            Perror("malloc");
            free( buf );
            return NULL;
        }
        strcpy( cmd.argv[i], ap);

        while( p != NULL && *p == ' ')
            p++;

        cmd.argc++;

    }

    free( buf );
    return &cmd;
}


struct smtp_resp *
smtp_readreply( vsock_t *vsock ) {
    static struct smtp_resp resp;
    
    char *buf;
    char *p = buf;
    char *endp = NULL;
    char *code_start = NULL;
    int rc;

    bzero( &resp, sizeof(struct smtp_resp));

    buf = smtp_get( vsock );
    if ( buf == NULL ) {
        return NULL;
    }
    debug("smtp_readreply: %s", buf);

    p = buf;
    
    resp.code = 0;
    resp.text = NULL;

    resp.code = strtol( p, &endp, 10 );

    if ( endp == p ) {
        error("no digits found in smtp reply");
        free(buf);
        return NULL;
    }
    p = endp;
    
    if ( *p == '-' ) {
        resp.cont = 1;
    }
    
    if ( *p == ' ' || *p == '-' ) {
        *p = 0;
        p++; 
    }

    resp.text = malloc(strlen(p) + 1); 
    if ( resp.text == NULL ) {
        Perror("malloc");
        free(buf);
        return NULL;
    }
    strncpy( resp.text, p, strlen(p)  );
    resp.text[strlen(p)] = 0;

    debug( "response: code [%d] text [%s]", resp.code, resp.text);

    free(buf);
    return &resp;
}

int smtp_putcmd( vsock_t *s, struct smtp_cmd *cmd) {

    char *b;
    size_t need = 1;
    int i, rc;

    debug("smtp_putcmd: start");
    if ( cmd == NULL || cmd->command == NULL ) {
        error("smtp_putcmd: invalid argument");
        return -1;
    }

    need += strlen( cmd->command );
    for ( i = cmd->argc ; i; ) {
        i--;
        if ( cmd->argv[i] == NULL ) {
            error("smtp_putcmd: invalid argument");
            return -1;
        }
        need += strlen(cmd->argv[i]) + 1;
    }
    b = malloc( need );
    if ( b == NULL ) {
        Perror("malloc");
    }

    strcpy( b, cmd->command );

    for ( i = 0; i < cmd->argc; i++ ) {
        strcat( b, " ");
        strcat( b, cmd->argv[i] );
    }
    debug("smtp_putcmd: <%s>", b);
    rc = smtp_putline( s, b ) <= 0;

    free( b );
    return rc;
}

struct mem_chunk *
chunk_add(  void *buf, size_t len ) {
    struct mem_chunk *chunk = NULL;

    if( (chunk = malloc(sizeof(struct mem_chunk))) == NULL ) {
        error("chunk_add: malloc failed");
        return chunk;
    }

    bzero( chunk, sizeof(struct mem_chunk) );

    if ( len == 0 ) {
        chunk->b = "";
    }
    else { 
       chunk->b = malloc( len + 1 ); /* +1 for trailing \0 */
       if ( chunk->b == NULL ) {
           error("chunk_add: malloc failed");
           return NULL;
       }
       chunk->size = len;
       strncpy( chunk->b, buf, len + 1 ); 
    }
    return chunk;
}


struct mem_chunk *smtp_readdata( vsock_t *s) {
    char *buf = NULL;
    int rc;

    struct mem_chunk *root;
    struct mem_chunk *prev;

    while( 1 ) {
        buf = smtp_get( s );
        if ( buf == NULL ) {
            Perror( "smtp_readdata");
            return NULL;
        }
        rc = strlen(buf);
        debug( "smtp_readdata: %d %s", rc, buf );
        
        if ( rc  == 1 && buf[0] == '.' ) {
            debug( "root->b = <%s>", root->b);
            prev->next = NULL;
            free( buf );
            return root;
        }

        if ( root == NULL ) {
            root = chunk_add( buf, rc );
            prev = root;
        }
        else {
            prev->next = chunk_add( buf, rc );
            prev = prev->next;
        }
        free( buf );
    }
    free( buf );
    return root;
}

int smtp_putdata( vsock_t *s, struct mem_chunk *root)  {
    int rc;

    if ( root == NULL ) {
        debug("root == NULL");
    }
    while ( root != NULL ) {
        if ( root->b == NULL ) {
            debug("root->b == NULL");
            break;
        }
        debug("putdate: putline = <%s>", root->b);
        rc = smtp_putline( s, root->b );
        if ( rc < 0) {
            Perror( "smtp_putdata");
            return rc;
        }
        root = root->next;
    }
    smtp_putline( s, "." );
    return 0;
}
