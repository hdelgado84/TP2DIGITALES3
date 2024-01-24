/*************************************************************** 
*  Prototipo para un sevidor que escucha por el porT 3490      *
*  usando la función getaddrinfo al cual le paso la structura  *
*  hints con la configuración necesaria                        *
****************************************************************
*/
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>


#define SIZE_TRAW   7      /* tamanaño del string que devuelve read */
#define CNT_LECT    1      /* canitdad de lecturas */ 


#define MAX_RETRIES 10                 //para la función init de los semaforos
#define SHM_SIZE    (250 * SIZE_TRAW)  // tamaño de la sahred memory

#define SHM_SIZE_COEF 21 //tamaño de la shared memory para los coeficientes del sensor


#define MYPORT "3490"
#define BACKLOG 10
#define MAXCON  15  // igual a cantidad de procesos

volatile int N_out = MAXCON;  

/*  consumer función que lee de las shared memory 

   recibe: tamaño de la shared memory, cantidad a leer , puntero a memoria 
*/
static int consumer(int , int, char *);

//static void coef_temp(unsigned int *c1, int *c2, int *c3);
static double temp_fine_compensada(long int );

static key_t key, key2, key3;
static int shmid, shmid2,shmid3, semid, semid2 ;

/* Declaro e inicializo las estructuras para los dos semaforos */

union semun {
int val;
struct semid_ds *buf;
ushort *array;
};
union semun arg;
int initsem(key_t , int ); /* función inicialización semaforo */

struct sembuf sb,sb2;

unsigned int dig_T1;
int dig_T2;
int dig_T3;

long int adc_t = 0;  //temperatura raw

   
void child_exit(int sig)
{

   printf("PADRE:dentro de la función child_exit\n");
    while(waitpid(-1,NULL,WNOHANG)>0) //WUNTRACED WNOHANG
    {
        write(0,"dentro de child \n",14);
        N_out--;
    }
    printf("PADRE: Hijos restantes: %d\n", N_out);
}

int main(void)
{
    pid_t pid;
    char *msg, *cdata=NULL, *datCoef;
    int  len_msg, msg_send, len;
    int status=0, sockfd, newfd, i=0, n=0;
    struct addrinfo hints, *servinfo;
    struct sockaddr_storage their_addr;
    struct sigaction act;
    socklen_t addr_size;

    sb.sem_num = 0;
    sb.sem_op = -1;            // Tomo el recurso con -1 
    sb.sem_flg = SEM_UNDO;

    sb2.sem_num = 0;
    sb2.sem_op = -1;           // Tomo el recurso con -1 */
    sb2.sem_flg = SEM_UNDO;    //IPC_NOWAIT para mirar y no bloquear al proceso llamante

    long int dataRcv;  // variable para la conversion del dato de string a long int

/* Variables donde guardo la página web servidor */

    char head[] = "HTTP/1.1 200 OK\r\n\n <html><body>\r\n<meta http-equiv=\"refresh\" content=\"5\">\r\n";
    char buffer[2048];
    char pagina[2048];
    char html[] = "<h2>TEMPERATURA</h2>\r\n";
    char tail[] = "</body></html>\r\n";
    char vec[6] = {0};

    /* quiero leer los coeficientes del sensor primero */ 
    
    char *l_coef, *memCoef, *data_coef, vecCoef[6];
    long int c_d[3];
    memCoef = malloc(sizeof(char)*21);
    memset(memCoef, 0, sizeof(char)*21);

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

    data_coef = shmat(shmid3, (void *)0, 0);
    if (data_coef == (char *)(-1)) 
    {
        perror("shmat");
        exit(1);
    }


     printf("datos de la shared memory %s\n",data_coef);


    strncpy(memCoef, data_coef, SHM_SIZE_COEF);
  
    l_coef = memCoef;  
    for(i=0; i<3; i++)
    {
        strncpy(vecCoef, l_coef, 6);    
        printf(" SERVIDOR: leyendo coeficientes %s\n", vecCoef);
        c_d[i] = atol(vecCoef);
        l_coef = (l_coef + 6);


    }
  
    dig_T1 = (unsigned int)c_d[0];
    dig_T2 = (int)c_d[1];
    dig_T3 = (int)c_d[2];

    free(memCoef); //libero la memoria 

   
    if(shmdt(data_coef) == -1) //me desconecto de la shred memory3
    {
        perror("shmdt");
        exit(1);
    }


/* genero las KEYS para los dos semaforos */

    if((key = ftok("producer.c", 'R')) == -1) 
    {
        perror("ftok");
        exit(1);
    }

/* inicializo el set de semaforo con initsem*/

    printf("Semaforo UNO \n");

    if((semid = initsem(key, 1)) == -1) 
    {
        perror("initsem");
        exit(1);
    }

    if((key2 = ftok("producer.c", 'M')) == -1) 
    {
        perror("ftok");
        exit(1);
    }

/* inicializo el set de semaforo con initsem*/
    printf("Semaforo DOS \n");

    if((semid2 = initsem(key2, 1)) == -1) 
    {
        perror("initsem");
        exit(1);
    }


/* preparo el handler de sigchild para atender el exit() de cada conexion */    

    act.sa_handler = child_exit;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;
    
    if(sigaction(SIGCHLD, &act, NULL ) == -1)
    {
        printf("Error función sigaction \n");
    }


    memset(&hints, 0, sizeof hints); /*para asegurar que la estructura este vacia */
    hints.ai_family = AF_UNSPEC;     /* no importa si es IPv4 o IPv6. Con AF_INET IPV4*/
    hints.ai_socktype = SOCK_STREAM; /* TCP stream sockets*/
    hints.ai_flags= AI_PASSIVE;      /* completa mi IP por mi*/

    /* si quiero una ip especifica desecho ai_flags y pongo la ip que
       quiero en primer argumento de getaddrinfo ej: "www.ejemplo.com" */


    if ( status = getaddrinfo(NULL, MYPORT, &hints, &servinfo) != 0 )
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    int a=0;
    //freeaddrinfo(servinfo); /* libero la lista*/
    /*  int socket(int domain, int type, int protocol)  */
                                                          
    if ( (sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1)
    {
        printf("ERROR Al ASIGNAR SOCKET \n" );
        exit(1);
    }

    printf("el numero de socket es : %d \n", sockfd); 
    
    if ( (a= bind(sockfd, (struct sockaddr *)servinfo->ai_addr, servinfo->ai_addrlen )) == -1 )
    {
        perror("ERROR AL HACER BIND\n");
        exit(1);
    }
   
    freeaddrinfo(servinfo); /* libero la lista, ya no uso servinfo*/

    if ( listen( sockfd, BACKLOG ) == -1)
    {
        printf("ERROR LISTEN() \n");
        exit(1);
    }
    
    printf("Pasando listen \n"); 


    for(i=0; i<MAXCON; i++)
    {
        addr_size = sizeof(their_addr);
        if ( (newfd = accept(sockfd, (struct sockaddr *)&their_addr, &addr_size)) == -1 )
        {
            printf("ERROR EN ACCEPT() \n");
            exit(1);
        }
    

        if(!(pid=fork())) // proceso hijo
        {    
            printf(" estoy en el hijo, PID: %d\n",getpid());
            close(sockfd);  //cierro el fd de escucha

            if((n = recv(newfd, buffer, 2048, 0)) == -1)
            {
                printf("ERROR EN RECV \n");
            }
            
            printf("Cantidad de datos recibido %d\n", n);
            printf("%s", buffer);

            /* guardo 10 lectura, en realidad es el temaño de la shared memory */
            int lect = 1; //una lectura
            char *d, *dc,*htm_d; 

            int lect_size = (CNT_LECT * SIZE_TRAW);
            
            if (lect_size < SHM_SIZE)
            {
                d = (char *)malloc(sizeof(char) * lect_size);
                memset(d, 0, sizeof(char) * lect_size);
            }
            else
            {
                d = (char *)malloc(sizeof(char) * SHM_SIZE);  // reservo el máximo de memoria
                memset(d, 0, sizeof(char) * lect_size);
            }   
            if(!consumer(lect_size,1,d)) //pido una lectura
            { 
                printf("lectura obtenida \n");
            }
            else
            {
                printf("lectura fallida \n");

            }
            double num;
            dc = d;
            htm_d = malloc(sizeof(char)*SIZE_TRAW);
            memset(htm_d, 0, sizeof(char) * SIZE_TRAW);

            for(i=0; i<CNT_LECT; i++)
            {
                dataRcv = atol(dc);
                dc=(dc+ 7);
                num = temp_fine_compensada(dataRcv);
                printf("temperatura formula uno %.2lf\n", num);
                sprintf(vec, "%.2lf", num);
                printf("temperatura covertida de double a string %s\n", vec);
                
            }
            strcpy(htm_d, vec);
            snprintf(pagina, sizeof pagina,"%s %s %s %s", head, html,htm_d,tail);
            len = strlen(pagina);
            printf(" tamaño de la pagina2  %d\n",len);

            
            free(d);
            free(htm_d);

            if ( (msg_send = send(newfd, pagina, len, 0)) == -1 )
            {
                printf("ERROR AL USAR SEND \n");
            }

            if ( len == msg_send)
            {
                printf("envio exitoso \n");
            }
            else
            {
                printf("Falta enviar partes del mensaje \n");
                printf("Faltan %d \n",(len - msg_send));

            }
            
            close(newfd);
            exit(0);
        }

    close(newfd);
    }

    
    
    printf("PADRE: Esperando salida de Hijos\n"); 
    while(N_out);
    
    printf("PADRE: Saliendo del Proceso PADRE!!!!!!!\n");

    close(sockfd);  //cierro el fd de escucha
    
    
    
    return 0;


}

/*******************************************************************************
* 
*   función:    initsem
*   argumentos: ket_t , int // recibe la key de ftok y la canitad de semaforos
*   retorna:    int         //  
* 
******************************************************************************/

int initsem(key_t key, int nsems) /* key from ftok() */
{
int i;
union semun arg;
struct semid_ds buf;
struct sembuf sb;
int semid;

semid = semget(key, nsems, IPC_CREAT | IPC_EXCL | 0666);

if (semid >= 0) { /* we got it first */
sb.sem_op = 1; sb.sem_flg = 0;
arg.val = 1;
printf("press return\n"); 
getchar();
for(sb.sem_num = 0; sb.sem_num < nsems; sb.sem_num++) {
/* do a semop() to "free" the semaphores. */
/* this sets the sem_otime field, as needed below. */
if (semop(semid, &sb, 1) == -1) {
int e = errno;
semctl(semid, 0, IPC_RMID); /* clean up */
errno = e;
return -1; /* error, check errno */
}
}
} else if (errno == EEXIST) { /* someone else got it first */
int ready = 0;
semid = semget(key, nsems, 0); /* get the id */
if (semid < 0) return semid; /* error, check errno */
/* wait for other process to initialize the semaphore: */
arg.buf = &buf;
for(i = 0; i < MAX_RETRIES && !ready; i++) {
semctl(semid, nsems-1, IPC_STAT, arg);
if (arg.buf->sem_otime != 0) {
ready = 1;
} else {
sleep(1);
}
}
if (!ready) {
errno = ETIME;
return -1;
}
} else {
return semid; /* error, check errno */
}
return semid;
}


static void leo(int n, int lenData, char * d) //cantidad a leer
{
    int i;
    for(i=0; i<n; i++)
    {

       // snprintf(d, sizeof (d),"%s %s %s %s", head, html,d,tail);
        printf("shared memory: %s\n",d);
        d = d + 25;
        sleep(15);
    }


}
/* size es el tamaño del dato */

static int consumer(int size, int cn, char *p)
{

/* veo si me puedo obtener los datos de alguna de las dos SHARED_MEMORY */

/* pruebo con la memory uno */
int sem_stus=0,i;
char *cdata=NULL, *lectura=NULL;

printf(" Func Consumer valor de size %d\n", size);


if(sem_stus = semctl(semid, 0, GETVAL))
{ 
    printf("Tomando recurso uno.....\n");    

    if (semop(semid, &sb, 1) == -1) 
    {
        perror("semop");
        exit(1);
    }

    printf("recurso uno tomado.\n");
    
    /*  Aca deberia ir el recurso */
    if((shmid = shmget(key, SHM_SIZE, 0644 | IPC_CREAT)) == -1) 
    {
        perror("shmget");
        exit(1);
    }

    cdata = shmat(shmid, (void *)0, 0);
    lectura = cdata;
    if (cdata == (char *)(-1)) 
    {
        perror("shmat");
        exit(1);
    }
    
    /*  le paso el puntero a la SHM*/
    for(i=0; i<cn; i++)
    {
        strncpy(p, lectura, size);
        printf(" valor de p %s: \n", p);
        p = p + size;
        lectura = lectura + size;
    }

    sb.sem_op = 1; /* libero el recurso */

    if (semop(semid, &sb, 1) == -1) 
    {
        perror("semop");
        exit(1);
    }

    printf("Recurso uno liberado\n");

    if(shmdt(cdata) == -1)
    {
        perror("shmdt");
        exit(1);
    }

    printf("Desconectando de la SHARED MEMORY UNO \n");

    return 0; // si pudo acceder termino acá
}
else
{
    printf("No se pudo acceder a la SHARED MEMORY UNO \n"); //pruebo con la otra shared memory
}


/* pruebo con la memory dos */ 


if((sem_stus = semctl(semid2, 0, GETVAL)))
{ 
    printf("Tomando recurso dos.....\n");    

    if(semop(semid2, &sb2, 1) == -1) 
    {
        perror("semop");
        exit(1);
    }

    printf("recurso dos tomado.\n");
    
    /*  Aca deberia ir el recurso */

    if ((shmid2 = shmget(key2, SHM_SIZE, 0644 | IPC_CREAT)) == -1) 
    {
        perror("shmget");
        exit(1);
    }

    cdata = shmat(shmid2, (void *)0, 0);
    if (cdata == (char *)(-1)) {
    perror("shmat");
    exit(1);
    }
    lectura = cdata;
    /*  le paso el puntero a la SHM*/
    for(i=0; i<cn; i++)
    {
        strncpy(p, cdata, size);
        p = p + size;
        lectura = lectura + size;
    }
    
    sb2.sem_op = 1; /* libero el recurso */

    if (semop(semid2, &sb2, 1) == -1) 
    {
        perror("semop");
        exit(1);
    }

    printf("Recurso dos liberado\n");

    if(shmdt(cdata) == -1)
    {
        perror("shmdt");
        exit(1);
    }

    printf("Desconectando de la SHARED MEMORY DOS \n");

    return 0; 

}
else
{
    printf("No se pudo acceder a la SHARED MEMORY DOS \n");
}

printf("No se pudo acceder a los datos \n");

return 1;

}



static double temp_fine_compensada(long int adc_t)
{
    
    int t_final = 0;
    double var1=0, var2=0, T =0;

    var1 = (((double)adc_t)/16384.0 - ((double)dig_T1)/1024.0)*((double)dig_T2);
    var2 = ((((double)adc_t)/131072.0 - ((double)dig_T1)/8192.0)*(((double)adc_t)/131072.0 - ((double)dig_T1)/8192.0))*((double)dig_T3);
    
    t_final = (int)(var1 + var2);

    T = (var1 + var2)/5120.0;
    return T;
} 
 
