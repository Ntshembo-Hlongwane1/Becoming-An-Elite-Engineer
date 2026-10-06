#include "unp.h"

int main(int argc, char **argv){

    /*
        - sockfd holds the socket file descriptor (THink of this as the handle to the open network connection)
        - n will store the number of bytes read from the server 
    */   
    int sockfd, n;

    /*
        - Char buffer to hold incoming data. MAXLINE + 1 ensuring that we have one extra byte to safely add a null terminator '\0' to treat the data as C-string.
    */
    char recvline[MAXLINE + 1];
    
    /*
        - A structure 'sockaddr_in' specifically for IPv4 address. It will hold server's IP and port number
    */
    struct sockaddr_in servaddr;


    /*
        - Checks if user provided exactly 2 arguments (the program name itself is argv[0], the IP is argv]1)
    */
    if (argc != 2){
        err_quit("usage: a.out <IPaddress>");
    };


    /*
        - socket() asks the OS to creata a new socket
        - AF_INET = IPv4 protocol family
        - SOCK_STREAM = TCP (reliable, stream-based, connection-orientated) as opposed to UDP
        - 0 = OS chooses the default protocol for this combination (TCP)
        - It returns an integer. If this integer is < 0, something went wrong
    */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        err_sys("Socket error");
    };


    /*
        - bzero (OR memset) sets all bytes in servaddr to ZERO. this prevents garbage data from being accidentally interpreted as part of the address.
        - Setting the address family for AF_INET (IPv4) to match the socket
        - Setting port to number 13 (the well-known port for the Daytime protocol).
        - htons() stands for Host To Network SHort. IT converts the integer 13 from your computer's byte order (LIttle-Endian on Intel, Big-Endian on some others)
    */

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(13);


    /*
        - inet_pton stands for Presentation to Numeric.
        - Takes the IP string typed on the CLI and convert it into the binary format required by the sin_addr (a 32-bit int)
        - Returns 1 on success, 0 if the string is invalid and -1 on actual sys error
    */
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0){
        err_quit("inent_pton error for %s", argv[1]);
    };


    /*
        - connect() performs the TCP three-way handshake with the server
        - sockfd = our active socket
        - (SA *) &servaddr cast our IPv4 structure to a generic struct sockaddr *
        - THis is necessary because connect was designed before C had void *, so it uses this generic socket type for addresses families (IPv4, IPv6, Unix domain)
    */
    if (connect(sockfd, (SA *) &servaddr, sizeof(servaddr)) < 0){
        err_sys("Connection error");
    };


    /*
        - read() attempts to pull data from the network connection 
        - Blocks (waits) until data arrives or the conenction closes
        - It returns the number of bytes read and stores them in the recvline. It reads up to MAXLINE bytes.
        - The while condition continues looping as long as n > 0. WHen the server closes its side of the connection, read() return 0 (EOF) breaking the loop
    */
    while ((n = read(sockfd, recvline, MAXLINE)) > 0){
        recvline[n] = 0;
        if (fputs(recvline, stdout) == EOF){
            err_sys("fputs error");
        };
    };


    /*
        - If we exit the while loop because n is negative (not because EOF n=0), that means a sys error occured during read()
        - 
    */
    if (n < 0){
        err_sys("read error");
    };

    exit(0);
};