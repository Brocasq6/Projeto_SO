#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "protocol.h"

int main() {
    // 1. Criar a FIFO pública (A "estrada" principal) [cite: 17, 99]
    unlink(SERVER_FIFO); 
    if (mkfifo(SERVER_FIFO, 0666) == -1) {
        perror("Erro mkfifo controller");
        return 1;
    }

    // 2. Abrir para ler os pedidos dos runners [cite: 17, 25]
    int fd_server = open(SERVER_FIFO, O_RDONLY);
    
    Message msg;
    // 3. Ficar em loop a receber pedidos [cite: 6, 51]
    while (read(fd_server, &msg, sizeof(Message)) > 0) {
        printf("[controller] Recebi pedido do PID %d\n", msg.runner_pid);

        // 4. DAR AUTORIZAÇÃO (O que o teu runner espera) [cite: 12, 15]
        char private_path[64];
        sprintf(private_path, "tmp/runner_%d", msg.runner_pid);
        
        int fd_private = open(private_path, O_WRONLY);
        if (fd_private != -1) {
            write(fd_private, "OK", 2); // Envia a "luz verde" [cite: 12, 94]
            close(fd_private);
            printf("[controller] Autorizei o PID %d\n", msg.runner_pid);
        }
    }

    close(fd_server);
    return 0;
}