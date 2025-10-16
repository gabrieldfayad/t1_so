#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

pid_t kernel_pid;

// Handler para o sinal que manda iniciar a contagem de E/S
void start_io_handler(int signum) {
    printf("InterController: Recebeu pedido de E/S. Contando 3 segundos...\n");
    
    // Fork para não bloquear o envio do timeslice
    if (fork() == 0) {
        sleep(3); // Operação de E/S leva 3 segundos
        printf("InterController: E/S concluída. Enviando IRQ1.\n");
        kill(kernel_pid, SIGALRM); // Envia IRQ1 (I/O Done)
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <PID_Kernel>\n", argv[0]);
        exit(1);
    }
    kernel_pid = atoi(argv[1]);

    printf("InterController (PID %d): Iniciado. Alvo: KernelSim (PID %d).\n", getpid(), kernel_pid);

    // Configura o handler para receber o pedido de início de E/S do KernelSim
    signal(SIGUSR2, start_io_handler);

    // Loop principal para enviar o sinal de timeslice (IRQ0) a cada 1s
    while (1) {
        sleep(1);
        // Envia SIGUSR1 para simular o fim do timeslice
        kill(kernel_pid, SIGUSR1);
    }
    return 0;
}
