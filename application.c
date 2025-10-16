#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define MAX_ITERATIONS 10

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <ID> <PID_Kernel> <Pipe_FD>\n", argv[0]);
        exit(1);
    }

    int app_id = atoi(argv[1]);
    pid_t kernel_pid = atoi(argv[2]);
    int pipe_fd = atoi(argv[3]);
    
    // Define um tempo diferente para a syscall de cada app
    // Para fazer outros testes, alterar os valores de syscall_time
    int syscall_time = -1;
    if (app_id == 1) syscall_time = 3;
    if (app_id == 2) syscall_time = 5;
    // if (app_id == 4) syscall_time = 7;
    
    int pc = 0; // Program Counter

    printf("APP %d (PID %d): Iniciou.\n", app_id, getpid());

    for (pc = 1; pc <= MAX_ITERATIONS; pc++) {
        printf("APP %d: Executando, PC = %d\n", app_id, pc);

        // Verifica se é hora de fazer uma chamada de sistema
        if (pc == syscall_time) {
            printf("APP %d: Fazendo SYSCALL para E/S no PC = %d\n", app_id, pc);
            
            // Escreve o contexto (o PC) no pipe para o KernelSim ler
            if (write(pipe_fd, &pc, sizeof(pc)) == -1) {
                perror("APP: Erro ao escrever no pipe");
            }
            
            // Notifica o KernelSim sobre a syscall via sinal SIGUSR2
            kill(kernel_pid, SIGUSR2);
        }
        
        sleep(1);
    }

    printf("APP %d (PID %d): Terminou.\n", app_id, getpid());
    close(pipe_fd);
    return 0;
}
