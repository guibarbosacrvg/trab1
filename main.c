#include <stdio.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <string.h>

#define MAX_CMD_LENGTH 100
#define MAX_PROCESSES 10

typedef struct {
    char name[MAX_CMD_LENGTH];
    int start_time;
    int duration;
    pid_t pid;
} Process;

int lastChangeTime = -1;

void initializeEmptyProcess(Process *process) {
    memset(process->name, 0, sizeof(process->name)); // Clear the name
    process->start_time = -1;
    process->duration = -1;
    process->pid = -1;
}

int isRealTime(Process process) {
    return process.start_time != -1 && process.duration != -1;
}

int setCurrentProcess(Process *currentProcess, Process newProcess, long int currentTime) {
    if (lastChangeTime == currentTime || newProcess.pid == currentProcess->pid || newProcess.pid == -1) {
        return 0;
    }

    int hour = ((currentTime % 86400) / 3600 + 21) % 24; // hora em UTC-3
    int minute = (currentTime % 3600) / 60;
    int second = currentTime % 60;
    printf("[%02d:%02d:%02d] ", hour, minute, second);
    if (isRealTime(newProcess)) {
        printf("RT ");
    } else {
        printf("RR ");
    }

    if (currentProcess->pid != -1) {
        kill(currentProcess->pid, SIGSTOP);
        printf("%s -> ", currentProcess->name);
    }
    
    lastChangeTime = currentTime;
    *currentProcess = newProcess;
    kill(newProcess.pid, SIGCONT);

    printf("%s\n", newProcess.name);
    return 1;
}

void createProcess(Process *process) {
    pid_t pid = fork();
    if (pid == 0) {
        char *args[] = { process->name, NULL };
        execv(process->name, args);
    }  else {
        kill(pid, SIGSTOP);
        process->pid = pid;
    }
}

void parseCommand(char *line, Process *cmd) {
    char *token = strtok(line, " ");
    strcpy(cmd->name, strtok(NULL, " "));
    size_t len = strlen(cmd->name);
    if (len > 0 && cmd->name[len - 1] == '\n') {
        cmd->name[len - 1] = '\0';
    }
    cmd->start_time = -1;
    cmd->duration = -1;
    char *parameter;
    while ((parameter = strtok(NULL, " ")) != NULL) {
        if (strncmp(parameter, "I=", 2) == 0) {
            cmd->start_time = atoi(parameter + 2);
        } else if (strncmp(parameter, "D=", 2) == 0) {
            cmd->duration = atoi(parameter + 2);
        }
    }
}

void escalonador(Process *process, int *flag){
    Process roundRobinQueue[MAX_PROCESSES];
    Process realTimeQueue[MAX_PROCESSES];
    Process currentProcess;
    initializeEmptyProcess(&currentProcess);

    int used_seconds[60];
    memset(&used_seconds, 0, sizeof(int) * 60);
    int roundRobinAmount = 0;
    int roundRobinCurrent = 0;
    int realTimeAmount = 0;
    int shouldSwitchProcess = 1;

    struct timeval tv;
    long int currentTime, currentSecond;
    gettimeofday(&tv, NULL);
    long int start_time = tv.tv_sec;    
    // Processo pai (escalonador)
    do {
        if (*flag) {
            Process new_process;
            memcpy(&new_process, process, sizeof(Process));
            if (strcmp(new_process.name, "") != 0) {
                int valid = 1;
                int end_time = new_process.start_time + new_process.duration;
                for (int i = new_process.start_time; i < end_time; i++) {
                    if (used_seconds[i]) {valid = 0;}
                }
                if (valid) {
                    for (int i = new_process.start_time; i < end_time; i++) {
                        used_seconds[i] = 1;
                    }

                    createProcess(&new_process);
                    if (isRealTime(new_process)) {
                        realTimeQueue[realTimeAmount++] = new_process;
                    } else {
                        roundRobinQueue[roundRobinAmount++] = new_process;
                    }
                } else {
                    printf("Processo invalido: %s\n", new_process.name);
                }
            }
            *flag = 0;
        }

        gettimeofday(&tv, NULL);
        currentTime = tv.tv_sec;
        currentSecond = tv.tv_sec % 60;

        if ((currentProcess.start_time + currentProcess.duration) % 60 == currentSecond  || currentProcess.start_time == -1) {
            shouldSwitchProcess = 1;
        }

        for (int i = 0; i < realTimeAmount; i++) {
            if (realTimeQueue[i].start_time == currentSecond) {
                setCurrentProcess(&currentProcess, realTimeQueue[i], currentTime);
                shouldSwitchProcess = 0;
            }
        }

        if (shouldSwitchProcess && roundRobinAmount > 0) {
            int changed = setCurrentProcess(&currentProcess, roundRobinQueue[roundRobinCurrent % roundRobinAmount], currentTime);
            if (changed) {
                roundRobinCurrent++;
            }
        }
    } while (currentTime - start_time < 150);

    // killing all processes
    for (int i = 0; i < realTimeAmount; i++) {
        kill(realTimeQueue[i].pid, SIGKILL);
    }
    for (int i = 0; i < roundRobinAmount; i++) {
        kill(roundRobinQueue[i].pid, SIGKILL);
    }
}

void interpretador(Process *process, int *flag) {
        // Processo filho (interpretador)
        FILE *file = fopen("exec.txt", "r");
        if (file == NULL) {
            perror("Erro ao abrir exec.txt");
            exit(EXIT_FAILURE);
        }

        char line[MAX_CMD_LENGTH];
        while (fgets(line, MAX_CMD_LENGTH, file) != NULL) {
            parseCommand(line, process);
            printf("Novo processo: %s, %d, %d\n", process->name, process->start_time, process->duration);
            *flag = 1;
            sleep(1);
        }

        fclose(file);
        shmdt(process);
        shmdt(flag);
        printf("Interpretador encerrado\n");
        exit(EXIT_SUCCESS);
}

int main() {
    int shmid = shmget(IPC_PRIVATE, sizeof(Process), IPC_CREAT | 0666);
    int shmidf = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("Erro ao criar memória compartilhada");
        exit(EXIT_FAILURE);
    }

    Process *process = shmat(shmid, NULL, 0);
    int *flag = shmat(shmidf, NULL, 0);
    *flag = 0;
    if (process == (void *)-1) {
        perror("Erro ao anexar a memória compartilhada");
        exit(EXIT_FAILURE);
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Erro ao criar um novo processo");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        interpretador(process, flag);
    } else {
        escalonador(process, flag);

        shmdt(process);
        shmdt(flag);
        shmctl(shmid, IPC_RMID, NULL);
        shmctl(shmidf, IPC_RMID, NULL);
        wait(NULL);
    }

    exit(EXIT_SUCCESS);
}