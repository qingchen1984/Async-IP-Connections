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


/// @file ip_network.h
/// @brief Platform and type abstractions for synchronous IP connections communication.
///
/// Multiplatform library for creation and handling of Internet Protocol (IP) 
/// sockets connections as server or client, using TCP or UDP protocols                           

#ifndef IP_NETWORK_H
#define IP_NETWORK_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

  
#define IP_MAX_MESSAGE_LENGTH 512       ///< Maximum allowed length of messages transmitted through an IP connection

#define IP_SERVER 0x01                  ///< IP server connection creation flag
#define IP_CLIENT 0x02                  ///< IP client connection creation flag

#define IP_TCP 0x10                     ///< IP TCP (stream) connection creation flag
#define IP_UDP 0x20                     ///< IP UDP (datagram) connection creation flag



/// Structure that stores data of a single IP connection
typedef struct _IPConnectionData IPConnectionData;
/// Opaque type to reference encapsulated IP connection structure
typedef IPConnectionData* IPConnection;


/// @brief Creates a new IP connection structure (with defined properties) and add it to the asynchronous connections list                              
/// @param[in] connectionType flag defining connection as client or server, TCP or UDP (see ip_connection.h)                                   
/// @param[in] host IPv4 or IPv6 host string (NULL for server listening on any local address)                                         
/// @param[in] port IP port number (local for server, remote for client)       
/// @return unique generic identifier to newly created connection (NULL on error) 
IPConnection IP_OpenConnection( uint8_t connectionType, const char* host, uint16_t port );

/// @brief Handle termination of given connection                                   
/// @param[in] connection connection reference
void IP_CloseConnection( IPConnection connection );
                                                                             
/// @brief Returns address string (host and port) for the given connection                                                
/// @param[in] connection connection reference                                         
/// @return address string ("<host>/<port>")
char* IP_GetAddress( IPConnection connection );
                                                                          
/// @brief Returns number of clients for the given server connection                                               
/// @param[in] connection server connection reference                                         
/// @return number of clients (1 for client connection and 0 on error)  
size_t IP_GetClientsNumber( IPConnection connection );

/// @brief Verifies if given connection is of server type/role
/// @param[in] connection connection reference 
/// @return true for server connection, false for client or on error
bool IP_IsServer( IPConnection connection );
                                                                      
/// @brief Defines fixed message length for the given connection                                                
/// @param[in] connection connection reference                                 
/// @param[in] messageLength desired length (in bytes, limited by IP_MAX_MESSAGE_LENGTH) of the connection messages                                               
/// @return actual new length of connection messages 
size_t IP_SetMessageLength( IPConnection connection, size_t messageLength );
 
/// @brief Calls type specific client method for receiving network messages                      
/// @param[in] connection client connection reference  
/// @return pointer to message string, overwritten on next call to ReceiveMessage() (NULL on error)  
char* IP_ReceiveMessage( IPConnection connection );
                                                                             
/// @brief Calls type specific connection method for sending network messages                                                
/// @param[in] connection connection reference   
/// @param[in] message message string pointer  
/// @return 0 on success, -1 on error  
int IP_SendMessage( IPConnection connection, const char* message );
                                                                            
/// @brief Calls type specific server method for accepting new network clients                                                
/// @param[in] connection server connection reference        
/// @return reference to already filled newly accepted client (NULL on error)  
IPConnection IP_AcceptClient( IPConnection connection );
                                                                             
/// @brief Blocks execution on calling thread for given time or until a network event (read/accept) is available                                                
/// @param[in] milliseconds timeout for network events waiting (in milliseconds)    
/// @return number of events detected (0 on timeout or error)  
int IP_WaitEvent( unsigned int milliseconds );
                                                                             
/// @brief Verifies if given connection has data (messages for clients, clients for server) to be read                                                
/// @param[in] connection connection reference        
/// @return true if data is available, false otherwise 
bool IP_IsDataAvailable( IPConnection connection );


#endif // IP_NETWORK_H
