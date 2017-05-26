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


/// @file async_ip_network.h
/// @brief Asynchronous IP network connection abstraction.
///
/// Library that combines threading utilities with the socket connection library 
/// to provide asyncronous and thread-safe network communication methods.           

#ifndef ASYNC_IP_NETWORK_H
#define ASYNC_IP_NETWORK_H

#include "namespaces.h"

#include "ip_network.h"

#define IP_CONNECTION_INVALID_ID -1      ///< Connection identifier to be returned on initialization errors
  

/// Asynchronous IP network/connections interface definition
#define ASYNC_IP_NETWORK_INTERFACE( Namespace, INIT_FUNCTION ) \
        INIT_FUNCTION( char*, Namespace, GetAddress, unsigned long ) \
        INIT_FUNCTION( size_t, Namespace, GetActivesNumber, void ) \
        INIT_FUNCTION( size_t, Namespace, GetClientsNumber, unsigned long ) \
        INIT_FUNCTION( size_t, Namespace, SetMessageLength, unsigned long, size_t ) \
        INIT_FUNCTION( unsigned long, Namespace, OpenConnection, uint8_t, const char*, uint16_t ) \
        INIT_FUNCTION( void, Namespace, CloseConnection, unsigned long ) \
        INIT_FUNCTION( char*, Namespace, ReadMessage, unsigned long ) \
        INIT_FUNCTION( bool, Namespace, WriteMessage, unsigned long, const char* ) \
        INIT_FUNCTION( unsigned long, Namespace, GetClient, unsigned long )

DECLARE_NAMESPACE_INTERFACE( AsyncIPNetwork, ASYNC_IP_NETWORK_INTERFACE );


#endif // ASYNC_IP_NETWORK_H


/// @struct AsyncIPNetwork
/// @brief Function pointer struct as asynchronous IP network interface 
///   
/// @memberof AsyncIPNetwork
/// @fn unsigned long OpenConnection( uint8_t connectionType, const char* host, uint16_t port )
/// @brief Creates a new IP connection structure (with defined properties) and add it to the asynchronous connections list                              
/// @param[in] connectionType flag defining connection as client or server, TCP or UDP (see ip_connection.h)                                   
/// @param[in] host IPv4 or IPv6 host string (NULL for server listening on any local address)                                         
/// @param[in] port IP port number (local for server, remote for client)       
/// @return unique generic identifier to newly created connection (IP_CONNECTION_INVALID_ID on error) 
///   
/// @memberof AsyncIPNetwork
/// @fn void CloseConnection( unsigned long connectionID )
/// @brief Handle termination of connection corresponding to given identifier                             
/// @param[in] connectionID connection identifier
///   
/// @memberof AsyncIPNetwork
/// @fn char* GetAddress( unsigned long connectionID )                                                                              
/// @brief Returns address string (host and port) for the connection of given identifier                                                
/// @param[in] connectionID connection identifier                                         
/// @return address string ("<host>/<port>")
///   
/// @memberof AsyncIPNetwork
/// @fn size_t GetActivesNumber( void )
/// @brief Returns the number of asyncronous connections created                          
/// @return number of created/active connections
///   
/// @memberof AsyncIPNetwork
/// @fn size_t GetClientsNumber( unsigned long serverID )                                                                            
/// @brief Returns number of clients for the server connection of given identifier                                                
/// @param[in] serverID server connection identifier                                         
/// @return number of clients (1 for client connection and 0 on error)  
///   
/// @memberof AsyncIPNetwork
/// @fn size_t SetMessageLength( unsigned long connectionID, size_t messageLength )                                                                              
/// @brief Defines fixed message length for connection of given identifier                                                
/// @param[in] connectionID connection identifier                                
/// @param[in] messageLength desired length (in bytes, limited by IP_MAX_MESSAGE_LENGTH) of the connection messages                                               
/// @return actual new length of connection messages 
///   
/// @memberof AsyncIPNetwork
/// @fn char* ReadMessage( unsigned long clientID )
/// @brief Pops first (oldest) queued message from read queue of client connection corresponding to given identifier                      
/// @param[in] clientID client connection identifier  
/// @return pointer to message string, overwritten on next call to ReadMessage() (NULL on error or no message available)  
///   
/// @memberof AsyncIPNetwork
/// @fn bool WriteMessage( unsigned long connectionID, const char* message )                                                                              
/// @brief Pushes given message string to write queue of connection corresponding to given identifier                                                
/// @param[in] connectionID connection identifier   
/// @param[in] message message string pointer  
/// @return true on success, false on error  
///   
/// @memberof AsyncIPNetwork
/// @fn unsigned long GetClient( unsigned long serverID )                                                                              
/// @brief Pops first (oldest) queued client identifier from read queue of server connection corresponding to given identifier                                                
/// @param[in] serverID server connection identifier        
/// @return client connection identifier (IP_CONNECTION_INVALID_ID on error or no client available)  
///
/// @memberof AsyncIPNetwork
