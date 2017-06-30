//////////////////////////////////////////////////////////////////////////////////////
//                                                                                  //
//  Copyright (c) 2016-2017 Leonardo Consoni <consoni_2519@hotmail.com>             //
//                                                                                  //
//  This file is part of Async IP Connections.                                      //
//                                                                                  //
//  Async IP Connections is free software: you can redistribute it and/or modify    //
//  it under the terms of the GNU Lesser General Public License as published        //
//  by the Free Software Foundation, either version 3 of the License, or            //
//  (at your option) any later version.                                             //
//                                                                                  //
//  Async IP Connections is distributed in the hope that it will be useful,         //
//  but WITHOUT ANY WARRANTY; without even the implied warranty of                  //
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                    //
//  GNU Lesser General Public License for more details.                             //
//                                                                                  //
//  You should have received a copy of the GNU Lesser General Public License        //
//  along with Async IP Connections. If not, see <http://www.gnu.org/licenses/>.    //
//                                                                                  //
//////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////
///// Multiplatform library for creation and handling of IP sockets connections /////
///// as server or client, using TCP or UDP protocols                           /////
/////////////////////////////////////////////////////////////////////////////////////

#include "ipc_base_ip.h"

#include "threads/threads.h"
#include "threads/thread_safe_queues.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
  
#ifdef WIN32
  //#include <winsock2.h>
  #include <ws2tcpip.h>
  #include <stdint.h>
  #include <time.h>
  
  #pragma comment(lib, "Ws2_32.lib")
  
  #define SHUT_RDWR SD_BOTH
  #define close( i ) closesocket( i )
  #define poll WSAPoll
  
  typedef SOCKET Socket;
#else
  #include <fcntl.h>
  #include <unistd.h>
  #include <errno.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <stropts.h>
  #include <poll.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>

  const int SOCKET_ERROR = -1;
  const int INVALID_SOCKET = -1;

  typedef int Socket;
#endif

#define PORT_LENGTH 6                                           // Maximum length of short integer string representation
  
#ifndef IP_NETWORK_LEGACY
  typedef struct pollfd SocketPoller;
  #define ADDRESS_LENGTH INET6_ADDRSTRLEN                       // Maximum length of IPv6 address (host+port) string
  typedef struct sockaddr_in6 IPAddressData;                    // IPv6 structure can store both IPv4 and IPv6 data
  #define IS_IPV6_MULTICAST_ADDRESS( address ) ( ((struct sockaddr_in6*) address)->sin6_addr.s6_addr[ 0 ] == 0xFF )
  #define ARE_EQUAL_IPV6_ADDRESSES( address_1, address_2 ) ( ((struct sockaddr*) address_1)->sa_family == AF_INET6 && \
                                                             ((IPAddressData*) address_1)->sin6_port == ((IPAddressData*) address_2)->sin6_port && \
                                                             memcmp( ((IPAddressData*) address_1)->sin6_addr.s6_addr, ((IPAddressData*) address_2)->sin6_addr.s6_addr, 16 ) == 0 )
#else
  typedef struct { Socket fd; } SocketPoller;
  #define ADDRESS_LENGTH INET_ADDRSTRLEN + PORT_LENGTH          // Maximum length of IPv4 address (host+port) string
  typedef struct sockaddr_in IPAddressData;                     // Legacy mode only works with IPv4 addresses
  #define IS_IPV6_MULTICAST_ADDRESS( address ) false
  #define ARE_EQUAL_IPV6_ADDRESSES( address_1, address_2 ) false
#endif
typedef struct sockaddr* IPAddress;                             // Opaque IP address type
#define IS_IPV4_MULTICAST_ADDRESS( address ) ( (ntohl( ((struct sockaddr_in*) address)->sin_addr.s_addr ) & 0xF0000000) == 0xE0000000 )
#define ARE_EQUAL_IPV4_ADDRESSES( address_1, address_2 ) ( ((struct sockaddr*) address_1)->sa_family == AF_INET && \
                                                           ((struct sockaddr_in*) address_1)->sin_port == ((struct sockaddr_in*) address_2)->sin_port )

#define IS_IP_MULTICAST_ADDRESS( address ) ( IS_IPV4_MULTICAST_ADDRESS( address ) || IS_IPV6_MULTICAST_ADDRESS( address ) )
#define ARE_EQUAL_IP_ADDRESSES( address_1, address_2 ) ( ARE_EQUAL_IPV4_ADDRESSES( address_1, address_2 ) || ARE_EQUAL_IPV6_ADDRESSES( address_1, address_2 ) )


///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                      INTERFACE DEFINITION                                       /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

const size_t QUEUE_MAX_ITEMS = 10;
const unsigned long EVENT_WAIT_TIME_MS = 5000;

// Generic structure to store methods and data of any connection type handled by the library
struct _IPConnectionData
{
  SocketPoller* socket;
  void (*ref_ReceiveMessage)( IPConnection );
  void (*ref_SendMessage)( IPConnection, const Byte*, const RemoteID* );
  void (*ref_Close)( IPConnection );
  IPAddressData addressData;
  RemoteID* clientsList;
  size_t clientsCount;
  TSQueue readQueue;
  TSQueue writeQueue;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                        GLOBAL VARIABLES                                         /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Thread for asyncronous connections update
static Thread globalReadThread = THREAD_INVALID_HANDLE;
static Thread globalWriteThread = THREAD_INVALID_HANDLE;
static volatile bool isNetworkRunning = false;

static IPConnection* globalConnectionsList = NULL;
static size_t activeConnectionsCount = 0;

#ifdef IP_NETWORK_LEGACY
static fd_set polledSocketsSet = { 0 };
static fd_set activeSocketsSet = { 0 };
#else
static SocketPoller polledSocketsList[ 1024 ] = { 0 };
#endif
static size_t polledSocketsNumber = 0;

/////////////////////////////////////////////////////////////////////////////
/////                        FORWARD DECLARATIONS                       /////
/////////////////////////////////////////////////////////////////////////////


static void ReceiveTCPClientMessage( IPConnection );
static void ReceiveUDPClientMessage( IPConnection );
static void ReceiveTCPServerMessages( IPConnection );
static void ReceiveUDPServerMessages( IPConnection );
static void SendTCPClientMessage( IPConnection, const Byte*, const RemoteID* );
static void SendUDPClientMessage( IPConnection, const Byte*, const RemoteID* );
static void SendServerMessages( IPConnection, const Byte*, const RemoteID* );
static void CloseTCPServer( IPConnection );
static void CloseUDPServer( IPConnection );
static void CloseTCPClient( IPConnection );
static void CloseUDPClient( IPConnection );

static void* AsyncReadQueues( void* );
static void* AsyncWriteQueues( void* );


//////////////////////////////////////////////////////////////////////////////////
/////                             INITIALIZATION                             /////
//////////////////////////////////////////////////////////////////////////////////

#ifndef IP_NETWORK_LEGACY
static int CompareSockets( const void* ref_socket_1, const void* ref_socket_2 )
{
  return ( ((SocketPoller*) ref_socket_1)->fd - ((SocketPoller*) ref_socket_2)->fd );
}
#endif

static SocketPoller* AddSocketPoller( Socket socketFD )
{
  #ifndef IP_NETWORK_LEGACY
  SocketPoller cmpPoller = { .fd = socketFD };
  SocketPoller* socketPoller = (SocketPoller*) bsearch( &cmpPoller, polledSocketsList, polledSocketsNumber, sizeof(SocketPoller), CompareSockets );
  if( csocketPoller == NULL )
  {
    socketPoller = &(polledSocketsList[ polledSocketsNumber ]);
    socketPoller->fd = socketFD;
    socketPoller->events = POLLRDNORM | POLLRDBAND;
    polledSocketsNumber++;
    qsort( polledSocketsList, polledSocketsNumber, sizeof(SocketPoller), CompareSockets );
  }
  #else
  SocketPoller* socketPoller = (SocketPoller*) malloc( sizeof(SocketPoller) );
  FD_SET( socketFD, &polledSocketsSet );
  if( socketFD >= polledSocketsNumber ) polledSocketsNumber = socketFD + 1;
  socketPoller->fd = socketFD;
  #endif
  return socketPoller;
}

// Handle construction of a IPConnection structure with the defined properties
static IPConnection AddConnection( Socket socketFD, IPAddress address, uint8_t transportProtocol, uint8_t networkRole )
{
  IPConnection connection = (IPConnection) malloc( sizeof(IPConnectionData) );
  memset( connection, 0, sizeof(IPConnectionData) );
  
  connection->socket = AddSocketPoller( socketFD );
  
  memcpy( &(connection->addressData), address, sizeof(IPAddressData) );
  
  connection->clientsList = NULL;
  connection->clientsCount = 0;
  
  if( networkRole == IPC_SERVER ) // Server role connection
  {
    connection->ref_ReceiveMessage = ( transportProtocol == IPC_TCP ) ? ReceiveTCPServerMessages : ReceiveUDPServerMessages;
    connection->ref_SendMessage = SendServerMessages;
    if( transportProtocol == IPC_UDP && IS_IP_MULTICAST_ADDRESS( address ) ) connection->ref_SendMessage = SendUDPClientMessage;
    connection->ref_Close = ( transportProtocol == IPC_TCP ) ? CloseTCPServer : CloseUDPServer;
  }
  else
  { 
    //connection->address->sin6_family = AF_INET6;
    connection->ref_ReceiveMessage = ( transportProtocol == IPC_TCP ) ? ReceiveTCPClientMessage : ReceiveUDPClientMessage;
    connection->ref_SendMessage = ( transportProtocol == IPC_TCP ) ? SendTCPClientMessage : SendUDPClientMessage;
    connection->ref_Close = ( transportProtocol == IPC_TCP ) ? CloseTCPClient : CloseUDPClient;
  }
  
  return connection;
}

// Add defined connection to the client list of the given server connection
static inline void AddClient( IPConnection server, RemoteID* client )
{
  size_t clientIndex = 0, clientsListSize = 0;
  
  while( clientIndex < server->clientsCount )
  {
    clientsListSize++;
    if( server->clientsList[ clientIndex ] == NULL ) break;
    clientIndex++;      
  }
  
  if( clientIndex == clientsListSize ) server->clientsList = (RemoteID*) realloc( server->clientsList, ( clientIndex + 1 ) * sizeof(RemoteID) );
  
  memcpy( &(server->clientsList[ clientIndex ]), client, IPC_MAX_ID_LENGTH );
  
  server->clientsCount++;

  return;
}

IPAddress LoadAddressInfo( const char* host, const char* port, uint8_t networkRole )
{
  static IPAddressData addressData;
  
  #ifdef WIN32
  static WSADATA wsa;
  if( wsa.wVersion == 0 )
  {
    if( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
    {
      fprintf( stderr, "%s: error initialiasing windows sockets: code: %d\n", __func__, WSAGetLastError() );
      return NULL;
    }
    
    fprintf( stderr, "%s: initialiasing windows sockets version: %d\n", __func__, wsa.wVersion );
  }
  #endif
  
  #ifndef IP_NETWORK_LEGACY
  struct addrinfo hints = { .ai_family = AF_UNSPEC }; // IPv6 (AF_INET6) or IPv4 (AF_INET) address
  struct addrinfo* hostsInfoList;
  struct addrinfo* hostInfo = NULL;
                        
  if( host == NULL )
  {
    if( networkRole == IPC_SERVER )
    {
      hints.ai_flags |= AI_PASSIVE;                   // Set address for me
      hints.ai_family = AF_INET6;                     // IPv6 address
    }
    else // if( networkRole == IPC_CLIENT )
      return NULL;
  }
  
  int errorCode = 0;
  if( (errorCode = getaddrinfo( host, port, &hints, &hostsInfoList )) != 0 )
  {
    fprintf( stderr, "getaddrinfo: error reading host info: %s", gai_strerror( errorCode ) );
    return NULL;
  }
  
  // loop through all the results and bind to the first we can
  for( hostInfo = hostsInfoList; hostInfo != NULL; hostInfo = hostInfo->ai_next ) 
  {
    // Extended connection info for debug builds
    char* addressString = GetAddressString( hostInfo->ai_addr );
    if( addressString == NULL ) continue;
    
    memcpy( &addressData, hostInfo->ai_addr, hostInfo->ai_addrlen );
    break;
  }
  
  freeaddrinfo( hostsInfoList ); // Don't need this struct anymore
  
  if( hostInfo == NULL ) return NULL;
  #else
  addressData.sin_family = AF_INET;   // IPv4 address
  uint16_t portNumber = (uint16_t) strtoul( port, NULL, 0 );
  addressData.sin_port = htons( portNumber );
  if( host == NULL ) addressData.sin_addr.s_addr = INADDR_ANY;
  else if( strcmp( host, "255.255.255.255" ) == 0 ) addressData.sin_addr.s_addr = INADDR_BROADCAST; 
  else if ( (addressData.sin_addr.s_addr = inet_addr( host )) == INADDR_NONE ) return NULL;
  #endif
  
  return (IPAddress) &addressData;
}

int CreateSocket( uint8_t protocol, IPAddress address )
{
  int socketType, transportProtocol;
  
  if( protocol == IPC_TCP ) 
  {
    socketType = SOCK_STREAM;
    transportProtocol = IPPROTO_TCP;
  }
  else if( protocol == IPC_UDP ) 
  {
    socketType = SOCK_DGRAM;
    transportProtocol = IPPROTO_UDP;
  }
  else
  {
    return INVALID_SOCKET;
  }
  
  // Create IP socket
  int socketFD = socket( address->sa_family, socketType, transportProtocol );
  if( socketFD == INVALID_SOCKET )
    fprintf( stderr, "socket: failed opening %s %s socket", ( protocol == IPC_TCP ) ? "TCP" : "UDP", ( address->sa_family == AF_INET6 ) ? "IPv6" : "IPv4" );                                                              
  
  return socketFD;
}

bool SetSocketConfig( int socketFD )
{
  #ifdef WIN32
  u_long nonBlocking = 1;
  if( ioctlsocket( socketFD, FIONBIO, &nonBlocking ) == SOCKET_ERROR )
  #else
  if( fcntl( socketFD, F_SETFL, O_NONBLOCK ) == SOCKET_ERROR )
  #endif
  {
    fprintf( stderr, "failure setting socket %d to non-blocking state", socketFD );
    close( socketFD );
    return false;
  }
  
  int reuseAddress = 1; // Allow sockets to be binded to the same local port
  if( setsockopt( socketFD, SOL_SOCKET, SO_REUSEADDR, (const char*) &reuseAddress, sizeof(reuseAddress) ) == SOCKET_ERROR ) 
  {
    fprintf( stderr, "setsockopt: failed setting socket %d option SO_REUSEADDR", socketFD );
    close( socketFD );
    return NULL;
  }
  
  return true;
}

bool BindServerSocket( int socketFD, IPAddress address )
{
  if( address->sa_family == AF_INET6 )
  {
    int ipv6Only = 0; // Let IPV6 servers accept IPV4 clients
    if( setsockopt( socketFD, IPPROTO_IPV6, IPV6_V6ONLY, (const char*) &ipv6Only, sizeof(ipv6Only) ) == SOCKET_ERROR )
    {
      fprintf( stderr, "setsockopt: failed setting socket %d option IPV6_V6ONLY", socketFD );
      close( socketFD );
      return false;
    }
  }
  
  // Bind server socket to the given local address
  size_t addressLength = ( address->sa_family == AF_INET6 ) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
  if( bind( socketFD, address, addressLength ) == SOCKET_ERROR )
  {
    fprintf( stderr, "bind: failed on binding socket %d", socketFD );
    close( socketFD );
    return false;
  }
  
  return true;
}

bool BindTCPServerSocket( int socketFD, IPAddress address )
{
  const size_t QUEUE_SIZE = 20;
  
  if( !BindServerSocket( socketFD, address ) ) return false;
  
  // Set server socket to listen to remote connections
  if( listen( socketFD, QUEUE_SIZE ) == SOCKET_ERROR )
  {
    fprintf( stderr, "listen: failed listening on socket %d", socketFD );
    close( socketFD );
    return false;
  }
  
  return true;
}

bool BindUDPServerSocket( int socketFD, IPAddress address )
{
  if( !BindServerSocket( socketFD, address ) ) return false;
  
  #ifndef IP_NETWORK_LEGACY
  int multicastTTL = 255; // Set TTL of multicast packet
  if( address->sa_family == AF_INET6 )
  {
    if( setsockopt( socketFD, IPPROTO_IPV6, IPV6_MULTICAST_HOPS, (const char*) &multicastTTL, sizeof(multicastTTL)) != 0 ) 
    {
      fprintf( stderr, "setsockopt: failed setting socket %d option IPV6_MULTICAST_HOPS", socketFD );
      close( socketFD );
      return false;
    }
    unsigned int interfaceIndex = 0; // 0 means default interface
    if( setsockopt( socketFD, IPPROTO_IPV6, IPV6_MULTICAST_IF, (const char*) &interfaceIndex, sizeof(interfaceIndex)) != 0 ) 
    {
      fprintf( stderr, "setsockopt: failed setting socket %d option IPV6_MULTICAST_IF", socketFD );
      close( socketFD );
      return false;
    }
  }
  else //if( address->sa_family == AF_INET )
  {
    if( setsockopt( socketFD, IPPROTO_IP, IP_MULTICAST_TTL, (const char*) &multicastTTL, sizeof(multicastTTL)) != 0 ) 
    {
      fprintf( stderr, "setsockopt: failed setting socket %d option IP_MULTICAST_TTL", socketFD );
      close( socketFD );
      return false;
    }
    in_addr_t interface = htonl( INADDR_ANY );
	  if( setsockopt( socketFD, IPPROTO_IP, IP_MULTICAST_IF, (const char*) &interface, sizeof(interface)) != 0 ) 
    {
      fprintf( stderr, "setsockopt: failed setting socket %d option IP_MULTICAST_IF", socketFD );
      close( socketFD );
      return false;
    }
  }
  #else
  int broadcast = 1; // Enable broadcast for IPv4 connections
  if( setsockopt( socketFD, SOL_SOCKET, SO_BROADCAST, (const char*) &broadcast, sizeof(broadcast) ) == SOCKET_ERROR )
  {
    fprintf( stderr, "setsockopt: failed setting socket %d option SO_BROADCAST", socketFD );
    close( socketFD );
    return false;
  }
  #endif
  
  return true;
}

bool ConnectTCPClientSocket( int socketFD, IPAddress address )
{
  // Connect TCP client socket to given remote address
  size_t addressLength = ( address->sa_family == AF_INET6 ) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
  if( connect( socketFD, address, addressLength ) == SOCKET_ERROR )
  {
    fprintf( stderr, "connect: failed on connecting socket %d to remote address", socketFD );
    close( socketFD );
    return false;
  }
  
  return true;
}

bool ConnectUDPClientSocket( int socketFD, IPAddress address )
{
  // Bind UDP client socket to available local address
  static struct sockaddr_storage localAddress;
  localAddress.ss_family = address->sa_family;
  if( bind( socketFD, (struct sockaddr*) &localAddress, sizeof(localAddress) ) == SOCKET_ERROR )
  {
    fprintf( stderr, "bind: failed on binding socket %d to arbitrary local port", socketFD );
    close( socketFD );
    return false;
  }
  
  #ifndef IP_NETWORK_LEGACY
  // Joint the multicast group
  if( address->sa_family == AF_INET6 )
  {
    if( IS_IPV6_MULTICAST_ADDRESS( address ) )
    {
      struct ipv6_mreq multicastRequest;  // Specify the multicast group
      memcpy( &multicastRequest.ipv6mr_multiaddr, &(((struct sockaddr_in6*) address)->sin6_addr), sizeof(multicastRequest.ipv6mr_multiaddr) );

      multicastRequest.ipv6mr_interface = 0; // Accept multicast from any interface

      // Join the multicast address
      if ( setsockopt( socketFD, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, (char*) &multicastRequest, sizeof(multicastRequest) ) != 0 ) 
      {
        fprintf( stderr, "setsockopt: failed setting socket %d option IPV6_ADD_MEMBERSHIP", socketFD );
        close( socketFD );
        return false;
      }
    }
  }
  else //if( address->sa_family == AF_INET )
  {
    if( IS_IPV4_MULTICAST_ADDRESS( address ) )
    {
      struct ip_mreq multicastRequest;  // Specify the multicast group
      memcpy( &(multicastRequest.imr_multiaddr), &(((struct sockaddr_in*) address)->sin_addr), sizeof(multicastRequest.imr_multiaddr) );

      multicastRequest.imr_interface.s_addr = htonl( INADDR_ANY ); // Accept multicast from any interface

      // Join the multicast address
      if( setsockopt( socketFD, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*) &multicastRequest, sizeof(multicastRequest)) != 0 ) 
      {
        fprintf( stderr, "setsockopt: failed setting socket %d option IP_ADD_MEMBERSHIP", socketFD );
        close( socketFD );
        return false;
      }
    }
  }
  #endif
  
  return true;
}

// Generic method for opening a new socket and providing a corresponding IPConnection structure for use
IPConnection IP_OpenConnection( uint8_t connectionType, const char* host, uint16_t port )
{
  const uint8_t TRANSPORT_MASK = 0xF0, ROLE_MASK = 0x0F;
  static char portString[ PORT_LENGTH ];
  
  // Assure that the port number is in the Dynamic/Private range (49152-65535)
  if( port < 49152 /*|| port > 65535*/ )
  {
    fprintf( stderr, "invalid port number value: %u", port );
    return NULL;
  }
  
  sprintf( portString, "%u", port );
  IPAddress address = LoadAddressInfo( host, portString, (connectionType & ROLE_MASK) );
  if( address == NULL ) return NULL;
  
  Socket socketFD = CreateSocket( (connectionType & TRANSPORT_MASK), address );
  if( socketFD == INVALID_SOCKET ) return NULL;
  
  if( !SetSocketConfig( socketFD ) ) return NULL;
  
  switch( connectionType )
  {
    case( IPC_TCP | IPC_SERVER ): if( !BindTCPServerSocket( socketFD, address ) ) return NULL;
      break;
    case( IPC_UDP | IPC_SERVER ): if( !BindUDPServerSocket( socketFD, address ) ) return NULL;
      break;
    case( IPC_TCP | IPC_CLIENT ): if( !ConnectTCPClientSocket( socketFD, address ) ) return NULL;
      break;
    case( IPC_UDP | IPC_CLIENT ): if( !ConnectUDPClientSocket( socketFD, address ) ) return NULL;
      break;
    default: fprintf( stderr, "invalid connection type: %x", connectionType );
      return NULL;
  } 
  
  return AddConnection( socketFD, address, (connectionType & TRANSPORT_MASK), (connectionType & ROLE_MASK) ); // Build the IPConnection structure
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                     ASYNCRONOUS UPDATE                                          /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ReadToQueue( unsigned long connectionID )
{
  AsyncIPConnection connection = TSM_AcquireItem( globalConnectionsList, connectionID );
  if( connection == NULL ) return;
  
  // Do not proceed if queue is full
  if( TSQ_GetItemsCount( connection->readQueue ) >= QUEUE_MAX_ITEMS ) 
  {
    TSM_ReleaseItem( globalConnectionsList, connectionID );
    return;
  }
  
  if( IP_IsDataAvailable( connection->baseConnection ) )
  {
    if( IP_IsServer( connection->baseConnection ) )
    {
      IPConnection newClient = IP_AcceptClient( connection->baseConnection );
      if( newClient != NULL )
      {
        char* addressString = IP_GetAddress( newClient );
        if( addressString != NULL )
        {
          TSM_ReleaseItem( globalConnectionsList, connectionID );
          unsigned long newClientID = AddAsyncConnection( newClient );
          TSQ_Enqueue( connection->readQueue, &newClientID, TSQUEUE_WAIT );
          return;
        }
      }
    }
    else
    {
      char* lastMessage = IP_ReceiveMessage( connection->baseConnection );
      if( lastMessage != NULL ) TSQ_Enqueue( connection->readQueue, (void*) lastMessage, TSQUEUE_WAIT );
    }
  }
  
  TSM_ReleaseItem( globalConnectionsList, connectionID );
}

// Loop of message reading (storing in queue) to be called asyncronously for client/server connections
static void* AsyncReadQueues( void* args )
{
  isNetworkRunning = true;
  
  while( isNetworkRunning )
  { 
    // Blocking call
    #ifndef IP_NETWORK_LEGACY
    int eventsNumber = poll( polledSocketsList, polledSocketsNumber, EVENT_WAIT_TIME_MS );
    #else
    struct timeval waitTime = { .tv_sec = EVENT_WAIT_TIME_MS / 1000, .tv_usec = ( EVENT_WAIT_TIME_MS % 1000 ) * 1000 };
    activeSocketsSet = polledSocketsSet;
    int eventsNumber = select( polledSocketsNumber, &activeSocketsSet, NULL, NULL, &waitTime );
    #endif
    if( eventsNumber == SOCKET_ERROR ) fprintf( stderr, "select: error waiting for events on %lu FDs", polledSocketsNumber );
    
    if( eventsNumber > 0 ) 
    {
      for( size_t connectionIndex = 0; connectionIndex < activeConnectionsCount; connectionIndex++ )
      {
        IPConnection connection = globalConnectionsList[ connectionIndex ];
        if( connection == NULL ) continue;
        
        connection->ref_ReceiveMessage( connection );
      }
    }
  }
  
  return NULL;
}

// Loop of message writing (removing in order from queue) to be called asyncronously for client connections
static void* AsyncWriteQueues( void* args )
{
  Message messageOut;
  
  isNetworkRunning = true;
  
  while( isNetworkRunning )
  {
    for( size_t connectionIndex = 0; connectionIndex < activeConnectionsCount; connectionIndex++ )
    {
      IPConnection connection = globalConnectionsList[ connectionIndex ];
      if( connection == NULL ) continue;
      
      // Do not proceed if queue is empty
      if( TSQ_GetItemsCount( connection->writeQueue ) == 0 ) continue;
      
      memset( &(messageOut), 0, sizeof(Message) );
      
      TSQ_Dequeue( connection->writeQueue, (void*) &messageOut, TSQUEUE_WAIT );
  
      connection->ref_SendMessage( connection, (const Byte*) &(messageOut.data), &(messageOut.address) );
    }
    
#ifdef _WIN32
    Sleep( 1000 );
#else
    usleep( 1000*1000 );  /* sleep for 1000 milliSeconds */
#endif
  }
  
  return NULL;//(void*) 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                      SYNCRONOUS UPDATE                                          /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Get (and remove) message from the beginning (oldest) of the given index corresponding read queue
// Method to be called from the main thread
bool IP_ReceiveMessage( IPConnection connection, Message* message )
{  
  if( connection == NULL ) return false;
    
  if( TSQ_GetItemsCount( connection->readQueue ) == 0 ) return false;

  TSQ_Dequeue( connection->readQueue, (void*) message, TSQUEUE_WAIT );
  
  return true;
}

bool IP_SendMessage( IPConnection connection, const Message* message )
{  
  if( connection == NULL ) return false;
  
  if( TSQ_GetItemsCount( connection->writeQueue ) >= QUEUE_MAX_ITEMS )
    fprintf( stderr, "connection index %p write queue is full", connection );
  
  TSQ_Enqueue( connection->writeQueue, (void*) message, TSQUEUE_NOWAIT );
  
  return true;
}

// Verify available incoming messages for the given connection, preventing unnecessary blocking calls (for syncronous networking)
int IP_WaitEvent( unsigned int milliseconds )
{
  #ifndef IP_NETWORK_LEGACY
  int eventsNumber = poll( polledSocketsList, polledSocketsNumber, milliseconds );
  #else
  struct timeval waitTime = { .tv_sec = milliseconds / 1000, .tv_usec = ( milliseconds % 1000 ) * 1000 };
  activeSocketsSet = polledSocketsSet;
  int eventsNumber = select( polledSocketsNumber, &activeSocketsSet, NULL, NULL, &waitTime );
  #endif
  if( eventsNumber == SOCKET_ERROR ) fprintf( stderr, "select: error waiting for events on %lu FDs", polledSocketsNumber );
  
  return eventsNumber;
}

bool IsDataAvailable( SocketPoller* socket )
{ 
  #ifndef IP_NETWORK_LEGACY
  if( socket->revents & POLLRDNORM ) return true;
  else if( socket->revents & POLLRDBAND ) return true;
  #else
  if( FD_ISSET( socket->fd, &activeSocketsSet ) ) return true;
  #endif
  
  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
/////                      SPECIFIC TRANSPORT/ROLE COMMUNICATION                    /////
/////////////////////////////////////////////////////////////////////////////////////////

static inline void RemoveSocket( Socket );

// Try to receive incoming message from the given TCP client connection and store it on its buffer
static void ReceiveTCPClientMessage( IPConnection connection )
{
  static Message messageIn;
  
  if( connection->socket->fd == INVALID_SOCKET ) return;

  //if( TSQ_GetItemsCount( connection->readQueue ) >= QUEUE_MAX_ITEMS ) return;
  
  if( IsDataAvailable( connection->socket ) == false ) return;
  
  // Blocks until there is something to be read in the socket
  int bytesReceived = recv( connection->socket->fd, &(messageIn.data), IPC_MAX_MESSAGE_LENGTH, 0 );

  if( bytesReceived == SOCKET_ERROR )
  {
    fprintf( stderr, "recv: error reading from socket %d", connection->socket->fd );
    //connection->socket->fd = INVALID_SOCKET;
    //RemoveSocket( connection->socket->fd );
    return;
  }
  else if( bytesReceived == 0 )
  {
    fprintf( stderr, "recv: remote connection with socket %d closed", connection->socket->fd );
    //connection->socket->fd = INVALID_SOCKET;
    RemoveSocket( connection->socket->fd );
    return;
  }
  
  socklen_t addressLength = 0;
  getpeername( connection->socket->fd, (IPAddress) &(messageIn.address), &addressLength );
  
  TSQ_Enqueue( connection->readQueue, &(messageIn), TSQUEUE_NOWAIT );
}

// Send given message through the given TCP connection
static void SendTCPClientMessage( IPConnection connection, const Byte* message, const RemoteID* client )
{
  (void) client;
  
  if( send( connection->socket->fd, message, IPC_MAX_MESSAGE_LENGTH, 0 ) == SOCKET_ERROR )
  {
    fprintf( stderr, "send: error writing to socket %d", connection->socket->fd );
    //return -1;
  }
  
  //return 0;
}

// Try to receive incoming message from the given UDP client connection and store it on its buffer
static void ReceiveUDPClientMessage( IPConnection connection )
{
  static Message messageIn;
  
  if( IsDataAvailable( connection->socket ) == false ) return;
  
  // Blocks until there is something to be read in the socket
  socklen_t addressLength = sizeof(IPAddressData);
  if( recvfrom( connection->socket->fd, &(messageIn.data), IPC_MAX_MESSAGE_LENGTH, 0, (IPAddress) &(messageIn.address), &addressLength ) == SOCKET_ERROR )
  {
    //fprintf( stderr, "recvfrom: error reading from socket %d", connection->socket->fd );
    return;
  }
    
  TSQ_Enqueue( connection->readQueue, &(messageIn), TSQUEUE_NOWAIT );
  
  // Default return message (the received one was not destined to this connection) 
  //return NULL;
}

// Send given message through the given UDP connection
static void SendUDPClientMessage( IPConnection connection, const Byte* message, const RemoteID* client )
{
  (void) client;
  
  if( sendto( connection->socket->fd, message, IPC_MAX_MESSAGE_LENGTH, 0, (IPAddress) &(connection->addressData), sizeof(IPAddressData) ) == SOCKET_ERROR )
  {
    fprintf( stderr, "sendto: error writing to socket %d", connection->socket->fd );
    //return -1;
  }
  
  //return 0;
}

// Send given message to all the clients of the given server connection
static void SendServerMessages( IPConnection connection, const Byte* message, const RemoteID* client )
{
  size_t clientIndex = 0;
  while( clientIndex < connection->clientsCount )
  {
    if( connection->clientsList[ clientIndex ] != NULL )
    {
      SocketPoller* clientSocket = (SocketPoller*) client;
      if( send( clientSocket->fd, message, IPC_MAX_MESSAGE_LENGTH, 0 ) == SOCKET_ERROR )
      {
        fprintf( stderr, "send: error writing to socket %d", clientSocket->fd );
        //return -1;
      }
      clientIndex++;
    }
  }
  
  //return 0;
}

// Waits for a remote connection to be added to the client list of the given TCP server connection
static void ReceiveTCPServerMessages( IPConnection server )
{ 
  static Message messageIn;
  
  if( IsDataAvailable( server->socket ) )
  {
    socklen_t addressLength = sizeof(IPAddressData);
    Socket clientSocketFD = accept( server->socket->fd, (IPAddress) &(messageIn.address), &addressLength );
    if( clientSocketFD == INVALID_SOCKET )
    {
      fprintf( stderr, "accept: failed accepting connection on socket %d", server->socket->fd );
      //return NULL;
    }
    else
    {
      SocketPoller* clientSocket = AddSocketPoller( clientSocketFD );
      server->clientsList = (RemoteID*) realloc( server->clientsList, ++server->clientsCount * sizeof(RemoteID) );
      memcpy( &(server->clientsList[ server->clientsCount - 1 ]), &(clientSocket), sizeof(SocketPoller) );
    }
  }
  
  size_t clientIndex = 0;
  while( clientIndex < server->clientsCount )
  {
    if( server->clientsList[ clientIndex ] != NULL )
    {
      memcpy( &clientSocketFD, &(server->clientsList[ clientIndex ]), sizeof(Socket) );
      if( IsDataAvailable( server->socket ) )
      {
        int bytesReceived = recv( clientSocketFD, &(messageIn.data), IPC_MAX_MESSAGE_LENGTH, 0 );
        if( bytesReceived == SOCKET_ERROR ) 
          fprintf( stderr, "recv: error reading from socket %d", clientSocketFD );
        else if( bytesReceived == 0 )
        {
          fprintf( stderr, "recv: remote connection with socket %d closed", clientSocketFD );
          RemoveSocket( clientSocketFD );
        }
        else
        {
          memcpy( &(messageIn.address), &clientSocketFD, sizeof(Socket) );
          TSQ_Enqueue( server->readQueue, &(messageIn), TSQUEUE_NOWAIT );
        }
      }
      clientIndex++;
    }
  }
}

// Waits for a remote connection to be added to the client list of the given UDP server connection
static void ReceiveUDPServerMessages( IPConnection server )
{
  static Message messageIn;
  
  if( IsDataAvailable( server->socket ) == false ) return;
  
  socklen_t addressLength = sizeof(IPAddressData);
  if( recvfrom( server->socket->fd, &(messageIn.data), IPC_MAX_MESSAGE_LENGTH, 0, (IPAddress) &(messageIn.address), &addressLength ) == SOCKET_ERROR )
  {
    fprintf( stderr, "recvfrom: error reading from socket %d", server->socket->fd );
    return;
  }
  
  TSQ_Enqueue( server->readQueue, &(messageIn), TSQUEUE_NOWAIT );
  
  // Verify if incoming message belongs to unregistered client (returns default value if not)
  for( size_t clientIndex = 0; clientIndex < server->clientsCount; clientIndex++ )
  {
    if( ARE_EQUAL_IP_ADDRESSES( &(server->clientsList[ clientIndex ]), &(messageIn.address) ) )
      return;
  }
  
  server->clientsList = (RemoteID*) realloc( server->clientsList, ++server->clientsCount * sizeof(RemoteID) );
  memcpy( &(server->clientsList[ server->clientsCount - 1 ]), &(messageIn.address), sizeof(IPAddressData) );
}


//////////////////////////////////////////////////////////////////////////////////
/////                               FINALIZING                               /////
//////////////////////////////////////////////////////////////////////////////////

// Handle proper destruction of any given connection type

void RemoveSocket( Socket socketFD )
{
  #ifndef IP_NETWORK_LEGACY
  SocketPoller cmpPoller = { .fd = socketFD };
  SocketPoller* poller = bsearch( &cmpPoller, polledSocketsList, polledSocketsNumber, sizeof(SocketPoller), CompareSockets );
  if( poller != NULL )
  {
    poller->fd = (Socket) 0xFFFF;
    qsort( polledSocketsList, polledSocketsNumber, sizeof(SocketPoller), CompareSockets );
    polledSocketsNumber--;
  }
  #else
  FD_CLR( socketFD, &polledSocketsSet );
  if( socketFD + 1 >= polledSocketsNumber ) polledSocketsNumber = socketFD - 1;
  #endif
  close( socketFD );
}

void CloseTCPServer( IPConnection server )
{
  shutdown( server->socket->fd, SHUT_RDWR );
  RemoveSocket( server->socket->fd );
  free( server->ref_clientsCount );
  if( server->clientsList != NULL ) free( server->clientsList );
  free( server );
}

void CloseUDPServer( IPConnection server )
{
  // Check number of client connections of a server (also of sharers of a socket for UDP connections)
  if( *(server->ref_clientsCount) == 0 )
  {
    RemoveSocket( server->socket->fd );
    free( server->ref_clientsCount );
    if( server->clientsList != NULL ) free( server->clientsList );
    free( server );
  }
}

static inline void RemoveClient( IPConnection server, IPConnection client )
{
  if( server == NULL ) return;
  
  size_t clientsNumber = *(server->ref_clientsCount);
  for( size_t clientIndex = 0; clientIndex < clientsNumber; clientIndex++ )
  {
    if( server->clientsList[ clientIndex ] == client )
    {
      server->clientsList[ clientIndex ] = NULL;
      (*(server->ref_clientsCount))--;
      break;
    }
  }
}

void CloseTCPClient( IPConnection client )
{
  RemoveClient( client->server, client );
  shutdown( client->socket->fd, SHUT_RDWR );
  RemoveSocket( client->socket->fd );
  if( client->buffer != NULL ) free( client->buffer );
  free( client );
}

void CloseUDPClient( IPConnection client )
{
  RemoveClient( client->server, client );
  
  if( client->server == NULL ) close( client->socket->fd );
  else if( *(client->server->ref_clientsCount) == 0 ) CloseUDPServer( client->server );

  if( client->buffer != NULL ) free( client->buffer );
  free( client );
}

void IP_CloseConnection( IPConnection connection )
{
  if( connection == NULL ) return;

  // Each TCP connection has its own socket, so we can close it without problem. But UDP connections
  // from the same server share the socket, so we need to wait for all of them to be stopped to close the socket
  connection->ref_Close( connection );
}
