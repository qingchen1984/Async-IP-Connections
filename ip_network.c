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

#include "ip_network.h"


///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                      INTERFACE DEFINITION                                       /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Generic structure to store methods and data of any connection type handled by the library
struct _IPConnectionData
{
  SocketPoller* socket;
  union {
    char* (*ref_ReceiveMessage)( IPConnection );
    IPConnection (*ref_AcceptClient)( IPConnection );
  };
  int (*ref_SendMessage)( IPConnection, const char* );
  void (*ref_Close)( IPConnection );
  IPAddressData addressData;
  size_t messageLength;
  union {
    char* buffer;
    IPConnection* clientsList;
  };
  union {
    size_t* ref_clientsCount;
    IPConnection server;
  };
};


///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                        GLOBAL VARIABLES                                         /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

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


static char* ReceiveTCPMessage( IPConnection );
static char* ReceiveUDPMessage( IPConnection );
static int SendTCPMessage( IPConnection, const char* );
static int SendUDPMessage( IPConnection, const char* );
static int SendMessageAll( IPConnection, const char* );
static IPConnection AcceptTCPClient( IPConnection );
static IPConnection AcceptUDPClient( IPConnection );
static void CloseTCPServer( IPConnection );
static void CloseUDPServer( IPConnection );
static void CloseTCPClient( IPConnection );
static void CloseUDPClient( IPConnection );

/////////////////////////////////////////////////////////////////////////////
/////                         NETWORK UTILITIES                         /////
/////////////////////////////////////////////////////////////////////////////

// System calls for getting IP address strings
char* GetAddressString( IPAddress address )
{                                           
  static char addressString[ ADDRESS_LENGTH ];
  
  #ifndef IP_NETWORK_LEGACY
  int error = getnameinfo( address, sizeof(IPAddressData), addressString, ADDRESS_LENGTH - PORT_LENGTH, NULL, 0, NI_NUMERICHOST );
  error = getnameinfo( address, sizeof(IPAddressData), NULL, 0, addressString + strlen( addressString ) + 1, PORT_LENGTH, NI_NUMERICSERV );
  if( error != 0 )
  {
    fprintf( stderr, "getnameinfo: failed getting address string: %s", gai_strerror( error ) );
    return NULL;
  }
  #else
  sprintf( addressString, "%s", inet_ntoa( ((IPAddressData*) address)->sin_addr ) );
  sprintf( addressString + strlen( addressString ) + 1, "%u", ((IPAddressData*) address)->sin_port );
  #endif
  addressString[ strlen( addressString ) ] = '/';
  
  return addressString;
}

// Utility method to request an address (host and port) string for client connections (returns default values for server connections)
char* IP_GetAddress( IPConnection connection )
{
  if( connection == NULL ) return NULL;
  
  return GetAddressString( (IPAddress) &(connection->addressData) );
}

// Returns number of active clients for a connection 
size_t IP_GetClientsNumber( IPConnection connection )
{
  if( connection == NULL ) return 0;
  
  if( IP_IsServer( connection ) )
    return *(connection->ref_clientsCount);
  
  return 1;
}

bool IP_IsServer( IPConnection connection )
{
  if( connection == NULL ) return false;
  
  if( connection->ref_Close == CloseTCPServer || connection->ref_Close == CloseUDPServer ) return true;
  
  return false;
}


//////////////////////////////////////////////////////////////////////////////////
/////                             INITIALIZATION                             /////
//////////////////////////////////////////////////////////////////////////////////

#ifndef IP_NETWORK_LEGACY
static int CompareSockets( const void* ref_socket_1, const void* ref_socket_2 )
{
  return ( ((SocketPoller*) ref_socket_1)->fd - ((SocketPoller*) ref_socket_2)->fd );
}
#endif

// Handle construction of a IPConnection structure with the defined properties
static IPConnection AddConnection( Socket socketFD, IPAddress address, uint8_t transportProtocol, uint8_t networkRole )
{
  IPConnection connection = (IPConnection) malloc( sizeof(IPConnectionData) );
  memset( connection, 0, sizeof(IPConnectionData) );
  
  #ifndef IP_NETWORK_LEGACY
  SocketPoller cmpPoller = { .fd = socketFD };
  connection->socket = (SocketPoller*) bsearch( &cmpPoller, polledSocketsList, polledSocketsNumber, sizeof(SocketPoller), CompareSockets );
  if( connection->socket == NULL )
  {
    connection->socket = &(polledSocketsList[ polledSocketsNumber ]);
    connection->socket->fd = socketFD;
    connection->socket->events = POLLRDNORM | POLLRDBAND;
    polledSocketsNumber++;
    qsort( polledSocketsList, polledSocketsNumber, sizeof(SocketPoller), CompareSockets );
  }
  #else
  connection->socket = (SocketPoller*) malloc( sizeof(SocketPoller) );
  FD_SET( socketFD, &polledSocketsSet );
  if( socketFD >= polledSocketsNumber ) polledSocketsNumber = socketFD + 1;
  connection->socket->fd = socketFD;
  #endif
  
  connection->messageLength = IP_MAX_MESSAGE_LENGTH;
  
  memcpy( &(connection->addressData), address, sizeof(IPAddressData) );
  connection->ref_clientsCount = malloc( sizeof(size_t) );
  
  if( networkRole == IP_SERVER ) // Server role connection
  {
    connection->clientsList = NULL;
    connection->ref_AcceptClient = ( transportProtocol == IP_TCP ) ? AcceptTCPClient : AcceptUDPClient;
    connection->ref_SendMessage = SendMessageAll;
    if( transportProtocol == IP_UDP && IS_IP_MULTICAST_ADDRESS( address ) ) connection->ref_SendMessage = SendUDPMessage;
    connection->ref_Close = ( transportProtocol == IP_TCP ) ? CloseTCPServer : CloseUDPServer;
    *(connection->ref_clientsCount) = 0;
  }
  else
  { 
    //connection->address->sin6_family = AF_INET6;
    connection->buffer = (char*) calloc( IP_MAX_MESSAGE_LENGTH, sizeof(char) );
    connection->ref_ReceiveMessage = ( transportProtocol == IP_TCP ) ? ReceiveTCPMessage : ReceiveUDPMessage;
    connection->ref_SendMessage = ( transportProtocol == IP_TCP ) ? SendTCPMessage : SendUDPMessage;
    connection->ref_Close = ( transportProtocol == IP_TCP ) ? CloseTCPClient : CloseUDPClient;
    connection->server = NULL;
  }
  
  return connection;
}

// Add defined connection to the client list of the given server connection
static inline void AddClient( IPConnection server, IPConnection client )
{
  size_t clientIndex = 0, clientsListSize = 0;
  
  client->server = server;
  
  size_t clientsNumber = *((size_t*) server->ref_clientsCount);
  while( clientIndex < clientsNumber )
  {
    clientsListSize++;
    if( server->clientsList[ clientIndex ] == NULL ) break;
    clientIndex++;      
  }
  
  if( clientIndex == clientsListSize ) server->clientsList = (IPConnection*) realloc( server->clientsList, ( clientIndex + 1 ) * sizeof(IPConnection) );
  
  server->clientsList[ clientIndex ] = client;
  
  (*(server->ref_clientsCount))++;

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
    if( networkRole == IP_SERVER )
    {
      hints.ai_flags |= AI_PASSIVE;                   // Set address for me
      hints.ai_family = AF_INET6;                     // IPv6 address
    }
    else // if( networkRole == IP_CLIENT )
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
  
  if( protocol == IP_TCP ) 
  {
    socketType = SOCK_STREAM;
    transportProtocol = IPPROTO_TCP;
  }
  else if( protocol == IP_UDP ) 
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
    fprintf( stderr, "socket: failed opening %s %s socket", ( protocol == IP_TCP ) ? "TCP" : "UDP", ( address->sa_family == AF_INET6 ) ? "IPv6" : "IPv4" );                                                              
  
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
    case( IP_TCP | IP_SERVER ): if( !BindTCPServerSocket( socketFD, address ) ) return NULL;
      break;
    case( IP_UDP | IP_SERVER ): if( !BindUDPServerSocket( socketFD, address ) ) return NULL;
      break;
    case( IP_TCP | IP_CLIENT ): if( !ConnectTCPClientSocket( socketFD, address ) ) return NULL;
      break;
    case( IP_UDP | IP_CLIENT ): if( !ConnectUDPClientSocket( socketFD, address ) ) return NULL;
      break;
    default: fprintf( stderr, "invalid connection type: %x", connectionType );
      return NULL;
  } 
  
  return AddConnection( socketFD, address, (connectionType & TRANSPORT_MASK), (connectionType & ROLE_MASK) ); // Build the IPConnection structure
}

size_t IP_SetMessageLength( IPConnection connection, size_t messageLength )
{
  if( connection == NULL ) return 0;
  
  connection->messageLength = ( messageLength > IP_MAX_MESSAGE_LENGTH ) ? IP_MAX_MESSAGE_LENGTH : (uint16_t) messageLength;
  
  return (size_t) connection->messageLength;
}


/////////////////////////////////////////////////////////////////////////////////////////
/////                             GENERIC COMMUNICATION                             /////
/////////////////////////////////////////////////////////////////////////////////////////

char* IP_ReceiveMessage( IPConnection connection ) 
{ 
  memset( connection->buffer, 0, IP_MAX_MESSAGE_LENGTH );
  
  return connection->ref_ReceiveMessage( connection ); 
}

int IP_SendMessage( IPConnection connection, const char* message ) 
{ 
  if( strlen( message ) + 1 > connection->messageLength )
  {
    fprintf( stderr, "message too long (%lu bytes for %lu max) !", strlen( message ), connection->messageLength );
    return 0;
  }
  
  //DEBUG_PRINT( "connection socket %d sending message: %s", connection->socket->fd, message );
  
  return connection->ref_SendMessage( connection, message ); 
}

IPConnection IP_AcceptClient( IPConnection connection ) { return connection->ref_AcceptClient( connection ); }

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

bool IP_IsDataAvailable( IPConnection connection )
{
  if( connection == NULL ) return false;
  
  #ifndef IP_NETWORK_LEGACY
  if( connection->socket->revents & POLLRDNORM ) return true;
  else if( connection->socket->revents & POLLRDBAND ) return true;
  #else
  if( FD_ISSET( connection->socket->fd, &activeSocketsSet ) ) return true;
  #endif
  
  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////
/////                      SPECIFIC TRANSPORT/ROLE COMMUNICATION                    /////
/////////////////////////////////////////////////////////////////////////////////////////

static inline void RemoveSocket( Socket );

// Try to receive incoming message from the given TCP client connection and store it on its buffer
static char* ReceiveTCPMessage( IPConnection connection )
{
  int bytesReceived;
  
  if( connection->socket->fd == INVALID_SOCKET ) return NULL;

  // Blocks until there is something to be read in the socket
  bytesReceived = recv( connection->socket->fd, connection->buffer, connection->messageLength, 0 );

  if( bytesReceived == SOCKET_ERROR )
  {
    fprintf( stderr, "recv: error reading from socket %d", connection->socket->fd );
    //connection->socket->fd = INVALID_SOCKET;
    //RemoveSocket( connection->socket->fd );
    return NULL;
  }
  else if( bytesReceived == 0 )
  {
    fprintf( stderr, "recv: remote connection with socket %d closed", connection->socket->fd );
    //connection->socket->fd = INVALID_SOCKET;
    RemoveSocket( connection->socket->fd );
    return NULL;
  }
  
  //DEBUG_PRINT( "socket %d received message: %s", connection->socketFD, connection->buffer );
  
  return connection->buffer;
}

// Send given message through the given TCP connection
static int SendTCPMessage( IPConnection connection, const char* message )
{
  if( send( connection->socket->fd, message, connection->messageLength, 0 ) == SOCKET_ERROR )
  {
    fprintf( stderr, "send: error writing to socket %d", connection->socket->fd );
    return -1;
  }
  
  return 0;
}

// Try to receive incoming message from the given UDP client connection and store it on its buffer
static char* ReceiveUDPMessage( IPConnection connection )
{
  struct sockaddr_storage address = { 0 };
  socklen_t addressLength;
  
  // Blocks until there is something to be read in the socket
  if( recvfrom( connection->socket->fd, connection->buffer, connection->messageLength, MSG_PEEK, (IPAddress) &address, &addressLength ) == SOCKET_ERROR )
  {
    //fprintf( stderr, "recvfrom: error reading from socket %d", connection->socket->fd );
    return NULL;
  }

  //DEBUG_PRINT( "socket %d received message: %s", connection->socket->fd, connection->buffer );
  //DEBUG_PRINT( "comparing %u to %u", ntohs( ((struct sockaddr_in*) &(connection->addressData))->sin_port ), ntohs( ((struct sockaddr_in*) &address)->sin_port ) ); 

  // Verify if incoming message is destined to this connection (and returns the message if it is)
  if( ARE_EQUAL_IP_ADDRESSES( &(connection->addressData), &address ) )
  {
    recv( connection->socket->fd, connection->buffer, connection->messageLength, 0 );  
    //DEBUG_PRINT( "socket %d received right message: %s", connection->socket->fd, connection->buffer );
    return connection->buffer;
  }
  
  // Default return message (the received one was not destined to this connection) 
  return NULL;
}

// Send given message through the given UDP connection
static int SendUDPMessage( IPConnection connection, const char* message )
{
  if( sendto( connection->socket->fd, message, connection->messageLength, 0, (IPAddress) &(connection->addressData), sizeof(IPAddressData) ) == SOCKET_ERROR )
  {
    fprintf( stderr, "sendto: error writing to socket %d", connection->socket->fd );
    return -1;
  }
  
  return 0;
}

// Send given message to all the clients of the given server connection
static int SendMessageAll( IPConnection connection, const char* message )
{
  size_t clientIndex = 0;
  size_t clientsNumber = *((size_t*) connection->ref_clientsCount);
  while( clientIndex < clientsNumber )
  {
    if( connection->clientsList[ clientIndex ] != NULL )
    {
      IP_SendMessage( connection->clientsList[ clientIndex ], message );
      clientIndex++;
    }
  }
  
  return 0;
}

// Waits for a remote connection to be added to the client list of the given TCP server connection
static IPConnection AcceptTCPClient( IPConnection server )
{
  IPConnection client;
  int clientSocketFD;
  static struct sockaddr_storage clientAddress;
  static socklen_t addressLength = sizeof(clientAddress);
  
  clientSocketFD = accept( server->socket->fd, (struct sockaddr *) &clientAddress, &addressLength );

  if( clientSocketFD == INVALID_SOCKET )
  {
    fprintf( stderr, "accept: failed accepting connection on socket %d", server->socket->fd );
    return NULL;
  }
  
  client = AddConnection( clientSocketFD, (IPAddress) &clientAddress, IP_TCP, false );

  AddClient( server, client );

  return client;
}

// Waits for a remote connection to be added to the client list of the given UDP server connection
static IPConnection AcceptUDPClient( IPConnection server )
{
  static char buffer[ IP_MAX_MESSAGE_LENGTH ];

  struct sockaddr_storage clientAddress = { 0 };
  socklen_t addressLength = sizeof(clientAddress);
  if( recvfrom( server->socket->fd, buffer, IP_MAX_MESSAGE_LENGTH, MSG_PEEK, (IPAddress) &clientAddress, &addressLength ) == SOCKET_ERROR )
  {
    fprintf( stderr, "recvfrom: error reading from socket %d", server->socket->fd );
    return NULL;
  }
  
  // Verify if incoming message belongs to unregistered client (returns default value if not)
  size_t clientsNumber = *(server->ref_clientsCount);
  for( size_t clientIndex = 0; clientIndex < clientsNumber; clientIndex++ )
  {
    if( ARE_EQUAL_IP_ADDRESSES( &(server->clientsList[ clientIndex ]->addressData), &clientAddress ) )
      return NULL;
  }
  
  IPConnection client = AddConnection( server->socket->fd, (IPAddress) &clientAddress, IP_UDP, false );

  AddClient( server, client );
  
  //DEBUG_PRINT( "client accepted (clients count after: %lu)", *(server->ref_clientsCount) );
  
  return client;
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
