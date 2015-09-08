#include <netinet/in.h>

struct NetworkReturnAddressStruct {
    struct sockaddr_in  cl_addr;
    unsigned int clilen;
    int sockfd;
};
