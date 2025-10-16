#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>

// Para alterar o número de APPs (3 a 6), alterar esse #define abaixo
#define NUM_APPS 6

typedef enum { READY, RUNNING, BLOCKED, TERMINATED } ProcessState;

typedef struct {
    pid_t pid;
    ProcessState state;
    int pc;
    int pipe_fd[2];
} PCB;

PCB process_table[NUM_APPS];
pid_t inter_controller_pid;

int ready_queue[NUM_APPS];
int ready_head = 0, ready_tail = 0;
int ready_queue_count = 0;

int blocked_queue[NUM_APPS];
int blocked_head = 0, blocked_tail = 0;
int blocked_queue_count = 0;

int running_app_index = -1;

void scheduler();
void check_all_terminated();

// --- Funções de Fila ---
void enqueue_ready(int app_index) {
    if (ready_queue_count < NUM_APPS) {
        ready_queue[ready_tail] = app_index;
        ready_tail = (ready_tail + 1) % NUM_APPS;
        ready_queue_count++;
    }
}
int dequeue_ready() {
    if (ready_queue_count > 0) {
        int app_index = ready_queue[ready_head];
        ready_head = (ready_head + 1) % NUM_APPS;
        ready_queue_count--;
        return app_index;
    }
    return -1;
}
void enqueue_blocked(int app_index) {
    if (blocked_queue_count < NUM_APPS) {
        blocked_queue[blocked_tail] = app_index;
        blocked_tail = (blocked_tail + 1) % NUM_APPS;
        blocked_queue_count++;
    }
}
int dequeue_blocked() {
    if (blocked_queue_count > 0) {
        int app_index = blocked_queue[blocked_head];
        blocked_head = (blocked_head + 1) % NUM_APPS;
        blocked_queue_count--;
        return app_index;
    }
    return -1;
}

// --- Handlers de Sinais ---

void handle_child_exit(int signum) {
    int status;
    pid_t pid;

    // Loop para coletar todos os filhos que possam ter terminado
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < NUM_APPS; i++) {
            if (process_table[i].pid == pid) {
                printf("\nKERNEL: APP %d (PID %d) terminou. Removendo do escalonamento.\n", i + 1, pid);
                process_table[i].state = TERMINATED;

                // Se o processo que terminou era o que estava rodando, chama o escalonador
                if (running_app_index == i) {
                    running_app_index = -1;
                    scheduler();
                }
                break;
            }
        }
    }
    check_all_terminated();
}


void handle_irq0(int signum) {
    if (running_app_index == -1) return;
    
    printf("\nKERNEL: IRQ0 (Timeslice)! Parando APP %d (PID %d).\n", running_app_index + 1, process_table[running_app_index].pid);
    kill(process_table[running_app_index].pid, SIGSTOP);
    
    process_table[running_app_index].state = READY;
    enqueue_ready(running_app_index);
    running_app_index = -1;
    
    scheduler();
}

void handle_syscall(int signum) {
    if (running_app_index == -1) return;
    
    int app_idx = running_app_index;
    PCB* pcb = &process_table[app_idx];
    
    read(pcb->pipe_fd[0], &pcb->pc, sizeof(pcb->pc));

    printf("\nKERNEL: SYSCALL da APP %d (PID %d) no PC=%d. Bloqueando.\n", app_idx + 1, pcb->pid, pcb->pc);
    kill(pcb->pid, SIGSTOP);

    pcb->state = BLOCKED;
    enqueue_blocked(app_idx);
    running_app_index = -1;

    if (blocked_queue_count == 1) {
        printf("KERNEL: Dispositivo de E/S estava livre. Acionando InterController.\n");
        kill(inter_controller_pid, SIGUSR2);
    }
    
    scheduler();
}

void handle_irq1(int signum) {
    if (blocked_queue_count == 0) return;

    int app_idx = dequeue_blocked();
    //Verifica se o processo não terminou enquanto estava bloqueado
    if (process_table[app_idx].state != TERMINATED) {
        process_table[app_idx].state = READY;
        enqueue_ready(app_idx);
        printf("\nKERNEL: IRQ1 (E/S Concluída) para APP %d. Movida para Prontos.\n", app_idx + 1);
    }
    
    if (blocked_queue_count > 0) {
        printf("KERNEL: Acionando InterController para o próximo da fila de E/S.\n");
        kill(inter_controller_pid, SIGUSR2);
    }

    scheduler();
}

// --- Escalonador ---
void scheduler() {
    if (running_app_index != -1 || ready_queue_count == 0) {
        return; 
    }

    int dequeued_index = dequeue_ready();

    // Se o processo retirado da fila já terminou, tenta o próximo
    if (process_table[dequeued_index].state == TERMINATED) {
        scheduler(); // Chama o escalonador recursivamente para pegar o próximo
        return;
    }

    running_app_index = dequeued_index;
    process_table[running_app_index].state = RUNNING;
    
    printf("KERNEL: Escalonador -> Executando APP %d (PID %d).\n\n", running_app_index + 1, process_table[running_app_index].pid);
    kill(process_table[running_app_index].pid, SIGCONT);
}

// Encerrar a simulação
void check_all_terminated() {
    int terminated_count = 0;
    for (int i = 0; i < NUM_APPS; i++) {
        if (process_table[i].state == TERMINATED) {
            terminated_count++;
        }
    }
    if (terminated_count == NUM_APPS) {
        printf("\nKERNEL: Todos os processos de aplicação terminaram. Encerrando simulação.\n");
        // Mata o processo InterController para limpar tudo
        kill(inter_controller_pid, SIGKILL);
        exit(0);
    }
}


// --- Função Principal ---
int main() {
    printf("KERNEL (PID %d): Iniciando sistema...\n", getpid());
    
    signal(SIGUSR1, handle_irq0);
    signal(SIGUSR2, handle_syscall);
    signal(SIGALRM, handle_irq1);
    signal(SIGCHLD, handle_child_exit);

    inter_controller_pid = fork();
    if (inter_controller_pid == 0) {
        char kernel_pid_str[10];
        sprintf(kernel_pid_str, "%d", getppid());
        execlp("./inter_controller", "inter_controller", kernel_pid_str, NULL);
        perror("execlp controller"); exit(1);
    }

    for (int i = 0; i < NUM_APPS; i++) {
        pipe(process_table[i].pipe_fd);
        process_table[i].pid = fork();
        
        if (process_table[i].pid == 0) {
            close(process_table[i].pipe_fd[0]);
            char id_str[3], kernel_pid_str[10], pipe_str[10];
            sprintf(id_str, "%d", i + 1);
            sprintf(kernel_pid_str, "%d", getppid());
            sprintf(pipe_str, "%d", process_table[i].pipe_fd[1]);
            execlp("./application", "application", id_str, kernel_pid_str, pipe_str, NULL);
            perror("execlp application"); exit(1);
        } else {
            close(process_table[i].pipe_fd[1]);
            kill(process_table[i].pid, SIGSTOP);
            process_table[i].state = READY;
            enqueue_ready(i);
            printf("KERNEL: Criou APP %d (PID %d) e colocou na fila de prontos.\n", i + 1, process_table[i].pid);
        }
    }
    
    sleep(1);
    printf("\nKERNEL: Iniciando escalonamento.\n");
    scheduler();

    while(1) {
        pause();
    }

    return 0;
}
