#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef OSCH
#define OSCH
#ifndef TRUE
typedef int Boolean;
#define TRUE 1
#define FALSE 0
#endif


/* Fixed byte width types */
typedef int int4;   /* 4 byte int */
typedef struct NetworkReturnAddressStruct_t {
    struct sockaddr_in  cl_addr; /* client information */
    struct sockaddr_in  my_addr; /* us */
    unsigned int clilen;
    int sockfd;
    fd_set readfds;
    struct timeval tv;
    int fdmax;	
} NetworkReturnAddressStruct;


typedef struct OSCPacketBuffer_struct {
    char *buf;			/* Contents of network packet go here */
    int n;			/* Overall size of packet */
    int refcount;		/* # queued things using memory from this buffer */
    struct OSCPacketBuffer_struct *nextFree;	/* For linked list of free packets */

    Boolean returnAddrOK;       /* Because returnAddr points to memory we need to
				   store future return addresses, we set this
				   field to FALSE in situations where a packet
				   buffer "has no return address" instead of
				   setting returnAddr to 0 */

     void *returnAddr;	/* Addr of client this packet is from */
	/* This was of type NetworkReturnAddressPtr, but the constness
           was making it impossible for me to initialize it.  There's
	   probably a better way that I don't understand. */

} OSCPacketBuffer;

struct OSCReceiveMemoryTuner {
    void *(*InitTimeMemoryAllocator)(int numBytes);
    void *(*RealTimeMemoryAllocator)(int numBytes);
    int receiveBufferSize;
    int numReceiveBuffers;
    int numQueuedObjects;
    int numCallbackListNodes;
};

#endif
