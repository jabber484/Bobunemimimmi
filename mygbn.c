#include <netdb.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <pthread.h>
#include "mygbn.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t listenerReady = PTHREAD_COND_INITIALIZER;
pthread_cond_t windowCond = PTHREAD_COND_INITIALIZER;

// Window set-up
int N;
int base;
int *window;
int *windowPacketSize;
int avalibleWindow;
int remainingLength;
int windowHead;

void mygbn_init_sender(struct mygbn_sender* mygbn_sender, char* ip, int port, int N, int timeout){
	struct hostent *ht;
 	struct sockaddr_in servaddr;
	int sd;

	/* create socket */	
	if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
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
	N = mygbn_sender->N;
	int sent = 0, i;

	struct senderThread_data data;
	data.mygbn_sender = mygbn_sender;
	data.buf = buf;
	data.len = len;
	data.sent = &sent;

	// Set-up
	avalibleWindow = N;
	avalibleWindow = N;
	base = 0;
	window = malloc(sizeof(int)*N);
	for(i=0;i<N;i++)
		window[i] = 0;
	windowHead = 0;;
	windowPacketSize = malloc(sizeof(int)*N);
  	remainingLength = len;


	// Start Listener
  	pthread_t AckListener;
	pthread_create(&AckListener, NULL, sender_ackListener, mygbn_sender);

  	pthread_t senderThread;
	pthread_create(&senderThread, NULL, sender_pthread, &data);
  	pthread_join(senderThread, NULL);

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
  	int base = receivedPacket;
  	int nextSeqNum = base + 1;
  	int bound = 8;
  	int i = 0, j = 0;
  	int localOffset = 0;

  	// Ack
  	int ACK[bound];
  	int payloadSegmentSize[bound];
  	for(i=0;i<bound;i++){
  		ACK[i] = 0;
  		payloadSegmentSize[i] = 0;
  	}

  	int received = 0;
  	struct MYGBN_Packet *packet;	
  	for(i = 0; i < bound; i++){ //suport for large window????
		// Get Packet
  		printf("GETTING DATA\n");
		packet = malloc(sizeof(struct MYGBN_Packet));
  		if(recvfrom(mygbn_receiver->sd, (char *)packet, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&mygbn_receiver->servaddr, (socklen_t *)&addrlen) == -1){
  			printf("  ERROR on receiving Data\n");
  			exit(-1);
  		}
  		if(localOffset == 0) /* First Time for function*/
  			localOffset = nextSeqNum;

  		 // Is END?
  		if(packet->type == EndPacket){
  			receivedPacket = 0;
			printf("  TERMINATED\n");
			break;
  		}

  		// Store Packet
  		int payloadSize = packet->length - HEADER_SIZE;
  		int localSeqNum = packet->seqNum - localOffset; /* 0 ~ 7 */
		if(payloadSize > 0 && packet->seqNum == nextSeqNum) {	/* Store only if has payload, and payload is valid */
  			printf("  Storing %d, Size %d\n", packet->seqNum, payloadSize);
			ACK[localSeqNum] = packet->seqNum;
			payloadSegmentSize[localSeqNum] = payloadSize;
  			memcpy((char *)&buf[localSeqNum * MAX_PAYLOAD_SIZE], packet->payload, payloadSize);

  			// packet is first segement
  			if(packet->seqNum == nextSeqNum){
  				// proprogate until packet not received
  				j = localSeqNum;
  				while(ACK[j] != 0 && j < bound){
  					received += payloadSegmentSize[j];
  					receivedPacket++;
  					base++; /* ACK for i */
  					nextSeqNum++;
  					j++;
  				}
  			
  				// Return Ack
  				int ackNum = base;
				struct MYGBN_Packet *ack = createPacket(AckPacket, ackNum, NULL, 0);
  				printf("  SEND ACK %d\n", ackNum);
		  		if(sendto(mygbn_receiver->sd, (char *)ack, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_receiver->servaddr), addrlen) == -1){
		  			printf("  ERROR on sending Ack\n");
		  			exit(-1);
		  		}
	  			free(ack);

  			}
  		} else { /* Shoot to the sea */
  			printf("  Discard %d\n", packet->seqNum);
  		}

  		// printf("  DONE\n");
  	}

  	printf("Exit, total size %d\n", received);
	return received;
}

void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver) {
	close(mygbn_receiver->sd);
}

// Utility
struct MYGBN_Packet *createPacket(unsigned char type, unsigned int seqNum, char *payload, int payloadSize){
	struct MYGBN_Packet *packet = malloc(sizeof(struct MYGBN_Packet));

	packet->protocol[0] = 'g';
	packet->protocol[1] = 'b';
	packet->protocol[2] = 'n';
	
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
void *sender_pthread(void *data) {
	// Enter Main
	struct senderThread_data *dataset = (struct senderThread_data *)data;
	struct mygbn_sender *mygbn_sender = dataset->mygbn_sender;
	unsigned char *buf = dataset->buf;
	int len = dataset->len;
	int *sent = dataset->sent;
	int processing = 0;
  	int i = 0, j = 0;

  	// Packet Related
  	int FragementSize;
	int totalPacket = 0;
  	remainingLength = len;
	base = 0;

	// Window set-up
	for(i=0;i<N;i++){
		window[i] = 0;
		windowPacketSize[i] = 0;
	}
	windowHead = 0;

	while((FragementSize = nextFragement(remainingLength)) > 0){
		// Use a window
      	pthread_mutex_lock(&mutex);
		j = windowHead;
		for(i = 0; i < N; i++) {
			if(window[j] == 0){
				// make packet (NEED FREE)
				int packetSeqNum = base + i + 1;
				window[j] = packetSeqNum;
				char *address = (char *)&buf[(base + i)*MAX_PAYLOAD_SIZE];
				struct MYGBN_Packet *packet = createPacket(DataPacket, packetSeqNum, address, FragementSize);
				windowPacketSize[j] = FragementSize;
				processing += FragementSize;

				// send packet
				printf("  [Sender]Sending %d\n", packetSeqNum);
				if(sendto(mygbn_sender->sd, (char *)packet, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_sender->servaddr), sizeof(mygbn_sender->servaddr)) == -1){
					printf("ERROR on sending Ack\n");
					exit(-1);
				}
				printf("  [Sender]Sent %d\n", packetSeqNum);

				free(packet);
				avalibleWindow--;
				totalPacket++;
				break;
			}

			// Next window
			j = (j+1) % N;
		}

		// Check windows
		if(avalibleWindow == 0 || processing == len){
			printf("  Sleeppp...\n");
			pthread_cond_wait(&windowCond, &mutex);
		} 
  		pthread_mutex_unlock(&mutex);
	}

	struct MYGBN_Packet *end = createPacket(EndPacket, totalPacket + 1, NULL, 0);
	if(sendto(mygbn_sender->sd, (char *)end, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_sender->servaddr), sizeof(mygbn_sender->servaddr)) == -1){
		printf("ERROR on sending Ack\n");
		exit(-1);
	}
	free(end);

	*sent = len;
 	pthread_exit(NULL);
	return 0;
}

void *sender_ackListener(void *data){
	struct mygbn_sender *mygbn_sender = ((struct mygbn_sender *)data);
	int sd = mygbn_sender->sd;
	int addrlen = sizeof(mygbn_sender->servaddr);
	int i;

	while(1){
		printf("  [Listener]Waiting Ack\n");
		struct MYGBN_Packet *response = calloc(sizeof(struct MYGBN_Packet),1);
		if(recvfrom(sd, (char *)response, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&mygbn_sender->servaddr, (socklen_t *)&addrlen) == -1){
			printf("ERROR on receiving Ack\n");
			exit(-1);
		}

		pthread_mutex_lock(&mutex);
		if(response->type == AckPacket){
			printf("  Ack %d\n", response->seqNum);
			int headSeqNum = window[windowHead];
			if(headSeqNum <= response->seqNum && headSeqNum > 0){ /* Slide Right */
				int slideOffset = response->seqNum - headSeqNum + 1;
				printf("  [debug]slide %d, response->seqNum %d, headSeqNum %d\n", slideOffset ,response->seqNum,headSeqNum);
				printf("Slide Offset %d\n", slideOffset);

				for(i = 0; i < slideOffset; i++){ /* reset window */
					remainingLength = remainingLength - windowPacketSize[windowHead];
					windowPacketSize[windowHead] = 0;
					window[windowHead] = 0;
					windowHead = (windowHead + 1) % N;
					avalibleWindow = avalibleWindow + 1;

					base++;
					headSeqNum++;
				}
				pthread_cond_signal(&windowCond);
			} else if(response->seqNum == headSeqNum - 1){
				// Resent first window
				printf("OPPS\n");
			} 
	  		pthread_mutex_unlock(&mutex);
		}

		free(response);
		// break? MISSING END CONDITION
	}

	pthread_exit(NULL);
	return 0;
}
