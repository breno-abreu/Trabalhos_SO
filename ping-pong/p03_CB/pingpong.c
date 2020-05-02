//#define DEBUG
#include "pingpong.h"
#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

#define STACKSIZE 32768

//Tarefa referente ao contexto da função main
task_t MainTask;
//Variável que aponta para a tarefa que está em andamento
task_t *CurrentTask;
//Variável que será utilizada para inicializar o campo 'tid' da struct 'task_t'...
//...tornando possível que cada tarefa tenha um identificador diferente.
int contador = 0;

task_t dispatcher;

task_t *readyQueue;


task_t* scheduler()
{
    return readyQueue;
}

void dispatcher_body()
{
    task_t * next;

    while(queue_size((queue_t*)readyQueue) > 0){
        next = scheduler();
        if(next){
            task_switch(next);
        }
    }
    task_exit(0);
}

//Inicializa algumas variáveis e desativa o buffer da saída padrão
void pingpong_init()
{
    //desativa o buffer da saida padrao (stdout), usado pela função printf
    setvbuf (stdout, 0, _IONBF, 0);
    //Inicialmente a tarefa atuar será a relacionada a função main
    CurrentTask = &MainTask;
    MainTask.tid = 0;

    task_create(&dispatcher, dispatcher_body, NULL);

    readyQueue = NULL;
}

int task_create( task_t *task,
                 void (*start_func)(void *),
                 void *arg)
{

    char *stack;
    contador++;
    //Inicializa o contexto de uma tarefa
    if(getcontext(&task->context) < 0)
        return -1;

    //Preenche os campos necessários do contexto antes de chamar a função 'makecontext()'
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
    //Relaciona a função 'start_func' com o contexto em questão
    makecontext(&task->context, (void*)start_func, 1, (char*) arg);
    //Dá a tarefa um identificador único
    task->tid = contador;

    //Código para depuração caso DEBUG esteja definido
    #ifdef DEBUG
    printf("task_create: criou tarefa %d\n", task->tid);
    #endif

    if(task != &dispatcher)
        queue_append((queue_t**) &readyQueue, (queue_t*) task);

    return task->tid;
}

int task_switch (task_t *task)
{
    //O ponteiro 'CurrentTask' aponta para a tarefa recebida
    task_t *Aux = CurrentTask;
    CurrentTask = task;
    //Salva o contexto da tarefa anterior e aciona o contexto da tarefa recebida
    if(swapcontext(&Aux->context, &task->context) < 0)
        return -1;

    //Código para depuração caso DEBUG esteja definido
    #ifdef DEBUG
    printf("task_switch: trocando contexto %d -> %d\n", Aux->tid, task->tid);
    #endif

    return 0;
}

void task_exit (int exitCode)
{
    //Código para depuração caso DEBUG esteja definido
    #ifdef DEBUG
    printf("task_exit: tarefa %d sendo encerrada\n", CurrentTask->tid);
    #endif

    if(CurrentTask != &dispatcher && CurrentTask != &MainTask){
        queue_remove((queue_t**) &readyQueue, (queue_t*) CurrentTask);
        task_switch(&dispatcher);
    }
    else
        task_switch(&MainTask);

    //Finaliza a tarefa atual retornando para a função main

}

int task_id ()
{
    //Retorna o identificador da tarefa que está em andamento
    return CurrentTask->tid;
}

void task_yield ()
{
    if(CurrentTask != &MainTask && CurrentTask != &dispatcher){
        queue_remove((queue_t**) &readyQueue, (queue_t*) CurrentTask);
        queue_append((queue_t**) &readyQueue, (queue_t*) CurrentTask);
    }
    task_switch(&dispatcher);
}




















