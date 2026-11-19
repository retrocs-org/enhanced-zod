#include "client_socket.h"
#include "common.h"
#include <SDL3/SDL.h>
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace COMMON;

ClientSocket::ClientSocket()
{
	s = -1;port=2137;
	shandler = NULL;
	event_list = NULL;
}

ClientSocket::~ClientSocket()
{
    ClearConnection();
#ifdef _WIN32
    if(network_started) WSACleanup();
#endif
}
bool ClientSocket::InitNetwork()
{
#ifdef _WIN32
    if(!network_started) { WSADATA data; if(WSAStartup(MAKEWORD(2,2),&data)!=0) return false; network_started=true; }
#endif
    return true;
}
bool ClientSocket::Adopt(ZConnectedSocket &connection)
{
    if(!InitNetwork()) return false;
    ClearConnection();
    s=connection.Take();
    if(s<0) return false;
    sockaddr_in address{}; socklen_t length=sizeof(address);
    if(getpeername(s,(sockaddr*)&address,&length)!=0) { ClearConnection(); return false; }
    port=ntohs(address.sin_port);
    shandler=new SocketHandler(s,address);
    if(event_list) event_list->push_back(new Event(OTHER_EVENT,CONNECT_EVENT,0,nullptr,0));
    return true;
}

int ClientSocket::SendMessage(int pack_id, const char *data, int size)
{
	if(shandler) return shandler->SendMessage(pack_id, data, size);
	else return 0;
}

int ClientSocket::SendMessageAscii(int pack_id, const char *data)
{
	if(shandler) return shandler->SendMessageAscii(pack_id, data);
	else return 0;
}

void ClientSocket::SetEventList(list<Event*> *event_list_)
{
	event_list = event_list_;
}

int ClientSocket::Start(const char *address_, int port_)
{
    if(shandler || s != -1) ClearConnection();
	address = address_;
	port = port_;
	
	if(!CreateSocket()) return 0;
	
    if(!Connect()) { ClearConnection(); return 0; }
	
	return 1;
}

void ClientSocket::ClearConnection()
{
	if(shandler)
	{
        shandler->Disconnect();
		delete shandler;
		shandler = NULL;
    }
    else if(s != -1) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
    }
	s = -1;
}

int ClientSocket::Process()
{
	char *message;
	int size;
	int pack_id;
	if(!shandler) return 0;
	
	//not connected, free it up
	if(!shandler->Connected())
	{
		ClearConnection();
		
		event_list->push_back(new Event(OTHER_EVENT, DISCONNECT_EVENT, 0, NULL, 0));
	}
	else if(shandler->DoRecv())
	{
		while(shandler->DoFastProcess(&message, &size, &pack_id))
			event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));
		shandler->ResetFastProcess();

		/*
		while(shandler->DoProcess(&message, &size, &pack_id))
		{
			event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));
 			//printf("ClientSocket::Process:got packet id:%d\n", pack_id);
		}*/
	}

	/*
	while(shandler->PacketAvailable() && shandler->GetPacket(&message, &size, &pack_id))
		event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));	
	*/

	return 1;
}

SocketHandler* ClientSocket::GetHandler()
{
	return shandler;
}

int ClientSocket::Connect()
{
	struct sockaddr_in c_in;
	struct hostent * host;
	
	host = gethostbyname(address.c_str());

	if(!host)
	{
		printf("could not resolve host '%s'\n", address.c_str());
		return 0;
	}

	// Only accept an IPv4 result: this is an AF_INET socket, and on IPv6-first
	// systems (e.g. Android) gethostbyname("localhost") can hand back a 16-byte
	// ::1, which would overflow the 4-byte sin_addr and aim connect() at
	// garbage - the server's accept() then never fires.
	if(host->h_addrtype != AF_INET || host->h_length != 4)
	{
		SDL_Log("ClientSocket: '%s' resolved to a non-IPv4 address (type=%d len=%d); cannot connect",
		        address.c_str(), host->h_addrtype, host->h_length);
		return 0;
	}

	memset((char*)&c_in, 0, sizeof(c_in));
	memcpy((char*)&c_in.sin_addr, (char*)host->h_addr, host->h_length);
	c_in.sin_family = AF_INET;
	c_in.sin_port = htons(port);

	{
		double first_failure = current_time();
		while(connect(s, (struct sockaddr *)&c_in, sizeof c_in) < 0)
		{
			if(current_time() - first_failure > 15.0)
			{
				printf("could not connect to '%s'\n", address.c_str());
				return 0;
			}
			// On BSD/macOS a failed connect() leaves the socket in a state
			// where subsequent connect()s on the same fd can hang. Close and
			// recreate before retrying.
#ifdef _WIN32
			closesocket(s);
#else
			close(s);
#endif
			s = -1;
			if(!CreateSocket()) return 0;
			uni_pause(50);
		}
	}
	
	shandler = new SocketHandler(s,c_in);
	
	event_list->push_back(new Event(OTHER_EVENT, CONNECT_EVENT, 0, NULL, 0));
	
	return 1;
}

int ClientSocket::CreateSocket()
{
	if(s != -1) return 1;
	
    if(!InitNetwork()) return 0;
	
	if((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		printf("ClientSocket::CreateSocket:error in socket creation\n");
		return 0;
	}
	
	return 1;
}
