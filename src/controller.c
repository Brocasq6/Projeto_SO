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
Job executing[MAX_JOBS]; //fila de quem está a excutar
Job scheduled[MAX_JOBS]; //fila de espera

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
        write(fd_private, "OK", 2); // Envia o sinal sonhado!
        close(fd_private);
    }
}

int main(int argc, char *argv[]) {
    int shutdown_requested = 0;
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

    // O_RDWR é crucial aqui: evita que o file descriptor feche se os runners acabarem momentaneamente
    int fd_server = open(SERVER_FIFO, O_RDWR);
    
    Message msg;
    // LOOP PRINCIPAL (Orquestrador)
    while (!shutdown_requested || exec_count > 0) {
        if (read(fd_server, &msg, sizeof(Message))<=0) {
            if(shutdown_requested && exec_count == 0) {
                break; // Se pediu shutdown e não há mais comandos a executar, sai do loop
            }
            continue;

        }

        if (msg.msg_type == MSG_SHUTDOWN) {
            shutdown_requested = 1; 
            // Avisamos o runner que enviou o -s que recebemos o pedido
            authorize_runner(msg.runner_pid); 
        }
        // CADEIA DE DECISÃO 1: Foi pedido um novo comando?
        if (msg.msg_type == MSG_EXECUTE) {
            
            if (shutdown_requested) {
                continue; 
            }

            // A) Há vagas na Lista de Execução Simultânea?
            if (exec_count < parallel_limit) {
                
                // Passa com distinção! Vai diretamente para execução
                executing[exec_count].runner_pid = msg.runner_pid;
                executing[exec_count].user_id = msg.user_id;
                executing[exec_count].command_id = msg.command_id;
                strncpy(executing[exec_count].command, msg.command, 256);
                gettimeofday(&executing[exec_count].start_time, NULL);
                exec_count++;
                
                // Emite LUZ VERDE para a resposta daquele RUNNER individual!
                authorize_runner(msg.runner_pid);
            } 
            else {
                // B) Está lotado! Vai para o agendamento de Fila de Espera (Scheduled)
                scheduled[sched_count].runner_pid = msg.runner_pid;
                scheduled[sched_count].user_id = msg.user_id;
                scheduled[sched_count].command_id = msg.command_id;
                strncpy(scheduled[sched_count].command, msg.command, 256);
                sched_count++;
            }
        } 
        
        // CADEIA DE DECISÃO 2: Um comando terminou e libertou a vaga?
        else if (msg.msg_type == MSG_DONE) {
            // Pesquisar em que posíção estava alocado
            int idx = -1;
            for (int i = 0; i < exec_count; i++) {
                if (executing[i].runner_pid == msg.runner_pid) {
                    idx = i;
                    break;
                }
            }
            
            // Eliminar da lista de Execução em tempo real
            if (idx != -1) {

                struct timeval end_time;
                gettimeofday(&end_time, NULL);
                long duration = end_time.tv_sec - executing[idx].start_time.tv_sec;
                long microduration = end_time.tv_usec - executing[idx].start_time.tv_usec;
                double total_time = duration + microduration * 1e-6;

                int fd_log = open("controller.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
                if (fd_log !=-1){
                    char log_entry[512];
                    int log_len = sprintf(log_entry, "user-id %d - command-id %d - duration %.2f seconds\n", executing[idx].user_id, executing[idx].command_id, total_time);
                    if(write(fd_log, log_entry, log_len) == -1) {
                        perror("Erro ao escrever no log");
                    }
                    close(fd_log);
                }

                // Dá Shifts nos outros para a Esquerda
                for (int i = idx; i < exec_count - 1; i++) {
                    executing[i] = executing[i+1];
                }
                exec_count--; //Diminui o número de comandos em execução(Abre vaga)
            }

            // MAGIA DAS FILAS DE ESPERA: Vamos puxar o da lista Scheduled?
            if (sched_count > 0 && exec_count < parallel_limit) {
                // Agarra o primeiro da lista de espera (Política FCFS)
                Job next_job = scheduled[0];
                
                // Apaga-o da fila de espera (shift)
                for (int i = 0; i < sched_count - 1; i++) {
                    scheduled[i] = scheduled[i+1];
                }
                sched_count--;

                // Transfere a responsabilidade para os que estão em "vias de Execução"
                executing[exec_count] = next_job;
                gettimeofday(&executing[exec_count].start_time, NULL);
                exec_count++;
                
                // Emite a LUZ VERDE que o coitado do runner novo esteve sempre à espera!
                authorize_runner(next_job.runner_pid);
            }
        }
        
        // CADEIA DE DECISÃO 3: Alguém utilizou o pedido "-c" ? (Status)
        else if (msg.msg_type == MSG_STATUS) {
            char out_buf[4096] = {0};
            char temp[256];
            strcat(out_buf, "--Executing\n");
            for(int i = 0; i < exec_count; i++) {
                sprintf(temp, "user-id %d - command-id %d\n", executing[i].user_id, executing[i].command_id);
                strcat(out_buf, temp);
            }
            strcat(out_buf, "--Scheduled\n");
            for(int i = 0; i < sched_count; i++) {
                sprintf(temp, "user-id %d - command-id %d\n", scheduled[i].user_id, scheduled[i].command_id);
                strcat(out_buf, temp);
            }

            // Responde os arrays todos concatenados na string final, para a FIFO local de quem interregou
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
    return 0;
}