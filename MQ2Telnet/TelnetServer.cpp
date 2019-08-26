/*
 * MacroQuest2: The extension platform for EverQuest
 * Copyright (C) 2002-2019 MacroQuest Authors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <Ws2tcpip.h>

#include "../MQ2Plugin.h"
#include "WinTelnet.h"
#include "TelnetServer.h"

#pragma comment(lib, "ws2_32")

CRITICAL_SECTION ProcessingCS;
CRITICAL_SECTION ListCS;
CRITICAL_SECTION BufferCS;
CRITICAL_SECTION CommandCS;
TELNET* Connections;
SOCKET Listener;
bool bListening;
bool bKillThread;
bool bThreading;
CHATBUF* Sends = nullptr;
CHATBUF* Commands = 0;
bool LocalOnly = true;
bool ANSI = true;
char TelnetLoginPrompt[MAX_STRING] = { 0 };
char TelnetPasswordPrompt[MAX_STRING] = { 0 };
char TelnetWelcome[MAX_STRING] = { 0 };
extern int PortUsed;

DWORD WINAPI ProcessingThread(void* lpParam)
{
	EnterCriticalSection(&ProcessingCS);

	CTelnetServer* server = (CTelnetServer*)lpParam;
	bThreading = true;
	char Buffer[4096] = { 0 };
	SOCKET incoming;
	char szAddr[MAX_STRING] = { 0 };

	while (!bKillThread)
	{
		// process new connections
		if (bListening)
		{
			incoming = accept(Listener, nullptr, nullptr);
			if (incoming != INVALID_SOCKET)
			{
				int ret = 0;
				sockaddr_in addr;
				if (LocalOnly)
				{
					int namesize = sizeof(addr);
					int ret;
					ret = getpeername(incoming, (sockaddr*)& addr, &namesize);
				}
				inet_ntop(AF_INET, &addr.sin_addr, szAddr, sizeof(szAddr));

				if ((bKillThread) || (ret == SOCKET_ERROR) || (LocalOnly && !strcmp("127.0.0.1", szAddr)))
				{
					DebugSpewAlways("ListenThread: Closing new connection...");
					closesocket(incoming);
					continue;
				}

				EnterCriticalSection(&ListCS);

				TELNET* NewConn = new TELNET;
				memset(NewConn, 0, sizeof(TELNET));

				CWinTelnet* telnet = new CWinTelnet;
				telnet->m_Socket = incoming;

				unsigned long nonblocking = 1;
				ioctlsocket(incoming, FIONBIO, &nonblocking);

				NewConn->connection = telnet;
				NewConn->pNext = Connections;
				if (Connections)
					Connections->pLast = NewConn;
				Connections = NewConn;

				LeaveCriticalSection(&ListCS);
			}
		}

		// process sends
		EnterCriticalSection(&BufferCS);
		TELNET* Conn = Connections;
		char szText[MAX_STRING * 25] = { 0 };
		CHATBUF* pBuff = Sends;
		while (pBuff)
		{
			CHATBUF* NextChat = pBuff->pNext;
			strcat_s(szText, pBuff->szText);
			strcat_s(szText, "\r\n");
			free(pBuff);
			pBuff = NextChat;
		}
		Sends = 0;
		LeaveCriticalSection(&BufferCS);

		EnterCriticalSection(&ListCS);
		while (Conn)
		{
			if (!Conn->connection->isConnected())
			{
				// remove connection...
				TELNET* Next = Conn->pNext;
				while (Conn->Received)
				{
					CHATBUF* rNext = Conn->Received->pNext;
					free(Conn->Received);
					Conn->Received = rNext;
				}
				if (Conn->pLast)
					Conn->pLast->pNext = Conn->pNext;
				else
					Connections = Conn->pNext;
				if (Conn->pNext)
					Conn->pNext->pLast = Conn->pLast;

				delete Conn->connection;
				delete Conn;
				Conn = Next;
				continue;
			}

			switch (Conn->State)
			{
			case TS_MAININPUT:
				Conn->connection->WriteStr(szText);
				break;
			case TS_SENDLOGIN:
				Conn->connection->WriteStr(TelnetLoginPrompt);
				Conn->State = TS_GETLOGIN;
				break;
			case TS_SENDPASSWORD:
				Conn->connection->WriteStr(TelnetPasswordPrompt);
				Conn->State = TS_GETPASSWORD;
				break;
			}
			Conn = Conn->pNext;
		}

		// process receives
		Conn = Connections;
		while (Conn)
		{
			// update receive buffer for this connection
			int isize = 0;
			if (isize = Conn->connection->isData())
			{
				memset(Buffer, 0, 4096);
				if (isize > 2047) isize = 2047;
				strcpy_s(Buffer, Conn->Buffer);
				isize = Conn->connection->Read(&Buffer[strlen(Buffer)], isize);
				int begin = 0;
				for (int i = 0; Buffer[i]; i++)
				{
					if (Buffer[i] == 0x0a || Buffer[i] == 0x0d || (i - begin > 250))
					{
						// carriage return, see if we have a length
						if (i - begin)
						{
							// we do. make a command out of it.
							CHATBUF* newcmd = (CHATBUF*)calloc(1, sizeof(CHATBUF));
							if (newcmd)
							{
								memcpy(newcmd->szText, &Buffer[begin], i - begin);
								newcmd->pNext = nullptr;
								if (!Conn->Received)
								{
									Conn->Received = newcmd;
								}
								else
								{
									CHATBUF* pCurrent = nullptr;
									for (pCurrent = Conn->Received; pCurrent->pNext; pCurrent = pCurrent->pNext);
									pCurrent->pNext = newcmd;
								}
							}
						}
						begin = i + 1;
					}
				}

				if (isize)
				{
					strcpy_s(Conn->Buffer, &Buffer[begin]);
				}
			}

			while (Conn->Received)
			{
				if (Conn->State == TS_MAININPUT)
				{
					EnterCriticalSection(&CommandCS);
					// Add command to list

					CHATBUF* pChat = (CHATBUF*)calloc(1, sizeof(CHATBUF));
					if (pChat)
					{
						strcpy_s(pChat->szText, Conn->Received->szText);
						pChat->pNext = nullptr;
						if (!Commands)
						{
							Commands = pChat;
						}
						else
						{
							CHATBUF* pCurrent;
							for (pCurrent = Commands; pCurrent->pNext; pCurrent = pCurrent->pNext);
							pCurrent->pNext = pChat;
						}
					}

					LeaveCriticalSection(&CommandCS);
				}
				else if (Conn->State == TS_GETLOGIN)
				{
					// process user name
					char pwd[32] = { 0 };
					if (server->IsValidUser(Conn->Received->szText, pwd))
					{
						Conn->State = TS_SENDPASSWORD; // send password prompt
						strcpy_s(Conn->Username, Conn->Received->szText);
						strcpy_s(Conn->Password, pwd);
					}
					else
					{
						Conn->connection->WriteStr("invalid\r\n");
						Conn->State = TS_SENDLOGIN;
					}
				}
				else if (Conn->State == TS_GETPASSWORD)
				{
					// process password
					if (!strcmp(Conn->Password, Conn->Received->szText))
					{
						Conn->connection->WriteStr(TelnetWelcome);
						Conn->connection->WriteStr("\r\n");
						Conn->State = TS_MAININPUT;
					}
					else
					{
						Conn->connection->WriteStr("invalid\r\n");
						if (++Conn->PasswordTries >= 3)
						{
							Conn->connection->WriteStr("3 strikes, you're out. later.\r\n");
							Conn->connection->Disconnect();
						}
						Conn->State = TS_SENDPASSWORD;
					}
				}
				else
				{
					break;
				}

				CHATBUF* Next = Conn->Received->pNext;
				free(Conn->Received);
				Conn->Received = Next;
			}

			Conn = Conn->pNext;
		}

		LeaveCriticalSection(&ListCS);
		Sleep(10);
	}

	LeaveCriticalSection(&ProcessingCS);
	DebugSpew("MQ2Telnet processing thread ending");
	bThreading = false;
	return 0;
}

CTelnetServer::CTelnetServer()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 0), &wsa) || (HIBYTE(wsa.wVersion) != 0) || (LOBYTE(wsa.wVersion) != 2))
	{
		// failed...
	}

	Sends = nullptr;
	Commands = nullptr;
	Connections = nullptr;
	bListening = false;
	bKillThread = false;
	LocalOnly = false;
	bThreading = false;
	InitializeCriticalSection(&ProcessingCS);
	InitializeCriticalSection(&ListCS);
	InitializeCriticalSection(&BufferCS);
	InitializeCriticalSection(&CommandCS);
	DWORD ThreadId;
	CreateThread(nullptr, 0, &ProcessingThread, this, 0, &ThreadId);

}

CTelnetServer::~CTelnetServer()
{
	DebugTry(Shutdown());
	DebugTry(DeleteCriticalSection(&ProcessingCS));
	DebugTry(DeleteCriticalSection(&ListCS));
	DebugTry(DeleteCriticalSection(&BufferCS));
	DebugTry(DeleteCriticalSection(&CommandCS));
	// DONT WSACleanup(?)
}


void CTelnetServer::ShutdownListener()
{
	if (bListening)
	{
		bListening = false;
		shutdown(Listener, SD_BOTH);
		closesocket(Listener);
		Listener = INVALID_SOCKET;
	}
}

bool CTelnetServer::Listen(int Port)
{
	if (bListening)
		ShutdownListener();
	Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (Listener == INVALID_SOCKET) {
		DebugSpewAlways("SetupServer: Unable to build socket");
		return false;
	}

	SOCKADDR_IN saServer;
	Port--;
	do {
		saServer.sin_family = AF_INET;
		saServer.sin_addr.s_addr = INADDR_ANY;
		saServer.sin_port = htons(++Port);

	} while (SOCKET_ERROR == bind(Listener, (LPSOCKADDR)& saServer, sizeof(struct sockaddr)));

	if (SOCKET_ERROR == listen(Listener, SOMAXCONN)) {
		DebugSpewAlways("SetupServer: Unable to listen on port %d", Port);
		closesocket(Listener);
		return false;
	}

	PortUsed = Port;
	unsigned long nonblocking = 1;
	ioctlsocket(Listener, FIONBIO, &nonblocking);
	bListening = true;
	return true;
}

void CTelnetServer::Broadcast(char* String)
{
	EnterCriticalSection(&BufferCS);
	CHATBUF* pChat = (CHATBUF*)malloc(sizeof(CHATBUF));

	if (pChat)
	{
		strcpy_s(pChat->szText, String);
		pChat->pNext = nullptr;
		if (!Sends)
		{
			Sends = pChat;
		}
		else
		{
			CHATBUF* pCurrent;
			for (pCurrent = Sends; pCurrent->pNext; pCurrent = pCurrent->pNext);
			pCurrent->pNext = pChat;
		}
	}

	LeaveCriticalSection(&BufferCS);
}

void CTelnetServer::Shutdown()
{
	// close listener
	DebugTry(ShutdownListener());
	bKillThread = true;
	while (bThreading) Sleep(20);

	// the critical section wasnt holding for some reason..
	//EnterCriticalSection(&ProcessingCS); // wait until thread shuts down..
	EnterCriticalSection(&CommandCS);

	// close all connections
	while (Connections)
	{
		// remove connection...
		TELNET* Next = Connections->pNext;
		while (Connections->Received)
		{
			CHATBUF* rNext = Connections->Received->pNext;
			free(Connections->Received);
			Connections->Received = rNext;
		}

		delete Connections->connection;
		delete Connections;
		Connections = Next;
	}

	// delete all extra shit
	while (Sends)
	{
		CHATBUF* pNext = Sends->pNext;
		free(Sends);
		Sends = pNext;
	}

	while (Commands)
	{
		CHATBUF* pNext = Commands->pNext;
		free(Commands);
		Commands = pNext;
	}

	LeaveCriticalSection(&CommandCS);
}

bool CTelnetServer::IsValidUser(char* user, char* pwdest)
{
	if (!GetPrivateProfileString("Users", user, nullptr, pwdest, 31, INIFileName))
		return false;

	pwdest[31] = 0;
	return true;
}

void CTelnetServer::ProcessIncoming()
{
	EnterCriticalSection(&CommandCS);
	if (Commands) // process only 1 per pulse, no loop.
	{
		CHARINFO* pCharInfo = GetCharInfo();
		SPAWNINFO* pSpawn = (SPAWNINFO*)pLocalPlayer;
		if (pCharInfo) pSpawn = pCharInfo->pSpawn;
		CHATBUF* Next = Commands->pNext;
		DoCommand(pSpawn, Commands->szText);
		free(Commands);
		Commands = Next;
	}

	LeaveCriticalSection(&CommandCS);
}
