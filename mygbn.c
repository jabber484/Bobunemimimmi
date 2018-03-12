#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include "mygbn.h"

void mygbn_init_sender(struct mygbn_sender* mygbn_sender, char* ip, int port, int N, int timeout){
	struct hostent *ht;
 	struct sockaddr_in servaddr;
	int sd;

	/* create socket */	
	if ((sd = socket(AF_INET, SOCK_DGRAM,0)) < 0) {
		perror("ERROR: cannot create socket\n");
		exit(-1);
	}

	/* fill up destination info in servaddr */
	memset(&servaddr, 0, sizeof(servaddr));
	ht = gethostbyname(ip);
	if (ht == NULL) {
		perror("ERROR: invalid host.\n");
		exit(-1);
	}
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	memcpy(&servaddr.sin_addr, ht->h_addr, ht->h_length);

	// associate the opened socket with the destination's address
	if (connect(sd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
		perror("ERROR: connect failed.\n");
		exit(-1);
	}

	mygbn_sender->sd = sd;
	mygbn_sender->servaddr = servaddr;
	printf("Client UP\n");
}

int mygbn_send(struct mygbn_sender* mygbn_sender, unsigned char* buf, int len){
  	int addrlen = sizeof(mygbn_sender->servaddr);
  	// int send = sendto(mygbn_sender->sd, buf, len, 0, (struct sockaddr *)&(mygbn_sender->servaddr), addrlen);

  	int remainingLength = len;
  	int sent = 0;
  	int next = 0;
  	if((next = nextPacket(remainingLength)) > 0){
  		char *packet = (char *)malloc(sizeof(char)*next);
  		memcpy(packet,(char *)&buf[sent],next);
  		int send = sendto(mygbn_sender->sd, packet, next, 0, (struct sockaddr *)&(mygbn_sender->servaddr), addrlen);

  		free(packet);
  		sent += send;
  		remainingLength -= send;
  	}

  	return sent;
}

void mygbn_close_sender(struct mygbn_sender* mygbn_sender){
	close(mygbn_sender->sd);
}

void mygbn_init_receiver(struct mygbn_receiver* mygbn_receiver, int port){
	struct sockaddr_in servaddr;
	int servsd;
	int one = 1;

	/* create socket */	
	if ((servsd = socket(AF_INET, SOCK_DGRAM,0)) < 0) {
		perror("ERROR: cannot create socket\n");
		exit(-1);
	}
	
	/* set socket option */
	if (setsockopt(servsd, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one)) < 0) {
		perror("ERROR: cannot set socket option\n");
		exit(-1);
	}
	
	/* prepare the address structure */	
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 

	/* bind the socket to network structure */
	if (bind(servsd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		perror("Can't bind\n");
		exit(-1);
	}
	mygbn_receiver->sd = servsd;

	printf("Server UP\n");
}

int mygbn_recv(struct mygbn_receiver* mygbn_receiver, unsigned char* buf, int len){
  	memset(buf,'\0', len);
  	int addrlen = sizeof(mygbn_receiver->servaddr);

  	int remainingLength = len;
  	int received = 0;
  	int next;
  	if((next = nextPacket(remainingLength)) > 0){
  		char *packet = (char *)malloc(sizeof(char)*next);
  		int recv = recvfrom(mygbn_receiver->sd, packet, next, 0, (struct sockaddr *)&mygbn_receiver->servaddr, (socklen_t *)&addrlen);
  		memcpy((char *)&buf[received],packet,next);

  		free(packet);
  		received += recv;
  		remainingLength -= recv;
  	}

	return received;
}

void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver) {
	close(mygbn_receiver->sd);
}

// Utility
struct MYGBN_Packet *createPacket(unsigned char type, unsigned int seqNum, unsigned int length){
	struct MYGBN_Packet *packet = malloc(sizeof(struct MYGBN_Packet));

	strcpy((char *)packet->protocol, (char *)"gbn");
	packet->type = type;
	packet->seqNum = seqNum;
	packet->length = length;

	return packet;
}

int nextPacket(int fileSize){
	if (fileSize < MAX_PAYLOAD_SIZE)
		return fileSize;
	else
		return MAX_PAYLOAD_SIZE;
}
