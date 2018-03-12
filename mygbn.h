/*
 * mygbn.h
 */

#ifndef __mygbn_h__
#define __mygbn_h__

#define MAX_PAYLOAD_SIZE 512
//NEW
#define DataPacket 0xA0
#define AckPacket 0xA1
#define EndPacket 0xA2

struct MYGBN_Packet {
  unsigned char protocol[3];                  /* protocol string (3 bytes) "gbn" */
  unsigned char type;                         /* type (1 byte) */
  unsigned int seqNum;                        /* sequence number (4 bytes) */
  unsigned int length;                        /* length(header+payload) (4 bytes) */
  unsigned char payload[MAX_PAYLOAD_SIZE];    /* payload data */
};

struct mygbn_sender {
  int sd; // GBN sender socket
  struct sockaddr_in servaddr;
  // ... other member variables
};

void mygbn_init_sender(struct mygbn_sender* mygbn_sender, char* ip, int port, int N, int timeout);
int mygbn_send(struct mygbn_sender* mygbn_sender, unsigned char* buf, int len);
void mygbn_close_sender(struct mygbn_sender* mygbn_sender);

struct mygbn_receiver {
  int sd; // GBN receiver socket
  struct sockaddr_in servaddr;
  // ... other member variables
};

void mygbn_init_receiver(struct mygbn_receiver* mygbn_receiver, int port);
int mygbn_recv(struct mygbn_receiver* mygbn_receiver, unsigned char* buf, int len);
void mygbn_close_receiver(struct mygbn_receiver* mygbn_receiver);
//NEW
struct  MYGBN_Packet *createPacket(unsigned char type, unsigned int seqNum, unsigned int length);
#endif
