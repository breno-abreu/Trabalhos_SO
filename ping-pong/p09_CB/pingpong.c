//#define DEBUG
#include "pingpong.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include "queue.h"

#define STACKSIZE 32768
#define QUANTUM 20

task_t MainTask;                            /*Tarefa referente ao contexto da função main*/
task_t *CurrentTask;                        /*Variável que aponta para a tarefa que está em andamento*/
int contador;                               /*Variável que será utilizada para inicializar o campo 'tid' da struct 'task_t'...
                                            ...tornando possível que cada tarefa tenha um identificador diferente.*/
task_t dispatcher;                          /*Tarefa referente ao dispatcher*/
task_t *readyQueue;                         /*Fila de tarefas prontas para serem executadas*/
task_t *sleepingQueue;                      /*Fila de tarefas adormecidas*/
int aging;                                  /*Fator de envelhecimento*/
struct sigaction action;                    /*Estrutura que define o tratador de sinal*/
struct itimerval timer;                     /*Estrutura de inicialização do timer*/
int contadorTimer;                          /*Contador referente ao Quantum de cada tarefa*/
unsigned int tempoAtual;                    /*Conta o tempo de execução do programa em milisegundo*/
int tempoAuxSegundos;

/*Função que será ativada quando o temporizador chegar a um determinado tempo*/
void tratador()
{
    if(CurrentTask){

        /*Tempo de processamento da tarefa atual é atualizado*/
        CurrentTask->tempoProcessamento++;

        /*Caso seja uma tarefa de usuário*/
        if(CurrentTask->tipoTarefa == USUARIO){

            /*Caso o quantum chegue a zero, seu quantum é atualizado para o valor de quantum predefinido, ...
            ...,a tarefa é adicionada ao final da fila de prontas e o contexto é mudado para o dispatcher*/
            if(CurrentTask->contadorQuantum <= 0){

                CurrentTask->contadorQuantum = CurrentTask->quantumEstatico;
                task_yield();
            }
            /*Decrementa o contador de quantum da tarefa atual*/
            CurrentTask->contadorQuantum--;
        }
    }
    /*Contador de tempo de execução é atualizado a cada 1 milisegundo*/
    tempoAtual++;

    /*Contador de tempo em milisegundos que possibilita a contagem de segundos. Será resetado no dispatcher*/
    tempoAuxSegundos++;
}

/*Retorna a próxima tarefa a ser executada*/
task_t* scheduler()
{
    return readyQueue;                      /*Retorna o primeiro elemento da fila de prontas*/
}

/*Função recebida pelo dispatcher, irá executar as tarefas que estão na fila, enquanto houverem elementos*/
void dispatcher_body()
{
    task_t * next;

    while(1){

        /*Caso haja elementos na fila de adormecidas e o contador auxiliar bata um segundo*/
        if(queue_size((queue_t*)sleepingQueue) > 0 && tempoAuxSegundos >= 1){
            task_t *aux = sleepingQueue;
            task_t *auxNext;
            int tamanhoFila = queue_size((queue_t*)sleepingQueue);

            int aux2 = 0;

            /*Percorre a lista de adormecidas*/
            for(int i = 0; i < tamanhoFila; i++){
                auxNext = aux->next;

                /*Atualiza o tempo em que a tarefa ficará adormecida*/
                aux->sleepTime--;

                /*Caso o tempo seja 0, acorda a tarefa, inserindo-a na fila de prontas*/
                if(aux->sleepTime <= 0 && aux2 == 0){

                    /*Caso o ponteiro aux aponte para o inicio da fila*/
                    if(aux == sleepingQueue){

                        /*Caso haja apenas um elemento na fila, o inicio da fila aponta para nulo*/
                        if(aux->next == aux)
                            sleepingQueue = NULL;

                        /*Caso haja mais de um elemento, o inicio da fila aponta para o próximo elemento de aux*/
                        else
                            sleepingQueue = aux->next;
                    }
                    /*Acorda a tarefa*/
                    task_resume(aux);

                    aux2 = 1;
                }
                aux = auxNext;
            }
            /*Reseta a variável tempoAuxSegundos*/
            tempoAuxSegundos = 0;
        }

        /*Enquanto há elementos na fila de prontas continua executando as tarefas*/
        if(queue_size((queue_t*)readyQueue) > 0){
            next = scheduler();                 /*next recebe a próxima tarefa a ser executada*/
            next->ativacoes++;                  /*Atualiza a quantidade de ativações da tarefa a ser executada*/
            if(next)
                task_switch(next);
        }

        /*Caso não haja elementos nem na fila de prontas nem da fila de adormecidas, encerra o laço*/
        if(queue_size((queue_t*)readyQueue) == 0 && queue_size((queue_t*)sleepingQueue) == 0)
            break;

    }
    /*Após executar todas as tarefas, retorna para o contexto da função main ou para o dispatcher*/
    task_exit(0);
}

void pingpong_init()
{
    /*desativa o buffer da saida padrao (stdout), usado pela função printf*/
    setvbuf (stdout, 0, _IONBF, 0);
    readyQueue = NULL;
    sleepingQueue = NULL;
    contador = -1;
    tempoAuxSegundos = -1;

    /*Cria a tarefa main, que fará referência a função principal do programa*/
    task_create(&MainTask, NULL, NULL);

    /*Inicialmente a tarefa atual será a relacionada a função main*/
    CurrentTask = &MainTask;

    /*Cria a tarefa dispatcher*/
    task_create(&dispatcher, dispatcher_body, NULL);
    dispatcher.tipoTarefa = SISTEMA;

    aging = -1;
    contadorTimer = QUANTUM;
    tempoAtual = -1;

    /*Registra a ação para o sinal de timer SIGALRM*/
    action.sa_handler = tratador;
    sigemptyset (&action.sa_mask);
    action.sa_flags = 0;
    if (sigaction (SIGALRM, &action, 0) < 0){
        perror ("Erro em sigaction: ");
        exit (1);
    }

    /*Ajusta valores  do temporizador, que irá gerar um pulso a cada 1 milisegundo*/
    timer.it_value.tv_usec = 1;             // primeiro disparo, em micro-segundos
    timer.it_value.tv_sec  = 0;             // primeiro disparo, em segundos
    timer.it_interval.tv_usec = 1000;       // disparos subsequentes, em micro-segundos
    timer.it_interval.tv_sec  = 0;          // disparos subsequentes, em segundos

    /*Arma o temporizador ITIMER_REAL*/
    if (setitimer (ITIMER_REAL, &timer, 0) < 0){
        perror ("Erro em setitimer: ");
        exit (1);
    }

    /*Dispatcher é ativado*/
    task_yield();
}

int task_create( task_t *task,
                 void (*start_func)(void *),
                 void *arg)
{
    char *stack;
    contador++;

    /*Inicializa o contexto de uma tarefa*/
    if(getcontext(&task->context) < 0)
        return -1;

    /*Preenche os campos necessários do contexto antes de chamar a função 'makecontext()'*/
    stack = malloc(STACKSIZE);
    if(stack){
        task->context.uc_stack.ss_sp = stack;
        task->context.uc_stack.ss_size = STACKSIZE;
        task->context.uc_stack.ss_flags = 0;
        task->context.uc_link = 0;
    }
    else{
        perror ("Erro na criação da pilha: ");
        return -1;
    }

    /*Relaciona a função 'start_func' com o contexto em questão*/
    makecontext(&task->context, (void*)start_func, 1, (char*) arg);

    /*Dá a tarefa um identificador único*/
    task->tid = contador;

    #ifdef DEBUG
    printf("task_create: criou tarefa %d\n", task->tid);
    #endif

    /*Inclui tarefas novas na lista de prontas, a menos que a tarefa seja o dispatcher*/
    if(task != &dispatcher)
        queue_append((queue_t**) &readyQueue, (queue_t*) task);

    /*Inicializa os valores de prioridade estática e dinâmica usadas no escalonamento*/
    task->prioridadeEstatica = 0;
    task->prioridadeDinamica = 0;

    task->tipoTarefa = USUARIO;
    task->quantumEstatico = QUANTUM;

    /*Inicializado o contador de quantum com o quantum predefinido*/
    task->contadorQuantum = task->quantumEstatico;

    task->tempoExecucao = systime();
    task->tempoProcessamento = 0;
    task->ativacoes = 0;
    task->suspendedQueue = NULL;
    task->exitCode = 0;
    task->sleepTime = 0;

    return task->tid;
}

int task_switch (task_t *task)
{
    /*O ponteiro 'CurrentTask' aponta para a tarefa recebida*/
    task_t *Aux = CurrentTask;
    CurrentTask = task;

    #ifdef DEBUG
    printf("task_switch: trocando contexto %d -> %d\n", Aux->tid, task->tid);
    #endif

    /*Salva o contexto da tarefa anterior e aciona o contexto da tarefa recebida*/
    if(swapcontext(&Aux->context, &task->context) < 0)
        return -1;

    return 0;
}

void task_exit (int exitCode)
{
    #ifdef DEBUG
    printf("task_exit: tarefa %d sendo encerrada\n", CurrentTask->tid);
    #endif

    task_t *aux = CurrentTask->suspendedQueue;

    /*Caso haja algum elemento na fila de tarefas suspensas da tarefa que será finalizada*/
    if(aux){
        task_t *aux2;
        int tamanhoFila = queue_size((queue_t*)aux);

        /*Percorre a fila e resume todas as tarefas que pertencem a ela*/
        for(int i = 0; i < tamanhoFila; i++){
            aux2 = aux->next;
            task_resume(aux);
            aux = aux2;
        }
        CurrentTask->suspendedQueue = NULL;
    }

    /*Escreve o código de saída da tarefa que será finalizada*/
    CurrentTask->exitCode = exitCode;

    /*Caso a tarefa atual não seja o dispatcher, remove a tarefa atual da fila de prontas e...
    ...troca para a tarefa dispatcher*/
    if(CurrentTask != &dispatcher){
        CurrentTask->estado = FINALIZADA;
        queue_remove((queue_t**) &readyQueue, (queue_t*) CurrentTask);
        CurrentTask->tempoExecucao = systime() - CurrentTask->tempoExecucao;
        printf("Task: %d exit: running time %d ms, cpu time %d ms, %d activations\n",
                CurrentTask->tid, CurrentTask->tempoExecucao, CurrentTask->tempoProcessamento, CurrentTask->ativacoes);
        task_switch(&dispatcher);
    }

    /*Caso a tarefa a ser finalizada seja o dispatcher, troca para a tarefa main*/
    else{
        dispatcher.tempoExecucao = systime() - dispatcher.tempoExecucao;
        printf("Task: %d exit: running time %d ms, cpu time %d ms, %d activations\n",
                 dispatcher.tid, dispatcher.tempoExecucao,  dispatcher.tempoProcessamento, dispatcher.ativacoes);
        task_switch(&MainTask);
    }
}

int task_id ()
{
    return CurrentTask->tid;                /*Retorna o identificador da tarefa que está em andamento*/
}

void task_yield ()
{
    #ifdef DEBUG
    printf("task_yield. Tarefa atual: %d. Trocando para dispatcher\n", CurrentTask->tid);
    #endif

    /*Caso a tarefa atual não seja o dispatcher, exclui o elemento da lista...
    ..e o reposiciona no final da fila*/
    if(CurrentTask != &dispatcher && CurrentTask->estado != SUSPENSA){
        queue_remove((queue_t**) &readyQueue, (queue_t*) CurrentTask);
        queue_append((queue_t**) &readyQueue, (queue_t*) CurrentTask);
    }

    dispatcher.ativacoes++;                 /*Atualiza a quantidade de ativações do dispatcher*/
    task_switch(&dispatcher);               /*Troca para o dispatcher*/
}

void task_suspend (task_t *task, task_t **queue)
{
    task_t* aux;

    /*Caso a tarefa recebida não seja nula, aux recebe a tarefa recebida*/
    if (task)
        aux = task;

    /*Caso a tarefa recebida seja nula, aux recebe a tarefa atual*/
    else
        aux = CurrentTask;

    /*Troca o estado da tarefa para suspensa*/
    aux->estado = SUSPENSA;

    /*Remove a tarefa da fila de prontas*/
    queue_remove((queue_t**) &readyQueue, (queue_t*) aux);

    /*Inseri a tarefa na fila recebida*/
    queue_append((queue_t**) queue, (queue_t*) aux);
}

void task_resume (task_t *task)
{
    if(task){

        /*Troca o estado da tarefa para pronta*/
        task->estado = PRONTA;

        /*Retira a tarefa de sua fila atual*/
        if(task->next && task->prev){
            task->prev->next = task->next;
            task->next->prev = task->prev;
        }
        task->next = NULL;
        task->prev = NULL;

        /*Inseri a tarefa recebida na lista de prontas*/
        queue_append((queue_t**) &readyQueue, (queue_t*) task);
    }
}

void task_setprio (task_t *task, int prio)
{
    #ifdef DEBUG
    printf("task_setprio. Escrevendo o valor de prioridade da tarefa: %d, para %d\n", task->tid, prio);
    #endif

    /*Caso o valor 'prio' recebido esteja entre -20 e 20, ou seja cumpre com os valores de prioridade padrões no estilo UNIX*/
    if(prio >= -20 && prio <= 20){

        /*Caso a tarefa recebida seja nula, a tarefa atual recebe os valores de prioridade*/
        if(!task){
            CurrentTask->prioridadeEstatica = prio;
            CurrentTask->prioridadeDinamica = prio;
        }

        /*Caso contrário, a tarefa recebida recebe os valores de prioridade*/
        else{
            task->prioridadeEstatica = prio;
            task->prioridadeDinamica = prio;
        }
    }
    /*Se o valor de 'prio' esteja além dos limites dos valores de prioridade do padrão UNIX, mostra a seguinte mensagem de erro*/
    else
        printf("ERRO: prioridade está fora do limite");
}

int task_getprio (task_t *task)
{
    /*Caso a tarefa seja nula, retorna o valor da prioridade estática da tarefa atual*/
    if(!task)
        return CurrentTask->prioridadeEstatica;

    /*Caso contrário, retrona o valor da prioridade estática da tarefa recebida*/
    return task->prioridadeEstatica;
}

unsigned int systime ()
{
    return tempoAtual;                      /*Retorna o tempo atual de execução do programa*/
}

int task_join (task_t *task)
{
    /*Caso a tarefa recebida e exista e ainda estiver ativa, suspende a tarefa atual, inseri-a na fila de suspensas...
    ...e ativa o dispatcher*/
    if(task && task->estado != FINALIZADA){
        task_suspend(NULL, &task->suspendedQueue);
        task_yield();
        return task->exitCode;
    }
    return -1;
}

void task_sleep (int t)
{
    /*Caso o tempo recebido seja maior que zero, atualiza a variável sleepTime da tarefa atual, retira-a da fila de prontas...
    ...e a coloca na fila de adormecidas.*/
    if(t > 0){
        CurrentTask->sleepTime = t * 1000;
        task_suspend(NULL, &sleepingQueue);
    }
    task_yield();
}


















