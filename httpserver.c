#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <stdbool.h> // true, false
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#define BUFFER_SIZE 1
#define MAX_HEADER_SIZE 4096







/*







***********************************************************


CITATIONS/ASSISTED RESOURCES


GIVEN FROM PDF: 
http://www.cs.kent.edu/~ruttan/sysprog/lectures/multi-thread/multi-thread.html


//Aided in my List Data Structure, Global Declarations sufficed to share amongst threads
https://prepinsta.com/data-structures/queue-program-and-implementation/


Youtube: //Aided in creating threads
https://www.youtube.com/watch?v=Pg_4Jz8ZIH4


***********************************************************






*** Reserved Space per thread ~9KiB initial + server requests (list nodes)




*/

//Global Declarations for mutex and condition
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition = PTHREAD_COND_INITIALIZER;

/*
Citation: I took cse130 with Alvaro but failed so Im reusing the same data structure that he gave us. Rest of code, however, is new since I am writing
in 1 bytes.
Data Structure httpObject:
method = holds type of request
filename = file specified in request (removes '/')
httpveersion = holds httpversion
content length = content length specified in request
status_code = status codes to transmit to client
closeCon = to close my connection, I close on 400, 500 and 501. (NOTE **** It would have been a hassle with 501 and 500 seems reasonable to close )
buffer = Buffer with 4096 bytes to hold my request/response






*/




struct httpObject {

  char method[5];         // PUT, HEAD, GET
  char filename[17];      // what is the file we are worried about
  char httpversion[9];// HTTP/1.1
  char hostname[270];    //hostname 2^8 + port # (o.w localhost)
  ssize_t content_length; // example: 13
  int status_code; // status codes 200, 201, 400 etc
  bool closeCon; // close conn early boolean
  char buffer[MAX_HEADER_SIZE]; //handles headers, reading files, writing response
  char logbuf[400]; //logbuf stores the log message to be written
};


struct connection{
  struct connection* next;
  int connfd;
};

typedef struct connection connectionT;


/*
  Declares the threadArg data structure

  This will be passed to each thread, contains logfile file descriptor and a boolean that is used as a 
  means to see if we need to log.


  threadArg could be expanded to hold individual content for threads, o.w. I could have used a global var for logfd

*/
struct threadArg{
  int logfd;//file descriptor for logfile
  bool toLog;//to log or not to log
};

typedef struct threadArg threadArg;

//Declare Queue
connectionT* head = NULL;
connectionT* tail = NULL;

/*
  connectionT* newConn(int connfd)

  Parameters = int connfd

  ------------------------------------
  This function creates a new connection node and returns it.
*/
connectionT* newConn(int connfd){
  connectionT *node = malloc(sizeof(connectionT));
  node->connfd = connfd;
  node->next = NULL;
  return node;
}

/*
  void addConnectionRequest(int connfd)

  Parameters = int connfd

  https://prepinsta.com/data-structures/queue-program-and-implementation/
  ------------------------------------
  This function creates a new connection and adds it into the queue
*/
void addConnectionRequest(int connfd){
  connectionT *newT = newConn(connfd);
  if(tail == NULL){
    head = newT;
  }else{
    tail->next = newT;
  }
  tail = newT;
}

/*
  int getConnectionRequest()

  https://prepinsta.com/data-structures/queue-program-and-implementation/
  ------------------------------------
  This function grabs the next connection from the queue and dequeues it.
*/
int getConnectionRequest(){
  if(head == NULL){
    return -1;
  }else{
    int value = head->connfd;
    connectionT* temp = head;
    head = head->next;
    if(head == NULL){
      tail = NULL;
    }
    free(temp);
    return value;
  }
}

/*
  void logging(char* logMsg, int logfd)

  Parameters = char* logMsg, int logfd

  --------------------------------------------------

  This function is called when a logfile is specified when the server is initiated.
  It grabs the length of the string/response to be logged and locks the operation to reserve the space with
  null chars with the offset given from lseek. Then we unlock to write and allow other threads to grab their offset.






CITATION: SHOWN IN DANIELS SECTION
*/
void logging(char* logMsg, int logfd){

  //grab length of msg
  int length = strlen(logMsg);


  //Im logging the bytes to suprress an unused integer warning and I got more warnings. This macro will stop that
  int __attribute__((unused))temp = 0; //https://stackoverflow.com/questions/3599160/how-to-suppress-unused-parameter-warnings-in-c 
  char nul = '\0';


  // Lock operation so thread can reserve its space with correct offset
  pthread_mutex_lock(&mutex);
  int offset = lseek(logfd, 0, SEEK_CUR);
  for(int i = 0; i < length; ++i){
    temp = write(logfd, &nul, 1);
  }
  pthread_mutex_unlock(&mutex);



  //Write to log
  temp = pwrite(logfd, logMsg, length, offset);
}


/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the range.
 */
uint16_t strtouint16(char number[]) {
  char *last;
  long num = strtol(number, &last, 10);
  if (num <= 0 || num > UINT16_MAX || *last != '\0') {
    return 0;
  }
  return num;
}

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port) {
  struct sockaddr_in addr;
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    err(EXIT_FAILURE, "socket error");
  }

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htons(INADDR_ANY);
  addr.sin_port = htons(port);
  if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) {
    err(EXIT_FAILURE, "bind error");
  }

  if (listen(listenfd, 500) < 0) {
    err(EXIT_FAILURE, "listen error");
  }

  return listenfd;
}

/*
void putResponse(int connfd, struct httpObject* message)

Parameters = connfd, holds the sockect connection to the client
             message = data structure/object to contents of request

What it does:
If the request is of PUT method, the function is called and it checks the filename against the directory thats being hosted. 
If it does not exist the function proceeds to create a file and allows permissions.
If it does exist the function proceeds to wipe the file clean to begin writing new content.
The function then loops by 1 byte and receives until it reaches the content length specified.
Response is then formulated and printed and continue
Errors are printed accordingly when a file is forbidden to be written or if there is an internal server error
If logging was enabled. Call snprintf with a NULL output string to grab needed bytes to formulate message in a buffer.

*/
void putResponse(int connfd,struct httpObject* message){
  //Declarations 
  int logZero;
  ssize_t bytes = 0, bytes_r = 0, bytes_w = 0, offset = 0, logBytes = 0;
  int check;
  int fd;
  //check if filename exists
  check = access(message->filename, F_OK);
  if(check != 0){//if not
    //create a new file
    fd = open(message->filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH | S_IRGRP | S_IWGRP);
    //loop the body request and write into file
    while(bytes_r < message->content_length){
      bytes = recv(connfd, message->buffer + offset, BUFFER_SIZE, 0);
      if(bytes <= 0 && bytes_r != message->content_length){
        logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
        snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
        close(fd);
        return;
      }
      bytes_r+= bytes;
      offset+= bytes;
      if(bytes_r == message->content_length){
        bytes_w = write(fd, message->buffer, offset);
        memset(message->buffer, 0, MAX_HEADER_SIZE);
        if(bytes_w != offset){
          message->status_code = 500;
          //Grab needed bytes to formulate message for logging
          logZero = dprintf(connfd, "%s %d Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n",message->httpversion, message->status_code);
          if(logZero <= 0){
            message->status_code = 0;
            logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
            snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
            message->closeCon = true;
          }else{
            logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
            snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
          }
          message->closeCon = true;
          close(fd);
          return;
        }
        break;
      }else if(offset == MAX_HEADER_SIZE){
        bytes_w = write(fd, message->buffer, offset);
        if(bytes_w != offset){
          message->status_code = 500;
          //Grab needed bytes to formulate message for logging
          logZero = dprintf(connfd, "%s %d Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n",message->httpversion, message->status_code);
          if(logZero <= 0){
            message->status_code = 0;
            logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
            snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
            message->closeCon = true;
          }else{
            logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
            snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
          }
          message->closeCon = true;
          close(fd);
          return;
        }
        offset = 0;
        memset(message->buffer, 0, MAX_HEADER_SIZE);
      }

      if(bytes == 0){
        break;
      }
    }
    //response
    message->status_code = 201;
    //Grab needed bytes to formulate message for logging
    logZero= dprintf(connfd, "%s %d Created\r\nContent-Length: 8\r\n\r\nCreated\n", message->httpversion, message->status_code);
    if(logZero <= 0){
      message->status_code = 0;
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
    }else{
      logBytes = snprintf(NULL, 0, "%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, message->content_length)+9;
      snprintf(message->logbuf, logBytes,"%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, message->content_length);
    }
    close(fd);
    return; 
  }else{//if it exists
    //check if filename is writable
    check = access(message->filename, W_OK);
    if(check == 0){
      //overwrite file
      fd = open(message->filename, O_WRONLY|O_TRUNC);
      //loop request body and write into file
      while(bytes_r < message->content_length){
        bytes = recv(connfd, message->buffer + offset, BUFFER_SIZE, 0);
        if(bytes <= 0 && bytes_r != message->content_length){
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
          close(fd);
          return;
        }
        bytes_r+= bytes;
        offset+= bytes;
        if(bytes_r == message->content_length){
          bytes_w = write(fd, message->buffer, offset);
          memset(message->buffer, 0, MAX_HEADER_SIZE);
          if(bytes_w != offset){
            message->status_code = 500;
            //Grab needed bytes to formulate message for logging
            logZero = dprintf(connfd, "%s %d Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n",message->httpversion, message->status_code);
            if(logZero <= 0){
              message->status_code = 0;
              logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
              snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
            }else{
              logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
              snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
            }
            message->closeCon = true;
            close(fd);
            return;
          }
          break;
        }else if(offset == MAX_HEADER_SIZE){
          bytes_w = write(fd, message->buffer, offset);
          if(bytes_w != offset){
            message->status_code = 500;
            //Grab needed bytes to formulate message for logging
            logZero = dprintf(connfd, "%s %d Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n",message->httpversion, message->status_code);
            if(logZero <= 0){
              message->status_code = 0;
              logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
              snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
            }else{
              logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
              snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
            }
            message->closeCon = true;
            close(fd);
            return;
          }
          offset = 0;
          memset(message->buffer, 0, MAX_HEADER_SIZE);
        }

        if(bytes == 0){
          bytes_w = write(fd, message->buffer, offset);
          if(bytes_w != offset){
            message->status_code = 500;
            //Grab needed bytes to formulate message for logging
            logZero = dprintf(connfd, "%s %d Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n",message->httpversion, message->status_code);
            if(logZero <= 0){
              message->status_code = 0;
              logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
              snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
            }else{
              logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
              snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
            }
            message->closeCon = true;
            close(fd);
            return;
          }
          offset = 0;
          memset(message->buffer, 0, MAX_HEADER_SIZE);
          break;
        }
      }
      //response
    message->status_code = 200;
    //Grab needed bytes to formulate message for logging
    logZero = dprintf(connfd, "%s %d OK\r\nContent-Length: 3\r\n\r\nOK\n", message->httpversion, message->status_code);
    if(logZero <= 0){
      message->status_code = 0;
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
    }else{
      logBytes = snprintf(NULL, 0, "%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, message->content_length)+9;
      snprintf(message->logbuf, logBytes,"%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, message->content_length);
    }
    close(fd);
    return; 
    }else{//if file exists but is forbidden
      //increment the buffer
      while(bytes_r < message->content_length){
        bytes = recv(connfd, message->buffer + offset, BUFFER_SIZE, 0);
        if(bytes <= 0 && bytes_r != message->content_length){
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
          return;
        }
        bytes_r+= bytes;
        offset+= bytes;
        if(bytes_r == message->content_length){
          memset(message->buffer, 0, MAX_HEADER_SIZE);
          break;
        }else if(offset == MAX_HEADER_SIZE){
          offset = 0;
          memset(message->buffer, 0, MAX_HEADER_SIZE);
        }

        if(bytes == 0){
          offset = 0;
          memset(message->buffer, 0, MAX_HEADER_SIZE);
          break;
        }
      }
      //response
      message->status_code = 403;
      logZero = dprintf(connfd, "%s %d Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", message->httpversion, message->status_code);
      if(logZero <= 0){
        message->status_code = 0;
        logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
        snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
        message->closeCon = true;
      }else{
        logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
        snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
      }
      return; 
    }
  }  
  return;
}
/*
void getResponse(int connfd, struct httpObject* message)

Parameters = connfd, holds the sockect connection to the client
             message = data structure/object to contents of request

What it does:
If the request is of GET method, the function is called and it checks the filename against the directory thats being hosted. 
If it does not exist the function proceeds to formulate a 404 response and continue the connection
If it does exist the function proceeds to send a 200 ok message and loop by 1 byte over the file and send it back to the client and continue.
If a file is forbidden then the function proceeds to formulate a 403 response and continue 
Errors are printed accordingly when a file is an internal server error
If logging was enabled. Call snprintf with a NULL output string to grab needed bytes to formulate message in a buffer.

*/
void getResponse(int connfd,struct httpObject* message){
  //Declarations
  ssize_t bytes = 0, bytes_r = 0, bytes_w = 0, offset = 0, logBytes = 0;
  int logZero;
  int fd;
  int check;
  //Open filename to read
  fd = open(message->filename, O_RDONLY);
  struct stat statt;
  fstat(fd, &statt);
  //check if filename exists
  check = access(message->filename, F_OK);
  message->closeCon = false;
  if(check != 0){//if it does not exist
    message->status_code = 404;


    //Grab needed bytes to formulate message for logging
    logZero = dprintf(connfd, "%s %d Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", message->httpversion, message->status_code);
    if(logZero <= 0){
      message->status_code = 0;
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
      message->closeCon = true;
    }else{
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
    }
    //close(fd);
    memset(message->buffer, 0, MAX_HEADER_SIZE);//clean buffer
    return;

  }
  //check if file is readable
  check = access(message->filename, R_OK);
  if(check != 0){//if its not
    message->status_code = 403;
    //Grab needed bytes to formulate message for logging
    logZero = dprintf(connfd, "%s %d Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n", message->httpversion, message->status_code);
    if(logZero <= 0){
      message->status_code = 0;
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
      message->closeCon = true;
    }else{
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
    }
    close(fd);
    memset(message->buffer, 0, MAX_HEADER_SIZE);//clean buffer
    return; 
  }
  //response
  message->status_code = 200;
  //Grab needed bytes to formulate message for logging
  logZero = dprintf(connfd, "%s %d OK\r\nContent-Length: %li\r\n\r\n", message->httpversion, message->status_code, statt.st_size);
  if(logZero <= 0){
    message->status_code = 0;
    logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
    snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
    message->closeCon = true;
    close(fd);
    return;
  }else{
    logBytes = snprintf(NULL, 0, "%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, statt.st_size)+9;
    snprintf(message->logbuf, logBytes,"%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, statt.st_size);
  }
  //loop the file until it reaches content length, send back to client
  while(bytes_r < statt.st_size){
    bytes = read(fd, message->buffer + offset, BUFFER_SIZE);
    bytes_r+= bytes;
    offset+= bytes;
    if(bytes_r == statt.st_size){
      bytes_w = write(connfd, message->buffer, offset);
      if(bytes_w <= 0 && bytes_r < statt.st_size){
        message->status_code = 0;
        logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
        snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
        close(fd);
        message->closeCon = true;
        memset(message->buffer, 0, MAX_HEADER_SIZE);
        return;
      }
      memset(message->buffer, 0, MAX_HEADER_SIZE);
      if(bytes_w != offset){
        message->status_code = 500;
        //Grab needed bytes to formulate message for logging
        logZero = dprintf(connfd, "%s %d Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n",message->httpversion, message->status_code);
        if(logZero <= 0){
          message->status_code = 0;
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
        }else{
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
        }
        message->closeCon = true;
        close(fd);
        return;
      }
      break;
    }else if(offset == MAX_HEADER_SIZE){
      bytes_w = write(connfd, message->buffer, offset);
      if(bytes_w <= 0 && bytes_r < statt.st_size){
        message->status_code = 0;
        logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
        snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
        message->closeCon = true;
        close(fd);
        return;
      }
      if(bytes_w != offset){
        message->status_code = 500;
        //Grab needed bytes to formulate message for logging
        logZero=dprintf(connfd, "%s %d Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error\n",message->httpversion, message->status_code);
        if(logZero <= 0){
          message->status_code = 0;
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
        }else{
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
        }
        message->closeCon = true;
        close(fd);
        return;
      }
      offset = 0;
      memset(message->buffer, 0, MAX_HEADER_SIZE);//clean buffer
    }

    if(bytes == 0){
      break;
    }
  }
  memset(message->buffer, 0, MAX_HEADER_SIZE);//clean buffer
  return;
}
/*
void headResponse(int connfd, struct httpObject* message)

Parameters = connfd, holds the sockect connection to the client
             message = data structure/object to contents of request

What it does:
If the request is of HEAD method, the function is called and it checks the filename against the directory thats being hosted. 
If it does not exist the function proceeds to formulate a 404 response and continue the connection
If it does exist the function proceeds to send a 200 ok message along with the content length
If a file is forbidden then the function proceeds to formulate a 403 response and continue the connection
If logging was enabled. Call snprintf with a NULL output string to grab needed bytes to formulate message in a buffer.
*/

void headResponse(int connfd,struct httpObject* message){
  //open file
  int fd = open(message->filename, O_RDONLY);
  ssize_t logBytes = 0;
  int logZero;
  struct stat statt;
  fstat(fd, &statt);
  int check;
  //check to see if file exists
  check = access(message->filename, F_OK);
  if(check != 0){ 
    message->status_code = 404;
    //Grab needed bytes to formulate message for logging
    logZero = dprintf(connfd, "%s %d Not Found\r\nContent-Length: 10\r\n\r\nNot Found\n", message->httpversion, message->status_code);
    if(logZero <= 0){
      message->status_code = 0;
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
    }else{
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
    }
    close(fd);
    memset(message->buffer, 0, MAX_HEADER_SIZE);
    return;

  }
  //check to see if file is readable
  check = access(message->filename, R_OK);
  if(check!=0){
    message->status_code = 403;
    //Grab needed bytes to formulate message for logging
    logZero = dprintf(connfd, "%s %d Forbidden\r\nContent-Length: 10\r\n\r\nForbidden\n",message->httpversion, message->status_code);
    if(logZero <= 0){
      message->status_code = 0;
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
    }else{
      logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
      snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
    }
    close(fd);
    memset(message->buffer, 0, MAX_HEADER_SIZE);
    return; 
  }
  //response
  message->status_code = 200;
  //Grab needed bytes to formulate message for logging
  logZero = dprintf(connfd, "%s %d OK\r\nContent-Length: %li\r\n\r\n", message->httpversion, message->status_code, statt.st_size);
  if(logZero <= 0){
    message->status_code = 0;
    logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion)+9;// https://stackoverflow.com/questions/7315936/which-of-sprintf-snprintf-is-more-secure
    snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t000\n", message->method, message->filename, message->httpversion);
  }else{
    logBytes = snprintf(NULL, 0, "%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, statt.st_size)+9;
    snprintf(message->logbuf, logBytes,"%s\t/%s\t%s\t%li\n", message->method, message->filename, message->hostname, statt.st_size);
  }
  close(fd);
  memset(message->buffer, 0, MAX_HEADER_SIZE);//clear buffer
  return;
}
/*
void handle_connection(int connfd, struct httpObject* message)

Parameters = connfd, holds the sockect connection to the client
             message = data structure/object to contents of request

What it does:
Loops the request(s) by 1 bytes.
Inside the loop it checks for the first line in the header specifically and grabs the filename, httpversion, and method
then checks if it is a valid method or filename, if not, send 501 or 400 and close connection
If so, it continues until the end of the header and then checks if its a put, get, head
if put, sscan the buffer for the content length number and call putResponse
if get, call getResponse
if head, call headResponse
Returns when no requests are available anymore
*/

void* handle_connection(int connfd, struct httpObject* message, int logfd, bool toLog) {
  //Declarations
  int confd = connfd;
  int logFd = logfd;
  bool toBe = toLog;
  ssize_t bytes = 0, bytes_r = 0, bytes_w = 0, logBytes = 0;
  int check = 0;
  char placeHolder[30];//sscanf was acting weird so this is a placeHolder
  char* temp0;
  memset(message->buffer, 0, sizeof(message->buffer)); // Relieve Errors in valgrind
  memset(message->filename, 0, sizeof(message->filename));
  memset(message->method, 0, sizeof(message->method));
  memset(message->httpversion, 0, sizeof(message->httpversion));
  memset(message->hostname, 0, sizeof(message->hostname));
  memset(message->logbuf, 0, sizeof(message->logbuf));
  message->closeCon = false;
  //loop the requests by 1 byte until it reaches end of header then calls a response function and repeats if neccessary
  while((bytes = recv(confd, message->buffer + bytes_r, BUFFER_SIZE, 0)) > 0 && bytes_r < MAX_HEADER_SIZE){
    bytes_r += bytes;
    if(check == 0 && strstr(message->buffer, "\r\n") != NULL){//first line, use check as a way to denote to call it once
      check+=1;
      sscanf(message->buffer, "%s %s %s", message->method, message->filename, message->httpversion);
      if(strcmp(message->method, "GET") != 0 && strcmp(message->method, "PUT") != 0 &&
        strcmp(message->method, "HEAD") != 0){ //check method
        message->status_code = 501;
        logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;
        snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
        if(toBe){
          logging(message->logbuf, logFd);
        }
        memset(message->logbuf, 0, 400);  
        dprintf(confd, "%s %d Not Implemented\r\nContent-Length: 15\r\n\r\nNot Implemented\n", message->httpversion, message->status_code);
        return NULL;
      }
      memmove(message->filename, message->filename+1, 16);//adjust the filename to get rid of '/'
      for (size_t i = 0; i < strlen(message->filename); i++) {
        if((isalpha(message->filename[i]) == 0 && isdigit(message->filename[i]) == 0) || strlen(message->filename) != 15 ) { //checks if filename is valid
          message->status_code = 400;
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
          if(toBe){
            logging(message->logbuf, logFd);
          }
          dprintf(confd, "%s %d Bad request\r\nContent-Length: 0\r\n\r\n", message->httpversion, message->status_code);
          memset(message->logbuf, 0, 400);  
          message->closeCon = true;
          return NULL;

        }
      }
      if(strcmp(message->httpversion, "HTTP/1.1") != 0){
          message->status_code = 400;
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
          if(toBe){
            logging(message->logbuf, logFd);
          }
          dprintf(confd, "%s %d Bad request\r\nContent-Length: 0\r\n\r\n", message->httpversion, message->status_code);
          memset(message->logbuf, 0, 400);  
          message->closeCon = true;
          return NULL;
      }
    }else if(strstr(message->buffer, "\r\n\r\n")){//end of header
      bytes_w = write(STDOUT_FILENO, message->buffer, bytes_r);//print header 
      assert(bytes_w == bytes_r);
      check = 0;
      char* tokenize0 = message->buffer;
      char* checker;
      char* checker2;
      int isHost = 0;
      temp0 = strtok_r(tokenize0, "\r\n", &tokenize0);
      while(temp0!=NULL){//loop buffer until we get content length line
        checker =strstr(temp0, "Host: ");
        checker2 =strstr(temp0, "Content-Length: ");
        if(checker){
          isHost = isHost + 1;
          sscanf(temp0, "%s %s", placeHolder , message->hostname);
        }   
        if(checker2){
          sscanf(temp0, "%s %li", placeHolder , &(message->content_length)); 
        }  
        temp0 = strtok_r(tokenize0, "\r\n", &tokenize0);
      }
      if(isHost == 0){
          message->status_code = 400;
          logBytes = snprintf(NULL, 0, "FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code)+9;
          snprintf(message->logbuf, logBytes,"FAIL\t%s /%s %s\t%d\n", message->method, message->filename, message->httpversion, message->status_code);
          if(toBe){
            logging(message->logbuf, logFd);
          }
          dprintf(confd, "%s %d Bad request\r\nContent-Length: 0\r\n\r\n", message->httpversion, message->status_code);
          memset(message->logbuf, 0, 400);  
          message->closeCon = true;
          return NULL;
      }
      if(strcmp(message->method, "PUT") == 0){
        memset(message->buffer, 0, MAX_HEADER_SIZE );//clear buffer
        bytes_r = 0;
        bytes = 0;
        putResponse(confd, message);
        if(toBe){
          logging(message->logbuf, logFd);
        }
        memset(message->logbuf, 0, 400);    
        if(message->closeCon){
          memset(message->buffer, 0, MAX_HEADER_SIZE);
          return NULL;
        }
      }else if(strcmp(message->method, "GET") == 0){
        memset(message->buffer, 0, MAX_HEADER_SIZE);        
        bytes_r = 0;
        bytes = 0;
        getResponse(confd, message);
        if(toBe){
          logging(message->logbuf, logFd);
        }
        memset(message->logbuf, 0, 400);    
        if(message->closeCon){
          memset(message->buffer, 0, MAX_HEADER_SIZE);
          return NULL;
        }

      }else if(strcmp(message->method, "HEAD") == 0){
        memset(message->buffer, 0, MAX_HEADER_SIZE);
        bytes_r = 0;
        bytes = 0;
        headResponse(confd, message); 
        if(toBe){
          logging(message->logbuf, logFd);
        }
        memset(message->logbuf, 0, 400);         
      }
    }
  }
  return NULL;

}
/*
void* thread_func(void* data)

Parameters = void* data

------------------------------

data holds the threadArg data structure so we cast threadArg to transform it back.

This is the home for each worker thread to wait for a connection.

When the dispatcher signals a new connection then an available worker takes the connection and begins working on it. O.W threads sleep and wait.
*/
void* thread_func(void* data){
  //int threadId = *((int*)data); 
  threadArg* arg = ((threadArg*)data);
  int logfd = arg->logfd;
  bool toLog = arg->toLog;
  while(1){
    struct httpObject message;
    int conn;
    pthread_mutex_lock(&mutex);
    if((conn = getConnectionRequest()) == -1){
      pthread_cond_wait(&condition, &mutex);
      conn = getConnectionRequest();
    }
    pthread_mutex_unlock(&mutex);
    if(conn != -1){
      handle_connection(conn, &message, logfd, toLog);
      close(conn);
    }else{
      sleep(1);
    }
  }
}

int main(int argc, char *argv[]) {
  int listenfd;
  uint16_t port;
  int opt_ret;
  char* logF;
  int numthreads = 4;
  char* thr = "";
  threadArg args;
  args.toLog = false;
  args.logfd=-1;
  while((opt_ret = getopt(argc, argv, "N:l:")) != -1){
    switch(opt_ret){
       case 'N':
          thr = optarg;
          break;
       case 'l':
          logF = optarg;
          args.logfd = open(logF, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH | S_IRGRP | S_IWGRP);
          args.toLog = true;
          break;
       case '?':
          return -1;
       default:
          break;
    }
 }
  if(thr!= NULL){
    sscanf(thr, "%d", &numthreads);
  }
  port = strtouint16(argv[optind]);
  //int threadId[numthreads];
  pthread_t thread_pool[numthreads];
  for (int i = 0; i < numthreads; ++i){
    //threadId[i] = i;
    pthread_create(&thread_pool[i], NULL, thread_func, &args);
  }
  if (port == 0) {
    errx(EXIT_FAILURE, "invalid port number: %s", argv[optind]);
  }
  listenfd = create_listen_socket(port);
  while(1) {
    int connfd = accept(listenfd, NULL, NULL);
    if (connfd < 0) {
      warn("accept error");
      continue;
    }
    pthread_mutex_lock(&mutex);
    addConnectionRequest(connfd);
    pthread_cond_signal(&condition);
    pthread_mutex_unlock(&mutex);
  }
  return EXIT_SUCCESS;
}
