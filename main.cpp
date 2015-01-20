#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

void print_usage()
{
	printf("USAGE: tsgraph dest_ip dest_port interface_ip\n");
}

int sd;
unsigned char buffer[2048];

void read_packets()
{
	int retcode;
	static unsigned int packets = 0;

	retcode = read(sd,buffer,sizeof(buffer));
	packets++;

	if ((packets % 1000) == 0) 
	{
		printf("%i packets received.\n",packets);
	}

}

static void recvpacket()
{
	static unsigned int packets = 0;
	struct msghdr msg;
	struct iovec entry;
	struct sockaddr_in from_addr;
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;
	int res;

	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = buffer;
	entry.iov_len = sizeof(buffer);
	msg.msg_name = (caddr_t)&from_addr;
	msg.msg_namelen = sizeof(from_addr);
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	res = recvmsg(sd, &msg, 0);
	if (res < 0) {
		printf("%s %s: %s\n",
		       "recvmsg",
		       strerror(errno));
	}

	
	packets++;

	if ((packets % 1000) == 0) 
	{
		struct cmsghdr *cmsg;
		printf("%i packets received.\n",packets);
		for (cmsg = CMSG_FIRSTHDR(&msg);
	     	 cmsg;
	         cmsg = CMSG_NXTHDR(&msg, cmsg)) 
		{
			printf(" cmsg [len:%zu] [level:%zu] \n", cmsg->cmsg_len, cmsg->cmsg_level);
		}
	}

}

void open_network_connection(char* dest_ip, char* dest_port, char* interface_ip)
{
	int retcode;
		
	sd = socket(AF_INET, SOCK_DGRAM, 0);

	if (sd < 0)
	{
		perror("Failed to open socket.");
		exit(1);
	}

	printf("Socket has been created.\n");

	int reuse = 1;
	retcode = setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse) , sizeof(reuse));
	if (retcode < 0)
	{
		perror("Failed setting SO_REUSEADDR");
		exit(1);
	}
	else
	{
		printf("SO_REUSEADDR has been successfully set.\n");
	}

	int enable = 1;
	retcode = setsockopt(sd, SOL_SOCKET, SO_TIMESTAMPNS, reinterpret_cast<char*>(&enable) , sizeof(enable));
	if (retcode < 0)
	{
		perror("Failed setting SO_TIMESTAMPNS");
		exit(1);
	}
	else
	{
		printf("SO_TIMESTAMPNS has been successfully set.\n");
	}
	

	unsigned short port = strtol(dest_port, NULL, 10);
	struct sockaddr_in localSock;
	memset(reinterpret_cast<char*>(&localSock), 0, sizeof(localSock));
	localSock.sin_family = AF_INET;
	localSock.sin_addr.s_addr = INADDR_ANY;
	localSock.sin_port = htons(port);

	retcode = bind(sd, (struct sockaddr*)&localSock, sizeof(localSock));

	if (retcode != 0)
	{
		perror("Error in bind call.");
		exit(1);
	}

	printf("Socket has been bound.\n");

	struct ip_mreq group;
	group.imr_multiaddr.s_addr = inet_addr(dest_ip);
	group.imr_interface.s_addr = inet_addr(interface_ip);
	retcode = setsockopt(sd, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<char*>(&group), sizeof(group));

	if (retcode < 0)
	{
		perror("Error adding multicast group");
		exit(1);
	}

	printf("Multicast group has been joined.\n");

}

int main(int argc, char** argv)
{
	printf("TS Graph Tool - Kevin Moore - Built %s\n", __DATE__);

	for (int i = 0; i < argc; ++i)
	{
		printf("Arg %i is %s\n",i,argv[i]);
	}

	if (argc < 4)
	{
		print_usage();
		exit(1);
	}

	open_network_connection(argv[1], argv[2], argv[3]);

	while(1)
	{
		read_packets();
	}	
	

	close(sd);
	printf("Socket has been closed.\n");

}