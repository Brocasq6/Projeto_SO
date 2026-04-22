#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "protocol.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: ./runner [-e <id> \"cmd\" | -c | -s]\n");
        return 1;
    }

    // 1. Criar nome e ficheiro da FIFO privada
    char private_fifo[256];
    sprintf(private_fifo, "tmp/runner_%d_fifo", getpid());
    if (mkfifo(private_fifo, 0666) == -1) {
        perror("Erro ao criar FIFO privado");
        return 1;
    }

    // 2. Preencher a mensagem conforme a flag
    Message msg;
    msg.runner_pid = getpid();

    // Usar o PID como seed para garantir IDs únicos entre runners
    srand(getpid());

    if (strcmp(argv[1], "-e") == 0 && argc == 4) {
        gettimeofday(&msg.start_time, NULL);
        msg.msg_type = MSG_EXECUTE;
        msg.user_id = atoi(argv[2]);
        msg.command_id = rand() % 100000;
        strncpy(msg.command, argv[3], 256);

        // Notificar utilizador: comando submetido
        char submitted_msg[64];
        int len = sprintf(submitted_msg, "[runner] command %d submitted\n", msg.command_id);
        write(STDOUT_FILENO, submitted_msg, len);
    }
    else if (strcmp(argv[1], "-c") == 0) {
        msg.msg_type = MSG_STATUS;
    }
    else if (strcmp(argv[1], "-s") == 0) {
        msg.msg_type = MSG_SHUTDOWN;
    }
    else {
        fprintf(stderr, "Erro nos argumentos.\n");
        unlink(private_fifo);
        return 1;
    }

    // 3. Enviar para o Controller
    int fd_server = open(SERVER_FIFO, O_WRONLY);
    if (fd_server == -1) {
        perror("Controller não encontrado");
        unlink(private_fifo);
        return 1;
    }
    write(fd_server, &msg, sizeof(Message));
    close(fd_server);

    // 4. Abrir FIFO privada para ler a resposta
    int fd_private = open(private_fifo, O_RDONLY);
    if (fd_private == -1) {
        perror("Erro ao abrir FIFO privado para leitura");
        unlink(private_fifo);
        return 1;
    }

    // 5. Lógica de resposta diferente para cada tipo
    if (msg.msg_type == MSG_EXECUTE) {
        char buffer[10];
        read(fd_private, buffer, sizeof(buffer)); // Espera o "OK" do controller

        // Notificar utilizador: comando em execução
        char exec_msg[64];
        int len = sprintf(exec_msg, "[runner] executing command %d...\n", msg.command_id);
        write(STDOUT_FILENO, exec_msg, len);

        pid_t pid = fork();
        if (pid == -1) {
            perror("Erro no fork");
        }
        else if (pid == 0) {
            // PROCESSO FILHO — divide o comando pelos espaços e executa
            char *args[64];
            int i = 0;
            char *token = strtok(msg.command, " ");
            while (token != NULL && i < 63) {
                args[i++] = token;
                token = strtok(NULL, " ");
            }
            args[i] = NULL;

            execvp(args[0], args);
            perror("Erro a executar o binário");
            exit(1);
        }
        else {
            // PROCESSO PAI — espera que o filho termine
            wait(NULL);

            // Notificar utilizador: comando terminado
            char fin_msg[64];
            int flen = sprintf(fin_msg, "[runner] command %d finished\n", msg.command_id);
            write(STDOUT_FILENO, fin_msg, flen);

            // Avisar o controller que a vaga ficou livre
            msg.msg_type = MSG_DONE;
            int final_fd = open(SERVER_FIFO, O_WRONLY);
            if (final_fd != -1) {
                write(final_fd, &msg, sizeof(Message));
                close(final_fd);
            }
        }
    }
    else if (msg.msg_type == MSG_STATUS) {
        char status_buffer[4096];
        int bytes = read(fd_private, status_buffer, sizeof(status_buffer));
        write(STDOUT_FILENO, status_buffer, bytes);
    }
    else if (msg.msg_type == MSG_SHUTDOWN) {
        // Três mensagens obrigatórias conforme o enunciado
        write(STDOUT_FILENO, "[runner] sent shutdown notification\n", 36);
        write(STDOUT_FILENO, "[runner] waiting for controller to shutdown...\n", 47);
        // Lê da FIFO privada — bloqueia até o controller confirmar a saída
        char buf[4];
        read(fd_private, buf, sizeof(buf));
        write(STDOUT_FILENO, "[runner] controller exited.\n", 28);
    }

    close(fd_private);
    unlink(private_fifo);
    return 0;
}