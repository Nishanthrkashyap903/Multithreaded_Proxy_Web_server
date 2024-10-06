#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_BYTES 4096                  // max allowed size of request/response
#define MAX_CLIENT 10                   // max number of client requests served at a time
#define MAX_SIZE 200 * (1 << 20)        // size of the cache
#define MAX_ELEMENT_SIZE 10 * (1 << 20) // max size of an element in cache

typedef struct cache_element Cache_element;

struct cache_element
{
    char *data;            // data stores response
    int len;               // length of data i.e.. sizeof(data)...
    char *url;             // url stores the request
    time_t lru_time_track; // url stores the request
    Cache_element *next;   // pointer to next cache element
};

Cache_element *find(char *url);
int add_cache_element(char *url, int size, char *data);
void remove_cache_element();

int port_number = 8080;
int proxy_socket_id;
pthread_t tid[MAX_CLIENT]; //*array to store the thread ids of clients

sem_t semaphore; // if client request exceeds the limit then the thread goes to the waiting state and when the trafic reduces then
                 // it wakes up the sleeping thread

// Wait()/Down() -> s=s-1  and Signal()/Up() ->s=s+1

pthread_mutex_t lock; //! it locks the cache

Cache_element *head; // head pointer to the cache
int cache_size;      //*cache_size denotes the current size occupied by the cache

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        // printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}

// parameters are url,port, returns: RemoteSocketId
int connectRemoteServer(char *host_addr, int port_num)
{
    // Creating Socket for remote server ---------------------------

    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (remoteSocket < 0)
    {
        printf("Error in Creating Socket.\n");
        return -1;
    }

    // Resolving Hostname to IP Address:
    struct hostent *host = gethostbyname(host_addr);

    if (host == NULL)
    {
        fprintf(stderr, "No such host exists.\n");
        return -1;
    }

    // inserts ip address and port number of host in struct `server_addr`
    struct sockaddr_in server_addr;

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    // Copying the Server's IP address to server_add
    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    // Connect to Remote server ----------------------------------------------------

    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (socklen_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting !\n");
        return -1;
    }
    // free(host_addr);
    return remoteSocket;
}

//* request contains the original request in parsed format , tempReq -> http request in original format
int handle_request(int clientSocket, struct ParsedRequest *request, char *tempReq)
{
    /*
    *HTTP GET Request Header

    GET /path/resource HTTP/1.1
    Host: www.example.com
    User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/90.0.4430.93 Safari/537.36
    Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp
    Accept-Language: en-US,en;q=0.5
    Accept-Encoding: gzip, deflate, br
    Connection: keep-alive
    Upgrade-Insecure-Requests: 1
    */

    //* Buffer contains the HTTP request in the unparsed format
    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    printf("\nIn handle request request host: %s, request path: %s\n",request->host,request->path);

    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        printf("set header key not work\n");
    }

    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set \"Host\" header key not working\n");
        }
    }
    
    //! Here we are adding the headers key: Value pairs to buffer String
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
    {
        printf("unparse failed\n");
    }

    printf("\nUnparsed Request in handle_request: %s\n",buf);

    int server_port = 80; // Default Remote Server Port is 80-> Http protocol
    if (request->port != NULL)
        server_port = atoi(request->port);

    

    //*Connecting to the remote server, parameters are url,port, returns: RemoteSocketId
    int remoteSocketID = connectRemoteServer(request->host, server_port);

    if (remoteSocketID < 0)
        return -1;

    //! Send the unparsed request to remote server
    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);

    // Clean the buffer
    bzero(buf, MAX_BYTES);

    // Initially receiving the response from server and putting in buffer
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    //*temp_buffer-> stores the complete response from the remote server
    char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES); // temp buffer
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    while (bytes_send > 0)
    {
        bytes_send = send(clientSocket, buf, bytes_send, 0);

        for (int i = 0; i < bytes_send / sizeof(char); i++)
        {
            temp_buffer[temp_buffer_index] = buf[i];
            // printf("%c",buf[i]); // Response Printing
            temp_buffer_index++;
        }

        //?Resizing the temp_buffer
        temp_buffer_size += MAX_BYTES;
        temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);

        if (bytes_send < 0)
        {
            perror("Error in sending data to client socket.\n");
            break;
        }

        // Again clean the buffer
        bzero(buf, MAX_BYTES);

        // Again receive the response from server in buffer
        bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';
    printf("Response buffer in handle request:\n%s\n",temp_buffer);

    free(buf);

    add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
    printf("Done\n");
    free(temp_buffer);

    close(remoteSocketID);
    return 0;
}

int checkHTTPversion(char *msg)
{
    int version = -1;

    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1; // Handling this similar to version 1.1
    }
    else
        version = -1;

    return version;
}

void *thread_fn(void *socketNew)
{
    /*
    Process syncronization

    *Entry code
    !Critical Section Code
    ?Exit code
    */

    sem_wait(&semaphore);

    int p;
    sem_getvalue(&semaphore, &p);

    int *t = (int *)socketNew;

    //*Client Socket
    int socket = *t;

    int bytes_send_client, len; // Bytes Transferred

    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));

    bzero(buffer, MAX_BYTES); // Making buffer zero

    //* Receiving the Request of client in buffer by proxy server, RETURN VALUES: These calls return the number of bytes received, or -1 if an error occurred.
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);

    while (bytes_send_client > 0)
    {
        len = strlen(buffer);
        // loop until u find "\r\n\r\n" in the buffer
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else
        {
            break;
        }
    }

    printf("In thread fn: Request Buffer Unparsed:\n%s\n",buffer);

    char *tempReq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);

    //*tempReq, buffer both store the http request sent by client
    for (int i = 0; i < strlen(buffer); i++)
    {
        tempReq[i] = buffer[i];
    }

    //?checking for the request in cache
    Cache_element *temp = find(tempReq);

    if (temp != NULL)
    {
        //! request found in cache, so sending the response to client from proxy's cache

        int size = temp->len / sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while (pos < size)
        {
            bzero(response, MAX_BYTES);

            for (int i = 0; i < MAX_BYTES; i++)
            {
                response[i] = temp->data[pos];
                pos++;
            }

            //
            send(socket, response, MAX_BYTES, 0);
        }

        printf("Data retrived from the Cache\n\n");
        printf("\n\nResponse From cache: %s\n\n", response);
    }

    // If not found in cache and recieved the request from client
    else if (bytes_send_client > 0)
    {
        //! Parse the request and fetch the html page from remote server and add to cache

        len = strlen(buffer);

        // Parsing the request
        struct ParsedRequest *request = ParsedRequest_create();

        // ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in
        //  the request
        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Parsing failed\n");
        }
        else
        {
            bzero(buffer, MAX_BYTES);
            if (!strcmp(request->method, "GET"))
            {

                if (request->host && request->path && (checkHTTPversion(request->version) == 1))
                {
                    printf("\nParsed Request->\nhost: %s\n,path: %s\n",request->host,request->path);
                    bytes_send_client = handle_request(socket, request, tempReq); // Handle GET request
                    if (bytes_send_client == -1)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }
                else
                    sendErrorMessage(socket, 500); // 500 Internal Error
            }
            else
            {
                printf("This code doesn't support any method other than GET\n");
            }
        }

        // freeing up the request pointer
        ParsedRequest_destroy(request);
    }

    else if (bytes_send_client < 0)
    {
        perror("Error in receiving from client.\n");
    }
    else if (bytes_send_client == 0)
    {
        printf("Client disconnected!\n");
    }

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);

    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value:%d\n", p);
    free(tempReq);
    return NULL;
}

int main(int argc, char *argv[])
{
        
    struct sockaddr_in server_addr, client_addr; // Address of client and server to be assigned

    sem_init(&semaphore, 0, MAX_CLIENT); // Initializing seamaphore and lock
    pthread_mutex_init(&lock, NULL);     // Initializing lock for cache

    if (argc == 2)
    { //?checking whether two arguments are received or not
        //./proxy port_number
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("Too few arguments\n");
        exit(1);
    }

    printf("Setting Proxy Server Port : %d\n", port_number);

    //! The proxy_socket_id is created to establish a point of communication for clients to connect to your proxy server.
    proxy_socket_id = socket(AF_INET, SOCK_STREAM, 0); // A -1 is returned if an error occurs, otherwise the return value is a
                                                       // descriptor referencing the socket.

    if (proxy_socket_id < 0)
    {
        perror("Failed to create socket.\n");
        exit(1);
    }

    int reuse = 1;

    // The option SO_REUSEADDR is used to reuse the same address
    if (setsockopt(proxy_socket_id, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) < 0)
    {
        perror("setsockopt(SO_REUSEADDR) failed\n");
    }

    // It initializes a block of memory by setting all its bytes to zero
    bzero((char *)&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;

    //! makes sure that regardless of the machine's internal representation (endianness), the value sent over the network is always in big-endian format.
    server_addr.sin_port = htons(port_number);

    //*server to listen for incoming connections on all available IP addresses like Local clients, remote clients
    server_addr.sin_addr.s_addr = INADDR_ANY;

    //?Binding the socket
    //! bind() -> associate the socket with a specific IP address and port number
    if (bind(proxy_socket_id, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Port is not free\n");
        exit(1); 
    }
    printf("Binding on port: %d\n", port_number);

    // Proxy socket listening to the requests
    int listen_status = listen(proxy_socket_id, MAX_CLIENT);

    if (listen_status < 0)
    {
        perror("Error while Listening !\n");
        exit(1);
    }

    printf("Proxy Socket is listening on port: %d\n", port_number);

    int i = 0;                          // Iterator for thread_id (tid) and Accepted Client_Socket for each thread
    int Connected_socketId[MAX_CLIENT]; // This array stores socket descriptors of connected clients

    while (1)
    {
        //*Clean the garbage value in client_addr

        bzero((char *)&client_addr, sizeof(client_addr));
        int client_len = sizeof(client_addr);

        //?accept the incoming client request from proxy_socket
        // client_socketId == to store the client socket id
        int client_socketId = accept(proxy_socket_id, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);

        if (client_socketId < 0)
        {

            fprintf(stderr, "Error in Accepting connection !\n");
            exit(1);
        }
        else
        {
            Connected_socketId[i] = client_socketId; // Storing accepted client into array
        }

        // Extract ip_add and port number of client
        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;

        struct in_addr ip_addr = client_pt->sin_addr;

        char str[INET_ADDRSTRLEN];

        //*converts the client's IP address from its binary format (used by the system) to a human-readable string format (the familiar "dotted-decimal" notation like "192.168.1.1").

        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);

        printf("Client is connected with port number: %d and ip address: %s \n", ntohs(client_addr.sin_port), str);

        // Creating a thread for each client accepted
        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]);

        i++;
    }

    close(proxy_socket_id);
}

Cache_element *find(char *url)
{

    Cache_element *site = NULL;

    int lock_val = pthread_mutex_lock(&lock);

    printf("\nfind Cache is locked: %d\n", lock_val);

    if (head != NULL)
    {
        site = head;

        while (site != NULL)
        {

            if (!strcmp(site->url, url))
            {
                //If found the url in the cache
                printf("\nFound in cache cache url:\n%s\n",site->url);

                printf("\nLRU Time Track Before : %ld\n", site->lru_time_track);
                printf("\nurl found\n");

                // Updating the time_track
                site->lru_time_track = time(NULL);

                printf("LRU Time Track After : %ld", site->lru_time_track);

                break;
            }

            site = site->next;
        }
    }
    else
    {
        printf("\nurl not found\n");
    }

    int unlocked_val = pthread_mutex_unlock(&lock);

    printf("find Cache is unlocked: %d", unlocked_val);

    return site;
}

void remove_cache_element()
{
    //* If cache is not empty searches for the node which has the least lru_time_track and deletes it

    Cache_element *prevptr; // Cache_element Pointer (Prev. Pointer)
    Cache_element *nextptr; // Cache_element Pointer (Next Pointer)
    Cache_element *temp;    // Cache element to remove points to the least lru_time_track

    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);

    if (head != NULL)       // Cache != empty
    {   
        prevptr = head;
        nextptr = head;
        temp = head;

        while (nextptr->next != NULL)
        {

            if (nextptr->next->lru_time_track < temp->lru_time_track)
            {

                temp = nextptr->next;
                prevptr = nextptr;
            }

            nextptr = nextptr->next;
        }

        if (temp == head)
        { // Edge case when the min lru time is the first element
            head = head->next;
        }

        //Update the cache_size removed 
        cache_size = cache_size - (temp->len + sizeof(Cache_element) + strlen(temp->url) + 1);

        free(temp->data);
        free(temp->url);

        //Deallocate the least lru_time cache element
        free(temp);
    }

    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
}

// data-> response , size -> size of response , url -> request
int add_cache_element(char *data, int size, char *url)
{

    // Adds element to the cache

    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n", temp_lock_val);

    int element_size = size + 1 + strlen(url) + sizeof(Cache_element); // Size of the new element which will be added to the cache

    if (element_size > MAX_ELEMENT_SIZE)
    {

        // If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache

        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);

        return 0;
    }
    // If the element_size is with in the range of Max_element_size then add to cache
    else
    {

        while (element_size + cache_size > MAX_SIZE)
        {
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }

        Cache_element *new_element = (Cache_element *)malloc(sizeof(Cache_element)); // Allocating memory for the new cache element

        new_element->data = (char *)malloc(size + 1); // Allocating memory for the response to be stored in the cache element
        strcpy(new_element->data, data);

        new_element->url = (char *)malloc(1 + (strlen(url) * sizeof(char))); // Allocating memory for the request to be stored in the cache element (as a key)
        strcpy(new_element->url, url);

        new_element->lru_time_track = time(NULL); // Updating the time_track
        new_element->next = head;
        new_element->len = size;

        head = new_element;
        // Update the cache size consumed so far
        cache_size += element_size;

        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);

        return 1;
    }

    return 0;
}
