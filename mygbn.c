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
	mygbn_sender->N = N;
	mygbn_sender->timeout = timeout;
	printf("Client UP\n");
}

int mygbn_send(struct mygbn_sender* mygbn_sender, unsigned char* buf, int len){
  	int FragementSize = 0;
  	int remainingLength = len;
  	int sent = 0;
  	int seqNum = 0;

  	int window[(len/MAX_PAYLOAD_SIZE) + 1 - (len%MAX_PAYLOAD_SIZE == 0)];
  	memset(window, 0, sizeof(int)*((len/MAX_PAYLOAD_SIZE) + 1 - (len%MAX_PAYLOAD_SIZE == 0)) );
  	pthread_t windowThread[mygbn_sender->N];
  	int avaliblewindow = mygbn_sender->N;

  	while((FragementSize = nextFragement(remainingLength)) > 0){
		// Choose a window (thread)
	  	
	  	// Window function (pthread point)
		// Create payload 
  		char *payload = (char *)malloc(sizeof(char)*FragementSize);
  		memcpy(payload, (char *)&buf[sent], FragementSize);

		// Create packet
	  	struct MYGBN_Packet *packet = createPacket(DataPacket, seqNum, payload, FragementSize);
	  	struct MYGBN_Packet *response = malloc(sizeof(struct MYGBN_Packet));

  		do {
	  		// Send Data
	  		if(sendto(mygbn_sender->sd, (char *)packet, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_sender->servaddr), sizeof(mygbn_sender->servaddr)) == -1){
	  			printf("ERROR on sending Data\n");
	  			exit(-1);
	  		}

	  		// Wait for Ack
  			int addrlen = sizeof(mygbn_sender->servaddr);
	  		if(recvfrom(mygbn_sender->sd, (char *)response, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&mygbn_sender->servaddr, (socklen_t *)&addrlen) == -1){
	  			printf("ERROR on receiving Ack\n");
	  			exit(-1);
	  		}
	  		// Ack is correct
	  		if(response->type == AckPacket && response->seqNum == seqNum){
	  			seqNum++;
	  			break;
	  		}
  		} while(1);

  		free(payload);
  		free(packet);
  		free(response);
  		sent += FragementSize;
  		remainingLength -= FragementSize;
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

  	int lastPackage = MAX_PAYLOAD_SIZE;
  	int received = 0;
  	int i = 0;
  	for(i = 0; i < 8 && lastPackage == MAX_PAYLOAD_SIZE; i++){
		// Get packet 
		struct MYGBN_Packet *packet = malloc(sizeof(struct MYGBN_Packet));
  		if(recvfrom(mygbn_receiver->sd, (char *)packet, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&mygbn_receiver->servaddr, (socklen_t *)&addrlen) == -1){
  			printf("ERROR on receiving\n");
  			exit(-1);
  		} 
  		// Store payload
  		int payloadSize = packet->length - HEADER_SIZE;
		if(payloadSize > 0)	
  			memcpy((char *)&buf[received], packet->payload, payloadSize);

  		// Send Ack
		struct MYGBN_Packet *ack = createPacket(AckPacket, packet->seqNum, NULL, 0);
  		if(sendto(mygbn_receiver->sd, (char *)ack, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_receiver->servaddr), addrlen) == -1){
  			printf("ERROR on sending Ack\n");
  			exit(-1);
  		}

  		free(packet);
  		free(ack);
  		received += payloadSize;
  		lastPackage = payloadSize;
  	}

	return received;
}

void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver) {
	close(mygbn_receiver->sd);
}

// Utility
struct MYGBN_Packet *createPacket(unsigned char type, unsigned int seqNum, char *payload, int payloadSize){
	struct MYGBN_Packet *packet = malloc(sizeof(struct MYGBN_Packet));

	strcpy((char *)packet->protocol, (char *)"gbn");
	packet->type = type;
	packet->seqNum = seqNum;
	packet->length = payloadSize + HEADER_SIZE;
	if(payloadSize > 0)
		memcpy((char *)packet->payload, payload, payloadSize);

	return packet;
}
int nextFragement(int fileSize){
	if (fileSize < MAX_PAYLOAD_SIZE)
		return fileSize;
	else
		return MAX_PAYLOAD_SIZE;
}

// Thread Function
void *sender_pthread(void *data){

}