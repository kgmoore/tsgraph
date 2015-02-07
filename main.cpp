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
	printf("USAGE: tsgraph FILE filename\n");
	printf("   or  tsgraph NETWORK dest_ip dest_port interface_ip\n");
}

int sd;
uint8_t buffer[2048];
uint32_t mpeg_packets_received = 0;

typedef uint8_t stream_type_t;

class mpeg_program
{
public:
	
	mpeg_program() : program_number(0xFFFF), pmt_pid(0xFFFF)
	{
	}

	mpeg_program(uint16_t prog_num, uint16_t pmt) : 
		program_number(prog_num),
		pmt_pid(pmt)
	{ 
	}

	// members
	uint16_t program_number;
	uint16_t pmt_pid;
	std::map<uint16_t, stream_type_t> program_pids;
};

map<uint16_t,uint32_t> pid_histogram;
map<uint16_t, mpeg_program> pmt_pids_and_programs;


void print_pid_histogram()
{
	printf("PID histogram:\n\t");
	for(auto kvp = pid_histogram.begin(); kvp != pid_histogram.end(); kvp++)
	{
		printf("0x%04X:%d ", kvp->first, kvp->second);
	}
	printf("\n");
}

void print_bytes(uint8_t* bytes, uint32_t count)
{
	uint32_t byte = 0;
	while (byte < count)
	{
		if (byte % 16 == 0) printf("%03u: ", byte);
		printf("%02X ", bytes[byte]);
		byte++;
		if (byte % 16 == 0) printf("\n");
	}
	printf("\n");
}

void print_packet(uint8_t* packet)
{
	print_bytes(packet, MPEG_PACKET_SIZE);
}

void process_pmt_packet(uint8_t* packet)
{
	//printf("About to process PMT packet [pid  = 0x%04X]\n",ts_get_pid(packet));

	uint8_t* p_pmt_payload = ts_section(packet);

	uint16_t this_pid = ts_get_pid(packet);
	uint16_t prog_num = pmt_get_program_number(p_pmt_payload);
	uint16_t pcrpid = pmt_get_pcrpid(p_pmt_payload);
	
	//printf( "PMT Found [PID: 0x%04X][Program: %d][PCR PID: 0x%04X]\n",
	//	this_pid, prog_num, pcrpid);

	if (this_pid == 0x190)
	{
		unsigned int offset = 0;
		while (offset < 188)
		{
			printf("0x%03X: ",offset);
			for (unsigned int i = 0; i < 16; ++i)
			{
				if (offset >= 188) break;
				printf("%02X ",packet[offset]);
				offset++;
			}
			printf("\n");
		}
	}
}

void process_pat_packet(uint8_t* packet)
{

	unsigned int program_index = 0;
	uint8_t* p_pat_payload = ts_section(packet);
	//printf("Got PAT Packet...\n");
	while(true)
	{
		uint8_t* p_pat_n = pat_get_program(p_pat_payload,program_index);
		if (p_pat_n == 0) break;
		unsigned int program_number = patn_get_program(p_pat_n);
		unsigned int pid = patn_get_pid(p_pat_n);
		pmt_pids_and_programs[pid] = mpeg_program(program_number,pid);
		//printf("\t Program %d at pid 0x%04X.\n",program_number, pid);
		program_index++;
	}
}

float pcr_to_seconds(uint64_t pcr)
{
	/*float ticks_90khz = static_cast<float>(pcr >>  9);
	float ticks_27mhz = static_cast<float>(pcr & 0x1FF);
	return ticks_90khz / 90e3f + ticks_27mhz / 27e6f;*/
	return pcr / 90000.0;

}

uint64_t extract_uint64(const uint8_t* bytes, uint32_t count)
{
	uint64_t ret = 0;

	for(int i = 0; i < count; ++i)
	{
		ret = ret << 8;
		ret += bytes[i];
	}

	return ret;
}

float extract_ntp_timestamp(const uint8_t* bytes)
{
	uint64_t seconds = extract_uint64(bytes + 0, 4);
	uint64_t fraction = extract_uint64(bytes + 4, 4);

	float fraction_float = static_cast<float>(fraction);
	fraction_float /= 0x100000000;
	fraction_float /= 0x100000000;

	return static_cast<float>(seconds) + fraction_float;
}

void process_mpeg_packet(uint8_t* packet)
{
	mpeg_packets_received++;
	uint16_t pid = ts_get_pid(packet);

	pid_histogram[pid]++;

	if (ts_has_adaptation(packet))
	{
		unsigned int adaptation_start = 4;
		if (tsaf_get_transport_private_data_flag(packet))
		{
			unsigned int private_data_offset = 3;
			uint64_t pcr = 0;

			if (tsaf_has_pcr(packet))
			{
				private_data_offset += 6;
				pcr = tsaf_get_pcr(packet);
			}

			uint8_t adaptation_length = packet[4];

			printf("PID:%u, pcr:%lu (%f sec), AF:\n", 
					pid, 
					pcr,
					pcr_to_seconds(pcr));

			print_bytes(packet + 4, adaptation_length + 1);
			uint64_t seconds = 0;

			if (packet[adaptation_start + private_data_offset] == 0xDF)
			{
				// Cable Labs
				seconds = extract_uint64(packet + 4 + 16, 4);
				printf("CableLabs EBP: %lu seconds\n",seconds);
			}
			if (packet[adaptation_start + private_data_offset] == 0xA9)
			{
				seconds = extract_uint64(packet + 4 + 12, 4);
				printf("Legacy EBP: %lu seconds\n",seconds);
			}
		}
/*
		//if ( (packet[13] == 0xDF) && (packet[14] == 0x0D))
		if ( (packet[13] == 0xDF) )
		{
			//000: 47 41 E1 30 17 72 00 2B 58 11 FE 7C 0F DF 0D 45
			//016: 42 50 30 C8 D8 7D 31 3F 3A D6 00 00 00 00 01 E0
			printf("PID:[%u] AF_private:[%02X %02X %02X %02X %02X %02X]\n", pid, 
				*(packet + 13), 
					*(packet + 14), 
					*(packet + 15), 
				*(packet + 16), 
				*(packet + 17), 
				*(packet + 18)); 
		
			print_packet(packet);

		}
*/		
	}

	if (pid == 0)
	{
		process_pat_packet(packet);
	}

	if (pmt_pids_and_programs.find(pid) != pmt_pids_and_programs.end())
	{
		process_pmt_packet(packet);
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

void process_file_packets(FILE* pFile)
{
	uint8_t packet_buffer[MPEG_PACKET_SIZE];
	while (!feof(pFile))
	{
		fread(packet_buffer,188,1,pFile);
		process_mpeg_packet(packet_buffer);
	}
}

int main(int argc, char** argv)
{
	printf("TS Graph Tool - Kevin Moore - Built %s\n", __DATE__);

	for (int i = 0; i < argc; ++i)
	{
		printf("Arg %i is %s\n",i,argv[i]);
	}

	if (argc < 3)
	{
		print_usage();
		exit(1);
	}
	
	if (strcmp(argv[1], "FILE") == 0)
	{
		if (argc =! 3)
		{
			print_usage();
			exit(1);
		}

		FILE* pFile = fopen(argv[2],"rb");

		if (pFile == 0)
		{
			printf("File %s cannot be opened.\n",argv[2]);
		}

		process_file_packets(pFile);

	}
	else if (strcmp(argv[1], "NETWORK"))
	{	
		if (argc =! 5)
		{
			print_usage();
			exit(1);
		}

		open_network_connection(argv[2], argv[3], argv[4]);
		
		unsigned int packets_processed = 0;

		while(packets_processed < 1000)
		{
			read_ip_packets();
			packets_processed++;
		}	

		
		close(sd);
		printf("Socket has been closed.\n");
	}
	else
	{
		print_usage();
		exit(1);
	}	

	print_pid_histogram();
}
