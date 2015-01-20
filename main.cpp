#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <map>

using namespace std;

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>

#define MPEG_PACKET_SENTINAL 0x47
#define MPEG_PACKET_SIZE 188
#define RTP_HEADER_SENTINAL 0x80
#define RTP_HEADER_SIZE 12

void print_usage()
{
	printf("USAGE: tsgraph dest_ip dest_port interface_ip\n");
}

int sd;
uint8_t buffer[2048];
uint32_t mpeg_packets_received = 0;

map<uint16_t,uint32_t> pid_histogram;

void print_pid_histogram()
{
	for(auto kvp = pid_histogram.begin(); kvp != pid_histogram.end(); kvp++)
	{
		printf("%d:%d ", kvp->first, kvp->second);
	}
}

void process_pat_packet(uint8_t* packet)
{

	unsigned int program_index = 0;
	uint8_t* p_pat_payload = ts_section(packet);
	printf("Got PID Packet...\n");
	while(true)
	{
		uint8_t* p_pat_n = pat_get_program(p_pat_payload,program_index);
		if (p_pat_n == 0) break;
		unsigned int program_number = patn_get_program(p_pat_n);
		unsigned int pid = patn_get_pid(p_pat_n);
		printf("\t Program %d at pid 0x%04X.\n",program_number, pid);
		program_index++;
	}
}

void process_mpeg_packet(uint8_t* packet)
{
	mpeg_packets_received++;
	uint16_t pid = ts_get_pid(packet);

	pid_histogram[pid]++;

	if (ts_has_adaptation(packet))
	{
		if (tsaf_has_pcr(packet))
		{
			uint64_t pcr = tsaf_get_pcr(packet);
			/* printf("PID:%u, packet:%u, pcr:%lu (%f sec)\n", 
				pid, 
				mpeg_packets_received,
				pcr,
				pcr/ 27000000.0); */
		}
	}

	if (pid == 0)
	{
		process_pat_packet(packet);
	}
}

void read_ip_packets()
{
	ssize_t recv_size;
	static unsigned int packets = 0;

	recv_size = recv(sd,buffer,sizeof(buffer), 0);
	
	if (recv_size == 0)
	{
		printf("recv returned Zero!\n");
		return;
	}

	unsigned int offset = 0;

	// check for an RTP header. skip if found
	if (buffer[offset] == RTP_HEADER_SENTINAL)
	{
		offset += RTP_HEADER_SIZE;
	}

	while (offset < recv_size)
	{
		if (buffer[offset] == MPEG_PACKET_SENTINAL)
		{
			process_mpeg_packet(buffer + offset);
		}
		else
		{
			printf("Expected 0x%02X marker, got 0x%02X\n", MPEG_PACKET_SENTINAL, buffer[offset] );
		}
		offset += MPEG_PACKET_SIZE;
	}

	packets++;
	if ((packets % 100) == 0) 
	{
//		printf("%i ip packets received, %i mpeg packets received. ",packets, mpeg_packets_received);
//		print_pid_histogram();
//		printf("\n");
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

	printf("SO_REUSEADDR has been successfully set.\n");

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
		read_ip_packets();
	}	
	

	close(sd);
	printf("Socket has been closed.\n");

}
