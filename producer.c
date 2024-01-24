#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>


/*  Sección Sensor */

#define DEVICE_NOMBRE  "/dev/DEVICE_I2C_HOST_TEMP"   


#define SIZE_TRAW   7      /* tamanaño del string que devuelve read */
#define CNT_LECT    1      /* canitdad de lecturas */ 
#define SIZE_BUFFER 10
#define CHIP_ID     0xd0   /* registro chip id*/
#define BMP280_SLA  0x76   /* Slave adress(7bits) 0x76 si SDO -> GND */
                           /* Slave adress(7bits) 0x77 si SDO -> VCC */ 

typedef unsigned char uint8_t;

unsigned char bufferR[SIZE_BUFFER] = {0};
unsigned char bufferW[SIZE_BUFFER] = {0};

#define ctrl_meas  0xf4   /* regitro de control 7,6,5 osrs_t 4,3,2 osrs_p 1,0 mode */
#define mode_meas   1     /* [1:0] 00 SLEEP MODE , 01 FORCE MODE, 11 NORMAL MODE*/
#define osrs_t      2<<5     // 2 oversampling, 0 para no medir temperatura   
#define osrs_p      0<<2     // 0 para no medir presión


unsigned int dig_T1;
int dig_T2;
int dig_T3;
uint8_t c_meas=0;
uint8_t t_xlsb=0;
uint8_t t_lsb =0;
uint8_t t_msb =0;

long int adc_t = 0;  //temperatura raw , vlores de temperatura sin compensar


long int get_RawT(void);             //obtengo temperatura raw
long int temp_compensada(long int); //devuelve la temperatura compensada
double temp_fine_compensada(long int);

struct DataBMP280 { unsigned char id_addr ;
                                    };          



#define TEMP_MED  0        // medir temperatura
#define SENS_COEF 1        // leer los coeficientes del sensor
#define ARCH_CONF 2        // aerchivo de configuración
#define CMD_READ  1        // comando para ioctl
#define CMD_CONF  2        // archivo de configuración



#define MAX_RETRIES 10                //para la función init de los semaforos  
#define SHM_SIZE    (250 * SIZE_TRAW) /* 250 lecturas reservo de Shared Momery*/

#define CNT_DATA    10   // cantidad de datos en la cantidad de veces la unidad que sea     

#define SHM_SIZE_COEF 21 //tamaño de la shared memory para los coeficientes del sensor

union semun {
int val;
struct semid_ds *buf;
ushort *array;
};
union semun arg;
int initsem(key_t , int ); /* función inicialización semaforo */

volatile sig_atomic_t stop_pro;

void siguser_handler(int sig)
{

    stop_pro = 1;

}


int main(void)
{
key_t key, key2, key3;
/* variables para shared memories y semaforos*/

int shmid, shmid2, shmid3, semid,semid2, len_msg=0, i, sem_stus=0, size_men=0;  //sem_stus me da el vavlor del semaforo con GETVAL
char *data=NULL, *data2=NULL, *data_coef, c, *mems=NULL, *cmem=NULL;
int a,fin=0;

/* variables para el driver  */

int lecturas=0, fd=0, lenR = 0, lenWr = 0;
char *memSens = NULL, *datSens, vec[6] = {0}; 
long int dataRcv;  // variable para la conversion del dato de string a long int


struct sigaction sa;
stop_pro = 0;
sa.sa_handler = siguser_handler;
sa.sa_flags = 0;
sigemptyset(&sa.sa_mask);
if (sigaction(SIGUSR1, &sa, NULL) == -1) {
perror("sigaction");
exit(1);
}

struct sembuf sb, sb2;
sb.sem_num = 0;
sb.sem_op = -1; /* Tomo el recurso con -1 */
sb.sem_flg = SEM_UNDO;

sb2.sem_num = 0;
sb2.sem_op = -1; /* Tomo el recurso con -1 */
sb2.sem_flg = SEM_UNDO;//IPC_NOWAIT para mirar y no bloquear al proceso llamante

/* Inicializo las KEYS */


if ((key = ftok("producer.c", 'R')) == -1) {
perror("ftok");
exit(1);
}

if ((key2 = ftok("producer.c", 'M')) == -1) {
perror("ftok");
exit(1);
}
/*  para  shared memory de coeficientes de calibración  */
if ((key3 = ftok("producer.c", 'C')) == -1) {
perror("ftok");
exit(1);
}


/* Inicializo el set de semaforo con initsem*/

printf("PRODUCER: Semaforo UNO \n");

if ((semid = initsem(key, 1)) == -1) {
perror("initsem");
exit(1);
}

printf("PRODUCER: Semaforo DOS \n");

if ((semid2 = initsem(key2, 1)) == -1) {
perror("initsem");
exit(1);
}

/* shared memory para coeficientes de sensor */


if ((shmid3 = shmget(key3, SHM_SIZE_COEF, 0644 | IPC_CREAT)) == -1)
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
        
    //copio los datos a la shm3

    //strncpy(data_coef, memSens, SHM_SIZE_COEF);

if((fd = open("/dev/DEVICE_I2C_HOST_TEMP" , O_RDWR)) < 0 )
{
    printf("Error al hacer open en el dispositivo \n");
    return -1; 
}

 /* quiero leer los coeficientes del sensor primero */    
    char *l_coef, *memCoef, *sh_data;
    long int c_d[3];
    memCoef = malloc(sizeof(char)*21);
    memset(memCoef, 0, sizeof(char)*21);
    ioctl( fd, CMD_READ, SENS_COEF); 
    read(fd, memCoef, 21);
    printf("tamaño de memcoef %d\n",strlen(memCoef));
   
    char sencoef[18] = {0};
    sh_data = sencoef; 
    l_coef = memCoef;  
    for(i=0; i<3; i++)
    {
        printf(" PRODUCER: leyendo coeficientes %s\n", l_coef);
        strncpy(sh_data, l_coef,6);
        c_d[i] = atol(l_coef);
        l_coef = (l_coef + 7);
        sh_data = (sh_data + 6);

    }
    strncpy(data_coef, sencoef, SHM_SIZE_COEF); 

    dig_T1 = (unsigned int)c_d[0];
    dig_T2 = (int)c_d[1];
    dig_T3 = (int)c_d[2];

    free(memCoef); //libero la memoria 

    if(shmdt(data_coef) == -1)
    {
        perror("shmdt");
        exit(1);
    }

close(fd);


lecturas = CNT_LECT * SIZE_TRAW;

memSens = malloc(sizeof(char) * lecturas);


while(1)
{

    printf("PRODUCER: Mi  PID: %d\n", getpid());
    printf("PRODUCER: PARA DETENER PRODUCER USAR COMANDO: kill -USR1 %d\n", getpid());
    
    sleep(1);
    if(stop_pro)
    {
        printf("PRODUCER: deteniendo PRODUCER\n");
        break;
    }
    

    if((fd = open("/dev/DEVICE_I2C_HOST_TEMP" , O_RDWR)) < 0 )
    {
    printf("Error al hacer open en el dispositivo \n");
    return -1; 
    }

   
    ioctl( fd, CMD_READ, TEMP_MED);  

    datSens = memSens;

    for(i=0 ; i<CNT_LECT ; i++)
    {
        read(fd, bufferR, lecturas);
        strcpy(datSens, bufferR);
        datSens = (datSens +7); 
    }
 
    datSens = memSens;
  
    for(i=0; i<CNT_LECT; i++)
    {

        printf(" leyendo de la memoria %s\n", datSens);
        datSens = (datSens + 7);

    }

    double num;
    datSens = memSens;
    //coef_temp(&dig_T1,&dig_T2,&dig_T3);
    
    for(i=0; i<CNT_LECT; i++)
    {
        dataRcv = atol(datSens);   //transforma el string en long int
        datSens = (datSens + 7);
        num = temp_fine_compensada(dataRcv);
        printf("temperatura formula uno %.2lf\n", num);
        sprintf(vec, "%.2lf", num);
        printf("temperatura covertida de double a string %s\n", vec);
    }

    close(fd);

    if((sem_stus = semctl(semid, 0, GETVAL))) 
    {
        printf("PRODUCER: Tomando recurso UNO.....\n");    
        if (semop(semid, &sb, 1) == -1) 
        {
            perror("semop");
            exit(1);
        }

        printf("PRODUCER: recurso UNO tomado.\n");
    /*  Aca deberia ir el recurso */
        if ((shmid = shmget(key, SHM_SIZE , 0644 | IPC_CREAT)) == -1)
        {
            perror("shmget");
            exit(1);
        }

        data = shmat(shmid, (void *)0, 0);
        if (data == (char *)(-1)) 
        {
            perror("shmat");
            exit(1);
        }
        
        //copio los datos a la shm1

        strncpy(data, memSens, lecturas);

        sb.sem_op = 1; /* libero el recurso */

        if (semop(semid, &sb, 1) == -1) 
        {
            perror("semop");
            exit(1);
        }

        printf("PRODUCER: Recurso UNO liberado\n");

        if(shmdt(data) == -1)
        {
            perror("shmdt");
            exit(1);
        }

        printf("PRODUCER: MEMEORY SHARED UNO DESCONECTADA \n");

    }
    else
    {
        printf("PRODUCER: Error al tomar SHARED_MEMORY_UNO \n");

    }

    if((sem_stus = semctl(semid2, 0, GETVAL)))   // recurso libre
    {
        printf("PRODUCER: Tomando recurso DOS.....\n");    
        if (semop(semid2, &sb2, 1) == -1) 
        {
            perror("semop");
            exit(1);
        }

    /*  Aca deberia ir el recurso */

        if ((shmid2 = shmget(key2, SHM_SIZE, 0644 | IPC_CREAT)) == -1) 
        {
            perror("shmget");
            exit(1);
        }

        data2 = shmat(shmid2, (void *)0, 0);
        if (data2 == (char *)(-1)) 
        {
            perror("shmat");
            exit(1);
        }

        strncpy(data2, memSens, lecturas);

        sb2.sem_op = 1; /* libero el recurso */

        if (semop(semid2, &sb2, 1) == -1) 
        {
            perror("semop");
            exit(1);
        }

        printf("PRODUCER: Recurso DOS liberado\n");

        if(shmdt(data2) == -1)
        {
            perror("shmdt");
            exit(1);
        }

        printf("PRODUCER: MEMEORY SHARED DOS DESCONECTADA \n"); 

    }
    else
    {
        printf("PRODUCER: Error al tomar SHARED_MEMORY_DOS \n");
    }


    sleep(1);
}

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
//printf("press return\n"); 
//getchar();
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


/* version devuelve entero */
long int temp_compensada(long int adc_T)
{
    long int t_fine;
    long int var1, var2, T;
    	
    var1 = ((((adc_T>>3) - ((long int)dig_T1<<1))) * ((long int)dig_T2)) >> 11;
    var2 = (((((adc_T>>4) - ((long int)dig_T1)) * ((adc_T>>4) - ((long int)dig_T1))) >> 12) * ((long int)dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    
    return T;
}


double temp_fine_compensada(long int adc_t)
{
    
    int t_final = 0;
    double var1=0, var2=0, T =0;

    var1 = (((double)adc_t)/16384.0 - ((double)dig_T1)/1024.0)*((double)dig_T2);
    var2 = ((((double)adc_t)/131072.0 - ((double)dig_T1)/8192.0)*(((double)adc_t)/131072.0 - ((double)dig_T1)/8192.0))*((double)dig_T3);
    
    t_final = (int)(var1 + var2);

    T = (var1 + var2)/5120.0;
    return T;
}    
