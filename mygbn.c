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

pthread_t AckListener;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t windowCond = PTHREAD_COND_INITIALIZER;
int killMode;

// Window set-up
int N;
int base;
int *window;
int *windowPacketSize;
int avalibleWindow;
int remainingLength;
int windowHead;

int fragementNum;
int lastAck;

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
  	// remainingLength = len;


	// Start Listener
	killMode = 0;
	pthread_create(&AckListener, NULL, sender_ackListener, mygbn_sender);

  	pthread_t senderThread;
	pthread_create(&senderThread, NULL, sender_pthread, &data);
  	pthread_join(senderThread, NULL);

  	return sent;
}

void mygbn_close_sender(struct mygbn_sender* mygbn_sender){
	// printf("START CLOSE: Last Ack %d\n", lastAck);
	killMode = 1;

	pthread_t escaper;
	pthread_create(&escaper, NULL, sender_final, mygbn_sender);
  	pthread_join(escaper, NULL);

  	printf("\nTransfer Complete\n");
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

  	// Ack
  	int payloadSegmentSize = 0;
  	int received = 0;

	// Get Packet
  	struct MYGBN_Packet *packet = malloc(sizeof(struct MYGBN_Packet));
	if(recvfrom(mygbn_receiver->sd, (char *)packet, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&mygbn_receiver->servaddr, (socklen_t *)&addrlen) == -1){
		printf("  ERROR on receiving Data\n");
		exit(-1);
	}

	printf("\n"); 
	if(packet->seqNum == 1){ /* Reset due to Moving to new section*/
		receivedPacket = 0;
		base = receivedPacket;
		nextSeqNum = base + 1;
	}
	
	// Store Packet
	int payloadSize = packet->length - HEADER_SIZE;
	if(payloadSize > 0 && packet->seqNum == nextSeqNum) {	/* Store only if has payload, and payload is valid */
		printf("  Storing %d, Size %d\n", packet->seqNum, payloadSize);
		payloadSegmentSize = payloadSize;
		memcpy((char *)buf, packet->payload, payloadSize);

		received += payloadSegmentSize;
		receivedPacket++;
		base++; /* ACK for i */

		// Return Ack
		struct MYGBN_Packet *ack = createPacket(AckPacket, packet->seqNum, NULL, 0);
		printf("  SEND ACK %d\n", packet->seqNum);
		if(sendto(mygbn_receiver->sd, (char *)ack, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_receiver->servaddr), addrlen) == -1){
			printf("  ERROR on sending Ack\n");
			exit(-1);
		}

		free(ack);
	} else { /* Shoot to the sea */
		printf("  Discard %d\n", packet->seqNum);
	}

  	free(packet);
  	printf("  Exit, total size %d\n", received);
	return received;
}

void mygbn_check_receiver(struct mygbn_receiver* mygbn_receiver) {
  	int addrlen = sizeof(mygbn_receiver->servaddr);
	struct MYGBN_Packet *packet = malloc(sizeof(struct MYGBN_Packet));
	if(recvfrom(mygbn_receiver->sd, (char *)packet, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&mygbn_receiver->servaddr, (socklen_t *)&addrlen) == -1){
		printf("  ERROR on receiving Data\n");
		exit(-1);
	}

	printf("\n"); 
	// Is END?
	if(packet->type == EndPacket){
		printf("TERMINATING\n");
		struct MYGBN_Packet *ack = createPacket(AckPacket, packet->seqNum, NULL, 0);
		printf("Reply ACK %d\n", packet->seqNum);
		if(sendto(mygbn_receiver->sd, (char *)ack, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_receiver->servaddr), addrlen) == -1){
			printf("  ERROR on sending Ack\n");
			exit(-1);
		}
		free(ack);
	}
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
int nextFragement(){
	if (remainingLength < MAX_PAYLOAD_SIZE){
		int returnVal = remainingLength;
		remainingLength = 0;
		return returnVal;
	} else {
		remainingLength -= MAX_PAYLOAD_SIZE;
		return MAX_PAYLOAD_SIZE;
	}
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
  	int FragementSize = 0;
  	fragementNum = (len / MAX_PAYLOAD_SIZE) + (len % MAX_PAYLOAD_SIZE != 0);
	// int totalPacket = 0;
  	remainingLength = len;
	lastAck = 0;
	base = 0;

	// Window set-up
	for(i=0;i<N;i++){
		window[i] = 0;
		windowPacketSize[i] = 0;
	}
	windowHead = 0;

	// Use a window
  	pthread_mutex_lock(&mutex);
	while(1){
		FragementSize = nextFragement();
		printf("FragementSize %d, Remaining %d\n", FragementSize, remainingLength);

		if(FragementSize > 0 ) {
			// Start a transfer
			j = windowHead;
			for(i = 0; i < N; i++) {
				if(window[j] == 0){
					// make packet
					int packetSeqNum = base + 1 + i;
					window[j] = packetSeqNum;
					char *address = (char *)&buf[(base + i)*MAX_PAYLOAD_SIZE];
					struct MYGBN_Packet *packet = createPacket(DataPacket, packetSeqNum, address, FragementSize);
					windowPacketSize[j] = FragementSize;
					processing += FragementSize;

					// send packet
					printf("  [Sender]Sending %d with size %d\n", packetSeqNum, FragementSize);
					if(sendto(mygbn_sender->sd, (char *)packet, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_sender->servaddr), sizeof(mygbn_sender->servaddr)) == -1){
						printf("ERROR on sending Ack\n");
						exit(-1);
					}
					printf("  [Sender]Sent %d\n", packetSeqNum);

					free(packet);
					avalibleWindow--;
					// totalPacket++;
					break;
				}

				// Next window
				j = (j+1) % N;
			}

			// Check Completion
			if(avalibleWindow == 0){
				printf("  Sleeppp...\n");
				pthread_cond_wait(&windowCond, &mutex);
			}

		} else {
			printf("[Reached Last of Window] ACK: %d/%d (%.2f %%)\n", lastAck, fragementNum, ((float)lastAck/(float)fragementNum)*100);
			while(lastAck != fragementNum) { /* Wait for other ack? */
				pthread_cond_wait(&windowCond, &mutex);
			} 

			// if no need resend
				break;
			// else
				// resent
		}

	}
	pthread_mutex_unlock(&mutex);

	*sent = len;
 	pthread_exit(NULL);
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
			if(killMode != 1){
				printf("ERROR on receiving Ack\n");
				exit(-1);
			}
		}

		if(killMode == 1){ /* Intercept EVERYTHING at Kill Mode */
			if((fragementNum + 1) == response->seqNum) {
				pthread_mutex_lock(&mutex);
				pthread_cond_signal(&windowCond);
	  			pthread_mutex_unlock(&mutex);
				break;
			} else {
				free(response);
				continue;
			}
		}

		pthread_mutex_lock(&mutex);
		if(response->type == AckPacket){
			int headSeqNum = window[windowHead];
			if(headSeqNum <= response->seqNum && headSeqNum > 0){ /* Slide Right */
				printf("  Ack %d\n", response->seqNum);
				lastAck = response->seqNum;
				int slideOffset = response->seqNum - headSeqNum + 1;

				for(i = 0; i < slideOffset; i++){ /* reset window */
					// remainingLength = remainingLength - windowPacketSize[windowHead];
					windowPacketSize[windowHead] = 0;
					window[windowHead] = 0;
					windowHead = (windowHead + 1) % N;
					avalibleWindow = avalibleWindow + 1;

					base++;
					headSeqNum++;
				}
				pthread_cond_signal(&windowCond);
			} else if(response->seqNum == headSeqNum - 1){
				// Wait Timeout to start resend
				printf("Old ACK %d received\n", response->seqNum);
			} 
	  		pthread_mutex_unlock(&mutex);
		}

		free(response);
	}

	pthread_exit(NULL);
}

void *sender_final(void *data){
	struct mygbn_sender *mygbn_sender = ((struct mygbn_sender *)data);

	printf("SENDING EndPacket\n");
	struct MYGBN_Packet *end = createPacket(EndPacket, fragementNum + 1, NULL, 0);
	if(sendto(mygbn_sender->sd, (char *)end, sizeof(struct MYGBN_Packet), 0, (struct sockaddr *)&(mygbn_sender->servaddr), sizeof(mygbn_sender->servaddr)) == -1){
		printf("ERROR on sending EndPacket\n");
		exit(-1);
	}

	pthread_mutex_lock(&mutex);
	pthread_cond_wait(&windowCond, &mutex);
	pthread_mutex_unlock(&mutex);

	free(end);
	pthread_exit(NULL);
}
