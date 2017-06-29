//////////////////////////////////////////////////////////////////////////////////////
//                                                                                  //
//  Copyright (c) 2016-2017 Leonardo Consoni <consoni_2519@hotmail.com>             //
//                                                                                  //
//  This file is part of Simple Async IPC.                                          //
//                                                                                  //
//  Simple Async IPC is free software: you can redistribute it and/or modify        //
//  it under the terms of the GNU Lesser General Public License as published        //
//  by the Free Software Foundation, either version 3 of the License, or            //
//  (at your option) any later version.                                             //
//                                                                                  //
//  Simple Async IPC is distributed in the hope that it will be useful,             //
//  but WITHOUT ANY WARRANTY; without even the implied warranty of                  //
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                    //
//  GNU Lesser General Public License for more details.                             //
//                                                                                  //
//  You should have received a copy of the GNU Lesser General Public License        //
//  along with Simple Async IPC. If not, see <http://www.gnu.org/licenses/>.        //
//                                                                                  //
//////////////////////////////////////////////////////////////////////////////////////


#include <stdio.h>

#include "ipc.h"

#include "ipc_base_ip.h"
//#include "ipc_shm.h"


#ifdef WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "threads/threads.h"
#include "threads/thread_safe_queues.h"
#include "threads/thread_safe_maps."

  
///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                      DATA STRUCTURES                                            /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////
  
const size_t QUEUE_MAX_ITEMS = 10;
  
// Structure that stores read and write message queues for a IPCConnection struct used asyncronously
typedef struct _IPCConnectionData
{
  IPConnection baseConnection;
  TSQueue readQueue;
  TSQueue writeQueue;
  size_t messageLength;
}
IPCConnectionData;

typedef struct _MessageData
{
  Byte data[ IPC_MAX_MESSAGE_LENGTH ];
  RemoteID address;
}
MessageData;

typedef MessageData* Message;


// Thread for asyncronous connections update
static Thread globalReadThread = THREAD_INVALID_HANDLE;
static Thread globalWriteThread = THREAD_INVALID_HANDLE;
static volatile bool isNetworkRunning = false;

// Internal (private) list of asyncronous connections created (accessible only by index)
static TSMap globalConnectionsList = NULL;


///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                      INFORMATION UTILITIES                                      /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns the number of asyncronous connections created (method for encapsulation purposes)
size_t IPC_GetActivesNumber()
{
  size_t activeConnectionsNumber = 0;
  
  size_t globalConnectionsListSize = TSM_GetItemsCount( globalConnectionsList );
  for( size_t connectionIndex = 0; connectionIndex < globalConnectionsListSize; connectionIndex++ )
  {
    if( TSM_GetItem( globalConnectionsList, connectionIndex, NULL ) ) activeConnectionsNumber++;
  }
  
  return activeConnectionsNumber;
}

// Returns address string (host and port) for the connection of given identifier
char* IPC_GetAddress( IPCConnection ref_connection )
{
  unsigned long connectionID = (unsigned long) ref_connection;
  IPCConnectionData* connection = (IPCConnectionData*) TSM_AcquireItem( globalConnectionsList, connectionID );
  if( connection == NULL ) return 0;
  
  TSM_ReleaseItem( globalConnectionsList, connectionID );
  
  return IP_GetAddress( connection->baseConnection );
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                       INITIALIZATION                                            /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Forward definition
static void* AsyncReadQueues( void* );
static void* AsyncWriteQueues( void* );

// Creates a new IPConnection structure (from the defined properties) and add it to the asynchronous connection list
IPCConnection IPC_OpenConnection( Byte connectionType, const char* host, uint16_t channel )
{
  IPConnection baseConnection = IP_OpenConnection( connectionType, host, channel );
  if( baseConnection == NULL )
  {
    fprintf( stderr, "failed to create connection type %x on host %s and port %u", connectionType, ( host == NULL ) ? "(ANY)" : host, port );
    return NULL;
  } 
  
  if( globalConnectionsList == NULL ) 
  {
    globalConnectionsList = TSM_Create( TSMAP_INT, sizeof(AsyncIPConnectionData) );
    globalReadThread = Thread_Start( AsyncReadQueues, (void*) globalConnectionsList, THREAD_JOINABLE );
    globalWriteThread = Thread_Start( AsyncWriteQueues, (void*) globalConnectionsList, THREAD_JOINABLE );
  }
  
  AsyncIPConnectionData connectionData = { .baseConnection = baseConnection };
  
  connectionData.readQueue = TSQ_Create( QUEUE_MAX_ITEMS, sizeof(MessageData) );  
  connectionData.writeQueue = TSQ_Create( QUEUE_MAX_ITEMS, sizeof(MessageData) );
  
  unsigned long connectionID = TSM_SetItem( globalConnectionsList, baseConnection, &connectionData );  
  
  return (IPCConnection) connectionID;
}

size_t IPC_SetMessageLength( IPCConnection connection, size_t messageLength )
{
  unsigned long connectionID = (unsigned long) ref_connection;
  IPCConnectionData* connection = (IPCConnectionData*) TSM_AcquireItem( globalConnectionsList, connectionID );
  if( connection == NULL ) return 0;
  
  connection->messageLength = ( messageLength > IPC_MAX_MESSAGE_LENGTH ) ? IPC_MAX_MESSAGE_LENGTH : messageLength;
  
  TSM_ReleaseItem( globalConnectionsList, connectionID );
  
  return connection->messageLength;
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
    if( IP_WaitEvent( 5000 ) > 0 ) 
      TSM_RunForAllKeys( globalConnectionsList, ReadToQueue );
  }
  
  return NULL;
}

static void WriteFromQueue( unsigned long connectionID )
{
  AsyncIPConnection connection = TSM_AcquireItem( globalConnectionsList, connectionID );
  if( connection == NULL ) return;
  
  char firstMessage[ IP_MAX_MESSAGE_LENGTH ];
  
  // Do not proceed if queue is empty
  if( TSQ_GetItemsCount( connection->writeQueue ) == 0 )
  {
    TSM_ReleaseItem( globalConnectionsList, connectionID );
    return;
  }
  
  TSQ_Dequeue( connection->writeQueue, (void*) firstMessage, TSQUEUE_WAIT );
  
  if( IP_SendMessage( connection->baseConnection, firstMessage ) == -1 )
  {
    TSM_ReleaseItem( globalConnectionsList, connectionID );
    TSM_RemoveItem( globalConnectionsList, connectionID );
    return;
  }
  
  TSM_ReleaseItem( globalConnectionsList, connectionID );
}

// Loop of message writing (removing in order from queue) to be called asyncronously for client connections
static void* AsyncWriteQueues( void* args )
{
  isNetworkRunning = true;
  
  while( isNetworkRunning )
  {
    TSM_RunForAllKeys( globalConnectionsList, WriteFromQueue );
    
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
char* IPC_ReadMessage( unsigned long clientID )
{
  static char messageData[ IP_MAX_MESSAGE_LENGTH ];
  char* firstMessage = NULL;
  
  AsyncIPConnection client = TSM_AcquireItem( globalConnectionsList, clientID );
  if( client != NULL )
  {
    if( !IP_IsServer( client->baseConnection ) )
    {
      if( TSQ_GetItemsCount( client->readQueue ) > 0 )
      {
        firstMessage = (char*) &messageData;
        TSQ_Dequeue( client->readQueue, firstMessage, TSQUEUE_WAIT );
      }
    }
    else
      fprintf( stderr, "connection index %lu is not of a client connection", clientID );
  }
  TSM_ReleaseItem( globalConnectionsList, clientID );
  
  return firstMessage;
}

bool IPC_WriteMessage( unsigned long connectionID, const char* message )
{
  AsyncIPConnection connection = TSM_AcquireItem( globalConnectionsList, connectionID );
  if( connection == NULL ) return false;
  
  if( TSQ_GetItemsCount( connection->writeQueue ) >= QUEUE_MAX_ITEMS )
    fprintf( stderr, "connection index %lu write queue is full", connectionID );
  
  TSQ_Enqueue( connection->writeQueue, (void*) message, TSQUEUE_NOWAIT );
  
  TSM_ReleaseItem( globalConnectionsList, connectionID );
  
  return true;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////                                           ENDING                                                /////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

// Handle socket closing and structures destruction for the given index corresponding connection
void IPC_CloseConnection( unsigned long connectionID )
{
  AsyncIPConnection connection = TSM_AcquireItem( globalConnectionsList, connectionID );
  if( connection == NULL ) return;
  
  IP_CloseConnection( connection->baseConnection );
  connection->baseConnection = NULL;
  
  TSQ_Discard( connection->readQueue );
  TSQ_Discard( connection->writeQueue );
  
  TSM_ReleaseItem( globalConnectionsList, connectionID );
  
  TSM_RemoveItem( globalConnectionsList, connectionID );
  
  if( TSM_GetItemsCount( globalConnectionsList ) == 0 )
  {
    isNetworkRunning = false;
    
    (void) Thread_WaitExit( globalReadThread, 5000 );   
    (void) Thread_WaitExit( globalWriteThread, 5000 );
    
    TSM_Discard( globalConnectionsList );
    globalConnectionsList = NULL;
  }
  
  return;
}
