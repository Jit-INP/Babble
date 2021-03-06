#ifndef __BABBLE_CONFIG_H__
#define __BABBLE_CONFIG_H__

#define BABBLE_BACKLOG 100

#define BABBLE_PORT 5656
#define MAX_CLIENT 1000
#define MAX_FOLLOW MAX_CLIENT

#define BABBLE_BUFFER_SIZE 256
#define BABBLE_SIZE 64
#define BABBLE_ID_SIZE 16

#define BABBLE_DELIMITER " "

#define BABBLE_TIMELINE_MAX 4

#define BABBLE_EXECUTOR_THREADS 1

#define BABBLE_PRODCONS_SIZE 10

/* defines the number of prodcons buffers in stage 3 */
#define BABBLE_PRODCONS_NB 1

/* expressed in micro-seconds */
#define MAX_DELAY 10000

#endif
