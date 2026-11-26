#ifndef _WIN32
#include <unistd.h>
#if defined(__APPLE__)
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "packet_io.h"
#include <errno.h>
#include <vector>

#include "socket_handler.h"
#include "common.h"

using namespace COMMON;

SocketHandler::SocketHandler()
{
	s = -1;
	buf_size = 0;
	connected = 0;
	fp_ptr = 0;
}

SocketHandler::SocketHandler(int s, struct sockaddr_in s_in)
{
	Init(s, s_in);
}

int SocketHandler::Init(int s_, struct sockaddr_in s_in_)
{
	u_long on_mode = 1;
	
	s = s_;
	s_in = s_in_;
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif
	
	buf_size = 0;

	fp_ptr = 0;
	
	connected = 1;
	
	strcpy(ipaddress,(char*)inet_ntoa(s_in.sin_addr));
	
#ifdef _WIN32
	ioctlsocket(s, FIONBIO, &on_mode);
#else		
	ioctl(s, FIONBIO, &on_mode);
#endif
	
	printf("socket connected:%s\n", ipaddress);
	
	return 1;
}

int SocketHandler::Connected()
{
	return connected;
}

int SocketHandler::Disconnect()
{
	if(connected)
	{
		//first close
#ifdef _WIN32
		closesocket(s);
#else
		close(s);
#endif
		
		//set stuff...
		connected = 0;
		printf("socket disconnected: %s\n", ipaddress);
		
		//clear out
		ipaddress[0] = 0;
	}

	return 1;
}

int SocketHandler::recv_good(int rcv_amt)
{
	if(rcv_amt > 0) return 1;
	if(rcv_amt == 0) { Disconnect(); return 0; }
#ifdef _WIN32
	int error = WSAGetLastError();
	if(error != WSAEWOULDBLOCK && error != WSAEINTR) Disconnect();
#else
	if(errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) Disconnect();
#endif
	return 0;
}

int SocketHandler::PacketAvailable()
{
	int rcv_amt;
	char temp_buf[MAX_BUF_SIZE];
	int pack_size;
	
	rcv_amt = recv(s, temp_buf, sizeof(int), MSG_PEEK);

	if(!recv_good(rcv_amt)) return 0;

	//get pack size
	if(rcv_amt != sizeof(int))
		return 0;
	else
		{
			int payload = PacketGetInt(temp_buf, 0);
			if(payload < 0 || payload > MAX_BUF_SIZE - 8) { Disconnect(); return 0; }
			pack_size = payload + 8;
		}

	//bad packet so just disconnect?
	if(pack_size < 8 || pack_size > MAX_BUF_SIZE)
	{
		Disconnect();
		return 0;
	}

	//check if there is the right amount of data on the buffer
	//there has to be a faster way...
	rcv_amt = recv(s, temp_buf, pack_size, MSG_PEEK);

	//if(!recv_good(rcv_amt)) return 0;

	return rcv_amt == pack_size;
}

int SocketHandler::GetPacket(char **message, int *size, int *pack_id)
{
	if(!PacketAvailable()) return 0;
	int rcv_amt;
	//char temp_buf[MAX_BUF_SIZE];
	char *temp_buf = dp_temp_buf;
	int pack_size;
	
	rcv_amt = recv(s, temp_buf, sizeof(int), MSG_PEEK);

	if(!recv_good(rcv_amt)) return 0;

	//get pack size
	if(rcv_amt != sizeof(int))
		return 0;
	else
		{
			int payload = PacketGetInt(temp_buf, 0);
			if(payload < 0 || payload > MAX_BUF_SIZE - 8) { Disconnect(); return 0; }
			pack_size = payload + 8;
		}

	//bad packet so just disconnect?
	if(pack_size < 8 || pack_size > MAX_BUF_SIZE)
	{
		Disconnect();
		return 0;
	}

	rcv_amt = recv(s, temp_buf, pack_size, 0);

	if(!recv_good(rcv_amt)) return 0;

	if(rcv_amt != pack_size) 
	{
		printf("SocketHandler::GetPacket:rcv_amt != pack_size\n");
		return 0;
	}

	*size = PacketGetInt(temp_buf, 0);
	*pack_id = PacketGetInt(temp_buf, 4);
	*message = temp_buf + 8;

	if(*size < 0) return 0;

	return 1;
}

int SocketHandler::DoRecv()
{
	int rcv_amt;
	char temp_buf[MAX_BUF_SIZE];
	
	rcv_amt = recv(s, temp_buf, MAX_BUF_SIZE, 0);

	if(!recv_good(rcv_amt)) return 0;

//#ifdef _WIN32
//	int terr = WSAGetLastError();
//	if(terr == 10054 || (!terr && !rcv_amt))
//#else
//		if(!rcv_amt)
//#endif
//	{
//		Disconnect();
//		return 0;
//	}
//	else if(rcv_amt == -1)
//	{
//		//simply no packet yet
//		return 0;
//	}
	
	//process that monkey yo!
	//should we clear?
	if(rcv_amt + buf_size > MAX_DATA_STORED) 
	{
		printf("WARNING: receive buffer exceeded\n");
		Disconnect();
		buf_size = fp_ptr = 0;
		return 0;
	}
	
	//fill it
	memcpy(buf + buf_size, temp_buf, rcv_amt);
	buf_size += rcv_amt;
	
	return 1;
}

int SocketHandler::DoProcess(char **message, int *size, int *pack_id)
{
	if(buf_size < 8) return 0;
	int payload = PacketGetInt(buf, 0);
	// Validate before adding the header, so INT_MAX cannot overflow.
	if(payload < 0 || payload > MAX_DATA_STORED - 8)
	{
		Disconnect(); buf_size = fp_ptr = 0; return 0;
	}
	int packet_size = payload + 8;
	if(packet_size > buf_size) return 0;
	*size = payload;
	*pack_id = PacketGetInt(buf, 4);
	memcpy(dp_temp_buf, buf + 8, payload);
	*message = dp_temp_buf;
	buf_size -= packet_size;
	if(buf_size) memmove(buf, buf + packet_size, buf_size);
	return 1;
}

int SocketHandler::DoFastProcess(char **message, int *size, int *pack_id)
{
	int remaining = buf_size - fp_ptr;
	if(remaining < 8) return 0;
	char *packet = buf + fp_ptr;
	int payload = PacketGetInt(packet, 0);
	if(payload < 0 || payload > MAX_DATA_STORED - 8)
	{
		Disconnect(); buf_size = fp_ptr = 0; return 0;
	}
	int packet_size = payload + 8;
	if(packet_size > remaining) return 0;
	*size = payload;
	*pack_id = PacketGetInt(packet, 4);
	*message = packet + 8;
	fp_ptr += packet_size;
	return 1;
}

void SocketHandler::ResetFastProcess()
{
	if(!fp_ptr) return;

	char *fp_buf = buf + fp_ptr;
	int fp_buf_size = buf_size - fp_ptr;

	//if some remaining then push it back to the front
	if(fp_buf_size) memmove(buf, fp_buf, fp_buf_size);

	//set real size to the fp size
	buf_size = fp_buf_size;

	//reset ptr
	fp_ptr = 0;
}

int SocketHandler::SendMessage(int pack_id, const char *data, int size)
{
	if(!connected || size < 0 || size > MAX_DATA_STORED - 8 || (size && !data)) return 0;
    // Replies can be sent from a receive callback on SDL's small server stack.
    // Keep packet storage off that stack, and allocate only the validated frame.
    std::vector<char> frame(static_cast<size_t>(size) + 8);
    char *temp_buf = frame.data();
	int error_no;
	
		//if(!pause_for_send())
	//{
	//	Disconnect();
	//	return 0;
	//}
	
	//pack
	PacketSetInt(temp_buf, 0, size);
	PacketSetInt(temp_buf, 4, pack_id);
	if(size) memcpy(temp_buf + sizeof(int) + sizeof(int), data, size);
	
	//send
#ifdef MSG_NOSIGNAL
	error_no = send(s, temp_buf, size + sizeof(int) + sizeof(int), MSG_NOSIGNAL);
#else
	error_no = send(s, temp_buf, size + sizeof(int) + sizeof(int), 0);
#endif
	
// 	print_dump(temp_buf, 20, "send");
	
	//error in send?
	if(error_no <= 0)
	{
		Disconnect();
		return 0;
	}
	
	return 1;
}

int SocketHandler::SendMessageAscii(int pack_id, const char *data)
{
	return SendMessage(pack_id, data, strlen(data) + 1);
}

int SocketHandler::socket_good_to_send(bool &kill_me)
{
	fd_set write_flags;
	fd_set error_flags;
	int the_stat;
	struct timeval waitd;
	
	waitd.tv_sec = 0;
	waitd.tv_usec = 0;
	
	FD_ZERO(&write_flags);
	FD_ZERO(&error_flags);
	FD_SET(s, &write_flags);
	FD_SET(s, &error_flags);
	
	the_stat = select(s + 1, (fd_set*)0, &write_flags, &error_flags, &waitd);
	
	if(the_stat < 0)
	{
#ifdef _WIN32
		printf("select errno:%d\n",errno);
#else
		printf("select errno:%d-%s\n",errno, strerror(errno));
#endif
		switch(errno)
		{
		case 9: //bad file descriptor
			kill_me = true;
			break;
		}
		return 0;
	}
	
	if(FD_ISSET(s, &error_flags))
	{
		printf("socket_good_to_send:error flag set\n");
		return 0;
	}
	
	if(FD_ISSET(s, &write_flags))
	{
		FD_CLR(s, &write_flags);
		return 1;
	}
	else
		return 0;
}

int SocketHandler::pause_for_send()
{
	const double max_wait = 0.5;
	double start_time;
	bool kill_me = false;
	
	if(socket_good_to_send(kill_me)) return 1;
	
	start_time = current_time();
	
	do
	{
		uni_pause(10);
		
		printf("pause_for_send:had to begin pausing\n");
		
		if(socket_good_to_send(kill_me)) return 1;
		if(kill_me) return 0;
	} while(current_time() - start_time < max_wait);
	
	return 0;
}

char *SocketHandler::GetMAC(char *buf)
{
	//clear
	memset(buf, 0, 6);

#ifdef _WIN32
	IP_ADAPTER_INFO AdapterInfo[16];

	DWORD dwBufLen = sizeof(AdapterInfo);

	DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
	if(dwStatus == ERROR_SUCCESS) 
	{
		PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;

		do 
		{
			for(int i=0; i<6;i++)
				if(pAdapterInfo->Address[i])
				{
					memcpy(buf, pAdapterInfo->Address, 6);
					return buf;
				}

			pAdapterInfo = pAdapterInfo->Next;
		}
		while(pAdapterInfo);
	}
#elif defined(__APPLE__)
	// macOS: use getifaddrs() to walk AF_LINK addresses (sockaddr_dl).
	struct ifaddrs *ifap = NULL;
	if (getifaddrs(&ifap) == 0)
	{
		for (struct ifaddrs *p = ifap; p; p = p->ifa_next)
		{
			if (!p->ifa_addr || p->ifa_addr->sa_family != AF_LINK) continue;
			struct sockaddr_dl *sdl = (struct sockaddr_dl *)p->ifa_addr;
			if (sdl->sdl_alen != 6) continue;
			unsigned char *mac = (unsigned char *)LLADDR(sdl);
			bool any = false;
			for (int i = 0; i < 6; i++) if (mac[i]) { any = true; break; }
			if (!any) continue;
			memcpy(buf, mac, 6);
			freeifaddrs(ifap);
			goto mac_done;
		}
		freeifaddrs(ifap);
	}
#else
	int skfd;
	struct ifreq sIfReq;
	struct if_nameindex *pIfList, *pIfList_tofree;

	//create socket
	skfd = socket(AF_INET,SOCK_DGRAM,0);
	if(skfd<0)
	{
		printf( "SocketHandler::GetMAC: create socket failed\n");
		return buf;
	}

	pIfList = if_nameindex();
	pIfList_tofree = pIfList;

	for ( pIfList; *(char *)pIfList != 0; pIfList++ )
	{
		strncpy( sIfReq.ifr_name, pIfList->if_name, IF_NAMESIZE );

		if ( ioctl(skfd, SIOCGIFHWADDR, &sIfReq) != 0 )
			printf( "SocketHandler::GetMAC: ioctl failed\n");
		else
		{
			for(int i=0; i<6;i++)
				if(sIfReq.ifr_ifru.ifru_hwaddr.sa_data[i])
				{
					memcpy(buf, sIfReq.ifr_ifru.ifru_hwaddr.sa_data, 6);

					if(pIfList_tofree) if_freenameindex(pIfList_tofree);
					close(skfd);

					return buf;
				}
		}
	}

	if(pIfList_tofree) if_freenameindex(pIfList_tofree);
	close(skfd);
#endif

mac_done:

	printf("MAC ADDRESS: ");
	for(int i=0;i<6;i++) printf("%02x-", (unsigned char)buf[i]);
	printf("\n");

	return buf;
}

