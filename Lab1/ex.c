#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <semaphore.h>
#include <sys/ipc.h>

#define READ 0
#define WRITE 1

// ************* STACK PART START *************

int n = -1; // stack size (maximum size, capacity)
int *stack;
int cur_elem = -1;

int isEmpty(){
     return cur_elem == -1;
}

int isFull(){
    return cur_elem == n-1;
}

void createStack(int size){
    n = size;
    stack = malloc(n * sizeof(int));
    printf("Stack was succesfully created\n");
}

void pop(){
    if(isEmpty()){
        printf("Stack is empty. Nothing to pop; -1 is returned.\n");
        return;
    }
    cur_elem--;
}

void push(int v){
    if(isFull()){
        printf("Stack is full. Can't add anymore\n");
        return;   
    }  
    cur_elem++;
    stack[cur_elem] = v;
    printf("%d Pushed succesfully\n", v);

}

int peek(){
    if(isEmpty()){
        printf("Stack is empty. Nothing to peek; -1 is returned \n");
        return -1;
    }
    return stack[cur_elem];
}



void display(){
    if(isEmpty()){
        printf("Stack is empty. Nothing to display\n");
        return;
    }

    for(int i = 0; i <= cur_elem; i++)
        printf("%d ", stack[i]);
    printf("\n");
    
}

int stack_size(){ 
    return cur_elem + 1;
}

void clear(){
    cur_elem = -1;
}

// ************* STACK PART END *************

int pid_parent;
int pid_child;

int pipe1[2]; // for commands
int pipe2[2]; // for arguments (dont ask me why i need second pipe, i know that it is easy to do without it but i want to do with it)

int work = 0;


void flushAll(){
    fflush(stdin);
    fflush(stdout);
}


// Custome SIGCONT handlers 
void child_handle_resume(int mysignal){
    printf("HANDLED \n");
    work = 1;
    flushAll();
}

void parent_handle_resume(int mysignal){
    printf("HANDLED \n");
    work = 1;
    flushAll();
}

void main() {
    printf("\n");

    // Create pipe
    if(pipe(pipe1) == -1 || pipe(pipe2) == -1){
        printf("Error: Something with pipe1 or pipe2\n");
        flushAll();
        return;
    }

    // save parent's pid and create new process
    pid_parent = getpid();
    int pid = fork();

    //Buffer for a command and for a number
    int intbuf[1];
    
    //if fork failed
    if(pid < 0){
        printf("Error: Cant create new proccess\n");
        fflush(stdin);
        exit(0);
    } 

    // child (server) works as stack
    if(pid == 0){
        pid_child = getpid();

        int res = signal(SIGCONT, child_handle_resume);    
        
        if(res == SIG_ERR){
            printf("Error: unable to set signal handler.\n");
            exit(0);
        }  

        while(1){
            printf("Child iteration started\n\n");
            //read command
            read(pipe1[READ], intbuf, sizeof(int));
            int command = intbuf[0];
            printf("Command readed by child = %d\n",command);
            int value;

            if(command == 0 || command == 1){
                //read value
                read(pipe2[READ], intbuf, sizeof(int));
                int value = intbuf[0];
                printf("Value readed by child = %d\n",value);

                if(command == 0){
                    createStack(value);
                    printf("N = %d and currentElem = %d\n", n, cur_elem);
                }
                else if(command == 1){
                    push(value);
                }
            }
            else if(command == 2){
                pop();
            }
            else if(command == 3){
                int res = peek();
                printf("Element on top of the stack is %d\n", res);
            }
            else if(command == 4){
                display();
            }
            else if(command == 5){
                printf("Size of stack is %d\n", n);
            }
            else if(command == 6){
                int res = isEmpty();
                if(res == 0){
                    printf("Stack isn't empty.\n");
                }
                else{
                    printf("Stack is empty.\n");
                }
            }

            kill(pid_parent, SIGCONT);
            printf("Child Iteration is done\n\n");
            
            work = 0;
            while(!work){}; // busy waiting

            flushAll();
        }
        printf("Child Done\n");
    }
    // parent (client) interface to user
    else{
        work = 1;
        pid_child = pid;

        int res = signal(SIGCONT, parent_handle_resume);
        
        if(res == SIG_ERR){
            printf("Error: unable to set signal handler.\n");
            exit(0);
        }  

        while(1){
            printf("Parent iteration started\n\n");
    
            printf("Write a command: ");

            char str_command[32];
            scanf("%s", str_command);
            int command = -10, value = -10;

            if(!strcmp(str_command,"create")){
                command = 0;
                scanf("%d", &value);

                //send value  
                intbuf[0] = value;
                write(pipe2[WRITE], intbuf, sizeof(int));
            }
            else if(!strcmp(str_command,"push")){
                command = 1;
                scanf("%d", &value);

                //send value  
                intbuf[0] = value;
                write(pipe2[WRITE], intbuf, sizeof(int));
            }
            else if(!strcmp(str_command,"pop")){
                command = 2;
            }
            else if(!strcmp(str_command,"peek")){
                command = 3;
            }
            else if(!strcmp(str_command,"display")){
                command = 4;
            }
            else if(!strcmp(str_command,"size")){
                command = 5;
            }
            else if(!strcmp(str_command,"empty")){
                command = 6;
            }
            else if(!strcmp(str_command,"exit")){
                printf("Good bye\n");
                kill(pid_child,SIGTERM);
                exit(0);
            }
            else{
                printf("There is no such command\n");
            }

            printf("command to send is %d\n", command);
            if(command == -1){
                printf("Bad Command. Try again\n");
                continue;
            }

            // send command 
            intbuf[0] = command;
            write(pipe1[WRITE], intbuf, sizeof(int));
            

            kill(pid_child, SIGCONT);
        
            printf("Parent iteration is done\n\n");

            flushAll();

            work = 0;
            while(!work){}; // busy waiting
        }
        printf("Parent Done\n");
    }
    
}
/*  
    Commands:
    0 - create n
    1 - push n
    2 - pop
    3 - peek
    4 - display
    5 - size
    6 - empty
*/