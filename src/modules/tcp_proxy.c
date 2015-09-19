/** 
 * \file   tcp_proxy.c
 * \author Kirill Scherba <kirill@scherba.ru>
 * 
 * # Teonet TCP Proxy module
 * 
 * See example: 
 * 
 * See test: test_tcp_proxy.c
 *
 * *Created on September 8, 2015, 1:59 AM*
 * 
 * ### Description:
 * 
 */

#include <stdlib.h>
#include <string.h>

#include "tcp_proxy.h"

#include "ev_mgr.h"
#include "net_core.h"
#include "net_tr-udp_.h"
#include "utils/rlutil.h"

// Local function definition
int ksnTCPProxyServerStart(ksnTCPProxyClass *tp);
void ksnTCPProxyServerStop(ksnTCPProxyClass *tp);
void ksnTCPProxyServerClientConnect(ksnTCPProxyClass *tp, int fd);
void ksnTCPProxyServerClientDisconnect(ksnTCPProxyClass *tp, int fd, 
        int remove_f);
int ksnTCPProxyPackageProcess(ksnTCPProxyClass *tp, void *data, size_t data_len);

/**
 * Pointer to ksnetEvMgrClass
 * 
 */
#define kev ((ksnetEvMgrClass*)tp->ke)

#define TCP_PROXY_VERSION 0 ///< TCP Proxy version

// Initialize / Destroy functions ---------------------------------------------

/**
 * Initialize TCP Proxy module 
 * 
 * @param ke Pointer to the ksnTCPProxyClass
 * 
 * @return Pointer to ksnTCPProxyClass
 */
ksnTCPProxyClass *ksnTCPProxyInit(void *ke) {
    
    ksnTCPProxyClass *tp = malloc(sizeof(ksnTCPProxyClass));
    tp->map = pblMapNewHashMap();
    tp->ke = ke;
    tp->fd = 0;  
    tp->fd_client = 0; 
    
    // Initialize input packet buffer parameters
    tp->packet.ptr = 0; // Pointer to data end in packet buffer
    tp->packet.length = 0; // Length of received packet
    tp->packet.stage = WAIT_FOR_START; // Stage of receiving packet
    tp->packet.header = (ksnTCPProxyHeader*) tp->packet.buffer; // Pointer to packet header
    
    // \todo Start TCP proxy client
    ksnTCPProxyClientConnetc(tp);
    
    // Start TCP proxy server
    ksnTCPProxyServerStart(tp);
    
    return tp;
}

/**
 * Destroy TCP Proxy module
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * 
 */
void ksnTCPProxyDestroy(ksnTCPProxyClass *tp) {
    
    if(tp != NULL) {
        ksnTCPProxyServerStop(tp); // Stop TCP Proxy server
        pblMapFree(tp->map); // Free clients map
        free(tp); // Free class memory
    }
}

// Common functions -----------------------------------------------------------

/**
 * Calculate checksum
 * 
 * Calculate byte checksum in data buffer
 * 
 * @param data Pointer to data buffer
 * @param data_length Length of the data buffer to calculate checksum
 * 
 * @return Byte checksum of the input buffer
 */
uint8_t ksnTCPProxyChecksumCalculate(void *data, size_t data_length) {
    
    int i;
    uint8_t *ch, checksum = 0;
    for(i = 0; i < data_length; i++) {
        ch = (uint8_t*)(data + i);
        checksum += *ch;
    }
    
    return checksum;
}

//! \file   tcp_proxy.c
//! * Client functions: 

//! \file   tcp_proxy.c
//!     * Connect to TCP Proxy server: ksnTCPProxyConnetc()

/**
 * TCP Proxy client callback
 * 
 * Get packet from TCP Proxy server connection and resend it to read host callback
 * 
 * @param loop Event manager loop
 * @param w Pointer to watcher
 * @param revents Events
 * 
 */
void cmd_tcppc_read_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    
    // \todo TCP Proxy client callback
    
}

/**
 * Connect to TCP Proxy server
 * 
 * Get address and port from teonet configuration and connect to R-Host TCP 
 * Server 
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * 
 * @return 0 - Successfully connected
 */
int ksnTCPProxyClientConnetc(ksnTCPProxyClass *tp) {
   
    // Get address and port from teonet config
    if(kev->ksn_cfg.r_tcp_f) {
    
        // Connect to R-Host TCP Server
        int fd_client = ksnTcpClientCreate(kev->kt, 
                kev->ksn_cfg.r_tcp_port, // Remote host TCP port number
                kev->ksn_cfg.r_host_addr // Remote host internet address
        );
        
        if(fd_client > 0) {

            // Register connection
            tp->fd_client = fd_client;

            // \todo TCP Proxy protocol connect
            
            // Create and start TCP Proxy client watcher
            ev_init (&tp->w_client, cmd_tcppc_read_cb);
            ev_io_set (&tp->w_client, fd_client, EV_READ);
            tp->w_client.data = tp;
            ev_io_start (kev->ev_loop, &tp->w_client);
        }
    }
    
    return 0;
}

/**
 * Create TCP Proxy package
 * 
 * Create TCP Proxy package from peers UDP address and port, data buffer and 
 * its length
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * @param buffer The buffer to create package in
 * @param buffer_length Package data length
 * @param addr String with peer UDP address
 * @param port UDP port number
 * @param data Package data
 * @param data_length Package data length
 * 
 * @return Return size of created package or error
 *  
 * @retval > 0 - size of created package
 * @retval -1 - error: The output buffer less than packet header
 * @retval -2 - error: The output buffer less than packet header + data
 */
size_t ksnTCPProxyPackageCreate(ksnTCPProxyClass *tp, void *buffer, 
        size_t buffer_length, const char *addr, int port, const void *data, 
        size_t data_length) { 
    
    size_t tcp_package_length;
    
    if(buffer_length >= sizeof(ksnTCPProxyHeader)) {
        
        ksnTCPProxyHeader *th = (ksnTCPProxyHeader*)buffer;

        th->version = TCP_PROXY_VERSION; // TCP Proxy protocol version 
        th->command = CMD_TCPP_PROXY; // TCP Proxy protocol command
        th->addr_length = strlen(addr) + 1; // Address string length
        th->port = port; // UDP port number
        th->packet_length = data_length; // Package data length   
        th->packet_checksum = ksnTCPProxyChecksumCalculate((void*)th + 1, 
                sizeof(ksnTCPProxyHeader) - 2);
        
        size_t p_length = sizeof(ksnTCPProxyHeader) + th->addr_length + data_length;        
        if(buffer_length >= p_length) {
            
            tcp_package_length = p_length;
            memcpy(buffer + sizeof(ksnTCPProxyHeader), addr, th->addr_length); // Address string
            memcpy(buffer + sizeof(ksnTCPProxyHeader) + th->addr_length, data, data_length); // Package data        
            th->checksum = ksnTCPProxyChecksumCalculate(buffer + 1, tcp_package_length - 1); // Package data length
        } 
        else tcp_package_length = -2; // Error code: The output buffer less than packet data + header
    }
    else tcp_package_length = -1; // Error code: The output buffer less than packet header
    
    return tcp_package_length;
}

//! \file   tcp_proxy.c
//! * Server functions: 

/**
 * UDP client/server Proxy callback
 * 
 * Get packet from UDP Proxy client connection and resend it to TCP Proxy
 * 
 * @param loop Event manager loop
 * @param w Pointer to watcher
 * @param revents Events
 * 
 */
void cmd_udpp_read_cb(struct ev_loop *loop, struct ev_io *w, int revents) {
    
    struct sockaddr_in remaddr; // Remote address
    socklen_t addrlen = sizeof(remaddr); // Length of addresses
    size_t data_len = KSN_BUFFER_DB_SIZE; // Buffer length
    ksnTCPProxyClass *tp = w->data; // Pointer to ksnTCPProxyClass
    char data[data_len]; // Buffer
    const int flags = 0; // Flags

    // Get UDP data
    ssize_t received = recvfrom(w->fd, data, data_len, flags, 
            (struct sockaddr *)&remaddr, &addrlen);
    
    #ifdef DEBUG_KSNET
    ksnet_printf(&kev->ksn_cfg, DEBUG, // \todo Change type to DEBUG_VV after debugging
            "%sTCP Proxy:%s "
            "Got something from UDP fd %d w->events = %d, received = %d ...\n", 
            ANSI_YELLOW, ANSI_NONE, w->fd, w->events, (int)received);
    #endif
  
    // \todo Resend UDP packet to TCP Proxy
    if(received > 0) {
        
        size_t data_send_len = received + sizeof(ksnTCPProxyHeader);
        char data_send[data_send_len];
        
        // Create TCP package
        size_t pl = ksnTCPProxyPackageCreate(tp, data_send, data_send_len, 
                inet_ntoa(((struct sockaddr_in *) &remaddr)->sin_addr), 
                ntohs(((struct sockaddr_in *) &remaddr)->sin_port), 
                data, data_len);
        
        // Get TCP fd from tcp proxy map
        size_t valueLength;
        ksnTCPProxyData* tpd = pblMapGet(tp->map, &w->fd, sizeof(w->fd), &valueLength);
        if(tpd != NULL) {   
        
            // Send TCP package
            ssize_t rv = write(tpd->tcp_proxy_fd, data_send, pl);
            
            // Check write to TCP Proxy client error
            if(rv != pl) {
                
                // Show write to TCP Proxy client error
                #ifdef DEBUG_KSNET
                ksnet_printf(&kev->ksn_cfg, DEBUG,
                    "%sTCP Proxy:%s "
                    "Send to TCP client error, rv: %d %s\n", 
                    ANSI_YELLOW, ANSI_RED, pl, ANSI_NONE);
                #endif
            } 
            
            // Show successfully write message
            else {
                
                #ifdef DEBUG_KSNET
                ksnet_printf(&kev->ksn_cfg, DEBUG, // \todo Change type to DEBUG_VV after debugging
                    "%sTCP Proxy:%s "
                    "Resent UDP packet to TCP client, packet size: %d bytes\n", 
                    ANSI_YELLOW, ANSI_NONE, pl);
                #endif
            }
        }
    }    
}

/**
 * TCP Proxy server client callback
 * 
 * Get packet from TCP Proxy server client connection and resend it to UDP Proxy
 * 
 * @param loop Event manager loop
 * @param w Pointer to watcher
 * @param revents Events
 * 
 */
void cmd_tcpp_read_cb(struct ev_loop *loop, struct ev_io *w, int revents) {

    size_t data_len = KSN_BUFFER_SIZE; // Buffer length
    ksnTCPProxyClass *tp = w->data; // Pointer to ksnTCPProxyClass
    char data[data_len]; // Buffer
    
    // Read TCP data
    ssize_t received = read(w->fd, data, data_len);
    #ifdef DEBUG_KSNET
    ksnet_printf(&kev->ksn_cfg, DEBUG, // \todo Change type to DEBUG_VV after debugging
            "%sTCP Proxy:%s "
            "Got something from fd %d w->events = %d, received = %d ...\n", 
            ANSI_YELLOW, ANSI_NONE, w->fd, w->events, (int)received);
    #endif

    // Disconnect client:
    // Close UDP and TCP connections and Remove data from TCP Proxy Clients map
    if(!received) {        
        
        #ifdef DEBUG_KSNET
        ksnet_printf(
            &kev->ksn_cfg , DEBUG,
            "%sTCP Proxy:%s "
            "Connection closed. Stop listening fd %d\n",
            ANSI_YELLOW, ANSI_NONE, w->fd
        );
        #endif
        
        ksnTCPProxyServerClientDisconnect(tp, w->fd, 1);
    } 
    
    // \todo Process reading error
    else if (received < 0) {
        
        //        if( errno == EINTR ) {
        //            // OK, just skip it
        //        }
    
        #ifdef DEBUG_KSNET
        ksnet_printf(
            &kev->ksn_cfg, DEBUG,
            "%sTCP Proxy:%s Read error ...%s\n", 
            ANSI_YELLOW, ANSI_RED, ANSI_NONE
        );
        #endif
    }
    
    // Success read. Process package and resend it to UDP proxy when ready
    else {
        
        // Process received buffer
        for(;;) {
            
            // Parse TCP packet        
            int rv = ksnTCPProxyPackageProcess(tp, data, received);

            // Send package to peer by UDP proxy connection
            if(rv > 0) {

                // Address string
                const char *addr = (const char *) (tp->packet.buffer + 
                    sizeof(ksnTCPProxyHeader));

                // Port number
                const int port = tp->packet.header->port;

                // Pointer to packet        
                const char *packet = (const char *) (tp->packet.buffer + 
                    sizeof(ksnTCPProxyHeader) + tp->packet.header->addr_length);

                // Packet length
                const size_t packet_len = tp->packet.header->packet_length;

                // Make address from string
                struct sockaddr_in remaddr; // remote address
                socklen_t addrlen = sizeof(remaddr); // length of addresses 
                if(!ksnTRUDPmakeAddr(addr, port, (__SOCKADDR_ARG) &remaddr, 
                        &addrlen)) {

                    // \todo Check and execute TCP Proxy packet command
                    switch(tp->packet.header->command) {

                        // Resend TCP packet to Peer by UDP proxy connection
                        case CMD_TCPP_PROXY: {

                            // Get TCP fd from tcp proxy map
                            size_t valueLength;
                            ksnTCPProxyData* tpd = pblMapGet(tp->map, &w->fd, 
                                    sizeof(w->fd), &valueLength);
                            
                            // Send TCP package
                            if(tpd != NULL) {   
                                sendto(tpd->udp_proxy_fd, packet, packet_len, 0, 
                                    (__CONST_SOCKADDR_ARG) &remaddr, addrlen); 
                            }

                        } break;

                        default:
                            break;
                    }                
                }

                // Process next part of received buffer
                if(!tp->packet.ptr == 0) {

                    received = 0;
                    continue; // Continue receiving buffer loop
                }
            } 

            // Process packed errors
            else if(rv < 0) {

                // Wrong process package stage error
                if(rv == -1) { 
                    #ifdef DEBUG_KSNET
                    ksnet_printf(&kev->ksn_cfg, DEBUG, 
                        "%sTCP Proxy:%s "
                        "Wrong process package stage ...%s\n", 
                        ANSI_YELLOW, ANSI_RED, ANSI_NONE);
                    #endif
                }

                // Wrong packet received error
                else if(rv == -2) {
                    #ifdef DEBUG_KSNET
                    ksnet_printf(&kev->ksn_cfg, DEBUG, 
                        "%sTCP Proxy:%s "
                        "Wrong packet received ...%s\n", 
                        ANSI_YELLOW, ANSI_RED, ANSI_NONE);
                    #endif
                }

                // Wrong packet body received error
                else if(rv == -3) {
                    #ifdef DEBUG_KSNET
                    ksnet_printf(&kev->ksn_cfg, DEBUG, 
                        "%sTCP Proxy:%s "
                        "Wrong packet checksum ...%s\n", 
                        ANSI_YELLOW, ANSI_RED, ANSI_NONE);
                    #endif
                }
                
                // Close TCP Proxy client
                ksnTCPProxyServerClientDisconnect(tp, w->fd, 1);
            }

            // A part of packet received - continue receiving next parts of package
            else {
                
                // Do nothing
                #ifdef DEBUG_KSNET
                ksnet_printf(&kev->ksn_cfg, DEBUG, 
                    "%sTCP Proxy:%s "
                    "Part of packet received. Wait for next part ...%s\n", 
                    ANSI_YELLOW, ANSI_GREY, ANSI_NONE);
                #endif                
            }
            
            break; // Exit from process received buffer loop
        }
    }
}

/**
 * TCP Proxy server accept callback
 * 
 * Register client, create UDP Proxy client/server, create event watchers with 
 * clients callback
 * 
 * @param loop Event manager loop
 * @param w Pointer to watcher
 * @param revents Events
 * @param fd File description of created client connection
 * 
 */
inline void cmd_tcpp_accept_cb(struct ev_loop *loop, struct ev_ksnet_io *w,
                       int revents, int fd) {
    
    ksnTCPProxyServerClientConnect(w->data, fd);    
}

//! \file   tcp_proxy.c
//!     * Start TCP Proxy server: ksnTCPProxyServerStart()
/**
 * Start TCP Proxy server
 * 
 * Create and start TCP Proxy server
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * 
 * @return If return value > 0 than server was created successfully
 */
int ksnTCPProxyServerStart(ksnTCPProxyClass *tp) {
    
    int fd = 0;
    
    if(kev->ksn_cfg.tcp_allow_f) {
        
        // Create TCP server at port, which will wait client connections
        int port_created;
        if((fd = ksnTcpServerCreate(
                    kev->kt, 
                    kev->ksn_cfg.tcp_port,
                    cmd_tcpp_accept_cb, 
                    tp, 
                    &port_created)) > 0) {

            ksnet_printf(&kev->ksn_cfg, MESSAGE, 
                    "%sTCP Proxy:%s TCP Proxy server fd %d started at port %d\n", 
                    ANSI_YELLOW, ANSI_NONE,
                    fd, port_created);

            kev->ksn_cfg.tcp_port = port_created;
            tp->fd = fd;
        }
    }
    
    return fd;
}

//! \file   tcp_proxy.c
//!     * Stop TCP Proxy server: ksnTCPProxyServerStop()
/**
 * Stop TCP Proxy server
 * 
 * Disconnect all connected clients and and stop TCP Proxy server
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * 
 */
void ksnTCPProxyServerStop(ksnTCPProxyClass *tp) {
    
    // If server started
    if(kev->ksn_cfg.tcp_allow_f && tp->fd) {
        
        // Disconnect all clients
        PblIterator *it = pblMapIteratorReverseNew(tp->map);
        if(it != NULL) {
            while(pblIteratorHasPrevious(it) > 0) {
                void *entry = pblIteratorPrevious(it);
                int *fd = (int *) pblMapEntryKey(entry);
                ksnTCPProxyServerClientDisconnect(tp, *fd, 0);
            }
            pblIteratorFree(it);
        }
        
        // Clear map
        pblMapClear(tp->map);
        
        // \todo Stop the server
    }
}

//! \file   tcp_proxy.c
//!     * Connect client to TCP Proxy server: ksnTCPProxyServerClientConnect()
//!         * Register connection 
//!         * Start UDP port Proxy
//!         * Start resending datagrams between TCP Server and UDP port Proxy 
/**
 * Connect TCP Proxy Server client
 * 
 * Called when tcp client connected to TCP Proxy Server. Create UDP Proxy and 
 * register this client in TCP Proxy map
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * @param fd TCP client connection file descriptor
 * 
 */
void ksnTCPProxyServerClientConnect(ksnTCPProxyClass *tp, int fd) {
    
    int udp_proxy_fd, udp_proxy_port = kev->ksn_cfg.port;
    
    ksnet_printf(&kev->ksn_cfg, CONNECT, 
            "%sTCP Proxy:%s "
            "TCP Proxy client fd %d connected\n", 
            ANSI_YELLOW, ANSI_NONE, fd);
   
    // Open UDP Proxy client/server
    ksnet_printf(&kev->ksn_cfg, CONNECT, 
            "%sTCP Proxy:%s "
            "Create UDP client/server Proxy at port %d ...\n", 
            ANSI_YELLOW, ANSI_NONE,
            udp_proxy_port);
    udp_proxy_fd = ksnCoreBindRaw(&kev->ksn_cfg, &udp_proxy_port);
    ksnet_printf(&kev->ksn_cfg, CONNECT, 
            "%sTCP Proxy:%s "
            "UDP client/server Proxy created at port %d\n", 
            ANSI_YELLOW, ANSI_NONE,
            udp_proxy_port);
            
    // Register client in tcp proxy map 
    ksnTCPProxyData data;
    data.tcp_proxy_fd = fd;
    data.udp_proxy_fd = udp_proxy_fd;
    data.udp_proxy_port = udp_proxy_port;
    pblMapAdd(tp->map, &fd, sizeof(fd), &data, sizeof(ksnTCPProxyData));
    pblMapAdd(tp->map, &udp_proxy_port, sizeof(udp_proxy_port), &data, sizeof(ksnTCPProxyData));
    
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wstrict-aliasing"

    // Create and start TCP and UDP watchers (start client processing)
    size_t valueLength;
    ksnTCPProxyData* tpd = pblMapGet(tp->map, &fd, sizeof(fd), &valueLength);
    if(tpd != NULL) {   
        
        // Create and start TCP watcher (start TCP client processing)
        ev_init (&tpd->w, cmd_tcpp_read_cb);
        ev_io_set (&tpd->w, fd, EV_READ);
        tpd->w.data = tp;
        ev_io_start (kev->ev_loop, &tpd->w);
        
        // Create and start UDP watcher (start UDP client processing)
        ev_init (&tpd->w_udp, cmd_udpp_read_cb);
        ev_io_set (&tpd->w_udp, udp_proxy_fd, EV_READ);
        tpd->w_udp.data = tp;
        ev_io_start (kev->ev_loop, &tpd->w_udp);
    }
    
    // Error: can't register TCP fd in tcp proxy map
    else {
        // \todo process error: can't register TCP fd in tcp proxy map
    }

    #pragma GCC diagnostic pop
}

//! \file   tcp_proxy.c
//!     * Disconnect client from TCP Proxy server: ksnTCPProxyServerClientDisconnect()
/**
 * Disconnect TCP Proxy Server client
 * 
 * Called when client disconnected or when the TCP Proxy server closing. Close 
 * UDP and TCP connections, remove data from TCP Proxy clients map.
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * @param fd TCP client connection file descriptor
 * @param remove_f If true than remove  disconnected record from map
 * 
 */
void ksnTCPProxyServerClientDisconnect(ksnTCPProxyClass *tp, int fd, 
        int remove_f) {
    
        size_t valueLength;
        
        // Get data from TCP Proxy Clients map, close TCP watcher and UDP PRoxy
        // connection and remove this data record from map
        ksnTCPProxyData* tpd = pblMapGet(tp->map, &fd, sizeof(fd), &valueLength); 
        if(tpd != NULL) {
            
            // Stop TCP Proxy client watcher
            ev_io_stop (kev->ev_loop, &tpd->w);
            
            // Stop UDP client/server Proxy watcher and Close UDP Proxy connection 
            ev_io_stop (kev->ev_loop, &tpd->w_udp);
            close(tpd->udp_proxy_fd); 
            
            // Remove data from map
            if(remove_f) {
                pblMapRemove(tp->map, &tpd->udp_proxy_fd, sizeof(tpd->udp_proxy_fd), &valueLength);
                pblMapRemove(tp->map, &fd, sizeof(fd), &valueLength);
            }
        }
        
        // Close TCP Proxy client        
        close(fd); 
        
        // Show disconnect message
        ksnet_printf(&kev->ksn_cfg, CONNECT, 
            "%sTCP Proxy:%s TCP Proxy client fd %d disconnected\n", 
            ANSI_YELLOW, ANSI_NONE, fd);
}

/**
 * Process TCP proxy package
 * 
 * Read tcp data from input buffer to tp->buffer until end of tcp proxy package, 
 * check checksum, take UDP address, port number and UDP package data.
 * 
 * @param tp Pointer to ksnTCPProxyClass
 * @param data Pointer to received TCP data
 * @param data_length TCP data length
 * 
 * @return Length of received packet, zero or error code. The TCP packets may 
 *         be received combined to one big buffer. If packet processed and this 
 *         function return value grate than 0 and less than input buffer size 
 *         we need run this function again with data_length = 0 to process next 
 *         part of input buffer. \n
 *         (see the "5) Check receiving" code of the test_5_2() function of the 
 *         test_tcp_proxy.c test)
 * 
 * @retval >0 - receiving done, the return value contain length of packet, 
 *              the packet saved to ksnTCPProxyClass::buffer
 * @retval 0 - continue reading current packet 
 * @retval -1 - wrong process package stage 
 * @retval -2 - wrong packet header checksum
 * @retval -3 - wrong packet checksum
 */
int ksnTCPProxyPackageProcess(ksnTCPProxyClass *tp, void *data, 
        size_t data_length) {
    
    int rv = 0;
    
    switch(tp->packet.stage) {
        
        // Wait for package began
        case WAIT_FOR_START:
            if(tp->packet.ptr != 0) {
                tp->packet.ptr = tp->packet.ptr - tp->packet.length;
                memmove(tp->packet.buffer, tp->packet.buffer+tp->packet.length, 
                        tp->packet.ptr);
            }
            tp->packet.length = 0;
            tp->packet.stage = WAIT_FOR_END;
            rv = ksnTCPProxyPackageProcess(tp, data, data_length);
            break;
            
        // Wait for package end
        case WAIT_FOR_END: {
            
            // \todo check buffer length
            if(data_length > 0) {
                memcpy(tp->packet.buffer + tp->packet.ptr, data, data_length);  
                tp->packet.ptr += data_length;
            }
            if(tp->packet.ptr >= sizeof(ksnTCPProxyHeader)) {
                
                // Check packet header 
                uint8_t packet_checksum = 
                        ksnTCPProxyChecksumCalculate(
                            (void*)tp->packet.header + 1, 
                            sizeof(ksnTCPProxyHeader) - 2
                        );
                
                // If packet checksum not equal - skip this packet
                if(!(tp->packet.header->addr_length 
                     && tp->packet.header->port     
                     && tp->packet.header->packet_checksum == packet_checksum)) {  
                    
                    rv = -2;
                    tp->packet.stage = WAIT_FOR_START;
                    tp->packet.ptr = 0;
                    break;
                }
                
                size_t pkg_len = sizeof(ksnTCPProxyHeader) + 
                        tp->packet.header->packet_length + 
                        tp->packet.header->addr_length;
                
                if(tp->packet.ptr >= pkg_len) {
                    tp->packet.stage = PROCESS_PACKET;
                    rv = ksnTCPProxyPackageProcess(tp, NULL, 0);
                }
            }
        }
        break;
            
        // Read process body
        case PROCESS_PACKET: {
            
            tp->packet.length = 
                    sizeof(ksnTCPProxyHeader) + 
                    tp->packet.header->packet_length + 
                    tp->packet.header->addr_length;
            
            // Get packet checksum 
            uint8_t checksum = 
                    ksnTCPProxyChecksumCalculate(
                        (void*)tp->packet.buffer + 1, 
                        tp->packet.length - 1
                    );
            
            tp->packet.stage = WAIT_FOR_START;
            if(!(tp->packet.ptr > tp->packet.length)) 
                tp->packet.ptr = 0;
            
            // Packet is processed
            if(tp->packet.header->checksum == checksum) {
                      
                rv = tp->packet.length;
            }
            
            // Wrong packet checksum error
            else rv = -3;
        }
        break;
            
        // Some wrong package processing stage
        default:
            rv = -1;
            break;
    }
            
    return rv;
}

#undef kev
