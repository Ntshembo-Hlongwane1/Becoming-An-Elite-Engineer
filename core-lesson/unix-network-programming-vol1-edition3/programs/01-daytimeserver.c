#include "unp.h"
#include <time.h>

int main(int argc , char **argv){

    /*
        - listenfd -> Socket descripto for listening socket (used to accpet incoming connections)
        - connfd -> Socket descriptor for the connected socket (used to coummunicate with a specific client)
    */
    int listenfd, connfd;

    /*
        - servaddr - A structure type sockaddr_in (IPv4 address structure) to hold the server's IP
    */
    struct sockaddr_in servaddr;

    /*
        - buff -> A char array  
    */
    char buff[MAXLINE];

    /*
        - ticks ->  A time_t variable to store the number of seconds since January 1, 1970
    */
    time_t ticks;

    /*
        - Calls the wrapper Socket (which internall call socket())
        - AF_INET use IPv4 addressing
        - SOCK_STREAM uses TCP reliable connection-oriented stream
        - 0 chooses the default protocol for TCP/IP
        - Return a socket descriptor store in listenfd
    */
    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    /*
        - bzero (depracted but used in older code; equivalent to memset) fills the servaddrr strufture with all zeroes. Ensuring no leftover garbage data affects the configuration 
    */
    bzero(&servaddr, sizeof(servaddr));

    /*
        - Tells the system we are using IPv4 address
    */
    servaddr.sin_family = AF_INET;

    /*
        - Set the IP address to bind to
        - INADDR_ANY is a special constant meaning "bind to available network interface on this machine" (e.g localhost, WI-FI, Ethernet)
        - htonl() - Host to Network Long. Converts the IP address from the system's bytes order (little edian on x86) to network byte order (big edian), which is required network protocols
    */
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    /*
        - Set the port number
        - Port 13 is well-known, reserved port for the Daytime Protocol (defined 867)
        - htons() - Host to Network Short. Converts the port number to network byte order (big edian) 
    */
    servaddr.sin_port = htons(13);

    /*
        - Bind the socket to the address / port
        - Calls the wrapper Bind (which call bind())
        - Attaches the socket listenfd to the IP address and port we just set up (servaddr)
        - (SA *) - Casts the structure to a generic struct sockaddr *, because the bind() is a generic function that works with both IPv4 and IPv6

    */
    Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));

    /*
        - Start listening for connections
        - Calls the wrapper Listen (which calls listen())
        - Marks the socket as passinve listening socket, meaing it will acccept incoming connection requests
        - LISTENQ (a constant from unp.h) defines the max length of the queue for pending connections (usally 1028 OR 18)
    */
    Listen(listenfd, LISTENQ);

    /*
        - Infinite loop
    */
    for (; ;) {
        /*
            - Accept a client connection
            - Calls the wrapper Accept (which calls accept())
            - This function blocks (wait) until a client tries to connect to port 12
            - (SA *) NULL, NULL - we Pass NULL Because we do not care about the client address or port number. We just want the connection 
            - Returns a new socket descriptor connfd specifically for commuication with this one client
        */
        connfd = Accept(listenfd, (SA *) NULL, NULL);

        /*
            - Get the current time
            - time(NULL) returns the current calendar time as the number of seconds elaspsed since Jan 1 1970 (The Unix epoch), and store in tikcs
        */
        ticks = time(NULL);

        /*
            - Format the time into string
            - ctime(&ticks) converts the raw ticks into human-readbale string (e.g Wed JUn 9 14:30:00 2026). BY default, this string is exactly 26 characters long and ends with a newline \n
            - "%.24s" This format specificer tell snprintf to print only the first 24 character of that string
        */
        snprintf(buff, sizeof(buff), "%.24s\r\n", ctime(&ticks));

        /*
            - Send the time string to the client
            - Callas the wrapper Write (which calls write() or send())
            - Sends the connts of buff (the formtaed dat/time) over the connecvted socket connfd to the remote client
        */
        Write(connfd, buff, strlen(buff));

        /*
            - Close the client connection 
            - Calls the wrapper Close (which calls close())
            - Close the connection to this specific client. THe server does not read any data from the client it just send the time and hangs up. The listening socket (listenfd) remians open
        */
        Close(connfd);
    };

    exit(0);
};