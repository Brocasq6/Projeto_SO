#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "protocol.h"

/*
struct que representa um segmento de um pipeline 

por exemplo o comando "grep system /etc/passwd | wc -l > out.txt"
separa-se em dois segmentos

segmento 1 : " args=["grep","system","/etc/passwd"], stdin_file = NULL, stdout_file = NULL "
segmento 2 : " args=["wc","-l"], stdin_file = NULL, stdout_file = "out.txt" "
*/

typedef struct runner
{
    char *args[64];
    int argc;
    char *stdin_file;  // ficheiro para < (ou NULL)
    char *stdout_file; // ficheiro para > (ou NULL)
    char *stdder_file; // ficheiro para 2> (ou NULL)
} Segment;

/*
    funcao parse_command:
        Partir o comando em tokens respeitando espaços,
        depois agrupar em segmentos separados por '|'
        e detetar operadores >, 2>, <
*/
int parse_command(char *command, Segment *segments) {
    int num_segments = 0;
    Segment *current = &segments[0];

    // inicializar o primeiro segmento
    current->argc = 0;
    current->stdin_file = NULL;
    current->stdout_file = NULL;
    current->stdder_file = NULL;

    char *token = strtok(command, " ");
    while (token != NULL) {
        if (strcmp(token, "|") == 0) {
            // terminar args do segmento atual
            current->args[current->argc] = NULL;
            num_segments++;
            // inicializar próximo segmento
            current = &segments[num_segments];
            current->argc = 0;
            current->stdin_file = NULL;
            current->stdout_file = NULL;
            current->stdder_file = NULL;
        }
        else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, " ");
            if (token != NULL) current->stdout_file = token;
        }
        else if (strcmp(token, "2>") == 0) {
            token = strtok(NULL, " ");
            if (token != NULL) current->stdder_file = token;
        }
        else if (strcmp(token, "<") == 0) {
            token = strtok(NULL, " ");
            if (token != NULL) current->stdin_file = token;
        }
        else {
            // argumento normal do comando
            current->args[current->argc++] = token;
        }
        token = strtok(NULL, " ");
    }

    // terminar args do último segmento
    current->args[current->argc] = NULL;
    num_segments++;

    return num_segments;
}

void exec_pipeline(Segment *segments, int number_segments) {

    int pipes[64][2];

    for (int i = 0; i < number_segments - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("Erro ao criar o Pipe");
            exit(1);
        }
    }

    for (int i = 0; i < number_segments; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("Erro no fork da pipeline");
            exit(1);
        }

        if (pid == 0) {
            // PROCESSO FILHO

            // 1. Primeiro ligar os pipes
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            if (i < number_segments - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            // 2. Depois aplicar redirecionamentos de ficheiro (sobrepõem pipes)

            // BUG CORRIGIDO: era dup2(fd, STDOUT_FILENO)
            if (segments[i].stdin_file != NULL) {
                int fd = open(segments[i].stdin_file, O_RDONLY);
                if (fd == -1) { perror("Erro ao abrir ficheiro de entrada"); exit(1); }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            // BUG CORRIGIDO: estava a abrir stdder_file em vez de stdout_file
            if (segments[i].stdout_file != NULL) {
                int fd = open(segments[i].stdout_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1) { perror("Erro ao abrir ficheiro de saída"); exit(1); }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            // BUG CORRIGIDO: faltava ; no fim do open()
            if (segments[i].stdder_file != NULL) {
                int fd = open(segments[i].stdder_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (fd == -1) { perror("Erro ao abrir ficheiro de erros"); exit(1); }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            // Fechar todos os descritores de pipe no filho
            for (int j = 0; j < number_segments - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execvp(segments[i].args[0], segments[i].args);
            perror("Erro a executar o binário");
            exit(1);
        }
    }

    // PROCESSO PAI
    for (int i = 0; i < number_segments - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    for (int i = 0; i < number_segments; i++) {
        wait(NULL);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: ./runner [-e <id> \"cmd\" | -c | -s]\n");
        return 1;
    }

    char private_fifo[256];
    sprintf(private_fifo, "/tmp/runner_%d_fifo", getpid());
    if (mkfifo(private_fifo, 0666) == -1) {
        perror("Erro ao criar FIFO privado");
        return 1;
    }

    // Abrir o FIFO privado em O_RDWR ANTES de avisar o controller.
    // Assim, quando o controller chamar open(O_WRONLY) em authorize_runner,
    // já existe um leitor — o open não bloqueia e elimina a race condition.
    int fd_private = open(private_fifo, O_RDWR);
    if (fd_private == -1) {
        perror("Erro ao abrir FIFO privado (RDWR)");
        unlink(private_fifo);
        return 1;
    }

    Message msg;
    msg.runner_pid = getpid();

    if (strcmp(argv[1], "-e") == 0 && argc >= 4) {
        gettimeofday(&msg.start_time, NULL);
        msg.msg_type = MSG_EXECUTE;
        msg.user_id = atoi(argv[2]);
        msg.command_id = getpid();
        int offset = 0;
        for (int i = 3; i < argc; i++) {
            offset += snprintf(msg.command + offset, sizeof(msg.command) - offset, i == 3 ? "%s" : " %s", argv[i]);
        }
        write(STDOUT_FILENO, "[runner] command submitted\n", 27);
    }
    else if (strcmp(argv[1], "-c") == 0) {
        msg.msg_type = MSG_STATUS;
    }
    else if (strcmp(argv[1], "-s") == 0) {
        msg.msg_type = MSG_SHUTDOWN;
    }
    else {
        fprintf(stderr, "Erro nos argumentos.\n");
        close(fd_private);
        unlink(private_fifo);
        return 1;
    }

    int fd_server = open(SERVER_FIFO, O_WRONLY);
    if (fd_server == -1) {
        perror("Controller não encontrado");
        close(fd_private);
        unlink(private_fifo);
        return 1;
    }
    write(fd_server, &msg, sizeof(Message));
    close(fd_server);

    if (msg.msg_type == MSG_EXECUTE) {
        char buffer[10];
        read(fd_private, buffer, sizeof(buffer));

        char exec_msg[64];
        int len = sprintf(exec_msg, "[runner] executing command %d...\n", msg.command_id);
        write(STDOUT_FILENO, exec_msg, len);

        // Parse do comando e execução da pipeline
        Segment segments[64];
        int num_segments = parse_command(msg.command, segments);
        exec_pipeline(segments, num_segments);

        char fin_msg[64];
        int flen = sprintf(fin_msg, "[runner] command %d finished\n", msg.command_id);
        write(STDOUT_FILENO, fin_msg, flen);

        msg.msg_type = MSG_DONE;
        int final_fd = open(SERVER_FIFO, O_WRONLY);
        if (final_fd != -1) {
            write(final_fd, &msg, sizeof(Message));
            close(final_fd);
        }
    }
    else if (msg.msg_type == MSG_STATUS) {
        char status_buffer[4096];
        int bytes = read(fd_private, status_buffer, sizeof(status_buffer));
        write(STDOUT_FILENO, status_buffer, bytes);
    }
    else if (msg.msg_type == MSG_SHUTDOWN) {
        write(STDOUT_FILENO, "[runner] sent shutdown notification\n", 36);
        write(STDOUT_FILENO, "[runner] waiting for controller to shutdown...\n", 47);
        char buf[4];
        read(fd_private, buf, sizeof(buf));
        write(STDOUT_FILENO, "[runner] controller exited.\n", 28);
    }

    close(fd_private);
    unlink(private_fifo);
    return 0;
}