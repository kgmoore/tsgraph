#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <map>
#include <vector>
#include <utility>

using namespace std;

#include <bitstream/mpeg/ts.h>
#include <bitstream/mpeg/psi.h>

#define MPEG_PACKET_SENTINAL 0x47
#define MPEG_PACKET_SIZE 188
#define RTP_HEADER_SENTINAL 0x80
#define RTP_HEADER_SIZE 12

#define NANOSEC_PER_SEC 1000000000

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
vector<pair<uint64_t,uint64_t>> pcrs_and_packet_times;

void print_pid_histogram()
{
	printf("PID histogram:\n\t");
	for(auto kvp = pid_histogram.begin(); kvp != pid_histogram.end(); kvp++)
	{
		printf("0x%04X:%d ", kvp->first, kvp->second);
	}
	printf("\n");
}


void write_pcrs_and_packet_times()
{
	FILE* output = fopen("pcrs_packet_times.txt","w");

	for( 	vector<pair<uint64_t,uint64_t>>::iterator iter = pcrs_and_packet_times.begin();
		iter != pcrs_and_packet_times.end(); 
		iter++)
	{
		fprintf(output, "%lu\t%lu\n",iter->first, iter->second);
	}

	fclose(output);
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


void process_mpeg_packet(uint8_t* packet, uint64_t packet_time)
{
	mpeg_packets_received++;
	
	uint16_t pid = ts_get_pid(packet);

	pid_histogram[pid]++;

	if (ts_has_adaptation(packet))
	{
		uint64_t pcr = 0;

		if (tsaf_has_pcr(packet))
		{
			pcr = tsaf_get_pcr(packet);
			pcrs_and_packet_times.push_back(make_pair(pcr,packet_time));		
		}	
		
		if (tsaf_get_transport_private_data_flag(packet))
		{
			uint8_t adaptation_length = packet[4];

			printf("PID:%u, pcr:%lu (%f sec), AF:\n", 
					pid, 
					pcr,
					pcr_to_seconds(pcr));

			print_bytes(packet + 4, adaptation_length + 1);
			uint64_t seconds = 0;

			if (packet[4+9] == 0xDF)
			{
				// Cable Labs
				seconds = extract_uint64(packet + 4 + 16, 4);
				printf("CableLabs EBP: %lu seconds\n",seconds);
			}
			if (packet[4+9] == 0xA9)
			{
				seconds = extract_uint64(packet + 4 + 12, 4);
				printf("Legacy EBP: %lu seconds\n",seconds);
			}
		}
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



uint64_t get_timestamp()
{
	uint64_t ret;

	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	ret = ts.tv_sec;
	ret *= NANOSEC_PER_SEC;
	ret += ts.tv_nsec;

	return ret;
}

void print_timer_resolution()
{
	uint64_t nanoseconds;

	timespec ts;
	clock_getres(CLOCK_MONOTONIC, &ts);

	nanoseconds = ts.tv_sec;
	nanoseconds *= NANOSEC_PER_SEC;
	nanoseconds += ts.tv_nsec;

	printf("On this system CLOCK_MONOTONIC reports a resolution of %lu nanoseconds.\n", nanoseconds);
}




void read_ip_packets()
{
	ssize_t recv_size;
	static unsigned int packets = 0;
	
	static bool last_recv_time_valid = 0;
	static uint64_t last_recv_time;

	recv_size = recv(sd,buffer,sizeof(buffer), 0);
	
	if (recv_size == 0)
	{
		printf("recv returned Zero!\n");
		return;
	}

	uint64_t recv_time = get_timestamp();

	// we need historical packet timing, so we bail early on the first packet reception
	if (last_recv_time_valid == false)
	{
		last_recv_time = recv_time;
		last_recv_time_valid = true;
		return;
	}

	unsigned int rtp_header_bytes = 0;

	// check for an RTP header. skip if found
	if (buffer[0] == RTP_HEADER_SENTINAL)
	{
		rtp_header_bytes = RTP_HEADER_SIZE;
	}

	unsigned int mpeg_packets = (recv_size - rtp_header_bytes) / MPEG_PACKET_SIZE;

	if ((mpeg_packets * MPEG_PACKET_SIZE + rtp_header_bytes) != recv_size)
	{
		printf(
			"There are leftover bytes in this packet. [recv_size:%u] [mpeg_packets:%u] [rtp_header_size:%u]\n",
			recv_size, mpeg_packets, rtp_header_bytes);
	}

	for (unsigned int packet = 0; packet < mpeg_packets; ++ packet)
	{
		// this code makes the assumption that individual MPEG packets arrived spread out in time, 
		// even though they actually arrived all at once. This should take some bias out of the PCR accuracy calculations.

		// packet 1 gets 1/7ths of the time difference, packet 2 gets 2/7ths, ... , packet 7 gets the current time
		uint64_t packet_time = last_recv_time + (recv_time - last_recv_time) * (packet + 1) / mpeg_packets;
		
		// get the byte position for this packet
		unsigned int offset = rtp_header_bytes + (packet * MPEG_PACKET_SIZE);
	
		// now do the packet processing
		if (buffer[offset] == MPEG_PACKET_SENTINAL)
		{
			process_mpeg_packet(buffer + offset, packet_time);
		}
		else
		{
			printf("Expected 0x%02X marker, got 0x%02X\n", MPEG_PACKET_SENTINAL, buffer[offset] );
		}
	}

	last_recv_time = recv_time;
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
		process_mpeg_packet(packet_buffer,0);
	}
}

int main(int argc, char** argv)
{
	printf("TS Graph Tool - Kevin Moore - Built %s\n", __DATE__);

	for (int i = 0; i < argc; ++i)
	{
		printf("Arg %i is %s\n",i,argv[i]);
	}

	print_timer_resolution();

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
	else if (strcmp(argv[1], "NETWORK") == 0)
	{	
		if (argc =! 5)
		{
			print_usage();
			exit(1);
		}

		open_network_connection(argv[2], argv[3], argv[4]);
		
		unsigned int packets_processed = 0;

		uint64_t start_time = get_timestamp();
		uint64_t timespan = NANOSEC_PER_SEC;
		timespan *= 60 * 60 * 2;
		uint64_t end_time = start_time + timespan;

		while(get_timestamp() < end_time)
		{
			read_ip_packets();
			packets_processed++;
			if (packets_processed % 10000 == 0) 
				printf("Packets received = %u\n",packets_processed);

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
	write_pcrs_and_packet_times();
}
