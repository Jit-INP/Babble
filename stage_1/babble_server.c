#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>

#include "babble_server.h"
#include "babble_types.h"
#include "babble_utils.h"
#include "babble_communication.h"
#include "babble_server_answer.h"
#include "fastrand.h"
#include "babble_registration.h"


#define NB_COM_THREADS 3
#define COMMAND_BUFFER_SIZE 5

/* to activate random delays in the processing of messages */
int random_delay_activated;

command_t* command_buffer[COMMAND_BUFFER_SIZE];
sem_t fullCount, emptyCount, mutex;
int in, out;

static void display_help(char *exec)
{
    printf("Usage: %s -p port_number -r [activate_random_delays]\n", exec);
}


static int parse_command(char* str, command_t *cmd)
{
    char *name = NULL;

    /* start by cleaning the input */
    str_clean(str);

    /* get command id */
    cmd->cid=str_to_command(str, &cmd->answer_expected);

    switch(cmd->cid){
    case LOGIN:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            name = get_name_from_key(cmd->key);
            fprintf(stderr,"Error from [%s]-- invalid LOGIN -> %s\n", name, str);
            free(name);
            return -1;
        }
        break;
    case PUBLISH:
        if(str_to_payload(str, cmd->msg, BABBLE_SIZE)){
            name = get_name_from_key(cmd->key);
            fprintf(stderr,"Warning from [%s]-- invalid PUBLISH -> %s\n", name, str);
            free(name);
            return -1;
        }
        break;
    case FOLLOW:
        if(str_to_payload(str, cmd->msg, BABBLE_ID_SIZE)){
            name = get_name_from_key(cmd->key);
            fprintf(stderr,"Warning from [%s]-- invalid FOLLOW -> %s\n", name, str);
            free(name);
            return -1;
        }
        break;
    case TIMELINE:
        cmd->msg[0]='\0';
        break;
    case FOLLOW_COUNT:
        cmd->msg[0]='\0';
        break;
    case RDV:
        cmd->msg[0]='\0';
        break;
    default:
        name = get_name_from_key(cmd->key);
        fprintf(stderr,"Error from [%s]-- invalid client command -> %s\n", name, str);
        free(name);
        return -1;
    }

    return 0;
}


/* processes the command and eventually generates an answer */
static int process_command(command_t *cmd, answer_t **answer)
{
    int res=0;

    switch(cmd->cid){
    case LOGIN:
        res = run_login_command(cmd, answer);
        break;
    case PUBLISH:
        random_delay(random_delay_activated);
        res = run_publish_command(cmd, answer);
        break;
    case FOLLOW:
        random_delay(random_delay_activated);
        res = run_follow_command(cmd, answer);
        break;
    case TIMELINE:
        random_delay(random_delay_activated);
        res = run_timeline_command(cmd, answer);
        break;
    case FOLLOW_COUNT:
        res = run_fcount_command(cmd, answer);
        break;
    case RDV:
        res = run_rdv_command(cmd, answer);
        break;
    default:
        fprintf(stderr,"Error -- Unknown command id\n");
        return -1;
    }

    if(res){
        fprintf(stderr,"Error -- Failed to run command ");
        display_command(cmd, stderr);
    }

    return res;
}


void* communication_thread(void* nsockfd)
{
    int newsockfd = (int) nsockfd;

    char* recv_buff=NULL;
    int recv_size=0;

    unsigned long client_key=0;
    char client_name[BABBLE_ID_SIZE+1];

    command_t *cmd;
    answer_t *answer=NULL;

    memset(client_name, 0, BABBLE_ID_SIZE+1);

    // Recv login message
    if((recv_size = network_recv(newsockfd, (void**)&recv_buff)) < 0){
        fprintf(stderr, "Error -- recv from client\n");
        close(newsockfd);
        return NULL;
    }

    cmd = new_command(0);

    // Parse the message to get the command
    if(parse_command(recv_buff, cmd) == -1 || cmd->cid != LOGIN){
        fprintf(stderr, "Error -- in LOGIN message\n");
        close(newsockfd);
        free(cmd);
        return NULL;
    }

    /* before processing the command, we should register the
     * socket associated with the new client; this is to be done only
     * for the LOGIN command */
    cmd->sock = newsockfd;

    // Process the login command
    if(process_command(cmd, &answer) == -1){
        fprintf(stderr, "Error -- in LOGIN\n");
        close(newsockfd);
        free(cmd);
        return NULL;
    }

    /* notify client of registration */
    // Send answer to the client
    if(send_answer_to_client(answer) == -1){
        fprintf(stderr, "Error -- in LOGIN ack\n");
        close(newsockfd);
        free(cmd);
        free_answer(answer);
        return NULL;
    }
    else{
        free_answer(answer);
    }

    /* let's store the key locally */
    client_key = cmd->key;

    strncpy(client_name, cmd->msg, BABBLE_ID_SIZE);
    free(recv_buff);
    free(cmd);

    while((recv_size=network_recv(newsockfd, (void**) &recv_buff)) > 0){

        cmd = new_command(client_key);

        // Parse the message to get the command
        if(parse_command(recv_buff, cmd) == -1){
            fprintf(stderr, "Warning: unable to parse message from client %s\n", client_name);
            notify_parse_error(cmd, recv_buff, &answer);
            send_answer_to_client(answer);
            free_answer(answer);
            free(cmd);
        }
        else{
            // Put the command in the command_buffer

            sem_wait(&emptyCount);
            sem_wait(&mutex);
            command_buffer[in] = cmd;
            in = (in+1)%COMMAND_BUFFER_SIZE;
            sem_post(&mutex);
            sem_post(&fullCount);

        }
        free(recv_buff);
    }

    // Unregister the client
    if(client_name[0] != 0){
        cmd = new_command(client_key);
        cmd->cid= UNREGISTER;

        if(unregisted_client(cmd)){
            fprintf(stderr,"Warning -- failed to unregister client %s\n",client_name);
        }
        free(cmd);
    }
}

void* execution_thread()
{
    /* seed for the per-thread random number generator */
    fastRandomSetSeed(time(NULL)+pthread_self()*100);

    while(1)
    {
        command_t *cmd;
        answer_t *answer=NULL;

        // Take a command from the command_buffer

        sem_wait(&fullCount);
        sem_wait(&mutex);
        cmd = command_buffer[out];
        out = (out+1)%COMMAND_BUFFER_SIZE;
        sem_post(&mutex);
        sem_post(&emptyCount);

        // Process he command and generate an answer
        if(process_command(cmd, &answer) == -1){
            fprintf(stderr, "Warning: unable to process command from client %lu\n", cmd->key);
        }
        free(cmd);

        // Send answer to the client
        if(send_answer_to_client(answer) == -1){
            fprintf(stderr, "Warning: unable to answer command from client %lu\n", answer->key);
        }
        free_answer(answer);
    }
}

int main(int argc, char *argv[])
{
    pthread_t com_tids[NB_COM_THREADS];
    pthread_t exec_tid;
    int tid = 0;

    int sockfd, newsockfd;
    int portno=BABBLE_PORT;

    int opt;
    int nb_args=1;

    // Parse input arguments
    while ((opt = getopt (argc, argv, "+hp:r")) != -1){
        switch (opt){
        case 'p':
            portno = atoi(optarg);
            nb_args+=2;
            break;
        case 'r':
            random_delay_activated=1;
            nb_args+=1;
            break;
        case 'h':
        case '?':
        default:
            display_help(argv[0]);
            return -1;
        }
    }

    if(nb_args != argc){
        display_help(argv[0]);
        return -1;
    }

    // Initialize the synchronization structures
    if(sem_init(&fullCount, 0, 0) == 1)
      printf("Error on fullCount semaphore\n");

    if(sem_init(&emptyCount, 0, COMMAND_BUFFER_SIZE) == 1)
      printf("Error on fullCount semaphore\n");

    if(sem_init(&mutex, 0, 1) == 1)
      printf("Error on mutex semaphore\n");

    in = 0;
    out = 0;

    // Initialize server data structures
    server_data_init();

    // Initialize and open the server socket
    if((sockfd = server_connection_init(portno)) == -1){
        return -1;
    }

    printf("Babble server bound to port %d\n", portno);

    /* seed for the per-thread random number generator */
    //fastRandomSetSeed(time(NULL)+pthread_self()*100);

    if(pthread_create (&exec_tid, NULL, execution_thread, NULL) != 0){
        fprintf(stderr,"Failed to create the execution thread\n");
        return EXIT_FAILURE;
    }

    /* main server loop */
    while(1){

        if((newsockfd= server_connection_accept(sockfd))==-1){
            return -1;
        }

        // Start a new communication thread to handle the new client with the file descriptor as argument
        if(pthread_create (&com_tids[tid], NULL, communication_thread, (void*) newsockfd) != 0){
            fprintf(stderr,"Failed to create the communication thread\n");
            return EXIT_FAILURE;
        }
        //break;
    }
    for(int i = 0; i < NB_COM_THREADS; i++)
    {
        pthread_join(com_tids[i], NULL);
    }
    pthread_join(exec_tid, NULL);
    close(sockfd);
    return 0;
}
