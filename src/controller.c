#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include "protocol.h"

// Tamanho máximo das filas
#define MAX_JOBS 1024

// Estrutura interna para guardar informação de um comando em processamento
typedef struct {
    pid_t runner_pid;
    int user_id;
    int command_id;
    char command[256];
    struct timeval start_time;
} Job;

// O Escalonador: Arrays do Controller
Job executing[MAX_JOBS]; // fila de quem está a executar
Job scheduled[MAX_JOBS]; // fila de espera

int exec_count = 0;
int sched_count = 0;
int parallel_limit = 1; // Por defeito, 1 tarefa de cada vez
char sched_policy[64] = "FCFS";

// Função utilitária para informar o Runner (O Semáforo Verde)
void authorize_runner(pid_t runner_pid) {
    char private_path[64];
    sprintf(private_path, "tmp/runner_%d_fifo", runner_pid);
    int fd_private = open(private_path, O_WRONLY);
    if (fd_private != -1) {
        write(fd_private, "OK", 2);
        close(fd_private);
    }
}

int main(int argc, char *argv[]) {
    int shutdown_requested = 0;
    pid_t shutdown_runner_pid = 0;

    // 0. Interpretar os argumentos de Limitação de Paralelismo
    if (argc >= 2) {
        parallel_limit = atoi(argv[1]);
    }
    if (argc >= 3) {
        strncpy(sched_policy, argv[2], 64);
    }

    // 1. Criar a FIFO pública
    unlink(SERVER_FIFO);
    if (mkfifo(SERVER_FIFO, 0666) == -1) {
        perror("Erro mkfifo controller");
        return 1;
    }

    // O_RDWR evita que o fd feche quando não há runners ligados momentaneamente
    int fd_server = open(SERVER_FIFO, O_RDWR);

    Message msg;

    // LOOP PRINCIPAL
    while (!shutdown_requested || exec_count > 0) {
        if (read(fd_server, &msg, sizeof(Message)) <= 0) {
            continue;
        }

        // CADEIA DE DECISÃO 0: Pedido de shutdown?
        if (msg.msg_type == MSG_SHUTDOWN) {
            shutdown_requested = 1;
            shutdown_runner_pid = msg.runner_pid;
            if (exec_count == 0) break;
        }

        // CADEIA DE DECISÃO 1: Foi pedido um novo comando?
        else if (msg.msg_type == MSG_EXECUTE) {

            if (shutdown_requested) {
                continue;
            }

            // A) Há vagas na lista de execução simultânea?
            if (exec_count < parallel_limit) {
                executing[exec_count].runner_pid = msg.runner_pid;
                executing[exec_count].user_id = msg.user_id;
                executing[exec_count].command_id = msg.command_id;
                strncpy(executing[exec_count].command, msg.command, 256);
                executing[exec_count].start_time = msg.start_time;
                exec_count++;

                authorize_runner(msg.runner_pid);
            }
            else {
                // B) Está lotado — vai para a fila de espera
                scheduled[sched_count].runner_pid = msg.runner_pid;
                scheduled[sched_count].user_id = msg.user_id;
                scheduled[sched_count].command_id = msg.command_id;
                strncpy(scheduled[sched_count].command, msg.command, 256);
                scheduled[sched_count].start_time = msg.start_time;
                sched_count++;
            }
        }

        // CADEIA DE DECISÃO 2: Um comando terminou e libertou a vaga?
        else if (msg.msg_type == MSG_DONE) {
            // Pesquisar a posição do runner que terminou
            int idx = -1;
            for (int i = 0; i < exec_count; i++) {
                if (executing[i].runner_pid == msg.runner_pid) {
                    idx = i;
                    break;
                }
            }

            if (idx != -1) {
                // Calcular duração e registar no log
                struct timeval end_time;
                gettimeofday(&end_time, NULL);
                long duration = end_time.tv_sec - executing[idx].start_time.tv_sec;
                long microduration = end_time.tv_usec - executing[idx].start_time.tv_usec;
                double total_time = duration + microduration * 1e-6;

                int fd_log = open("controller.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
                if (fd_log != -1) {
                    char log_entry[512];
                    int log_len = sprintf(log_entry,
                        "user-id %d - command-id %d - duration %.2f seconds\n",
                        executing[idx].user_id, executing[idx].command_id, total_time);
                    write(fd_log, log_entry, log_len);
                    close(fd_log);
                }

                // Shift para a esquerda para remover o job terminado
                for (int i = idx; i < exec_count - 1; i++) {
                    executing[i] = executing[i+1];
                }
                exec_count--;

                // Se shutdown pedido e não há mais jobs, sair do loop
                if (shutdown_requested && exec_count == 0) {
                    break;
                }
            }

            // Promover o próximo da fila de espera (FCFS)
            if (sched_count > 0 && exec_count < parallel_limit) {
                Job next_job = scheduled[0];

                // Shift para a esquerda na fila scheduled
                for (int i = 0; i < sched_count - 1; i++) {
                    scheduled[i] = scheduled[i+1];
                }
                sched_count--;

                executing[exec_count] = next_job;
                exec_count++;

                authorize_runner(next_job.runner_pid);
            }
        }

        // CADEIA DE DECISÃO 3: Pedido de consulta de estado ("-c")
        else if (msg.msg_type == MSG_STATUS) {
            char out_buf[4096] = {0};
            char temp[256];

            // Formato conforme o enunciado: "---\nExecuting\n"
            strcat(out_buf, "---\nExecuting\n");
            for (int i = 0; i < exec_count; i++) {
                sprintf(temp, "user-id %d - command-id %d\n",
                        executing[i].user_id, executing[i].command_id);
                strcat(out_buf, temp);
            }
            strcat(out_buf, "---\nScheduled\n");
            for (int i = 0; i < sched_count; i++) {
                sprintf(temp, "user-id %d - command-id %d\n",
                        scheduled[i].user_id, scheduled[i].command_id);
                strcat(out_buf, temp);
            }

            char private_path[64];
            sprintf(private_path, "tmp/runner_%d_fifo", msg.runner_pid);
            int fd_private = open(private_path, O_WRONLY);
            if (fd_private != -1) {
                write(fd_private, out_buf, strlen(out_buf));
                close(fd_private);
            }
        }
    }

    close(fd_server);
    unlink(SERVER_FIFO);

    // Autorizar o runner que pediu o shutdown (confirmar saída)
    if (shutdown_requested && shutdown_runner_pid != 0) {
        authorize_runner(shutdown_runner_pid);
    }

    return 0;
}