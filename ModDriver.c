
/*
*   Creación de un modulo LKM  
*
**/

#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/ioport.h>
#include <linux/gpio.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include "ModDriver.h"
#include <linux/uaccess.h> //header para copy to user


#define MEM_SIZE 4096   // 4kb, una página.

/* Macros para la activación y desactivación de bits*/

#define act_bit(registro,pin)   (registro) |= (uint32_t)(1<<(pin));    //activo bit
#define pass_bit(registro,pin)  (registro) &= (uint32_t)(~(1<<(pin))); //desact bit

/* defines para IOCTL */
#define TEMP_MED  0        // medir temperatura
#define SENS_COEF 1        // leer los coeficientes del sensor
#define ARCH_CONF 2        // aerchivo de configuración
#define CMD_READ  1        // comando para ioctl
#define CMD_CONF  2        // archivo de configuración

/*-----------------------------------------------------------------------------------------
-                   Declaro variables para el sensor BMP280                               -
-------------------------------------------------------------------------------------------*/

uint8_t c_meas=0;

static long int t_raw=0;
static uint8_t  t_xlsb=0;
static uint8_t  t_lsb =0;
static uint8_t  t_msb =0;
static int ioctlMode = TEMP_MED; //por default mide temp raw 
bmpcoef tempCoef;

//static struct coef bmpCoef;

static void modeForce(void);
static double temp_fine_compensada(long int);
static long int temp_compensada(long int );
static void coeficientes(bmpcoef *);
long int get_RawT(void);              //obtiene la temperatura sin compensar
static void enToStr(long int, char *c, int N); //convierte int a string

/* función de copia de memoria */
static void cpy(char *, const char *, size_t );
/*-----------------------------------------------------------------------------------------
-                   DECLARO TODO LO QUE TIENE QUE VER CON EL DEVICE DRIVER                -
-------------------------------------------------------------------------------------------*/

#define CHARDEV_NOMBRE  "CHARDEV_I2C_HOST_TEMP"
#define CLASS_NOMBRE    "CLASS_I2C_HOST_TEMP" 
#define DEVICE_NOMBRE   "DEVICE_I2C_HOST_TEMP"   
#define MenorCant 1  // Cantidad de numero menores



MODULE_LICENSE("GPL");
MODULE_AUTHOR("Humberto Delgado");  
MODULE_DESCRIPTION("Controlador Temperatura");


static int I2C_HOST_Probe(struct platform_device *);
static int I2C_HOST_Remove(struct platform_device *);
/*--------------------------------------------------------------------------------------- 
-              Declaro dos waitqueue  para RX y TX , las uso en read y  write            -
-----------------------------------------------------------------------------------------*/

wait_queue_head_t wqI2C_RX;
wait_queue_head_t wqI2C_TX;

/* Inicializo las colas de espera */

DECLARE_WAIT_QUEUE_HEAD(wqI2C_RX);

DECLARE_WAIT_QUEUE_HEAD(wqI2C_TX);

/* declaro los flag de condición para las wait_queue */
int cond_wqI2C_TX = 0;
int cond_wqI2C_RX = 0;
// wait_event_interruptible(wq_I2C, wqI2c !=0);

//wait_event_interruptible_timeout(wq, condition, timeout);  puedo usar está si el sensor se deconecta



//------------------------------- declaro las funciones de mi driver ------------------------

static int     Open_I2C_HOST (struct inode *, struct file *);
static int     Close_I2C_HOST(struct inode *, struct file *);
static ssize_t Read_I2C_HOST (struct file *, char __user *, size_t , loff_t *);
static ssize_t Write_I2C_HOST(struct file *, const char *, size_t , loff_t *);


irqreturn_t Handler_I2C_HOST_Isr(int irq, void *dev_id, struct pt_regs *regs); 
static long int Ioctl_I2C_HOST(struct file *, unsigned int , unsigned long );
//-------------------------------------------------------------------------------------------




/*----------------------------------------------------------------------------------------  
-    Declaro las operaciones de mi driver                                                - 
-----------------------------------------------------------------------------------------*/
static struct file_operations I2C_HOST_Op = {

    .owner    = THIS_MODULE,
    .open     = Open_I2C_HOST,                     /* hago open /dev*/
    .release  = Close_I2C_HOST, 
    .read     = Read_I2C_HOST, 
    .write    = Write_I2C_HOST, 
    .unlocked_ioctl = Ioctl_I2C_HOST, 
};


static const struct of_device_id i2c_TD3_of_match[] = {
   {.compatible = "ti_TD3,omap4-i2c"},  /*cargo el string del campo compatible del DT*/
   { },
};



/*----------------------------------------------------------------------------------------  
-    Declaro struc platform_driver                                                       - 
-----------------------------------------------------------------------------------------*/


static struct platform_driver Platform_I2C_HOST = {
	.probe  = I2C_HOST_Probe,                                                                                                            
    .remove = I2C_HOST_Remove,  
	.driver = {
                  .name = "I2C_HOST_TEMP",
                  .owner = THIS_MODULE,
                  .of_match_table = of_match_ptr(i2c_TD3_of_match), 
              },
}; 


static dev_t NumI2C;                   /* Declaro variable para numero mayor y menor*/
static struct cdev *I2C_HOST_Cdev ;     
static struct class *I2C_HOST_Class ;
static struct device *I2C_HOST_Device ;
static unsigned int PriMenor=0; /*primer menor */


/*-----------------------------------------------------------------------------------------
-                   Declaro los puntero para el remapeo de memoria                        -
-------------------------------------------------------------------------------------------*/

static void *ptr_Clk = NULL;    /* Puntero para la configuración del Clock*/
static void *ptr_pin = NULL;    /* puntero para la configuración del pinmux de los pad*/
static void *ptr_i2c = NULL;    /* puntero para los registros del I2C */


/*-----------------------------------------------------------------------------------------
-                            Variables Globales                                           -
-------------------------------------------------------------------------------------------*/


static char *Buff_tx = NULL;   /* puntero para kmalloc trasmision */
static char *Buff_rx = NULL;   /* puntero para kmalloc recpción   */
static int I2C_Irq=0;
static int tx_size = 0;        /* cantidad maxima a copiar en Buff_tx */
static int tx_indice = 0;      /* indice para Buff_Tx */
static int rx_size = 0;        /* cantidad maxima a copiar en Buff_rx */        
static int rx_indice = 0;      /* indice para Buff_rx */   




static int __init I2C_Driver_init(void)    
{
     
   
    if((alloc_chrdev_region(&NumI2C, PriMenor, MenorCant, CHARDEV_NOMBRE)) < 0 )   //I2C_HOST_TEMP es el nombre en el /dev donde hago open
    {
        printk(KERN_ERR "TEMP_DRIVER: error alloc_cherdev_region \n");
        
        return -1;
    }
    
    /* Creo la clase*/

    I2C_HOST_Class = class_create(THIS_MODULE, CLASS_NOMBRE);
    
    if(IS_ERR(I2C_HOST_Class))  /* Me fijo que a tipo error corresponde el NULL*/
    {
        printk(KERN_ERR "TEMP_DRIVER: ERROR al crear la clase\n ");
        unregister_chrdev_region(NumI2C, MenorCant); 
        return  PTR_ERR(I2C_HOST_Class);  /*Con PTR_ERR retorno el codigo de error correspondiente */
    }   
    
    I2C_HOST_Cdev = cdev_alloc();             /* resevo memoria para las estructura Cdev en kernel*/
    I2C_HOST_Cdev->ops = &I2C_HOST_Op;        /* asocio las operaciones de mi Driver */
    I2C_HOST_Cdev->owner = THIS_MODULE;

    printk(KERN_ERR "TEMP_DRIVER: añadiendo cdev.........\n");

    if(cdev_add(I2C_HOST_Cdev, NumI2C, MenorCant) < 0)
    {
        printk(KERN_ERR "TEMP_DRIVER: error añadiendo cdev. \n");
        unregister_chrdev_region(NumI2C, MenorCant); 
        class_destroy(I2C_HOST_Class);
        return -1;
    }
    
    printk(KERN_ERR "TEMP_DRIVER: cdev añadido.\n");

    /* Creo el Device Driver */
    printk(KERN_ERR "TEMP_DRIVER: creando device ......... \n");
    I2C_HOST_Device = device_create(I2C_HOST_Class,NULL,NumI2C,NULL,DEVICE_NOMBRE);  /*    "/dev/DEVICE_NOMBRE"             */
    
    if(IS_ERR(I2C_HOST_Device))
    {
        printk(KERN_ERR "TEMP_DRIVER: error al crear el device driver  \n");
        cdev_del(I2C_HOST_Cdev);
        unregister_chrdev_region(NumI2C, MenorCant); 
        class_destroy(I2C_HOST_Class);
        return PTR_ERR(I2C_HOST_Device);

    }
     
    printk(KERN_ERR "TEMP_DRIVER: device creado.\n");

    printk(KERN_ERR "TEMP_DRIVER: Registrando platform driver \n");
   
    platform_driver_register(&Platform_I2C_HOST);

    printk(KERN_ERR "TEMP_DRIVER: Controlador platform registrado\n"); 
  
    return 0;

}

static void __exit I2C_Driver_exit(void)
{
    
    cdev_del(I2C_HOST_Cdev);
    unregister_chrdev_region(NumI2C, MenorCant); 
    device_destroy(I2C_HOST_Class, NumI2C);
    class_destroy(I2C_HOST_Class);

    printk(KERN_ERR "TEMP_DRIVER: Desregistrando controlador driver \n");  
    platform_driver_unregister(&Platform_I2C_HOST);
    printk(KERN_ERR "TEMP_DRIVER: Controlador platform desregistrado\n");

}


MODULE_DEVICE_TABLE(of, i2c_TD3_of_match);




static int I2C_HOST_Probe(struct platform_device *pdev)   /*el Kernel me pasa un puntero a struct platform_device*/
{
   
    unsigned int reg_Ctrl_Clk=0, reg_Pin_Sda=0, aux_Ctrl=0, aux_sda=0 ,aux_clk=0;
    unsigned int reg_Pin_Clk=0;  
   

    /* Configuración habilitación de Clock I2C */ 
    /* Reservo el espacio  Clock Module Peripheral Register */

    ptr_Clk = ioremap(CM_PER_BASE, CM_PER_SIZE);              /* Pág 179 CM_PER (clock module peripheral register ) */ 
    if (IS_ERR(ptr_Clk))
    {
        printk(KERN_ERR "TEMP_DRIVER: Error al asignar ioremap para modulo controlador de periferico I2C\n");
        
        cdev_del(I2C_HOST_Cdev);
        unregister_chrdev_region(NumI2C, MenorCant); 
        device_destroy(I2C_HOST_Class, NumI2C);
        class_destroy(I2C_HOST_Class);
        platform_driver_unregister(&Platform_I2C_HOST);

        return PTR_ERR(ptr_Clk);

    } 

    printk(KERN_ERR "TEMP_DRIVER: leyendo el registro de clock module I2C..... \n");
    reg_Ctrl_Clk = ioread32(ptr_Clk + CM_PER_I2C2_OFFSET);  /* CM_PER_I2C2_CLKCTRL Register (offset = 44h)   Pág 1267 sitara*/
    printk(KERN_ERR "TEMP_DRIVER: valor del registro clock modulo I2C  0x%x \n",reg_Ctrl_Clk);
    reg_Ctrl_Clk &= MASC_I2C2_CLKTRL;
    
    reg_Ctrl_Clk |= 0x2;                                      /* Habilito el clock para el modulo I2C */
    iowrite32(reg_Ctrl_Clk, ptr_Clk + CM_PER_I2C2_OFFSET);    /* configuro el registro */ 
    reg_Ctrl_Clk = ioread32(ptr_Clk + CM_PER_I2C2_OFFSET); 
    aux_Ctrl = (reg_Ctrl_Clk & (~MASC_I2C2_CLKTRL));


    while(aux_Ctrl !=2 )                                      /*me quedo acá hasta que se haya escrito el 2 */
    {
       msleep (1);                                            /*espero 1ms por los cambios*/
       reg_Ctrl_Clk = ioread32(ptr_Clk + CM_PER_I2C2_OFFSET); 
       aux_Ctrl = (reg_Ctrl_Clk & (~MASC_I2C2_CLKTRL));       /* lee los dos primeros bit del registro*/

    } 
    
    reg_Ctrl_Clk = ioread32(ptr_Clk + CM_PER_I2C2_OFFSET);
    printk(KERN_ERR "TEMP_DRIVER: valor del registro clock modulo I2C  0x%x \n",reg_Ctrl_Clk);  
    /* Configuración del PinMux de SDA y CLK */
    /* Mapeo y reservo el espacio en memoria  */

    ptr_pin = ioremap(CM_BASE, CM_SIZE);
    if (IS_ERR(ptr_pin))
    {
        printk(KERN_ERR "TEMP_DRIVER: Error al asignar ioremap para  Control Module Registers  I2C\n");
        // desmontar todo
        iounmap(ptr_Clk);
        cdev_del(I2C_HOST_Cdev);
        unregister_chrdev_region(NumI2C, MenorCant); 
        device_destroy(I2C_HOST_Class, NumI2C);
        class_destroy(I2C_HOST_Class);
        platform_driver_unregister(&Platform_I2C_HOST);
        return PTR_ERR(ptr_pin);

    } 
    

    reg_Pin_Sda = ioread32(ptr_pin + PIN_I2C_SDA_OFFSET);                           /*leo el registro*/
    printk(KERN_ERR "TEMP_DRIVER: valor del registro pin SDA antes de configurar %x \n",reg_Pin_Sda);
    reg_Pin_Sda &= CM_MASK;                                                        /* enmascaro los bits a modificar sin tocar los otros*/ 
    
    reg_Pin_Sda |= SDA_PAD_CONFIG;                                                 /*configuro el pad*/
    iowrite32(reg_Pin_Sda, ptr_pin + PIN_I2C_SDA_OFFSET);
    reg_Pin_Sda = ioread32(ptr_pin + PIN_I2C_SDA_OFFSET);
    aux_sda = (reg_Pin_Sda  & (~CM_MASK));   
   
     while(aux_sda != SDA_PAD_CONFIG )                                             /*me quedo acá hasta que se produzcan los cambios */
    {
       msleep (1);
       reg_Pin_Sda = ioread32(ptr_pin + PIN_I2C_SDA_OFFSET);
       aux_sda = (reg_Pin_Sda  & (~CM_MASK));                                      /* leo los bits modificados*/

    } 

    printk(KERN_ERR "TEMP_DRIVER: valor del registro reg_Pin_Sda configurado 0x%x \n",reg_Pin_Sda); 

    reg_Pin_Clk = ioread32(ptr_pin +  PIN_I2C_CLK_OFFSET);
    printk(KERN_ERR "TEMP_DRIVER: valor del registro reg_Pin_Clk 0x%x \n",reg_Pin_Clk);
    reg_Pin_Clk &= CM_MASK;                                                        /* enmascaro los bits a modificar sin tocar los otros*/ 
    
    reg_Pin_Clk |= CLK_PAD_CONFIG;                                                 /*configuro el pad*/
    iowrite32(reg_Pin_Clk, ptr_pin + PIN_I2C_CLK_OFFSET);  
    reg_Pin_Clk = ioread32(ptr_pin +  PIN_I2C_CLK_OFFSET);
    aux_clk = (reg_Pin_Clk  & (~CM_MASK)); 
    
    while(aux_clk != CLK_PAD_CONFIG )                                             /*me quedo acá hasta que se produzcan los cambios (do{}while)*/
    {   
       msleep (1);
       reg_Pin_Clk = ioread32(ptr_pin + PIN_I2C_CLK_OFFSET);
       aux_clk = (reg_Pin_Clk  & (~CM_MASK));                                      /* leo los bits modificados*/

    } 
    
    printk(KERN_ERR "TEMP_DRIVER: valor del registro reg_Pin_Clk configurado 0x%x \n",reg_Pin_Clk);

    
    
    ptr_i2c = ioremap(DIR_BASE_I2C2_REG, SIZE_I2C2);                               /* Mapeo el espacio de memoria para los registros I2C */
    
    if (IS_ERR(ptr_i2c))
    {
        printk(KERN_ERR "TEMP_DRIVER: Error al asignar ioremap para  I2C Register \n");
        // desmontar todo
        iounmap(ptr_Clk);
        iounmap(ptr_pin);
        cdev_del(I2C_HOST_Cdev);
        unregister_chrdev_region(NumI2C, MenorCant); 
        device_destroy(I2C_HOST_Class, NumI2C);
        class_destroy(I2C_HOST_Class);
        platform_driver_unregister(&Platform_I2C_HOST);
        return PTR_ERR(ptr_i2c);

    } 

    /* Pedido de una linea de interrupción */ 

    I2C_Irq = platform_get_irq(pdev,0);

    if(I2C_Irq == 0 ) /* si es 0, hubo un error en la asignación de la linea */ 
    {

        //Desmontar todo
        iounmap(ptr_Clk);
        iounmap(ptr_pin);
        iounmap(ptr_i2c);
        cdev_del(I2C_HOST_Cdev);
        unregister_chrdev_region(NumI2C, MenorCant); 
        device_destroy(I2C_HOST_Class, NumI2C);
        class_destroy(I2C_HOST_Class);
        platform_driver_unregister(&Platform_I2C_HOST);
        return EBUSY;

    } 

    printk(KERN_ERR "TEMP_DRIVER: Numero de IRQ disponible:  %d\n", I2C_Irq);

    if(request_irq(I2C_Irq, (irq_handler_t)Handler_I2C_HOST_Isr, IRQF_TRIGGER_RISING, pdev->name,NULL) )   /* si hay un error retorna != 0*/
    {
        printk(KERN_ERR "TEMP_DRIVER: no se puede registrar la IRQ ");
        //desmontar todo
        iounmap(ptr_Clk);
        iounmap(ptr_pin);
        iounmap(ptr_i2c);
        cdev_del(I2C_HOST_Cdev);
        unregister_chrdev_region(NumI2C, MenorCant); 
        device_destroy(I2C_HOST_Class, NumI2C);
        class_destroy(I2C_HOST_Class);
        platform_driver_unregister( &Platform_I2C_HOST);
        return EBUSY;
    }  
     
    return 0;
}

/* Handler de interrupción  */
irqreturn_t Handler_I2C_HOST_Isr(int irq, void *dev_id, struct pt_regs *regs)
{

	unsigned int aux = 0;
    uint32_t data =0,conf=0;
	aux = ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS);
    aux &= (~MASC_I2C_IRQSTATUS);

    printk(KERN_ERR "TEMP_DRIVER: Handler: contenido registro dcount %x ",ioread32( ptr_i2c + OFFSET_I2C_CNT ));   // cantidad a transmitir 

 	printk(KERN_ERR "TEMP_DRIVER: Handler: contenido registro I2CIRQSTATUS %x ",aux ); 
       
    iowrite32(0xffff, ptr_i2c + OFFSET_I2C_IRQSTATUS);  //apago las interrupciones según el manual

	if( aux&XRDY )   // transmit data ready
	{
	    if( tx_size == tx_indice )
        {
	        /*Desactivo la IRQ de TX*/
		
            aux = 0;
		    aux = ioread32 (ptr_i2c + OFFSET_I2C_IRQENABLE_CLR );
            act_bit(aux, XRDY_CLR ); 
            iowrite32 (aux, ptr_i2c + OFFSET_I2C_IRQENABLE_CLR ); /* apago la irq tx */
		    aux = 0;
		    aux = ioread32( ptr_i2c + I2C_CON_OFFSET );
                
		    printk(KERN_ALERT "TEMP_DRIVER: Dentro de la irq TX, valor del I2C_CON = %x\n", aux);
		    
            printk(KERN_ALERT "TEMP DRIVER: Dentro de la irq TX, mando STOP\n");
            conf = 0;
            conf =  ioread32( ptr_i2c + I2C_CON_OFFSET ); 
            act_bit(conf, STOP);
            iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );
            
            /*Despierto el proceso*/
		  
            cond_wqI2C_TX = 1;
		    wake_up_interruptible(&wqI2C_TX);
		    goto end_irq;
        }
       
        /*Saco el nuevo dato por el i2c*/
        data = ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS);
	    data &= 0xffffff00;
        data |= (uint32_t)Buff_tx[tx_indice];
        printk(KERN_ALERT "TEMP_DRIVER: Dentro de la irq TX, valor data = %x\n", data);
        //iowrite32 ( Buff_tx[tx_indice] , ptr_i2c + OFFSET_I2C_DATA );
        iowrite32 ( data , ptr_i2c + OFFSET_I2C_DATA );
  
	    printk(KERN_ALERT "TEMP_DRIVER: Dentro de la irq TX, valor del I2C_CON = %x\n", aux);
     
        if( tx_size == 1 )
        {
            aux = 0;
		    aux = ioread32 (ptr_i2c + OFFSET_I2C_IRQENABLE_CLR );
            act_bit(aux, XRDY_CLR ); 
            iowrite32 (aux, ptr_i2c + OFFSET_I2C_IRQENABLE_CLR ); /* apago la irq tx */
		    aux = 0;
		    aux = ioread32( ptr_i2c + I2C_CON_OFFSET );
                
		    printk(KERN_ALERT "TEMP_DRIVER: Dentro de la irq TX, valor del I2C_CON = %x\n", aux);
		    
            printk(KERN_ALERT "TEMP DRIVER: Dentro de la irq TX, mando STOP\n");
            conf = 0;
            conf =  ioread32( ptr_i2c + I2C_CON_OFFSET ); 
            act_bit(conf, STOP);
            iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );
              
            /*Despierto el proceso*/
            cond_wqI2C_TX = 1;
		    wake_up_interruptible(&wqI2C_TX);
            //goto end_irq;
        }
	tx_indice++;
	goto end_irq;
   
	}

	if(aux&RRDY)   // receive data ready                                                   
    {                                        
        /*recibo el nuevo dato por el i2c*/

	    //Buff_rx[rx_indice] = (uint8_t)ioread32(ptr_i2c + OFFSET_I2C_DATA ); // castear
	    Buff_rx[rx_indice] = ioread32(ptr_i2c + OFFSET_I2C_DATA ); // castear

        aux = Buff_rx [rx_indice];
	    printk(KERN_ALERT "TEMP_DRIVER: _IRQ RX_:, valor = 0x%x\n", aux);
	    aux =  ioread32 (ptr_i2c + I2C_CON_OFFSET);
	    printk(KERN_ALERT "TEMP_DRIVER: _IRQ RX_, valor del I2C_CON = %x\n", aux);  
	    
        /*Me fijo si llegue al final del buffer*/
	    
        if( rx_size == (rx_indice +1 ))
	    {
		    /*Desactivo la IRQ de RX*/

            aux = 0;
            aux = ioread32 (ptr_i2c + OFFSET_I2C_IRQENABLE_CLR );
            act_bit(aux, RRDY_CLR ); 
            iowrite32 (aux, ptr_i2c + OFFSET_I2C_IRQENABLE_CLR );
            
		    printk(KERN_ALERT "TEMP_DRIVER: Dentro de la irq RX, valor del I2C_CON = %x\n", aux);
		    //printk(KERN_ALERT "TEMP DRIVER: Dentro de la irq RX Mando STOP\n");
            printk(KERN_ALERT "TEMP_DRIVER: Dentro de la irq RX contenido registro dcount %x ",ioread32( ptr_i2c + OFFSET_I2C_CNT ));
            
            conf=0;
            conf=ioread32(ptr_i2c + I2C_CON_OFFSET );    
            act_bit(conf, STOP);
            iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );	
            
            /*Despierto la wait_queue*/
		  
            cond_wqI2C_RX = 1;
		    wake_up_interruptible(&wqI2C_RX);
            
	   }
        printk(KERN_ERR "TEMP_DRIVER: _IRQ RX_ contenido registro dcount %x ",ioread32( ptr_i2c + OFFSET_I2C_CNT )); 
	    rx_indice++;
	    goto end_irq;
    
    } 
              

end_irq:

return IRQ_HANDLED;
}


static int I2C_HOST_Remove(struct platform_device *pdev)
{
  
    printk(KERN_ERR "TEMP_DRIVER: Desmontando IRQ... \n");
    free_irq(I2C_Irq, NULL);           
    
    printk(KERN_ERR "TEMP_DRIVER: Desmontando mapa de memoria... \n");
    iounmap(ptr_Clk); 
    iounmap(ptr_pin);
    iounmap(ptr_i2c);

    printk(KERN_ERR "TEMP_DRIVER: Todos los elementos desmontados \n");

    return 0;
}




/*  a partir de acá metodos para open,close, reas, write.ioctl          */




int Open_I2C_HOST(struct inode *i2c_ino, struct file *P)
{
    uint32_t aux=0, i=0;
    
    aux=ioread32(ptr_i2c + I2C_CON_OFFSET);
    pass_bit(aux,I2C_CON_EN);                       /*  deshabilito el modulo, deberia estar en 0 por default */
    iowrite32( aux,ptr_i2c + I2C_CON_OFFSET);       /* escribo el bit de reset */
 
    aux=ioread32(ptr_i2c + I2C_CON_OFFSET);  
    printk(KERN_ERR "TEMP_DRIVER: registro de control I2C Deshabilitado bit 15 = 0 %x\n", aux);
    
    /* configuro el prescaler */

    aux=0;
    aux=ioread32(ptr_i2c + I2C_PSC_OFFSET);
    aux&=MASC_PSC;
    aux|= I2C_PSC;
    iowrite32(aux,ptr_i2c + I2C_PSC_OFFSET);
    
    /* comprobar modificación*/
    
    while(!(ioread32(ptr_i2c + I2C_PSC_OFFSET)&I2C_PSC))
        msleep(1);
    
    printk(KERN_ERR "TEMP_DRIVER: valor configuración prescaler %x\n", ioread32(ptr_i2c + I2C_PSC_OFFSET));
    
    /* escribo SCLL y SCLH */
    
    aux=0;
    aux=ioread32(ptr_i2c + I2C_SCLL_OFFSET);   
    aux&=MASC_I2C_SCLL;
    aux|=I2C_SCLL;
    iowrite32(aux,ptr_i2c + I2C_SCLL_OFFSET);

    while(!(ioread32(ptr_i2c + I2C_SCLL_OFFSET)&I2C_SCLL))       //comprobar modificación
        msleep(1);

    printk(KERN_ERR "TEMP_DRIVER: valor configuración SCLL %x\n", ioread32(ptr_i2c + I2C_SCLL_OFFSET));
   
    aux=0;
    aux=ioread32(ptr_i2c + I2C_SCLH_OFFSET);   
    aux&=MASC_I2C_SCLH;
    aux|=I2C_SCLH;
    iowrite32(aux,ptr_i2c + I2C_SCLH_OFFSET);

    while(!(ioread32(ptr_i2c + I2C_SCLH_OFFSET)&I2C_SCLH))      //comprobar modificación
        msleep(1);

    printk(KERN_ERR "TEMP_DRIVER: valor configuración SCLH %x\n", ioread32(ptr_i2c + I2C_SCLH_OFFSET));


    /* comprobar modificación*/
    aux=0;  
    aux=ioread32(ptr_i2c + I2C_OA_OFFSET);   
    aux&=MASC_I2C_OA;
    aux|=I2C_OA;         /* cargo la dirección de master */
    iowrite32(aux,ptr_i2c + I2C_OA_OFFSET);
   

    aux=0;  
    aux=ioread32(ptr_i2c + I2C_SA_OFFSET);   
    aux&=MASC_I2C_SA;                      
    aux|=I2C_SA;         /* cargo la dirección de slave */
    iowrite32(aux,ptr_i2c + I2C_SA_OFFSET);

    while(!(ioread32(ptr_i2c + I2C_SA_OFFSET)&I2C_SA))      //comprobar modificación
        msleep(1);

    printk(KERN_ERR "TEMP_DRIVER: valor slave address 0x%x.\n",ioread32(ptr_i2c + I2C_SA_OFFSET));

    aux=0;  
    aux=ioread32(ptr_i2c + OFFSET_I2C_SYSC);   
    aux&=MASC_I2C_SYSC;                      
    aux|=0x308;         /* cargo la dirección de slave */
    iowrite32(aux,ptr_i2c + OFFSET_I2C_SYSC);

    //comprobar modificación
        msleep(20);

    printk(KERN_ERR "TEMP_DRIVER: valor registro sysc 0x%x.\n",ioread32(ptr_i2c + OFFSET_I2C_SYSC));

    aux=0; 
    aux=ioread32(ptr_i2c + I2C_CON_OFFSET);
    act_bit(aux,I2C_CON_EN);                       /*  habilito el modulo ya configurado */
    iowrite32( aux,ptr_i2c + I2C_CON_OFFSET);       /* escribo el bit de reset */  
 
    //printk(KERN_ERR "TEMP_DRIVER: valor slave address 0x%x.\n",ioread32(ptr_i2c + I2C_SA_OFFSET));

    if((Buff_tx = (char * )kmalloc(MEM_SIZE , GFP_KERNEL)) == 0)
    {
        printk(KERN_ERR "TEMP_DRIVER: Falló al asignar memoria de Kernel TX\"Kmalloc\".\n");
        return -1;
    }
    
    printk(KERN_ERR "TEMP_DRIVER: Memoria de Kernel TX obtenida...!!!\n");
    
    if ((Buff_rx = (char *)kmalloc(MEM_SIZE, GFP_KERNEL)) == 0)
    {
        printk(KERN_ERR "TEMP_DRIVER: falló al asignar memoria de Kernel RX\"kmalloc\".\n");
        return -1;         
    }
    
    printk(KERN_ERR "TEMP_DRIVER: Memoriade kernel RX obtenida....!!!\n");

    coeficientes(&tempCoef);    //obtengo los coeficientes del sensor 
    
    printk(KERN_ERR "TEMP_DRIVER: temperaturar RAW %ld.\n",t_raw);

    return 0;

}


static int Close_I2C_HOST(struct inode *i, struct file *f)  
{

    kfree(Buff_rx);
    kfree(Buff_tx);

    return 0;
}

static ssize_t Read_I2C_HOST(struct file *P, char __user *Dato_User, size_t cantidad, loff_t *offp)
{
    int aux=0, i=0;
    unsigned int conf=0;
    double t_comp;
    char data[7] = {0} ;
    char coef_data[21]={0}, *co;
    printk(KERN_ERR "TEMP_DRIVER: _Read_  dentro de read... \n");
    //unsigned long copy_to_user(void __user *to, const void *from, unsigned long count);  //retorna el numero de uint8_t s que no se pudo copiar
    if(cantidad > MEM_SIZE)
    {
    	printk(KERN_ERR "TEMP_DRIVER: Error no es posible procesar la cantidad pedida \n");
        return -ENOMEM;
    }
    
    

   
    // Copio la información a USER espace 
    /* dentro de las funciones tengo las waitqueue de tx y rx que suspenden el proceso */

    if(ioctlMode == TEMP_MED)
    {

        t_raw = get_RawT();
        enToStr(t_raw, data, 6);

        printk(KERN_ALERT "TEMP_DRIVER: _read_  valor cantidad %ld\n",cantidad);
        printk(KERN_ALERT "TEMP_DRIVER: _read_  tamaño de t_raw %ld\n",sizeof(t_raw));
        printk(KERN_ALERT "TEMP_DRIVER: _read_  tamaño de t_raw %ld\n",t_raw);
    
        if ((aux = (__copy_to_user(Dato_User ,  data , cantidad))) < 0)
        {
	        printk(KERN_ALERT "TEMP_DRIVER: Error en la copia del read\n");
	        return -ENOMEM;
        }
    
        return cantidad;

    }

    co = coef_data;

    if(ioctlMode == SENS_COEF)
    {

        

        enToStr(tempCoef.c1, co, 6);
        enToStr(tempCoef.c2, co + 7, 6); 
        enToStr(tempCoef.c3, co + 14, 6);    

        printk(KERN_ALERT "TEMP_DRIVER: _read_  coeficiente c1 %d\n",tempCoef.c1);
        printk(KERN_ALERT "TEMP_DRIVER: _read_  coeficiente c2 %d\n",tempCoef.c2);
        printk(KERN_ALERT "TEMP_DRIVER: _read_  coeficiente c3 %d\n",tempCoef.c3);

        if ((aux = (__copy_to_user(Dato_User ,  coef_data , 21))) < 0)
        {
	        printk(KERN_ALERT "TEMP_DRIVER: Error en la copia del read\n");
	        return -ENOMEM;
        }
        return cantidad;
    }




}




static ssize_t Write_I2C_HOST(struct file *P, const char __user *Dato_User, size_t cantidad, loff_t *offp)
{
   
   unsigned int conf=0, aux=0,i=0;
    modeForce();
   // unsigned long copy_from_user(void *to, const void __user *from, unsigned long count); //retorna el numero de uint8_t s que no se pudo copiar
    if(cantidad > MEM_SIZE)                                                                          
    {                                                                                                       
        printk(KERN_ERR "TEMP_DRIVER: Error no es posible procesar la cantidad pedida \n");          
        return -ENOMEM;                                                                              
    }            

    /* Pido memoria para el buffer*/

    if ( (Buff_tx = (char *)  kmalloc ( cantidad , GFP_KERNEL )) == NULL)
    {
        printk(KERN_ALERT "TEMP_DRIVER: no hay memoria disponible el buffer de tx\n");
	return -ENOMEM;
    }

    /* Copio los datos a enviar a la memoria de kernel*/
    if ((aux = (__copy_from_user(Buff_tx, Dato_User , cantidad))) < 0)
    {
	printk(KERN_ALERT "TEMP_DRIVER: Error en la copia del write\n");
	return -ENOMEM;
    }
    printk(KERN_ALERT "TEMP_DRIVER: _Write_ valor registro I2C_CON_EN antes de configurar 0x%x\n",ioread32(ptr_i2c + I2C_CON_OFFSET ));
    /*Espero que el bus este libre*/
    while ((ioread32 (ptr_i2c + OFFSET_I2C_IRQSTATUS_RAW) & 0x1000))
        msleep(1);

    printk(KERN_ALERT "TEMP_DRIVER: _write_ bus libre!!!\n");
    printk(KERN_ALERT "TEMP_DRIVER: _write_ bus cantidad: %d\n",cantidad);

	/*Seteo cuanto es lo que tengo que copiar*/

    for(i=0; i<cantidad; i++ )
    {
        printk(KERN_ALERT "TEMP_DRIVER: _write_ datos pasados por user space :0x%x\n", Buff_tx[i]);
    }
    
   // msleep(50);
    tx_size = cantidad;              //pongo la cantidad maxima a copiar                       
    tx_indice = 0;                   //indice para copiar a memoria                           
    cond_wqI2C_TX = 0;               //condición para dormir el proceso con la waitqueue    
   
   
    iowrite32(I2C_SA, ptr_i2c + I2C_SA_OFFSET );       // Slave address
    iowrite32(cantidad, ptr_i2c + OFFSET_I2C_CNT );   // cantidad a transmitir 
     
    
    conf = ioread32(ptr_i2c + I2C_CON_OFFSET );
    act_bit(conf, TRX);                               // Transmiter mode
    act_bit(conf, MST);                                // Master Mode
    iowrite32 (conf , ptr_i2c + I2C_CON_OFFSET );
   
    //printk(KERN_ALERT "TEMP_DRIVER: _Write_ chequeo configuración registro I2C_CON_EN 0x%x\n",ioread32(ptr_i2c + I2C_CON_OFFSET ));
    /*Activo la IRQ de TX y mando Start*/

    conf=0;
    conf = ioread32(ptr_i2c + OFFSET_I2C_IRQENABLE_SET); 
    act_bit(conf, XRDY_SET);                               
    iowrite32 (conf, ptr_i2c + OFFSET_I2C_IRQENABLE_SET );
    conf = 0;
    conf = ioread32( ptr_i2c + I2C_CON_OFFSET);
    act_bit(conf, START);                           //activo bit de Start
    iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );    //Mando Start

    printk(KERN_ALERT "TEMP_DRIVER: _Write_ chequeo configuración registro I2C_IRQENABLE_SET 0x%x\n",ioread32( ptr_i2c + OFFSET_I2C_IRQENABLE_SET ));
    
    printk(KERN_ALERT "TEMP_DRIVER: _Write_ chequeo configuración registro I2C_CON_EN 0x%x\n",ioread32(ptr_i2c + I2C_CON_OFFSET ));
    /* activo irq de transmision para debug*/

    /*wait_event_interruptible_timeout (wq_head, condition, timeout) timeout está en jiffis 1 = 10ms */

    printk(KERN_ALERT "TEMP_DRIVER: _Write_ antes de wait_event_interruptible TX\n");
    
    //if ( (aux = wait_event_interruptible_timeout (wqI2C_TX, cond_wqI2C_TX>0,500) < 0 ))
    if ( (aux = wait_event_interruptible (wqI2C_TX, cond_wqI2C_TX>0)) < 0 )
    {
	    printk(KERN_ALERT "TEMP DRIVER: _Write_ Error en waitqueue de TX\n");
	    kfree(Buff_tx);
	    return aux;
    }

    printk(KERN_ALERT "TEMP DRIVER: _Write_ saliendo de la suspensión\n");
    /*Mando stop*/
    /*
    printk(KERN_ALERT "TEMP DRIVER: _Write_ Mando STOP\n");
    conf = 0;
    conf =  ioread32( ptr_i2c + I2C_CON_OFFSET ); 
    act_bit(conf, STOP);
    iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );
    */
    
    while( ( ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS ) & 0x1000) )
        msleep(1);
    
    msleep(50);
    
    
    rx_size = cantidad;              //pongo la cantidad maxima a copiar
    rx_indice = 0;                   //indice para copiar a memoria 
    cond_wqI2C_RX = 0;               //condición para dormir el proceso con la waitqueue

    conf=0;
    conf=ioread32(ptr_i2c + I2C_CON_OFFSET );    
    act_bit(conf, MST);                          // master mode
    pass_bit(conf, TRX);                         //  receive mode
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );


    iowrite32(I2C_SA, ptr_i2c + I2C_SA_OFFSET );       // Slave address
    iowrite32(cantidad, ptr_i2c + OFFSET_I2C_CNT );   // cantidad a transmitir
    //Buff_tx[0]=0xd0; //ya lo tengo cargado desde copy_from_user
    // Activo IRQ RX
    
    conf=0;
    conf=ioread32(ptr_i2c + OFFSET_I2C_IRQENABLE_SET);
    act_bit(conf, RRDY_SET);  
    act_bit(conf, XRDY_SET);
    iowrite32(conf, ptr_i2c + OFFSET_I2C_IRQENABLE_SET ); 
    
    conf=0;
    conf = ioread32(ptr_i2c + I2C_CON_OFFSET ); 
    act_bit(conf, START);
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );

    // pongo a dormir el proceso hasta que venga una interrpción de rx
    printk(KERN_ALERT "TEMP_DRIVER: _Read_ antes de wait_event_interruptible RX\n");

    if ( (aux = wait_event_interruptible ( wqI2C_RX, cond_wqI2C_RX>0)) < 0 )
	{
	    printk(KERN_ALERT "TEMP_DRIVER: Error en waitqueue de RX\n");
	    return aux;
	} 
    printk(KERN_ALERT "TEMP DRIVER: _Read_ saliendo de la supensión\n");
    //mando la condición de stop 
    
    /*printk(KERN_ALERT "TEMP DRIVER: _Read_ Mando STOP\n");
    conf=0;
    conf=ioread32(ptr_i2c + I2C_CON_OFFSET );    
    act_bit(conf, STOP);
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );	
  */
    
    /*Libero la memoria que pedí*/
   // kfree(Buff_tx);

    
    return cantidad;

}


long int Ioctl_I2C_HOST(struct file *f, unsigned int cmd, unsigned long arg)
{
    printk(KERN_ALERT "TEMP DRIVER _IOCTL_ : dentro de ioctl!!!\n");
	if (cmd == CMD_READ ) {
		if (arg == 0)
			ioctlMode = TEMP_MED;  //leer los coeficientes de temp
		if (arg == 1)
			ioctlMode = SENS_COEF; //coeficientes
		if (arg == 2)
			ioctlMode = ARCH_CONF; //cargar archivo
		return 1;
	}


    return 0;
}



/* modeForce: Es uno de los modos de lectura del sensor, aplicarlo para cada lectura*/


static void modeForce(void)     
{
    uint32_t aux=0, conf=0;
    c_meas=(osrs_t | osrs_p | mode_meas);
    /* mando force mode */
    while( ( ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS ) & 0x1000) )
        msleep(1);

    tx_size = 2;              //pongo la cantidad maxima a copiar                       
    tx_indice = 0;   
    cond_wqI2C_TX = 0;
    iowrite32(I2C_SA, ptr_i2c + I2C_SA_OFFSET );       // Slave address
    iowrite32(2, ptr_i2c + OFFSET_I2C_CNT );   // cantidad a transmitir 
    Buff_tx[0] = 0xf4;    // registro de configuracion
    Buff_tx[1] = c_meas;  // configuro force mode
 
    conf = ioread32(ptr_i2c + I2C_CON_OFFSET );
    act_bit(conf, TRX);                               // Transmiter mode
    act_bit(conf, MST);                                // Master Mode
    iowrite32 (conf , ptr_i2c + I2C_CON_OFFSET );

    /*Activo la IRQ de TX y mando Start*/

    conf=0;
    conf = ioread32(ptr_i2c + OFFSET_I2C_IRQENABLE_SET); 
    act_bit(conf, XRDY_SET);                               
    iowrite32 (conf, ptr_i2c + OFFSET_I2C_IRQENABLE_SET );
    conf = 0;
    conf = ioread32( ptr_i2c + I2C_CON_OFFSET);
    act_bit(conf, START);                           //activo bit de Start
    iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );    //Mando Start

    if( (aux = wait_event_interruptible (wqI2C_TX, cond_wqI2C_TX>0)) < 0 )
    {
	    printk(KERN_ALERT "TEMP DRIVER: _configbmp_ Error en waitqueue de TX\n");
	    kfree(Buff_tx);
	    //return aux;
    }
    /*
    conf = 0;
    conf =  ioread32( ptr_i2c + I2C_CON_OFFSET ); 
    act_bit(conf, STOP);
    iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );
*/

}

/* obtengo los coeficientes del sensor */

static void coeficientes(bmpcoef *c)
{
   unsigned int d_t1=0, daux = 0;
   int aux = 0, d_t2 = 0, d_t3 = 0 ;
   uint32_t conf;
   unsigned char c_Temp[6];
   uint8_t comp = 0x88,z=0;
   
   
        
        
    /* WRITE*/
    while( ( ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS ) & 0x1000) )
        msleep(1);
    printk(KERN_ALERT "TEMP_DRIVER: _coef_ registro 0x%x\n",comp);
    tx_size = 1;              //pongo la cantidad maxima a copiar                       
    tx_indice = 0;                   //indice para copiar a memoria                           
    cond_wqI2C_TX = 0;               //condición para dormir el proceso con la waitqueue    
   
    iowrite32(I2C_SA, ptr_i2c + I2C_SA_OFFSET );       // Slave address
    iowrite32(1, ptr_i2c + OFFSET_I2C_CNT );   // cantidad a transmitir     
    Buff_tx[0]=comp;
    conf = ioread32(ptr_i2c + I2C_CON_OFFSET );
    act_bit(conf, TRX);                               // Transmiter mode
    act_bit(conf, MST);                                // Master Mode
    iowrite32 (conf , ptr_i2c + I2C_CON_OFFSET );

    /*Activo la IRQ de TX y mando Start*/
    
    conf=0;
    conf = ioread32(ptr_i2c + OFFSET_I2C_IRQENABLE_SET); 
    act_bit(conf, XRDY_SET);                                
    iowrite32 (conf, ptr_i2c + OFFSET_I2C_IRQENABLE_SET );
    conf = 0;
    conf = ioread32( ptr_i2c + I2C_CON_OFFSET);
    act_bit(conf, START);                           //activo bit de Start
    iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );    //Mando Start


    printk(KERN_ALERT "TEMP_DRIVER: _coef_ antes de wait_event_interruptible TX\n");
    
    if ( (aux = wait_event_interruptible (wqI2C_TX, cond_wqI2C_TX>0)) < 0 )
    {
	    printk(KERN_ALERT "TEMP DRIVER: _coef_ Error en waitqueue de TX\n");
	    kfree(Buff_tx);
	    //return aux;
    }

    printk(KERN_ALERT "TEMP DRIVER: _coef_ saliendo de la suspensión\n");
    /*Mando stop*/
    /*
    printk(KERN_ALERT "TEMP DRIVER: _coef_ Mando STOP\n");
    conf = 0;
    conf =  ioread32( ptr_i2c + I2C_CON_OFFSET ); 
    act_bit(conf, STOP);
    iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );
      */
    /* READ */
        
    while( ( ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS ) & 0x1000) )
        msleep(1);

    msleep(50);
    rx_size =6;                      //pongo la cantidad maxima a copiar
    rx_indice = 0;                   //indice para copiar a memoria 
    cond_wqI2C_RX = 0;               //condición para dormir el proceso con la waitqueue

    conf=0;
    conf=ioread32(ptr_i2c + I2C_CON_OFFSET );    
    act_bit(conf, MST);                          // master mode
    pass_bit(conf, TRX);                         //  receive mode
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );

    iowrite32(I2C_SA, ptr_i2c + I2C_SA_OFFSET );   // Slave address
    iowrite32(6, ptr_i2c + OFFSET_I2C_CNT );       // cantidad a transmitir
    Buff_tx[0]=comp;
    
    /* Activo IRQ RX*/
    
    conf=0;
    conf=ioread32(ptr_i2c + OFFSET_I2C_IRQENABLE_SET);
    act_bit(conf, RRDY_SET);  
    act_bit(conf, XRDY_SET);
    iowrite32(conf, ptr_i2c + OFFSET_I2C_IRQENABLE_SET ); 
    
    conf=0;
    conf=ioread32(ptr_i2c + I2C_CON_OFFSET ); 
    act_bit(conf, START);
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );

    /* pongo a dormir el proceso hasta que venga una interrpción de rx*/
    printk(KERN_ALERT "TEMP_DRIVER: _coef_ antes de wait_event_interruptible RX\n");
    if ( (aux = wait_event_interruptible ( wqI2C_RX, cond_wqI2C_RX>0)) < 0 )
	{
	    printk(KERN_ALERT "TEMP_DRIVER: Error en waitqueue de RX\n");
	    //return aux;
	} 
    printk(KERN_ALERT "TEMP DRIVER: _coef_ saliendo de la supensión\n");
    /*mando la condición de stop */
    /*
    printk(KERN_ALERT "TEMP DRIVER: _coef_ Mando STOP\n");
    conf=0;
    conf=ioread32(ptr_i2c + I2C_CON_OFFSET );    
    act_bit(conf, STOP);
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );	
    */
    //obtengo los coeficientes y los guardo en c_temp

    for(z=0; z<6; z++)
    {

        c_Temp[z] = Buff_rx[z];
 
    }

    d_t1 = c_Temp[0];
    daux = c_Temp[1];
    daux = daux<<8;
    d_t1 = d_t1 | daux;
    d_t2 = c_Temp[3];
    d_t2 = d_t2<<8;
    d_t2 = d_t2 | c_Temp[2];  
    d_t3 = c_Temp[5];
    d_t3 = d_t3<<8;
    d_t3 = d_t3 | c_Temp[4];
    c->c1 = d_t1;
    c->c2 = d_t2;
    c->c3 = d_t3;

    printk(KERN_ALERT "TEMP DRIVER: _coef_ imprimiendo coeficientes \n");
    /*
    for(z=0; z<6; z++)
    {

    printk(KERN_ALERT "TEMP DRIVER: _coef_ %d, %x \n",z, c_Temp[z]);

    }
    */
    printk(KERN_ALERT "TEMP DRIVER: _coef_ coeficiente d_t1 %d \n",d_t1);
    printk(KERN_ALERT "TEMP DRIVER: _coef_ coeficiente d_t2 %d \n",d_t2);
    printk(KERN_ALERT "TEMP DRIVER: _coef_ coeficiente d_t3 %d \n",d_t3);
     
    
}



long int get_RawT(void)
{
    long int raw=0;
    char Temp[3];
    int a, aux = 0;
    uint32_t conf=0;
    modeForce();
     /* WRITE*/
    while( ( ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS ) & 0x1000) )
        msleep(1);
    
    tx_size = 1;              //pongo la cantidad maxima a copiar                       
    tx_indice = 0;                   //indice para copiar a memoria                           
    cond_wqI2C_TX = 0;               //condición para dormir el proceso con la waitqueue    
   
    iowrite32(I2C_SA, ptr_i2c + I2C_SA_OFFSET );       // Slave address
    iowrite32(1, ptr_i2c + OFFSET_I2C_CNT );   // cantidad a transmitir     
    Buff_tx[0]=0xfa;
    conf = ioread32(ptr_i2c + I2C_CON_OFFSET );
    act_bit(conf, TRX);                               // Transmiter mode
    act_bit(conf, MST);                                // Master Mode
    iowrite32 (conf , ptr_i2c + I2C_CON_OFFSET );

    /*Activo la IRQ de TX y mando Start*/
    
    conf=0;
    conf = ioread32(ptr_i2c + OFFSET_I2C_IRQENABLE_SET); 
    act_bit(conf, XRDY_SET);                                
    iowrite32 (conf, ptr_i2c + OFFSET_I2C_IRQENABLE_SET );
    conf = 0;
    conf = ioread32( ptr_i2c + I2C_CON_OFFSET);
    act_bit(conf, START);                           //activo bit de Start
    iowrite32 (conf, ptr_i2c + I2C_CON_OFFSET );    //Mando Start


    printk(KERN_ALERT "TEMP_DRIVER: _tempRaw_ antes de wait_event_interruptible TX\n");
    
    if ( (aux = wait_event_interruptible (wqI2C_TX, cond_wqI2C_TX>0)) < 0 )
    {
	    printk(KERN_ALERT "TEMP DRIVER: _tempRaw_ Error en waitqueue de TX\n");
	    kfree(Buff_tx);
	    //return aux;
    }

    printk(KERN_ALERT "TEMP DRIVER: _tempRaw_ saliendo de la suspensión\n");
   
    /* READ */
        
    while( ( ioread32( ptr_i2c + OFFSET_I2C_IRQSTATUS ) & 0x1000) )
        msleep(1);

    msleep(50);
    rx_size =3;                      //pongo la cantidad maxima a copiar
    rx_indice = 0;                   //indice para copiar a memoria 
    cond_wqI2C_RX = 0;               //condición para dormir el proceso con la waitqueue

    conf=0;
    conf=ioread32(ptr_i2c + I2C_CON_OFFSET );    
    act_bit(conf, MST);                          // master mode
    pass_bit(conf, TRX);                         //  receive mode
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );

    iowrite32(I2C_SA, ptr_i2c + I2C_SA_OFFSET );   // Slave address
    iowrite32(3, ptr_i2c + OFFSET_I2C_CNT );       // cantidad a transmitir
    Buff_tx[0]=0xfa;
    
    /* Activo IRQ RX*/
    
    conf=0;
    conf=ioread32(ptr_i2c + OFFSET_I2C_IRQENABLE_SET);
    act_bit(conf, RRDY_SET);  
    act_bit(conf, XRDY_SET);
    iowrite32(conf, ptr_i2c + OFFSET_I2C_IRQENABLE_SET ); 
    
    conf=0;
    conf=ioread32(ptr_i2c + I2C_CON_OFFSET ); 
    act_bit(conf, START);
    iowrite32(conf, ptr_i2c + I2C_CON_OFFSET );

    /* pongo a dormir el proceso hasta que venga una interrpción de rx*/
    printk(KERN_ALERT "TEMP_DRIVER: _tempRaw_ antes de wait_event_interruptible RX\n");
    if ( (aux = wait_event_interruptible ( wqI2C_RX, cond_wqI2C_RX>0)) < 0 )
	{
	    printk(KERN_ALERT "TEMP_DRIVER: Error en waitqueue de RX\n");
	    //return aux;
	} 
    printk(KERN_ALERT "TEMP DRIVER: _tempRaw_ saliendo de la supensión\n");



    for(a=0; a<3; a++)
    {

        Temp[a] = Buff_rx[a];
        printk(KERN_ALERT "TEMP DRIVER: _tempRaw_ %d, %x \n",a, Temp[a]);

    }

    t_msb  = Temp[0];
    t_lsb  = Temp[1];
    t_xlsb = Temp[2];

  
    aux = (int)t_msb;
    raw = aux<<12;
    aux = 0;
    aux = (int)t_lsb;
    aux = aux<<4;
    raw = raw | aux;
    aux = 0;
    aux = (int)t_xlsb;
    raw = raw | aux;

    return raw;
}

static double temp_fine_compensada(long int adc_t)
{
    int t_final = 0;
    double var1=0, var2=0, T =0;

    var1 = (((double)adc_t)/16384.0 - ((double)tempCoef.c1)/1024.0)*((double)tempCoef.c2);
    var2 = ((((double)adc_t)/131072.0 - ((double)tempCoef.c1)/8192.0)*(((double)adc_t)/131072.0 - ((double)tempCoef.c1)/8192.0))*((double)tempCoef.c3);
    
    t_final = (int)(var1 + var2);

    T = (var1 + var2)/5120.0;
    return T;
}    

static long int temp_compensada(long int adc_T)
{
    long int t_fine;
    long int var1, var2, T;
    	
    var1 = ((((adc_T>>3) - ((long int)tempCoef.c1<<1))) * ((long int)tempCoef.c2)) >> 11;
    var2 = (((((adc_T>>4) - ((long int)tempCoef.c1)) * ((adc_T>>4) - ((long int)tempCoef.c1))) >> 12) * ((long int)tempCoef.c3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    
    return T;
}


void enToStr(long int number, char *c, int N)
{


    char vec[8]={0};
    long int valor, i,m,digitos ;

    valor = number;
    
    digitos = N;
    for(i=0; i< (digitos-1); i++)
    {  

        if(i == 0)
        {
            vec[i] = number % 10;
            number= number/10; 
        }
        else 
        {
            vec[i] = number % 10;
            number = number / 10; 
        }
    }

    vec[i] = number;

    for(m=0; m<(digitos); m++)
    {
        c[(digitos-1)-m] = vec[m]+48;
    }
 


}

/*  1°arg destino, 2°origen, 3° cantidad  */

static void cpy(char *dst, const char *src, size_t cnt )
{
    int i;

    for(i=0; i<cnt; i++)
    {
        *dst=*src;
        dst++;
        src++;

    }
    
}

module_init(I2C_Driver_init);
module_exit(I2C_Driver_exit);

