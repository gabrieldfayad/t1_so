#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

pid_t pid_kernel;

// Handler para o sinal que manda iniciar a contagem de E/S
void handler_iniciar_es(int signum) {
    printf("InterController: Recebeu pedido de E/S. Contando 3 segundos...\n");
    
    if (fork() == 0) {
        sleep(3); // Operação de E/S leva 3 segundos
        printf("InterController: E/S concluída. Enviando IRQ1.\n");
        kill(pid_kernel, SIGALRM); // Envia IRQ1
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <PID_Kernel>\n", argv[0]);
        exit(1);
    }
    pid_kernel = atoi(argv[1]);

    printf("InterController (PID %d): Iniciado. Alvo: KernelSim (PID %d).\n", getpid(), pid_kernel);

    signal(SIGUSR2, handler_iniciar_es);

    // enviar o sinal de timeslice (IRQ0) a cada 1s
    while (1) {
        sleep(1);
        kill(pid_kernel, SIGUSR1);
    }
    return 0;
}
