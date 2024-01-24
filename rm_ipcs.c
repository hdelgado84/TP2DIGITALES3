/*

    remuevo semaforos y shared memory


*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>


#define  SHM_SIZE 250
#define  SHM_SIZE_COEF 21 //tamaño de la shared memory para los coeficientes del sensor



union semun {
int val;
struct semid_ds *buf;
ushort *array;
};


int main(void)
{
key_t key, key2, key3;
int semid, semid2, shmid, shmid2, shmid3;
union semun arg;

    if((key = ftok("producer.c", 'R')) == -1) 
    {
        perror("ftok");
        exit(1);
    }

/* grab the semaphore set created by seminit.c: */

    if((semid = semget(key, 1, 0)) == -1) 
    {
        perror("semget");
        exit(1);
    }

/* remove it: */

    if(semctl(semid, 0, IPC_RMID, arg) == -1) 
    {
        perror("semctl");
        exit(1);
    }

    if((key2 = ftok("producer.c", 'M')) == -1) 
    {
        perror("ftok");
        exit(1);
    }

/* grab the semaphore set created by seminit.c: */

    if((semid2 = semget(key2, 1, 0)) == -1) 
    {
        perror("semget");
        exit(1);
    }

/* remove it: */

    if(semctl(semid2, 0, IPC_RMID, arg) == -1) 
    {
        perror("semctl");
        exit(1);
    }



    if((shmid = shmget(key, SHM_SIZE , 0644 | IPC_CREAT)) == -1)
    {
        perror("shmget");
        exit(1);
    }

    if(shmctl(shmid, IPC_RMID, NULL) == -1) 
    {
        perror("semctl");
        exit(1);
    }


    if((shmid2 = shmget(key2, SHM_SIZE , 0644 | IPC_CREAT)) == -1)
    {
        perror("shmget");
        exit(1);
    }

    if(shmctl(shmid2, IPC_RMID, NULL) == -1) 
    {
        perror("semctl");
        exit(1);
    }

    /*  para  shared memory de coeficientes de calibración  */
    if ((key3 = ftok("producer.c", 'C')) == -1) 
    {
        perror("ftok");
        exit(1);
    }

    if((shmid3 = shmget(key3, SHM_SIZE_COEF, 0644 | IPC_CREAT)) == -1)
    {
        perror("shmget");
        exit(1);
    }

    if(shmctl(shmid3, IPC_RMID, NULL) == -1) 
    {
        perror("semctl");
        exit(1);
    }

    return 0;
}