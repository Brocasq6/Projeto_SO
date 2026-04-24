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
    sprintf(private_fifo, "/tmp/runner_%d_fifo", getpid());
    if (mkfifo(private_fifo, 0666) == -1) {
        perror("Erro ao criar FIFO privado");
        return 1;
    }

    // 2. Preencher a mensagem conforme a flag
    Message msg;
    msg.runner_pid = getpid();
    
    if (strcmp(argv[1], "-e") == 0 && argc >= 4) {
        gettimeofday(&msg.start_time, NULL); // Marca o tempo de início do comando
        msg.msg_type = MSG_EXECUTE;
        msg.user_id = atoi(argv[2]);
        msg.command_id = getpid();
        int offset = 0;
        for (int i = 3; i < argc; i++) {
            offset += snprintf(msg.command + offset, sizeof(msg.command) - offset,
                                i == 3 ? "%s" : " %s", argv[i]);
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
        return 1;
    }

    // 5. Lógica de resposta diferente para cada tipo
    if (msg.msg_type == MSG_EXECUTE) {
        char buffer[10];
        read(fd_private, buffer, sizeof(buffer)); // Espera o "OK" do controller para poder executar o comando
        write(STDOUT_FILENO, "[runner] executing command...\n", 30);
        
        pid_t pid = fork();
        if (pid == -1) {
            perror("Erro no fork");
        } 
        else if (pid == 0) {
            // PROCESSO FILHO - (Pega no comando exemplo: "sleep 10" e parte pelos espaços)
            char *args[64];
            int i = 0;
            char *token = strtok(msg.command, " "); // Divide a string em tokens(corta no primeiro espaço)
            while (token != NULL && i < 63) {
                args[i] = token; //guardamos os tokens(palavras da string) em posições diferentes do array args
                i++;
                token = strtok(NULL, " "); // Continua a dividir a string em tokens(corta no segundo espaço, terceiro, etc)
            }
            args[i] = NULL;
            
            // O execvp substitui completamente o código/memória deste Processo Filho pela do programa apontado em args[0] (ex: "sleep").
            // Passamos o array "args" inteiro para o programa novo saber como operar.
            execvp(args[0], args);
            perror("Erro a executar o binário"); // Só ocorre erro se não existir o programa
            exit(1);
        } 
        else {
            // PROCESSO PAI (Runner) - Espera que termine
            wait(NULL);
            write(STDOUT_FILENO, "[runner] command finished\n", 26);
            
            // Avisa o Controller do término para libertar "vaga"
            msg.msg_type = MSG_DONE;
            int final_fd = open(SERVER_FIFO, O_WRONLY);
            if (final_fd != -1) {
                write(final_fd, &msg, sizeof(Message));
                close(final_fd);
            }
        }
    } 
    else if (msg.msg_type == MSG_STATUS) {
        char status_buffer[4096]; // Buffer maior para a lista
        int bytes = read(fd_private, status_buffer, sizeof(status_buffer));
        write(STDOUT_FILENO, status_buffer, bytes);
    }
    else if (msg.msg_type == MSG_SHUTDOWN) {
        write(STDOUT_FILENO, "[runner] controller exited.\n", 28);
    }

    close(fd_private);
    unlink(private_fifo);
    return 0;
}