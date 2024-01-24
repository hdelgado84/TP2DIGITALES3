/* Configuración de registros para la beagle                                      */

/*      start addr     end addr   size    
I2C2   0x4819_C000  0x4819_CFFF  4KB     I2C2 Registers

#define Dir*/

/**************************************************************************************************************
*                                                                                                             *
*                                                                                                             *
*                                         Registros del BMP280                                                *
*                                                                                                             *
*                                                                                                             *  
*                                                                                                             *
*                                                                                                             * 
***************************************************************************************************************/
#define CHIP_ID       0xd0    /* registro que contiene chip id*/
#define ADDR_BMP280   0x76    /* 0x76 si SDO --> GND, 0x77 si SD0 --> VCC */
                             



/**************************************************************************************************************
*                                                                                                             *
*                                                                                                             *
*                               Configuración del modulo    CM_PER                                            *
*                                Clock Module Peripheral Register                                             *
*                                                                                                             *  
*                  Habilita el modulo I2c                                                                     *
*                                                                                                             * 
***************************************************************************************************************/

#define CM_PER_BASE          0x44E00000  //dir base
#define CM_PER_I2C2_OFFSET   0x44        //offset para el I2C
#define MASC_I2C2_CLKTRL     0xfffffffc   //mascara para modificar bits 0:1
#define CM_PER_SIZE          0x400        // Tamaño reservado  
#define MODULEMODE_ON        0x2           // (bits 0:1),  2 = enable, 0 = disable
#define MODULEMODE_OFF       0
    


/**************************************************************************************************************
*                                                                                                             *
*                                                                                                             *
*                               Configuración del modulo    CM_register                                       *
*                               Control Module Registers                                                      *
*                                                                                                             *  
*                        Configuro los pads scl y sda del I2C                                                 *
*                                                                                                             * 
***************************************************************************************************************/

#define CM_BASE             0x44E10000   // dirección base del modulo       
#define CM_SIZE             0x2000       // Tamaño reservado  
#define PIN_I2C_SDA_OFFSET  0x978        // PIN SDA: conf_uart1_ctsn
#define PIN_I2C_CLK_OFFSET  0x97C        //  PIN CLK: conf_uart1_rtsn
#define CM_MASK             0xffffff80   // mascara para leer los bits del 0 la 6
#define CLK_PAD_CONFIG      0x3B         // configuracion del pad del clock del I2C como salida unicamente(pullup disable)
#define SDA_PAD_CONFIG      0x3B         // configuracion del pad del SDA del I2C como entrada y salida (pullup disable)

/* ************************************************************************************************************ 
*                                                                                                             *
*                                                                                                             *
*                           configuración de registros I2C                                                    *  
*                                                                                                             *    
*                                                                                                             *
***************************************************************************************************************
***************************************************************************************************************/

#define DIR_BASE_I2C2_REG  0x4819C000  /* dirección inicial I2C2 */
#define SIZE_I2C2          0x2000      /* Tamaño reservado 4K*/


/***************************************************************************************************************
*                             Configuración Prescaler I2C_PSC                                                  *
* -------------------------------------------------------------------------------------------------------------*                            
*                                                                                                              *
*  Para la entrada de clock del I2C la frecunecia que recomienda el manual de usuario es 12 Mhz                *
*  entonces el divisor en el registro del prescaler debe ser 4                                                 *
*                                                                                                              *  
****************************************************************************************************************/


#define F_CLK           48000000        /* frecuencia de entrada prescaler */
#define F_ICLK          12000000        /* 12 MHZ entrada de reloj I2C */ 
#define DIV             (F_CLK/F_ICLK)  /* Divisor prescaler */
       
#define MASC_PSC        0xfffffffc      /* mascara para los primeros 8 bits prescaler */

 
#define I2C_PSC_OFFSET   0xb0           /* offset registro prescaler */
#define I2C_PSC          (DIV-1)        /* valor del prescaler */


/***************************************************************************************************************
*                             Configuración registros  I2C_SCLL  y I2C_SCLH                                    *
* -------------------------------------------------------------------------------------------------------------* 
*                                                                                                              *
*  Calculo para 100Khz de clock I2C, con periodo T= 1/100khz --> T=10useg                                      *
*  si tLow = tHigh = 5useg                                                                                     *
*  T_ICLK = 1/F_ICLK = 83nseg                                                                                  *
*                                                                                                              *
*  tLow  = (SCLL + 7 )*T_ICLK  despejando SCLL = 53                                                            *
*  tHigh = (SCLH + 5 )*T_ICLK  despejando SCLH = 55                                                            *
*                                                                                                              *
****************************************************************************************************************/


#define MASC_I2C_SCLL     0xffffff00 
#define I2C_SCLL_OFFSET   0xb4
#define I2C_SCLL          53
#define MASC_I2C_SCLH     0xffffff00
#define I2C_SCLH_OFFSET   0xb8
#define I2C_SCLH          55

/***************************************************************************************************************
*                             Configuración registros    I2C_CON                                               *
* -------------------------------------------------------------------------------------------------------------* 
*                                                                                                              *
*                                                                                                              *
*                                                                                                              *
*                                                                                                              *
*                                                                                                              *
*                                                                                                              *
*                                                                                                              *
*                                                                                                              *
****************************************************************************************************************/

#define I2C_CON_OFFSET   0xA4
#define MASC_I2C_CON     0xFFFF400c /*mascara para modificar solo estos bits */ 
#define I2C_CON_EN       15      /* bit I2C_EN  0, reset/deshabilitado / 1 habilitado  */
#define OPL              12
#define OPH              13
#define OPMODE           0       /* (bits 12 y 13) Selección del modo de operación Fast/Standard mode , el valor es 0 por defecto. 1,2,3 estan reservados */ 
#define STB              11      /* bit Start byte mode (I2C master mode only). 1 Start byte mode, 0 normal mode */
#define MST              10      /* bit Master/slave mode : 1/0 */
#define TRX              9       /* bit en 0 para "Transmitter/receiver" mode (i2C master mode only).  */
#define STOP             1       /* bit de Stop condicion */
#define START            0       /* bit de start condicion, 0 nada , 1 condicion de start*/  




/***************************************************************************************************************
*                         Registros    I2C_OA                                                                  *
* -------------------------------------------------------------------------------------------------------------* 
*                                                                                                              *
*                      Configuración cantidad de bits de direccionamiento                                      *
*                      configuro para 7bits                                                                    *
*                      Dirección Master: 110                                                                   *
*                                                                                                              *
****************************************************************************************************************/

#define I2C_OA_OFFSET   0xA8
#define MASTER_ADRR     60
#define MASC_I2C_OA     0xfffffc00                      //mascara para modificar solo bits [9:0] 
#define MASC_7BITS_OA   0x7F                            /*mascara para 7 bits address bits[6:0] 1111111 */
#define I2C_OA          (MASC_7BITS_OA & MASTER_ADRR)   /* mi dirección de master, cargo los 7 bits de adddress*/
                                                      

/***************************************************************************************************************
*                         Registros    I2C_SA                                                                  *
* -------------------------------------------------------------------------------------------------------------* 
*                                                                                                              *
*                      Configuración cantidad de bits de direccionamiento                                      *
*                      configuro para 7bits                                                                    *
*                      Dirección Slave: lo busco en la hoja de dato del sensor                                 *
*                                                                                                              *
****************************************************************************************************************/


#define I2C_SA_OFFSET  0xAC
#define SLAVE_ADRR     ADDR_BMP280 
#define MASC_I2C_SA    0xFFFFFC00         //mascara para modificar solo bits [9:0] 
#define MASC_7BITS_SA  0x7F                /*mascara para 7 bits address bits[6:0] 1111111 */
#define I2C_SA         (MASC_7BITS_SA & SLAVE_ADRR)  /* dirección de slave*/


/***************************************************************************************************************
*                               Registro   I2C_IRQSTATUS_RAW                                                   *
* -------------------------------------------------------------------------------------------------------------* 
*                                                                                                              *
*                  Información del estado de flags para la atención de interrupciones                          *
*                  para saber a que se debe la interrupción                                                    *
*                  Los campos son de lectura y escritura. Escribir un 1 a un bit lo establecerá en 1, es decir,*
*                  activará la IRQ.  ( Sirve para hacer debug !!!!..Puedo activar las interrupciiones a mano ) *
****************************************************************************************************************/


#define OFFSET_I2C_IRQSTATUS_RAW  0x24
#define MASC_I2C_IRQSTATUS_RAW    0xffff8000 /* uso solo los bits [15:0]*/
#define AL                        0       /*Arbitration lost IRQ status*/
#define NACK                      1       /*No acknowledgment IRQ status */  
#define ARDY                      (1<<2)
#define RRDY                      (1<<3)  /*Receive data ready IRQ enabled status*/
#define XRDY                      (1<<4)  /*Transmit data ready IRQ status.*/
#define GC                        5       /*General call IRQ status.*/
#define STC                       6       /*Start Condition IRQ status.*/
#define AERR                      7       /*Access Error IRQ status*/
#define BF                        (1<<8)       /* BUS FREE */
#define AAS                       9       /* Address recognized as slave IRQ status.*/
#define XUDF                      10      /*Transmit underflow status. */
#define ROVR                      11      /*Receive overrun status*/ 
#define BB                        12      /*This read-only bit indicates the state of the serial bus. 0:free/1:occupied*/
#define RDR                       13      /* Receive draining IRQ status.*/
#define XDR                       14      /*Transmit draining IRQ status.*/



/***************************************************************************************************************
*                          Registro   I2C_IRQSTATUS                                                            *
* -------------------------------------------------------------------------------------------------------------* 
*                                                                                                              *
*         Muestra información  del estado de las interrupciones, Escribiendo un 1 en un bit apaga la 
*         interrupción, escribiendo un 0 no afecta el bit.                                                        
* 
*                                                                                                              *
****************************************************************************************************************/

#define OFFSET_I2C_IRQSTATUS      0X28
#define MASC_I2C_IRQSTATUS        0xffff8000 /* uso solo los bits [14:0]*/
/*son los mismos bits que IRQSTATUS_RAW*/



/***************************************************************************************************************
*                          Registro   I2C_SYSC                                                            *
* -------------------------------------------------------------------------------------------------------------* 
*                                                                                                              *
*         Muestra información  
*                                                                 
* 
*                                                                                                              *
****************************************************************************************************************/

#define OFFSET_I2C_SYSC         0x10
#define MASC_I2C_SYSC           0xfffffc00
/***************************************************************************************************************
*                          Registro   I2C_IRQENABLE_SET                                                        *
* -------------------------------------------------------------------------------------------------------------* 
*         Mascara para habilitar interrupciones                                                                *
*         Escribir un 1 en un campode bit habilita la interrupción, escribir un 0 no tiene efecto              *   
*                                                                                                              *
*                                                                                                              *
****************************************************************************************************************/

#define OFFSET_I2C_IRQENABLE_SET 0x2C
#define MASC_I2C_IRQENABLE_SET   0xffff8000 /* uso solo los bits [15:0]*/
#define AL_SET                    0       /*Arbitration lost IRQ status*/
#define NACK_SET                  1       /*No acknowledgment IRQ status */  
#define ARDY_SET                  2
#define RRDY_SET                  3
#define XRDY_SET                  4       /*Transmit data ready IRQ status.*/
#define GC_SET                    5       /*General call IRQ status.*/
#define STC_SET                   6       /*Start Condition IRQ status.*/
#define AERR_SET                  7       /*Access Error IRQ status*/
#define BF_SET                    8       /* BUS FREE */
#define AAS_SET                   9       /* Address recognized as slave IRQ status.*/
#define XUDF_SET                  10      /*Transmit underflow status. */
#define ROVR_SET                  11      /*Receive overrun status*/ 
#define BB_SET                    12      /*This read-only bit indicates the state of the serial bus. 0:free/1:occupied*/
#define RDR_SET                   13      /* Receive draining IRQ status.*/
#define XDR_SET                   14      /*Transmit draining IRQ status.*/


/***************************************************************************************************************
*                          Registro   I2C_IRQENABLE_CLR                                                        *
* -------------------------------------------------------------------------------------------------------------* 
*         Mascara para deshabilitar interrupciones                                                             *
*         Escribir un 1 en un campo de bit deshabilita la interrupción, escribir un 0 no tiene efecto          *   
*                                                                                                              *
*                                                                                                              *
****************************************************************************************************************/


#define OFFSET_I2C_IRQENABLE_CLR 0x30
#define MASC_I2C_IRQENABLE_CLR   0xffff8000 /* uso solo los bits [15:0]*/
#define AL_CLR                     0       /*Arbitration lost IRQ status*/
#define NACK_CLR                   1       /*No acknowledgment IRQ status */  
#define ARDY_CLR                   2
#define RRDY_CLR                   3
#define XRDY_CLR                   4       /*Transmit data ready IRQ status.*/
#define GC_CLR                     5       /*General call IRQ status.*/
#define STC_CLR                    6       /*Start Condition IRQ status.*/
#define AERR_CLR                   7       /*Access Error IRQ status*/
#define BF_CLR                     8       /* BUS FREE */
#define AAS_CLR                    9       /* Address recognized as slave IRQ status.*/
#define XUDF_CLR                   10      /*Transmit underflow status. */
#define ROVR_CLR                   11      /*Receive overrun status*/ 
#define BB_CLR                     12      /*This read-only bit indicates the state of the serial bus. 0:free/1:occupied*/
#define RDR_CLR                    13      /* Receive draining IRQ status.*/
#define XDR_CLR                    14      /*Transmit draining IRQ status.*/




/***************************************************************************************************
*				Registro I2C_CNT 
*--------------------------------------------------------------------------------------------------
*                                                                                                 *
*                                                                                                 *
***************************************************************************************************/
#define OFFSET_I2C_CNT 0x98     /* DCOUNT bits [15:0] cantidad a trasnmitir */




/**************************************************************************************************
*                              Registro I2C_DATA
*--------------------------------------------------------------------------------------------------
*
*
**************************************************************************************************/

#define OFFSET_I2C_DATA 0x9c /*dato a transmitir/recibir */



struct I2C_REG {
 
                 unsigned int I2C_CON;




};



/**************************************************************************************************
*                              Registros BMP280                                                   *
*--------------------------------------------------------------------------------------------------
*                                                                                                 *
*                    defino los registros y datos de configuracion de sensor                      * 
*                                                                                                 *
*                                                                                                 *
**************************************************************************************************/

#define ctrl_meas  0xf4      /* regitro de control 7,6,5 osrs_t 4,3,2 osrs_p 1,0 mode */
#define mode_meas   1        /* [1:0] 00 SLEEP MODE , 01 FORCE MODE, 11 NORMAL MODE*/
#define osrs_t      2<<5     // 2 oversampling, 0 para no medir temperatura   
#define osrs_p      0<<2     // 0 para no medir presión



typedef struct coeficientes { 
                            unsigned int c1;
                                     int c2;
                                     int c3;    
}bmpcoef;