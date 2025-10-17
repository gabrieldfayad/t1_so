#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>

//O número de aplicações vai ser passado na linha de comando
int num_aplicacoes;

// Estados de um processo
typedef enum { PRONTO, EXECUTANDO, BLOQUEADO, TERMINADO } EstadoProcesso;

typedef struct {
    pid_t pid;
    EstadoProcesso estado;
    int pc;
    int pipe_fd[2];
} BCP;

BCP* tabela_processos;
int* fila_prontos;
int* fila_bloqueados;

pid_t pid_controlador_interrupcao;

int cabeca_fila_prontos = 0, cauda_fila_prontos = 0;
int cont_fila_prontos = 0;

int cabeca_fila_bloqueados = 0, cauda_fila_bloqueados = 0;
int cont_fila_bloqueados = 0;

int indice_app_executando = -1;

void escalonador();
void verificar_termino_total();

// --- Funções de Fila ---
void enfileirar_pronto(int indice_app) {
    if (cont_fila_prontos < num_aplicacoes) {
        fila_prontos[cauda_fila_prontos] = indice_app;
        cauda_fila_prontos = (cauda_fila_prontos + 1) % num_aplicacoes;
        cont_fila_prontos++;
    }
}
int desenfileirar_pronto() {
    if (cont_fila_prontos > 0) {
        int indice_app = fila_prontos[cabeca_fila_prontos];
        cabeca_fila_prontos = (cabeca_fila_prontos + 1) % num_aplicacoes;
        cont_fila_prontos--;
        return indice_app;
    }
    return -1;
}
void enfileirar_bloqueado(int indice_app) {
    if (cont_fila_bloqueados < num_aplicacoes) {
        fila_bloqueados[cauda_fila_bloqueados] = indice_app;
        cauda_fila_bloqueados = (cauda_fila_bloqueados + 1) % num_aplicacoes;
        cont_fila_bloqueados++;
    }
}
int desenfileirar_bloqueado() {
    if (cont_fila_bloqueados > 0) {
        int indice_app = fila_bloqueados[cabeca_fila_bloqueados];
        cabeca_fila_bloqueados = (cabeca_fila_bloqueados + 1) % num_aplicacoes;
        cont_fila_bloqueados--;
        return indice_app;
    }
    return -1;
}

// --- Handlers de Sinais ---

void handler_termino_filho(int signum) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < num_aplicacoes; i++) {
            if (tabela_processos[i].pid == pid) {
                printf("\nKERNEL: APP %d (PID %d) terminou. Removendo do escalonamento.\n", i + 1, pid);
                tabela_processos[i].estado = TERMINADO;
                if (indice_app_executando == i) {
                    indice_app_executando = -1;
                    escalonador();
                }
                break;
            }
        }
    }
    verificar_termino_total();
}

void handler_irq0(int signum) {
    if (indice_app_executando == -1) return;
    printf("\nKERNEL: IRQ0 (Timeslice)! Parando APP %d (PID %d).\n", indice_app_executando + 1, tabela_processos[indice_app_executando].pid);
    kill(tabela_processos[indice_app_executando].pid, SIGSTOP);
    tabela_processos[indice_app_executando].estado = PRONTO;
    enfileirar_pronto(indice_app_executando);
    indice_app_executando = -1;
    escalonador();
}

void handler_syscall(int signum) {
    if (indice_app_executando == -1) return;
    int indice_app = indice_app_executando;
    BCP* bcp = &tabela_processos[indice_app];
    read(bcp->pipe_fd[0], &bcp->pc, sizeof(bcp->pc));
    printf("\nKERNEL: SYSCALL da APP %d (PID %d) no PC=%d. Bloqueando.\n", indice_app + 1, bcp->pid, bcp->pc);
    kill(bcp->pid, SIGSTOP);
    bcp->estado = BLOQUEADO;
    enfileirar_bloqueado(indice_app);
    indice_app_executando = -1;
    if (cont_fila_bloqueados == 1) {
        printf("KERNEL: Dispositivo de E/S estava livre. Acionando InterController.\n");
        kill(pid_controlador_interrupcao, SIGUSR2);
    }
    escalonador();
}

void handler_irq1(int signum) {
    if (cont_fila_bloqueados == 0) return;
    int indice_app = desenfileirar_bloqueado();
    if (tabela_processos[indice_app].estado != TERMINADO) {
        tabela_processos[indice_app].estado = PRONTO;
        enfileirar_pronto(indice_app);
        printf("\nKERNEL: IRQ1 (E/S Concluída) para APP %d. Movida para Prontos.\n", indice_app + 1);
    }
    if (cont_fila_bloqueados > 0) {
        printf("KERNEL: Acionando InterController para o próximo da fila de E/S.\n");
        kill(pid_controlador_interrupcao, SIGUSR2);
    }
    escalonador();
}

void escalonador() {
    if (indice_app_executando != -1 || cont_fila_prontos == 0) return;
    int indice_desenfileirado = desenfileirar_pronto();
    if (tabela_processos[indice_desenfileirado].estado == TERMINADO) {
        escalonador();
        return;
    }
    indice_app_executando = indice_desenfileirado;
    tabela_processos[indice_app_executando].estado = EXECUTANDO;
    printf("KERNEL: Escalonador -> Executando APP %d (PID %d).\n\n", indice_app_executando + 1, tabela_processos[indice_app_executando].pid);
    kill(tabela_processos[indice_app_executando].pid, SIGCONT);
}

void verificar_termino_total() {
    int cont_terminados = 0;
    for (int i = 0; i < num_aplicacoes; i++) {
        if (tabela_processos[i].estado == TERMINADO) cont_terminados++;
    }
    if (cont_terminados == num_aplicacoes) {
        printf("\nKERNEL: Todos os processos de aplicação terminaram. Encerrando simulação.\n");
        kill(pid_controlador_interrupcao, SIGKILL);
        // <<-- MUDANÇA: Liberando memória antes de sair -->>
        free(tabela_processos);
        free(fila_prontos);
        free(fila_bloqueados);
        exit(0);
    }
}

// --- Função Principal ---
int main(int argc, char *argv[]) {
    // Validação dos argumentos
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <num_apps> <tempo_syscall_app1> <tempo_syscall_app2> ...\n", argv[0]);
        fprintf(stderr, "Exemplo: %s 3 2 0 5  (3 apps, syscalls em PC=2, nenhuma, PC=5)\n", argv[0]);
        exit(1);
    }

    num_aplicacoes = atoi(argv[1]);

    if (num_aplicacoes <= 0 || argc != num_aplicacoes + 2) {
        fprintf(stderr, "Erro: Número de argumentos inválido para %d aplicações.\n", num_aplicacoes);
        fprintf(stderr, "Uso: %s <num_apps> <tempo_syscall_app1> <tempo_syscall_app2> ...\n", argv[0]);
        exit(1);
    }

    // Alocação dinâmica da memória
    tabela_processos = (BCP*) malloc(num_aplicacoes * sizeof(BCP));
    fila_prontos = (int*) malloc(num_aplicacoes * sizeof(int));
    fila_bloqueados = (int*) malloc(num_aplicacoes * sizeof(int));

    if (tabela_processos == NULL || fila_prontos == NULL || fila_bloqueados == NULL) {
        perror("Falha ao alocar memória");
        exit(1);
    }
    
    printf("KERNEL (PID %d): Iniciando sistema para %d aplicações...\n", getpid(), num_aplicacoes);
    
    signal(SIGUSR1, handler_irq0);
    signal(SIGUSR2, handler_syscall);
    signal(SIGALRM, handler_irq1);
    signal(SIGCHLD, handler_termino_filho);

    pid_controlador_interrupcao = fork();
    if (pid_controlador_interrupcao == 0) {
        char pid_kernel_str[10];
        sprintf(pid_kernel_str, "%d", getppid());
        execlp("./inter_controller", "inter_controller", pid_kernel_str, NULL);
        perror("execlp controller"); exit(1);
    }

    for (int i = 0; i < num_aplicacoes; i++) {
        pipe(tabela_processos[i].pipe_fd);
        tabela_processos[i].pid = fork();
        
        if (tabela_processos[i].pid == 0) {
            close(tabela_processos[i].pipe_fd[0]);
            
            char id_str[3], pid_kernel_str[10], pipe_str[10];
            // O tempo de syscall vem de argv[i + 2]
            char* tempo_syscall_str = argv[i + 2];
            
            sprintf(id_str, "%d", i + 1);
            sprintf(pid_kernel_str, "%d", getppid());
            sprintf(pipe_str, "%d", tabela_processos[i].pipe_fd[1]);

            execlp("./application", "application", id_str, pid_kernel_str, pipe_str, tempo_syscall_str, NULL);
            
            perror("execlp application"); exit(1);
        } else {
            close(tabela_processos[i].pipe_fd[1]);
            kill(tabela_processos[i].pid, SIGSTOP);
            tabela_processos[i].estado = PRONTO;
            enfileirar_pronto(i);
            printf("KERNEL: Criou APP %d (PID %d) e colocou na fila de prontos.\n", i + 1, tabela_processos[i].pid);
        }
    }
    
    sleep(1);
    printf("\nKERNEL: Iniciando escalonamento.\n");
    escalonador();

    while(1) {
        pause();
    }

    return 0; // Se der certo, nunca chega aqui
}