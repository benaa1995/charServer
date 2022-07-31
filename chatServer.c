#include "chatServer.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/select.h>

#define WAIT 10
#define NEW 11
#define DESCRIPTOR 12
#define RECEIVED 13
#define CLOSE 14
#define REMOVE 15

void check_argument(int, char *[]);

conn_t *init_conn(conn_t *, int);

void print_status(int, int, int, int);

void create_client_fd(int, int, conn_pool_t *);

void read_from_client(char *, int, conn_pool_t *);

void create_msg(char *, int, conn_t *, conn_pool_t *, int);

int creating_connection(int, int);

void free_pool(conn_pool_t * pool);

int remove_with = 0;

static int end_server = 0;

void intHandler(int SIG_INT) {
    end_server = true;
}

int main(int argc, char *argv[]) {

    int data;
    int new_sd;
    char buff[BUFFER_SIZE];


    //check the argument and the port:
    check_argument(argc, argv);

    //the signal:
    signal(SIGINT, intHandler);

    //init pool function:
    conn_pool_t *pool = malloc(sizeof(conn_pool_t));
    init_pool(pool);

    /*************************************************************/
    /* Create an AF_INET stream socket to receive incoming      */
    /* connections on                                            */
    /*************************************************************/

    int main_socket = 0,port;
    port = atoi(argv[1]);
    main_socket = creating_connection(main_socket, port);

    /*************************************************************/
    /* Set socket to be nonblocking. All of the sockets for      */
    /* the incoming connections will also be nonblocking since   */
    /* they will inherit that state from the listening socket.   */
    /*************************************************************/

    int on = 1;
    ioctl(main_socket, (int) FIONBIO, (char *) &on);

    /*************************************************************/
    /* Initialize fd_sets  			                             */
    /*************************************************************/

    FD_SET(main_socket, &pool->read_set);
    pool->maxfd = main_socket;

    /*************************************************************/
    /* Loop waiting for incoming connects, for incoming data or  */
    /* to write data, on any of the connected sockets.           */
    /*************************************************************/

    do {
        /**********************************************************/
        /* Copy the master fd_set over to the working fd_set.     */
        /**********************************************************/

        pool->ready_read_set = pool->read_set;
        pool->ready_write_set = pool->write_set;

        /**********************************************************/
        /* Call select() 										  */
        /**********************************************************/

        print_status(pool->maxfd, 0, 0, WAIT);
        data = select(pool->maxfd + 1, &pool->ready_read_set, &pool->ready_write_set, 0, 0);
        pool->nready = data;

        for (new_sd = 0; new_sd <= pool->maxfd; new_sd++) {

            if (FD_ISSET(new_sd, &pool->ready_read_set)) {
                if (new_sd == main_socket) {
                    create_client_fd(new_sd, main_socket, pool);
                } else {
                    memset(buff, 0, BUFFER_SIZE);
                    read_from_client(buff, new_sd, pool);
                }
            }
            if (FD_ISSET(new_sd, &pool->ready_write_set)) {
                write_to_client(new_sd, pool);
            }
        }
        if (pool->nr_conns == 0) {
            break;
        } else {
            remove_with = pool->maxfd;
        }

    } while (end_server == false);

    /*************************************************************/
    /* If we are here, Control-C was typed,						 */
    /* clean up all open connections					         */
    /*************************************************************/

    print_status(0, remove_with, 0, REMOVE);
    free_pool(pool);
    free(pool);
    pool=NULL;

    return EXIT_SUCCESS;
}
int init_pool(conn_pool_t* pool) {

    //struct conn_pool:
    pool->maxfd = 0;
    pool->nready = 0;
    pool->nr_conns = 0;
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->ready_read_set);
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->ready_write_set);
    pool->conn_head = NULL;

    return EXIT_SUCCESS;
}
int add_conn(int sd, conn_pool_t *pool) {
    /*
     * 1. allocate connection and init fields
     * 2. add connection to pool
     * */

    int i = 0, finish_to_add = 0;
    conn_t *current, *previous;
    current = pool->conn_head;

    while (1) {

        if (finish_to_add == 1) {
            FD_SET(sd, &pool->read_set);
            if (pool->maxfd < sd) {
                pool->maxfd = sd;
            }
            break;
        }

        if (current == NULL) {
            current = init_conn(current, sd);
            pool->nr_conns++;
            finish_to_add = 1;
            if (i > 0) {
                previous->next = current;
            } else {
                pool->conn_head = current;
            }
        } else {
            previous = current;
            current = current->next;
        }
        i++;

    }

    return EXIT_SUCCESS;

}
conn_t *init_conn(conn_t *conn, int sd) {

    conn = (conn_t *) malloc(sizeof(conn_t));
    conn->prev = NULL;
    conn->next = NULL;
    conn->write_msg_head = NULL;
    conn->write_msg_tail = NULL;
    conn->fd = sd;

    return conn;
}
int remove_conn(int sd, conn_pool_t* pool) {
    /*
    * 1. remove connection from pool
    * 2. deallocate connection
    * 3. remove from sets
    * 4. update max_fd if needed
    */

    conn_t *previous = NULL, *current, *remove = NULL;
    int max_fd = pool->maxfd;
    current = pool->conn_head;
    int finish = 0;
    int i = 0;

    while (1) {

        if (finish == 1) {
            pool->nr_conns--;

            remove_with=sd;
            if (max_fd == sd) {
                pool->maxfd--;
            }
            msg_t *current_msg=NULL,*next_msg;
            current_msg=remove->write_msg_head;
            while (current_msg!=NULL){
                next_msg=current_msg->next;
                free(current_msg->message);
                free(current_msg);
                current_msg=next_msg;
            }
            free(remove);
            FD_CLR(sd, &pool->read_set);
            FD_CLR(sd, &pool->write_set);
            close(sd);
            break;
        }

        if (current != NULL) {
            if (current->fd == sd) {
                remove = current;
                current = current->next;
                finish = 1;
                if (i > 0) {
                    previous->next = current;
                } else {
                    pool->conn_head = current;
                }
            } else {
                previous = current;
                current = current->next;
            }
        }
        i++;
    }

    return EXIT_SUCCESS;
}
int add_msg(int sd,char* buffer,int len,conn_pool_t* pool) {

    /*
     * 1. add msg_t to write queue of all other connections
     * 2. set each fd to check if ready to write
     */
    conn_t *current;
    int finish = 0;
    current = pool->conn_head;
    msg_t *current_msg = NULL;
    int null;

    while (1) {
        if (current == NULL) {
            finish = 1;
        }
        if (finish == 1) {
            break;
        }
        if (current->fd != sd) {
            current_msg = current->write_msg_head;
            if (current_msg == NULL) {
                null = 0;
            } else {
                null = 1;
            }
            create_msg(buffer, (int) len, current, pool, null);
        }
        current = current->next;
    }

    return EXIT_SUCCESS;
}
void create_msg(char *buffer, int size, conn_t *client,conn_pool_t* pool,int null) {

    msg_t *msg = (msg_t *) malloc(sizeof(msg_t));
    msg_t *prev_msg =NULL;
    msg->message = (char *) malloc((size + 1) * sizeof(char));
    msg->size = size;
    strncpy(msg->message, buffer, size);

    if (null == 0) {
        client->write_msg_head = msg;
    }
    if (null == 1) {
        while (client->write_msg_head != NULL) {
            prev_msg = client->write_msg_head;
            client->write_msg_head = client->write_msg_head->next;
        }
        client->write_msg_head=msg;
        prev_msg->next=client->write_msg_head;
    }
    client->write_msg_head->next = NULL;
    client->write_msg_head->prev = NULL;
    FD_SET(client->fd, &pool->write_set);

}
int write_to_client(int sd,conn_pool_t* pool) {

    /*
     * 1. write all msgs in queue
     * 2. deallocate each writen msg
     * 3. if all msgs were writen successfully, there is nothing else to write to this fd... */

    int finish = 0;
    conn_t *write_to_client;
    msg_t *remove;
    msg_t *msg_to_write;
    write_to_client = pool->conn_head;

    while (1) {

        if (write_to_client == NULL) {
            finish = 1;
        } else{
            if (write_to_client->fd == sd) {
                if(write_to_client->write_msg_head!=NULL){
                    msg_to_write=write_to_client->write_msg_head;
                    while (msg_to_write!=NULL){
                        write(sd, msg_to_write->message, msg_to_write->size);
                        remove = msg_to_write;
                        msg_to_write = msg_to_write->next;
                    }
                } else{
                    write_to_client=write_to_client->next;
                }
                write_to_client->write_msg_head=NULL;
                write_to_client->write_msg_tail=NULL;
            }
        }
        if (finish == 1) {
            FD_CLR(sd, &pool->write_set);
            break;
        }
        write_to_client = write_to_client->next;
    }
    free(remove->message);
    free(remove);
    return EXIT_SUCCESS;
}
void check_argument(int argc, char *argv[]) {
    if (argc != 2) {
        perror("Usage: ./chatServer <port>\n");
        exit(EXIT_FAILURE);
    }
    if (atoi(argv[1]) < 0) {
        perror("Usage: ./chatServer <port>\n");
        exit(EXIT_FAILURE);
    }
}
void print_status(int max_fd, int sd, int len, int status) {

    if (status == WAIT) {
        printf("Waiting on select()...\nMaxFd %d\n", max_fd);
    }
    if (status == NEW) {
        printf("New incoming connection on sd %d\n", sd);
    }
    if (status == DESCRIPTOR) {
        printf("Descriptor %d is readable\n", sd);
    }
    if (status == RECEIVED) {
        printf("%d bytes received from sd %d\n", len, sd);

    }
    if (status == CLOSE) {
        printf("Connection closed for sd %d\n", sd);
    }
    if (status == REMOVE) {
        printf("removing connection with sd %d\n", sd);
    }

}
void create_client_fd(int new_sd, int main_socket, conn_pool_t *pool) {

    new_sd = accept(main_socket, NULL, NULL);
    if (new_sd > 0) {
        print_status(0, new_sd, 0, NEW);
        add_conn(new_sd, pool);
    } else {
        perror("Error: accept()\n");
        exit(EXIT_FAILURE);
    }
}
void read_from_client(char *buffer,int client_sd ,conn_pool_t* pool){

    size_t status;
    int len;
    status = read(client_sd, buffer, BUFFER_SIZE);
    print_status(0, client_sd, 0, DESCRIPTOR);

    if (status == 0) {
        remove_conn(client_sd, pool);
        print_status(0, client_sd, 0, CLOSE);
    } else {
        len = (int) status;
        add_msg(client_sd, buffer, len, pool);
        print_status(0, client_sd, len, RECEIVED);
    }
}
int creating_connection(int main_socket, int port) {

    int status;

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = htonl(INADDR_ANY);

    main_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (main_socket < 0) {
        perror("Error:socket()\n");
        exit(EXIT_FAILURE);
    }

    status = bind(main_socket, (struct sockaddr *) &server, sizeof(server));
    if (status < 0) {
        perror("Error: bind()\n");
        exit(EXIT_FAILURE);
    }

    status = listen(main_socket, 5);
    if (status < 0) {
        perror("Error: listen()\n");
        exit(EXIT_FAILURE);
    }
    return main_socket;
}

void free_pool(conn_pool_t * pool){
    conn_t *current_con = NULL;
    conn_t *next_con = NULL;
    current_con = pool->conn_head;
    while (current_con != NULL) {
        next_con = current_con->next;
        remove_conn(current_con->fd,pool);
        current_con = next_con;
    }
}

