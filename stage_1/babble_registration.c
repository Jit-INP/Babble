#include <stdio.h>
#include <string.h>
#include <semaphore.h>

#include "babble_registration.h"

client_bundle_t *registration_table[MAX_CLIENT];
int nb_registered_clients;

sem_t mutex_r, mutex_w;
int readcount;

void registration_init(void)
{
    // Initialize synchronization structures
    if(sem_init(&mutex_r, 0, 1) == 1)
      printf("Error on mutex_r semaphore\n");

    if(sem_init(&mutex_w, 0, 1) == 1)
      printf("Error on mutex_w semaphore\n");

    readcount = 0;

    nb_registered_clients=0;

    memset(registration_table, 0, MAX_CLIENT * sizeof(client_bundle_t*));
}

client_bundle_t* registration_lookup(unsigned long key)
{
    sem_wait(&mutex_r);
    readcount++;
    if(readcount == 1)
      sem_wait(&mutex_w);
    sem_post(&mutex_r);

    int i=0;
    client_bundle_t *c = NULL;

    for(i=0; i< nb_registered_clients; i++){
        if(registration_table[i]->key == key){
            c = registration_table[i];
            break;
        }
    }

    sem_wait(&mutex_r);
    readcount--;
    if(readcount == 0)
      sem_post(&mutex_w);
    sem_post(&mutex_r);

    return c;
}

int registration_insert(client_bundle_t* cl)
{
    sem_wait(&mutex_w);
    if(nb_registered_clients == MAX_CLIENT){
        fprintf(stderr, "ERROR: MAX NUMBER OF CLIENTS REACHED\n");
        sem_post(&mutex_w);
        return -1;
    }

    /* lookup to find if key already exists */
    int i=0;


    for(i=0; i< nb_registered_clients; i++){
        if(registration_table[i]->key == cl->key){
            break;
        }
    }


    if(i != nb_registered_clients){
        fprintf(stderr, "Error -- id % ld already in use\n", cl->key);
        sem_post(&mutex_w);
        return -1;
    }

    /* insert cl */
    registration_table[nb_registered_clients]=cl;
    nb_registered_clients++;

    sem_post(&mutex_w);
    return 0;
}


client_bundle_t* registration_remove(unsigned long key)
{
    sem_wait(&mutex_w);
    int i=0;

    for(i=0; i<nb_registered_clients; i++){
        if(registration_table[i]->key == key){
            break;
        }
    }

    if(i == nb_registered_clients){
        fprintf(stderr, "Error -- no client found\n");
        sem_post(&mutex_w);
        return NULL;
    }

    client_bundle_t* cl= registration_table[i];

    nb_registered_clients--;
    registration_table[i] = registration_table[nb_registered_clients];

    sem_post(&mutex_w);
    return cl;
}
