/**
 * File:   net_tcp.c
 * Author: Kirill Scherba
 *
 * Created on March 29, 2015, 6:43 PM
 * Updated to use in libksmesh on May 06, 2015, 21:55
 * Adapted to use in libteonet on July 24, 2015, 11:56
 *
 * TCP client/server based on teonet event manager
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "ev_mgr.h"
#include "net_tcp.h"
#include "utils/rlutil.h"

#if M_ENAMBE_TCP

#define KSNET_TRY_PORT 1000
#define KSNET_IPV6_ENABLE

// Local functions
int ksnTcpServerStart(ksnTcpClass *kt, int *port);
void ksnTcpServerAccept(struct ev_loop *loop, ev_io *w, int revents);


// Subroutines
#define kev ((ksnetEvMgrClass*)kt->ke)
#define ev_ksnet_io_start(loop, ev, cb, fd, events) \
    ev_io_init (ev, cb, fd, events); \
    ev_io_start (loop, ev)
#define ev_ksnet_io_stop(loop, watcher) \
    ev_io_stop (loop, watcher)


/******************************************************************************/
/* TCP Client/Server module methods                                           */
/*                                                                            */
/******************************************************************************/

/**
 * Initialize the TCP client/server module
 *
 * @param ke Pointer to ksnetEvMgrClass
 * @return
 */
ksnTcpClass *ksnTcpInit(void *ke) {

    ksnTcpClass *kt = malloc(sizeof(ksnTcpClass));
    kt->ke = ke;
    kt->map = pblMapNewHashMap();

    return kt;
}

/**
 * Destroy the TCP client/server module
 *
 * @param kt
 */
void ksnTcpDestroy(ksnTcpClass *kt) {

    ksnetEvMgrClass *ke = kt->ke;
    
    // Stop TCP servers
    ksnTcpServerStopAll(kt);
    
    // Free servers and clients map
    pblMapFree(kt->map);
    
    // Free ksnTcpClass
    free(kt);
    ke->kt = NULL;
}

/**
 * Set callback to receive client data
 *
 * @param loop Event loop
 * @param w Server watcher
 * @param fd Client fd
 * @param ksnet_read_cb Data received callback
 * @oaram data User data will connected to client watcher
 */
struct ev_io *ksnTcpCb(
    struct ev_loop *loop,
    ev_ksnet_io *w,
    int fd,
    void (*ksnet_cb)(struct ev_loop *loop, ev_io *watcher, int revents),
    void* data) {

    // Define READ event (event when TCP server data is available for reading)
    struct ev_io *w_client = (ev_io*) malloc (sizeof(ev_io));
    w_client->data = data;
    ev_io_init (w_client, ksnet_cb, fd, EV_READ);
    ev_io_start (loop, w_client);
    
    // Add watcher to client map
    pblMapAdd(w->clients_map, &fd, sizeof(fd), &w_client, sizeof(*w_client));
    
    return w_client;
}

/**
 * Stop receive client data callback 
 * @param loop
 * @param watcher
 * @param close_fl
 */
void ksnTcpCbStop(struct ev_loop *loop, ev_io *watcher, int close_flg, int remove_flg) {
    
    if(close_flg) close(watcher->fd); // Close socket
    ev_io_stop(loop, watcher); // Stop watcher
    
    // Remove client from clients_map
    if(remove_flg) {
    int fd = ksnTcpGetServer(((ksnetEvMgrClass*)watcher->data)->kt, watcher->fd);
        if(fd) {
            size_t valueLength;
            ev_ksnet_io **w = pblMapGet(((ksnetEvMgrClass*)watcher->data)->kt->map, &fd, sizeof(fd), &valueLength);        
            pblMapRemove((*w)->clients_map, &watcher->fd, sizeof(watcher->fd), &valueLength);
        }
    }
    
    #ifdef DEBUG_KSNET
    ksnet_printf(&((ksnetEvMgrClass*)watcher->data)->ksn_cfg, DEBUG,
            "%sTCP Server:%s Stop client %d\n", 
            ANSI_MAGENTA, ANSI_NONE, watcher->fd);
    #endif

    free(watcher); // Free watchers memory
}

/******************************************************************************/
/* TCP Server methods                                                         */
/*                                                                            */
/******************************************************************************/

/**
 * Create TCP server and add it to Event manager
 *
 * @param kt [in] Pointer to ksnTcpClass
 * @param port [in] Servers port
 * @param ksnet_accept_cb Server Accept callback, calls when client is connected
 * @param data Some user data
 * @param port_created [out] Servers port may be changed if the input port is busy
 * 
 * @return Servers SD (socket descriptor) > 0 if success
 */
int ksnTcpServerCreate(
    ksnTcpClass *kt,
    int port,
    void (*ksnet_accept_cb) (struct ev_loop *loop, ev_ksnet_io *watcher, int revents, int fd),
    void *data,
    int *port_created) {

    #ifdef DEBUG_KSNET
    ksnet_printf(&((ksnetEvMgrClass*)kt->ke)->ksn_cfg, MESSAGE,
            "%sTCP Server:%s Create TCP server at port %d ...\n", 
            ANSI_MAGENTA, ANSI_NONE, port);
    #endif

    ev_ksnet_io *w_accept = (ev_ksnet_io*) malloc(sizeof(ev_ksnet_io));

    // Start TCP server
    int sd, server_port = port;
    if( (sd = ksnTcpServerStart(kt, &server_port)) > 0 ) {

        if(port_created != NULL) *port_created = server_port;

        // Initialize and start a watcher to accepts client requests
        w_accept->tcpServerAccept_cb = ksnTcpServerAccept; // TCP Server Accept callback
        w_accept->ksnet_cb = ksnet_accept_cb; // Users Server Accept callback
        w_accept->events = EV_READ; // Events
        w_accept->fd = sd; // TCP Server socket
        w_accept->data = data; // Some user data
        w_accept->io.data = kev;

        // Create clients map
        w_accept->clients_map = pblMapNewHashMap();

        // Start watcher
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wstrict-aliasing"
        ev_ksnet_io_start (kev->ev_loop, &w_accept->io,
                           w_accept->tcpServerAccept_cb, w_accept->fd, EV_READ);
        #pragma GCC diagnostic pop

        // Add server to TCP servers map
        pblMapAdd(kt->map, &sd, sizeof(sd), &w_accept, sizeof(ev_ksnet_io*));       
    }
    
    // Can't create server
    else { 
        if(port_created != NULL) *port_created = 0;
        free(w_accept);
        #ifdef DEBUG_KSNET
        ksnet_printf(&((ksnetEvMgrClass*)kt->ke)->ksn_cfg, ERROR_M,
                "%sTCP Server:%s Can't create TCP server at port %d\n", 
                ANSI_MAGENTA, ANSI_NONE, port);
        #endif
    }

    return sd;
}

/**
 * Stop and destroy TCP server
 * 
 * @param kt
 * @param sd
 * 
 * @return 
 */
void ksnTcpServerStop(ksnTcpClass *kt, int sd) {
    
    size_t valueLength;
    ev_ksnet_io **w_accept = pblMapGet(kt->map, &sd, sizeof(sd), &valueLength);
    if(w_accept != NULL) {
        
        // Stop and destroy all TCP server clients
        ksnTcpServerStopAllClients(kt, sd);
        
        // Free clients map
        pblMapFree((*w_accept)->clients_map);
        
        // Close socked and stop watcher
        close((*w_accept)->fd);
        ev_ksnet_io_stop (kev->ev_loop, &((*w_accept)->io));
        
        #ifdef DEBUG_KSNET
        ksnet_printf(&((ksnetEvMgrClass*)kt->ke)->ksn_cfg, MESSAGE,
                "%sTCP Server:%s Server fd %d was stopped\n", 
                ANSI_MAGENTA, ANSI_NONE, (*w_accept)->fd);
        #endif      
        
        free(*w_accept);
        pblMapRemove(kt->map, &sd, sizeof(sd), &valueLength);
    }
}

/**
 * Stop and destroy all TCP server clients
 * 
 * @param kt
 * @param sd
 * 
 * @return 
 */
void ksnTcpServerStopAllClients(ksnTcpClass *kt, int sd) {
    
    size_t valueLength;
    ev_ksnet_io **w_accept = pblMapGet(kt->map, &sd, sizeof(sd), &valueLength);
    if(w_accept != NULL) {
        
        // Stop and destroy all server clients
        PblIterator *it = pblMapIteratorNew((*w_accept)->clients_map);
        if(it != NULL) {
            while(pblIteratorHasNext(it)) {
                void *entry = pblIteratorNext(it);
                ev_io **w = (ev_io **) pblMapEntryValue(entry);  
                ksnTcpCbStop(kev->ev_loop, *w, 1, 0);
            }
            pblIteratorFree(it);
        }        
        
        pblMapClear((*w_accept)->clients_map);
    }
}

/**
 * Get server SD by clients SD
 * 
 * @param kt
 * @param sd
 * 
 * @return
 */
int ksnTcpGetServer(ksnTcpClass *kt, int sd) {
    
    int server_sd = 0;
    
    PblIterator *it = pblMapIteratorNew(kt->map);
    if(it != NULL) {
        while(pblIteratorHasNext(it)) {
            void *entry = pblIteratorNext(it);
            ev_ksnet_io **w = (ev_ksnet_io **) pblMapEntryValue(entry); 
            size_t valueLength;
            if(pblMapGet((*w)->clients_map, &sd, sizeof(sd), &valueLength) != NULL) {
                server_sd = (*w)->fd;
                break;
            }
        }
        pblIteratorFree(it);
    }
    
    return server_sd;
}

/**
 * Stop and destroy all TCP servers
 * 
 * @param kt
 */
void ksnTcpServerStopAll(ksnTcpClass *kt) {
    
    int stop = 0;
    do {
        PblIterator *it = pblMapIteratorNew(kt->map);
        if(it != NULL) {
            if(pblIteratorHasNext(it)) {
                void *entry = pblIteratorNext(it);
                int *sd = (int *) pblMapEntryKey(entry);            
                ksnTcpServerStop(kt, *sd);
            } else stop = 1;
            pblIteratorFree(it);
        } else stop = 1;
    } while(!stop);
}

/**
 * Create TCP server socket, bind to port and start listen in
 *
 * @param port [in/out] Servers port, it may be changed if selected port is busy
 * 
 * @return
 */
int ksnTcpServerStart(ksnTcpClass *kt, int *port) {

    int sd = 0;

    #ifdef KSNET_IPV6_ENABLE
    struct sockaddr_in6 addr;
    #else
    struct sockaddr_in addr;
    #endif

    int try_port = KSNET_TRY_PORT;
    while(try_port--) {

        /***********************************************************************/
        /* The socket() function returns a socket descriptor, which represents */
        /* an endpoint.  Get a socket for address family AF_INET6 to           */
        /* prepare to accept incoming connections on IPv6 and IPv4.            */
        /***********************************************************************/
        #ifdef KSNET_IPV6_ENABLE
        if ((sd = socket(AF_INET6, SOCK_STREAM, 0)) < 0) {
            ksnet_printf(&kev->ksn_cfg, ERROR_M, 
                    "%sTCP Server:%s socket error\n", 
                    ANSI_MAGENTA, ANSI_NONE);
            return -1;
        }
        #else
        // Create IPv4 only server socket
        if( (sd = socket(PF_INET, SOCK_STREAM, 0)) < 0 )
        {
            ksnet_printf(ERROR, "%sTCP Server:%s: socket error", 
                    ANSI_MAGENTA, ANSI_NONE);
            return -1;
        }
        #endif

        // Set server socket options
        int yes = 1;
        if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
            // error handling
            ksnet_printf(&kev->ksn_cfg, ERROR_M,
                    "%sTCP Server:%s can't set socket options\n", 
                    ANSI_MAGENTA, ANSI_NONE);
            return -1;
        }

        #ifdef KSNET_IPV6_ENABLE
        bzero(&addr, sizeof(addr));
        addr.sin6_flowinfo = 0;
        addr.sin6_family = AF_INET6;
        addr.sin6_port   = htons(*port);
        addr.sin6_addr = in6addr_any;
        #else
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        #endif

        // Bind socket to address
        if (bind(sd, (struct sockaddr*) &addr, sizeof(addr)) != 0) {
            ksnet_printf(&kev->ksn_cfg, ERROR_M,
                    "%sTCP Server:%s Can't bind on port %d, "
                    "try next port number ...%s\n", 
                    ANSI_MAGENTA, ANSI_GREY, *port, ANSI_NONE);
            close(sd);
            if(try_port) (*port)++;
        }
        else break;
    }

    // Start listing on the socket
    if (listen(sd, 2) < 0) {
        ksnet_printf(&kev->ksn_cfg, ERROR_M,
                "%sTCP Server:%s Listen on port %d error\n", 
                ANSI_MAGENTA, ANSI_NONE, *port);
        return -1;
    }

    // Set non block mode
    set_nonblock(sd);

    // Server welcome message
    #ifdef DEBUG_KSNET
    ksnet_printf(&kev->ksn_cfg, MESSAGE,
            "%sTCP Server:%s Start listen at port %d, socket fd %d\n", 
            ANSI_MAGENTA, ANSI_NONE, *port, sd);
    #endif

    return sd;
}

/**
 * Accept TCP server client requests callback
 *
 * @param loop
 * @param watcher
 * @param revents
 */
void ksnTcpServerAccept(struct ev_loop *loop, ev_io *w, int revents) {

    ev_ksnet_io *watcher = (ev_ksnet_io *) w;
    ksnetEvMgrClass *ke = (ksnetEvMgrClass *) watcher->io.data;

    // Check event
    if(EV_ERROR & revents) {

        // Close server signal got
        if(revents == EV_ERROR + EV_READ + EV_WRITE) {

            #ifdef DEBUG_KSNET
            ksnet_printf(&ke->ksn_cfg, DEBUG,
                    "%sTCP Server:%s accept, server with FD %d was closed\n", 
                    ANSI_MAGENTA, ANSI_NONE, w->fd);
            #endif

            // Free watcher
            //close(w->fd);
            free(w);
        }
        else {

            #ifdef DEBUG_KSNET
            ksnet_printf(&ke->ksn_cfg, DEBUG,
                    "%sTCP Server:%s accept, got invalid event: 0x%04X\n", 
                    ANSI_MAGENTA, ANSI_NONE, revents);
            #endif
        }

        return;
    }

    // Define sockaddr structure
    #ifdef KSNET_IPV6_ENABLE
    struct sockaddr_in6 client_addr;
    #else
    struct sockaddr client_addr;
    #endif
    socklen_t client_len = sizeof(client_addr);

    // Accept client request
    int client_sd = accept(watcher->io.fd, (struct sockaddr *)&client_addr, &client_len);
    if (client_sd < 0) {
        ksnet_printf(&ke->ksn_cfg, ERROR_M, 
                "%sTCP Server:%s accept, accept error\n",
                ANSI_MAGENTA, ANSI_NONE);
        return;
    }

    // Increment total_clients count
//    total_clients ++;

    // Get Client IPv4 & port number
    int client_family = AF_INET;
    char client_ip[100];
    struct sockaddr_in *addr = (struct sockaddr_in*) &client_addr;
    #ifndef KSNET_IPV6_ENABLE
    strcpy(client_ip, inet_ntoa(addr->sin_addr))
    printf("IP address is: %s\n", client_ip);
    #endif
    int client_port = (int) ntohs(addr->sin_port);
    #ifdef DEBUG_KSNET
    ksnet_printf(&ke->ksn_cfg, DEBUG,
            "%sTCP Server:%s Client port is: %d\n", 
            ANSI_MAGENTA, ANSI_NONE, client_port);
    #endif

    // Sockets Layer Call: inet_ntop()
    #ifdef KSNET_IPV6_ENABLE
    inet_ntop(AF_INET6, &(client_addr.sin6_addr), client_ip, sizeof(client_ip));
    // Check client inet family
    #define IPV4_PREF "::ffff:"
    if(strstr(client_ip, IPV4_PREF) == client_ip) {
        memmove(client_ip, client_ip + sizeof(IPV4_PREF) - 1,
                strlen(client_ip) - (sizeof(IPV4_PREF) - 1) + 1 );
        #ifdef DEBUG_KSNET
        ksnet_printf(&ke->ksn_cfg, DEBUG,
                "%sTCP Server:%s Client IP address is: %s\n", 
                ANSI_MAGENTA, ANSI_NONE, client_ip);
        #endif
    } else {
        client_family = AF_INET6;
        #ifdef DEBUG_KSNET
        ksnet_printf(&ke->ksn_cfg, DEBUG,
                "%sTCP Server:%s Client IPv6 address is: %s\n", 
                ANSI_MAGENTA, ANSI_NONE, client_ip);
        #endif
    }
    #endif

    #ifdef DEBUG_KSNET
    ksnet_printf(
        &ke->ksn_cfg,
        DEBUG,
        "%sTCP Server:%s Successfully connected with client (%d), from IP%s: %s.\n",
        ANSI_MAGENTA, ANSI_NONE, 
        client_sd, client_family == AF_INET6 ? "v6" : "" , client_ip
    );
    //ksnet_printf(&ke->ksn_cfg, DEBUG, "TCP Server: %d client(s) connected.\n", total_clients);
    #endif

    // Execute ksnet callback (to Initialize and start watcher to read Servers client requests)
    if(watcher->ksnet_cb != NULL)
        watcher->ksnet_cb(loop, watcher, revents, client_sd);

    //! \todo: Add client to map
//    pblMapAdd(watcher->clients_map, &client_sd, sizeof(client_sd),)
//    w->fd; // Server sd
//    client_sd; // Client sd
    
    // Add socket FD to the Event manager FD list
    //ksnet_EventMgrAddFd(client_sd);
}


/******************************************************************************/
/* TCP Client methods                                                         */
/*                                                                            */
/******************************************************************************/

/**
 * Create TCP client and connect to server
 * 
 * @param kt Pointer to the ksnTcpClass
 * @param port Server port
 * @param server Server IP or name
 * 
 * @return Socket description: > 0 - success connection
 */
int ksnTcpClientCreate(ksnTcpClass *kt, int port, const char *server) {

    /* Variable and structure definitions. */
    int sd, rc;
    struct sockaddr_in serveraddr;
    struct hostent *hostp;

    /* The socket() function returns a socket */
    /* descriptor representing an endpoint. */
    /* The statement also identifies that the */
    /* INET (Internet Protocol) address family */
    /* with the TCP transport (SOCK_STREAM) */
    /* will be used for this socket. */
    /******************************************/
    /* get a socket descriptor */
    if((sd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    
        ksnet_printf(&kev->ksn_cfg, ERROR_M, 
                "%sTCP client:%s Client-socket() error\n", 
                ANSI_LIGHTMAGENTA, ANSI_NONE);
        return(-1);
    }
    else {
        #ifdef DEBUG_KSNET
        ksnet_printf(&kev->ksn_cfg, DEBUG, 
                "%sTCP client:%s Client-socket() OK\n", 
                ANSI_LIGHTMAGENTA, ANSI_NONE);
        #endif
    }

    #ifdef DEBUG_KSNET
    ksnet_printf(&kev->ksn_cfg, DEBUG,
            "%sTCP client:%s Connecting to the f***ing %s, port %d ...\n", 
            ANSI_LIGHTMAGENTA, ANSI_NONE, server, port);
    #endif

    memset(&serveraddr, 0x00, sizeof(struct sockaddr_in));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(port);

    if((serveraddr.sin_addr.s_addr = inet_addr(server)) == (unsigned long) INADDR_NONE) {  

        /* When passing the host name of the server as a */
        /* parameter to this program, use the gethostbyname() */
        /* function to retrieve the address of the host server. */
        /***************************************************/
        /* get host address */
        hostp = gethostbyname(server);
        if(hostp == (struct hostent *)NULL) {
        
            ksnet_printf(&kev->ksn_cfg, ERROR_M,
                    "%sTCP client:%s HOST NOT FOUND --> h_errno = %d\n", 
                    ANSI_LIGHTMAGENTA, ANSI_NONE, h_errno);
            close(sd);
            return(-2);
        }
        memcpy(&serveraddr.sin_addr, hostp->h_addr, sizeof(serveraddr.sin_addr));
    }

    /* After the socket descriptor is received, the */
    /* connect() function is used to establish a */
    /* connection to the server. */
    /***********************************************/
    /* connect() to server. */
    if((rc = connect(sd, (struct sockaddr *)&serveraddr, sizeof(serveraddr))) < 0) {
    
        ksnet_printf(&kev->ksn_cfg, ERROR_M, 
                "%sTCP client:%s Client-connect() error\n", 
                ANSI_LIGHTMAGENTA, ANSI_NONE);
        close(sd);
        return(-3);
    }
    else {
        #ifdef DEBUG_KSNET
        ksnet_printf(&kev->ksn_cfg, DEBUG, 
                "%sTCP client:%s Connection established...\n", 
                ANSI_LIGHTMAGENTA, ANSI_NONE);
        #endif
    }

    // Set non block mode
    set_nonblock(sd);

    return sd;
}

#endif
