#include <sys/socket.h>
#include <sys/stat.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <fcntl.h>
#include <unistd.h> // write
#include <string.h> // memset
#include <stdlib.h> // atoi
#include <stdbool.h> // true, false
#include <ctype.h>  // alnum
#include <errno.h>  // error
#include <sys/stat.h> // stat
#include <pthread.h>
#include <limits.h> // queue
#include <getopt.h>

#define BUFFER_SIZE 512
#define BUFFERSIZE 16000
#define QUEUE_SIZE 100

int queue_size = 0;

// Source: GeeksforGeeks
// C program for array implementation of queue 
struct Queue 
{ 
    int front, rear, size; 
    unsigned capacity; 
    int* array; 
}; 
struct Queue* createQueue(unsigned capacity) 
{ 
    struct Queue* queue = (struct Queue*) malloc(sizeof(struct Queue)); 
    queue->capacity = capacity; 
    queue->front = queue->size = 0;  
    queue->rear = capacity - 1;  // This is important, see the enqueue 
    queue->array = (int*) malloc(queue->capacity * sizeof(int)); 
    return queue; 
} 
int isFull(struct Queue* queue) 
{  return (queue->size == queue->capacity);  } 
int isEmpty(struct Queue* queue) 
{  return (queue->size == 0); } 
void enqueue(struct Queue* queue, int item) 
{ 
    if (isFull(queue)) 
        return; 
    queue->rear = (queue->rear + 1)%queue->capacity; 
    queue->array[queue->rear] = item; 
    queue->size = queue->size + 1; 
} 
int dequeue(struct Queue* queue) 
{ 
    if (isEmpty(queue)) 
        return INT_MIN; 
    int item = queue->array[queue->front]; 
    queue->front = (queue->front + 1)%queue->capacity; 
    queue->size = queue->size - 1; 
    return item; 
} 
int front(struct Queue* queue) 
{ 
    if (isEmpty(queue)) 
        return INT_MIN; 
    return queue->array[queue->front]; 
} 
int rear(struct Queue* queue) 
{ 
    if (isEmpty(queue)) 
        return INT_MIN; 
    return queue->array[queue->rear]; 
} 

int cnt = 0, error_cnt = 0;
// int isHealthCheck = 0;
int offset = 0;
int NUM_THREADS = 4;
struct Queue* queue; 
int log_fd = 0;

struct httpObject {
    char method[BUFFER_SIZE];         // PUT, HEAD, GET
    char filename[BUFFER_SIZE]; // what is the file we are worried about
    char httpversion[BUFFER_SIZE];    // HTTP/1.1
    int content_length; // example: 13
    char status_code[30];
    char buffer[BUFFERSIZE];
    int myoffset;
    int isHealthCheck;
};

pthread_cond_t dispatcher_cond = PTHREAD_COND_INITIALIZER;
typedef struct thread_arg_t {
    int val;
    int available;
    pthread_mutex_t *pmutex;
    pthread_cond_t pcondition_variable;
    int socket;
    struct httpObject message;
} ThreadArg;

int bad_request(struct httpObject* message) {
    // httpversion validity
    if (strcmp(message->httpversion, "HTTP/1.1") != 0) {
        return -1;
    }
    // method validity
    if (!(strcmp(message->method, "GET")==0 || strcmp(message->method, "HEAD")==0 || strcmp(message->method, "PUT")==0)) {
        return -1;
    }
    // filename validity
    if (strlen(message->filename) == 0 || strlen(message->filename) > 27) {
        return -1;
    }
    for (int i = 0; i < strlen(message->filename); i++) {
        if (!isascii(message->filename[i])) {
            return -1;
        }
        if (!isalnum(message->filename[i])) {
            if (!(message->filename[i] == '-' || message->filename[i] == '_')) {
                return -1;
            }
        }
    }
    return 0;
}

int read_http_request(int client_sockd, struct httpObject* message) {
    int exit_status = 0;

    /* recv HTTP request */
    char buff_temp[BUFFER_SIZE+1];
    ssize_t bytes = recv(client_sockd, buff_temp, BUFFER_SIZE, MSG_PEEK);
    buff_temp[bytes] = 0; // null terminate
    printf("[+] received %ld bytes from client\n[+] response: ", bytes);

    int ii = 0;
    while (buff_temp[ii] != '\0') {
        if (buff_temp[ii] == '\r') {
            if (buff_temp[ii+2] == '\r') {
                break;
            }
        }
        ii++;
    }
    printf("bytes_to_recv is %d\n", ii);
    int bytes_to_recv = ii+4;

    char buff[bytes_to_recv+1];
    ssize_t bytes1 = recv(client_sockd, buff, bytes_to_recv, 0);
    buff[bytes_to_recv] = '\0'; // null terminate
    printf("[+] received %ld bytes1 from client\n[+] response: ", bytes1);
    write(STDOUT_FILENO, buff, bytes1);

    /* parse HTTP Request */
    char buff_cpy[sizeof(buff)];
    strcpy(buff_cpy, buff);
    char method[BUFFER_SIZE], slash_filename[BUFFER_SIZE], filename[BUFFER_SIZE], version[BUFFER_SIZE];

    /* Method, Filename, Version */
    sscanf(buff_cpy, "%s %s %s\r\n%*s", method, slash_filename, version);
    sscanf(slash_filename, "/%s", filename);
    strcpy(message->method, method);
    strcpy(message->httpversion, version);
    strcpy(message->filename, filename);

    /* Content Length */
    char *cl; int con_len = 0;
    cl = strstr(buff_cpy, "Content-Length: ");
    if (cl != NULL)
        sscanf(cl, "Content-Length: %d\r\n%*s", &con_len);
    message->content_length = con_len;

    exit_status = bad_request(message);   // check request validity
    if (exit_status == -1) {
        printf("400 BAD REQUEST\n");
        strcpy(message->status_code, "400 BAD REQUEST");
        message->content_length = 0;
    }
    printf("%s %s %s %d\n", message->method, message->httpversion, message->filename, message->content_length);

    return exit_status;
}

int resource_check(struct httpObject* message) {

    struct stat stats;
    int n = stat(message->filename, &stats);
    mode_t filemode = stats.st_mode;
    printf("filemode is %d\n", filemode);

    if (strcmp(message->method, "GET") == 0 || strcmp(message->method, "HEAD") == 0) {
        if (n == -1) {
            // perror(message->filename);
            if (errno == ENOENT) {
                strcpy(message->status_code, "404 NOT FOUND");
            } else {
                strcpy(message->status_code, "500 INTERNAL SERVER ERROR");
            }
            return -1;
        } else {
            if (S_ISREG(filemode)) {
                // read permission
                if (!((filemode & S_IRUSR) && (filemode & S_IREAD))) {
                    strcpy(message->status_code, "403 FORBIDDEN");
                    return -1;
                } 
                int filesize = stats.st_size;
                message->content_length = filesize;
                strcpy(message->status_code, "200 OK");
                return filesize;  // offset update: could be 0
            } else if (S_ISDIR(filemode)) {
                strcpy(message->status_code, "403 FORBIDDEN");
                return -1;
            } else {
                strcpy(message->status_code, "500 INTERNAL SERVER ERROR");
                return -1;
            }
        }
    }
    if (strcmp(message->method, "PUT") == 0) {
        // normal for PUT if file does not exist: n == -1
        if (n != -1) {
            if (S_ISREG(filemode)) {
                // write permission
                if (!(filemode & S_IWUSR) && (filemode & S_IWRITE)) {
                    strcpy(message->status_code, "403 FORBIDDEN");
                    return -1;
                } 
                strcpy(message->status_code, "201 CREATED");
                printf("%d\n", message->content_length);
                return message->content_length; // offset update: could be 0
            } else if (S_ISDIR(filemode)) {
                strcpy(message->status_code, "403 FORBIDDEN");
                return -1;
            } else {
                strcpy(message->status_code, "500 INTERNAL SERVER ERROR");
                return -1;
            }
        }
        printf("%d\n", message->content_length);
        strcpy(message->status_code, "201 CREATED");
        return message->content_length; // offset update: could be 0
    }

    return 0; // -1 indicates error
}

int ppwrite(unsigned char* pbuffer, int poffset, int n_recv, int* ppwrite_bytes) {

    unsigned char ready_for_pwirte[BUFFERSIZE];
    for (size_t i = 0; i < n_recv; ++i) {
        if (*(ppwrite_bytes) % 20 == 0) {
            sprintf(ready_for_pwirte, "\n%08d", *(ppwrite_bytes));
            pwrite(log_fd, ready_for_pwirte, 9, poffset);
            poffset += 9;
            memset(ready_for_pwirte, 0, sizeof(ready_for_pwirte));
        }

        sprintf(ready_for_pwirte, " %02x", pbuffer[i]);
        pwrite(log_fd, ready_for_pwirte, 3, poffset);
        poffset += 3;

        memset(ready_for_pwirte, 0, sizeof(ready_for_pwirte));
        ++*(ppwrite_bytes);
    }


    return poffset;
}

void process_request(int client_sockd, struct httpObject* message) {
    printf("Processing Request...\n");

    if (strcmp(message->method, "PUT") == 0) {
        int cl = message->content_length, n;
        if (strcmp(message->status_code, "201 CREATED") != 0) {
            printf("are you throwing out trash?\n");
            while ((n=read(client_sockd, message->buffer, ((cl>BUFFERSIZE)? BUFFERSIZE: cl))) > 0) {
                cl -= n;
                memset(message->buffer, 0, sizeof(message->buffer));
            } // throwing out trash
        } else {
            int fd = open(message->filename, O_CREAT|O_WRONLY|O_TRUNC, 0644), n;
            printf("fd being created %d\n", fd);
            int ppwrite_bytes = 0;
            while ((n=read(client_sockd, message->buffer, ((cl>BUFFERSIZE)? BUFFERSIZE: cl))) > 0) {
                printf("PUTTING file...\n");
                write(fd, message->buffer, n);
                if (log_fd) {
                    int new_offset = ppwrite(message->buffer, message->myoffset, n, &ppwrite_bytes);
                    message->myoffset = new_offset;
                }
                cl -= n;
                memset(message->buffer, 0, sizeof(message->buffer));
            }
            close(fd);
        }
    }

    if (!(strcmp(message->status_code, "200 OK")==0 || strcmp(message->status_code, "201 CREATED")==0)) {
        message->content_length = 0;
    }

    printf("Request processing done...\n");
    return;
}

void construct_http_response(int client_sockd, struct httpObject* message) {
    printf("%s\n", "constructing response...");

    char cl[10], response[BUFFERSIZE];
    memset(response, 0, sizeof(response));
    sprintf(cl, "%d", message->content_length);  // convert int to string

    sprintf(response, "%s %s\r\nContent-Length: %s\r\n\r\n",
        message->httpversion, message->status_code, (strcmp(message->method, "PUT") == 0)? "0": cl);
    printf("%s", response);
    write(client_sockd, response, strlen(response));

    return;
}

void send_response(int client_sockd, struct httpObject* message) {

    if (message->isHealthCheck) {
        int ppwrite_bytes = 0;
        write(client_sockd, message->buffer, strlen(message->buffer));
        int new_offset = ppwrite(message->buffer, message->myoffset, message->content_length, &ppwrite_bytes);
        message->myoffset = new_offset;
        memset(message->buffer, 0, sizeof(message->buffer));
        // isHealthCheck = 0;
        message->isHealthCheck = 0;
        return;
    }

    if (strcmp(message->method, "GET") == 0) {
        int fd = open(message->filename, O_RDONLY), n;
        int ppwrite_bytes = 0;
        while ((n=read(fd, message->buffer, BUFFERSIZE)) > 0) {
            write(client_sockd, message->buffer, n);
            if (log_fd) {
                int new_offset = ppwrite(message->buffer, message->myoffset, n, &ppwrite_bytes);
                message->myoffset = new_offset;
            }
            memset(message->buffer, 0, sizeof(message->buffer));
        }
        close(fd);
    }
}

void offset_calc(struct httpObject* message, bool error) { // everyone should do this
    
    message->myoffset = offset;

    char log_header[BUFFERSIZE], code[4]; int header_length;
    sscanf(message->status_code, "%s %*s", code); code[4] = '\0';

    if (message->isHealthCheck) {
        printf("message filename is %s\n", message->filename);
        int hc_length = snprintf(message->buffer, sizeof(message->buffer), "%d\n%d", error_cnt, cnt);
        message->content_length = hc_length;
    }
    if (error) {
        header_length = snprintf(log_header, sizeof(log_header), "FAIL: %s /%s %s --- response %s", 
            message->method, message->filename, message->httpversion, code);
        
        offset += header_length + 10;
    } else {
        // no error
        printf("you should get here? \n");
        int content_length = message->content_length;
        header_length = snprintf(log_header, sizeof(log_header), "%s /%s length %d", 
            message->method, message->filename, content_length);
        offset += header_length + 10;
        if (strcmp(message->method, "HEAD") != 0) { // GET/PUT contains file content
            int quo = content_length/20;
            int rem = content_length%20;
            offset += quo*69 + (8+rem*3+1);
            printf("what this number is? %d\n", quo*69 + (8+rem*3+1));
        }
    
        
    }

    printf("log msg is\n%s\n", log_header);
    pwrite(log_fd, log_header, header_length, message->myoffset);
    message->myoffset += header_length;
    return;
}

void* consumer(void *obj) {
    ThreadArg* argp = (ThreadArg*) obj;
    
    while (true) {
        // pthread_mutex_lock(argp->pmutex);

        while (argp->available) {
            pthread_mutex_lock(argp->pmutex);

            printf("\033[0;32m[+] Consumer %d going to Sleep\n\033[0m", argp->val);
            pthread_cond_wait(&argp->pcondition_variable, argp->pmutex);
            printf("\033[0;32m[+] Consumer %d Signaled to WAKE UP\n\033[0m", argp->val);

            pthread_mutex_unlock(argp->pmutex);
        }

        
        pthread_mutex_lock(argp->pmutex);
        
        int client_sockd = dequeue(queue);

        int isBadRequest = read_http_request(client_sockd, &(argp->message)); // detect bad request

        // check if the request is healthcheck?

        int code = 0;
        // if (!argp->message.isHealthCheck) {
            if (isBadRequest != -1) {
                code = resource_check(&argp->message);
                if (code == -1) {
                    printf("status_code is %s\n", argp->message.status_code);
                    argp->message.content_length = 0;
                } else {
                    printf("filesize being used to calculate offset is %d\n", code);
                }
            }
        // }

        argp->message.isHealthCheck = 0;
        if (strcmp((argp->message).filename, "healthcheck") == 0 ) {
            printf("HERE THE FILE NAME IS %s\n", (argp->message).filename);
            if (log_fd > 0) {
                if (strcmp((argp->message).method, "GET") == 0 ) {
                    strcpy(argp->message.status_code, "200 OK");
                    argp->message.isHealthCheck = 1;
                    code = 0;
                }
                else {
                    strcpy(argp->message.status_code, "403 FORBIDDEN");
                    argp->message.content_length = 0;
                    // isBadRequest = -1;  // make sure it won't go into resource_check
                    argp->message.isHealthCheck = 0;
                    code = -1;
                }
            } else {
                strcpy(argp->message.status_code, "404 NOT FOUND");
                argp->message.content_length = 0;
                // isBadRequest = -1;
                argp->message.isHealthCheck = 0;
                code = -1;
            }
            printf("healthcheck code is %d\n", code);
        }

        
        printf("\033[0;32m[+] Consumer %d calculates the offset\n\033[0m", argp->val);   
        printf("offset was %d\n", offset);
        offset_calc(&argp->message, (code==-1 || isBadRequest==-1));
        printf("offset incremented to %d\n", offset);
        pthread_mutex_unlock(argp->pmutex);  


        pthread_mutex_lock(argp->pmutex);
        printf("\033[0;32m[+] Consumer %d has the lock\n\033[0m", argp->val);
        // if (isBadRequest != -1)
            process_request(client_sockd, &argp->message); // PUT
        printf("\033[0;32m[+] Consumer %d releases the lock\n\033[0m", argp->val);
        pthread_mutex_unlock(argp->pmutex);


        construct_http_response(client_sockd, &argp->message);

        pthread_mutex_lock(argp->pmutex);
        if (!(code == -1 || isBadRequest == -1)) { 
            printf("Does bad healthcheck gets here? %s\n", argp->message.filename);
            send_response(client_sockd, &argp->message); // GET
        }
        printf("------- Response Sent -------\n");
        pthread_mutex_unlock(argp->pmutex);

        pthread_mutex_lock(argp->pmutex);
        cnt++;
        if (isBadRequest == -1 || code == -1) {
            error_cnt++;
        }
        pthread_mutex_unlock(argp->pmutex);

        pwrite(log_fd, "\n========\n", strlen("\n========\n"), (&argp->message)->myoffset);
        close(client_sockd);
        printf("client_sockd being closed %d\n", (client_sockd));
        printf("\033[0;32m[+] Consumer %d Done consuming...\n\033[0m", argp->val);

        argp->available = 1;    

        // pthread_mutex_lock(argp->pmutex);
        // pthread_cond_signal(&dispatcher_cond);
        // pthread_mutex_unlock(argp->pmutex);

        
    }    
}

void* producer(void *obj) {
    ThreadArg* argp = (ThreadArg*) obj;
    printf("--------- Producer ---------\n");
    printf("\033[0;33m[-] In Producer thread...\n\033[0m");

    while (true) {
        // pthread_mutex_lock(argp[0].pmutex);

        int sum = 0;
        for (int i = 1; i < NUM_THREADS; i++) 
            sum += argp[i].available;
        while(sum == 0) {
            // pthread_mutex_lock(argp[0].pmutex);

            // printf("available worker number is indeed %d\n", sum);
            // printf("\033[0;33m[-] Producer going to Sleep\n\033[0m");
            // pthread_cond_wait(&dispatcher_cond, argp[0].pmutex);
            // pthread_mutex_unlock(argp[0].pmutex);
            // check workers status again
            for (int i = 1; i < NUM_THREADS; i++) 
                sum += argp[i].available;

            // printf("\033[0;33m[-] Producer Signaled to WAKE UP\n\033[0m");
        }

        if (queue_size != 0) { // if there are assignments in queue
            printf("\033[0;33m[-] Producer: getting data from h/w\n\033[0m");
            for (int i = 1; i < NUM_THREADS; i++) {
                printf("consumer %d's availability is %d\n", argp[i].val, argp[i].available);
                if (argp[i].available) {
                    // argp[i].socket = dequeue(queue);
                    queue_size--;
                    argp[i].available = 0;
                    pthread_mutex_lock(argp[0].pmutex);
                    int c = pthread_cond_signal(&argp[i].pcondition_variable);
                    pthread_mutex_unlock(argp[0].pmutex);
                    printf("consumer %d being Signaled\n", argp[i].val);
                    break;
                }
            }
        }
        
    }
}


int main(int argc, char** argv) {

    /* get opt */
    // need to set default port, number of threads, and log file
    char *port = "8080"; 
    int opt, n_th = 4;
    if (argc > 1) {
        printf("port: %s\n", port);
        while ((opt = getopt(argc, argv, "N:l:")) > 0) {
            if (opt == 'N') {
                NUM_THREADS = atoi(optarg);
                NUM_THREADS = (NUM_THREADS < 2)? 2: NUM_THREADS;
            } else if (opt == 'l') {
                char* log_file = optarg;
                log_fd = open(log_file, O_CREAT|O_WRONLY|O_TRUNC, 0644);
            } else {
                printf("Invalid opt: %c\n", opt);
            }
        }
        if (argv[optind] != NULL) {
            port = argv[optind];
        }
    }

    /* Create sockaddr_in with server information */
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(port));
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t addrlen = sizeof(server_addr);
    int server_sockd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockd < 0) { perror("socket"); }
    /* Configure server socket */
    int enable = 1;
    int ret = setsockopt(server_sockd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    ret = bind(server_sockd, (struct sockaddr *) &server_addr, addrlen);
    ret = listen(server_sockd, 5); // 5 should be enough, if not use SOMAXCONN
    if (ret < 0) { return EXIT_FAILURE; }

    /* Connecting with a client */
    struct sockaddr client_addr;
    socklen_t client_addrlen;

    struct httpObject message;
    message.myoffset = offset;
    queue = createQueue(QUEUE_SIZE);

    /* create threads */
    ThreadArg args[NUM_THREADS];
    pthread_t thread_ids[NUM_THREADS];
    pthread_mutex_t my_mutex;
    pthread_mutex_init(&my_mutex, NULL);
    for (int idx = 1; idx < NUM_THREADS; ++idx) {
        args[idx].val = idx;
        args[idx].available = 1;
        args[idx].pmutex = &my_mutex;
        pthread_cond_init(&args[idx].pcondition_variable, NULL);
        pthread_create(&thread_ids[idx], NULL, &consumer, &args[idx]);
        printf("thread %d's availability is %d\n", args[idx].val, args[idx].available);
    }
    args[0].val = 0;
    args[0].available = 1;
    args[0].pmutex = &my_mutex;
    pthread_create(&thread_ids[0], NULL, &producer, &args);

    while (true) {
        
        printf("[+] server is waiting...\n");
        int client_sockd = accept(server_sockd, &client_addr, &client_addrlen);
        if (client_sockd < 0) { perror("client"); }
        if (client_sockd > 0) {
            if (queue->size < QUEUE_SIZE) {
                pthread_mutex_lock(&my_mutex);
                enqueue(queue, client_sockd);
                queue_size++; 
                pthread_mutex_unlock(&my_mutex);
            } else {
                printf("%s\n", "too much requests in queue");
            }
        }

    }

    free(queue);
    for (size_t idx = 0; idx < NUM_THREADS; ++idx) {
        pthread_join(thread_ids[idx], NULL);
    }
    pthread_mutex_destroy(&my_mutex);

    return EXIT_SUCCESS;
}
