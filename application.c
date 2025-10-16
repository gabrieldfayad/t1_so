#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define MAX_ITERACOES 10

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr, "Uso: %s <ID> <PID_Kernel> <Pipe_FD> <Tempo_Syscall>\n", argv[0]);
        exit(1);
    }

    int id_app = atoi(argv[1]);
    pid_t pid_kernel = atoi(argv[2]);
    int fd_pipe = atoi(argv[3]);
    int tempo_syscall = atoi(argv[4]); 

    int pc = 0; // Program Counter

    printf("APP %d (PID %d): Iniciou. Syscall agendada para PC=%d (0=nenhuma).\n", id_app, getpid(), tempo_syscall);

    for (pc = 1; pc <= MAX_ITERACOES; pc++) {
        printf("APP %d: Executando, PC = %d\n", id_app, pc);

        if (tempo_syscall > 0 && pc == tempo_syscall) {
            printf("APP %d: Fazendo SYSCALL para E/S no PC = %d\n", id_app, pc);
            
            if (write(fd_pipe, &pc, sizeof(pc)) == -1) {
                perror("APP: Erro ao escrever no pipe");
            }
            
            kill(pid_kernel, SIGUSR2);
        }
        
        sleep(1);
    }

    printf("APP %d (PID %d): Terminou.\n", id_app, getpid());
    close(fd_pipe);
    return 0;
}
