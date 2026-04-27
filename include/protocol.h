#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <sys/types.h>
#include <sys/time.h>

#define SERVER_FIFO "/tmp/controller_fifo"

#define MSG_EXECUTE      1  // runner quer executar
#define MSG_DONE         2  // runner terminou
#define MSG_STATUS       3  // runner quer lista (-c)
#define MSG_SHUTDOWN     4  // runner pede shutdown (-s)
#define MSG_STATUS_DONE  5  // filho do -c terminou; controller deve fazer waitpid

typedef struct {
    pid_t runner_pid;
    int   user_id;
    int   command_id;
    char  command[256];
    int   msg_type;
    struct timeval start_time;
} Message;


#endif