#include <netinet/in.h>

struct NetworkReturnAddressStruct {
    struct sockaddr_in  cl_addr;
    int clilen;
    int sockfd;
};
