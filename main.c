#include "em_device.h"
#include "em_usart.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "sl_system_init.h"
#include "sl_event_handler.h"
#include "sl_system_process_action.h"
#include "em_chip.h"
#include <stdio.h>

//ez jol muködott 1 pont pipa

//test joci
#define USART_TX_PORT         gpioPortC//tx
#define USART_TX_PIN          11
#define USART_RX_PORT         gpioPortC//rx
#define USART_RX_PIN          10

#define RS485_DE_PORT        gpioPortA//Engedelyezo Max3485
#define RS485_DE_PIN         12

#define A_PORT gpioPortC  //ssr kiment A phase (L1) vezérlése
#define A_PIN  0

#define B_PORT gpioPortC //ssr kimenet B phase(L2)vezérlése
#define B_PIN  1

#define C_PORT gpioPortC //ssr kiment C phase(L3)vezérlése
#define C_PIN  13

#define sdf 64//buffer

#define time_period 5000//5sec->5000milisec
#define boljler_W 180000//*100 mert ennyivel több et olsasunk be
#define full_heat_time 11628000//dT*viz*liter/P=50Cfok*4186*100/1800=11628sec-> *1000-> 11628000milsec

volatile float time_period_A = 0;
volatile float time_period_B = 0;
volatile float time_period_C = 0;

volatile uint64_t time_A = 0;
volatile uint64_t time_B = 0;
volatile uint64_t time_C = 0;

volatile uint32_t msTicks = 0;

volatile uint8_t response[sdf]; // válasz buffer
volatile uint16_t l = 0; //adress cime
volatile uint32_t ii = 0; // buffer szamlalo
// test volatile uint32_t ii2 = 0;
volatile uint8_t ready = 0; // beérkezett data kész
volatile uint8_t db = 7; // kivánt regiszterek száma
volatile uint8_t day_restart = 0; // milyen napvan minden nap ujra ind tani az értéket

volatile uint8_t invalid = 0;

volatile uint8_t k = 1; // beolvasni kivánt regiszter száma

void
SysTick_Handler (void)
{
  msTicks++;  // minden 1 ms-ban növeljük
}

void
DelayMs (uint32_t ms)
{
  uint32_t target = msTicks + ms;
  while (msTicks < target)
    ;
}

void
billentes_ssr (uint32_t A, uint32_t B, uint32_t C)
{
  //priodusba rendezni
  time_period_A = ((float) A) / boljler_W * time_period;
  time_period_B = ((float) B) / boljler_W * time_period;
  time_period_C = ((float) C) / boljler_W * time_period;
  // felvan e futve
  time_A = time_A + time_period_A;
  time_B = time_B + time_period_B;
  time_C = time_C + time_period_C;

  GPIO_PinOutSet (A_PORT, A_PIN);
  GPIO_PinOutSet (B_PORT, B_PIN);
  GPIO_PinOutSet (C_PORT, C_PIN);
  // periodus megvalositása
  for (volatile uint16_t j = 0; j < time_period; j++)
    {
      DelayMs (1);  // 1 ms-onként vizsgálunk (elég gyakori)
      if (j >= time_period_A)
        {
          GPIO_PinOutClear (A_PORT, A_PIN);

        }
      if (j >= time_period_B)
        {
          GPIO_PinOutClear (B_PORT, B_PIN);

        }
      if (j >= time_period_C)
        {
          GPIO_PinOutClear (C_PORT, C_PIN);

        }

    }

}

//ssr lábak felkonfikurálása
void
GPIO_init (void)
{
  CMU_ClockEnable (cmuClock_GPIO, true);
  GPIO_PinModeSet (A_PORT, A_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet (B_PORT, B_PIN, gpioModePushPull, 0);
  GPIO_PinModeSet (C_PORT, C_PIN, gpioModePushPull, 0);

}

void
usart0_init (void)
{

  CMU_ClockEnable (cmuClock_GPIO, true);

  // USART0 órajel engedélyezése (nem UART0!)
  CMU_ClockEnable (cmuClock_USART0, true);

  // USART aszinkron mód inicializálása
  USART_InitAsync_TypeDef USART0_init = USART_INITASYNC_DEFAULT;
  USART0_init.baudrate = 9600;
  USART0_init.enable = usartDisable;
  USART_InitAsync (USART0, &USART0_init);
  //GPIO_PinModeSet(gpioPortF, 7, gpioModePushPull, 1);
  // TX és RX pin módok beállítása
  GPIO_PinModeSet (USART_TX_PORT, USART_TX_PIN, gpioModePushPull, 1);  // TX
  GPIO_PinModeSet (USART_RX_PORT, USART_RX_PIN, gpioModeInput, 0);     // RX

  GPIO_PinModeSet (RS485_DE_PORT, RS485_DE_PIN, gpioModePushPull, 0);  // DE pin

  // Pin routing (PC10/11 → LOC2)
  USART0->ROUTELOC0 = USART_ROUTELOC0_TXLOC_LOC2 | USART_ROUTELOC0_RXLOC_LOC2;
  USART0->ROUTEPEN = USART_ROUTEPEN_TXPEN | USART_ROUTEPEN_RXPEN;

  USART_IntClear (USART0, _USART_IFC_MASK); // Minden meglévő megszakítás törlése
  USART_IntEnable (USART0, USART_IEN_RXDATAV); // RX megszakítás engedélyezése (adat érkezett)
  NVIC_ClearPendingIRQ (USART0_IRQn);       // Függőben lévő megszakítás törlése
  NVIC_EnableIRQ (USART0_IRQn);          // Megszakítás engedélyezése a NVIC-ben
  USART_Enable (USART0, usartEnable);

}



void
USART0_IRQHandler (void)
{

  /* Check for RX data valid interrupt */
  if (USART0->STATUS & USART_STATUS_RXDATAV)
    {
      /* Copy data into RX Buffer */
      response[ii++] = USART_Rx (USART0);
      if (ii >= 30)
        ready = 1;
      /* Clear RXDATAV interrupt */
      USART_IntClear (USART0, USART_IF_RXDATAV);
    }
}

//crc ellenörző
uint16_t
modbus_crc16 (volatile uint8_t *buf, uint8_t len)
{
  uint16_t crc = 0xFFFF;
  for (uint8_t pos = 0; pos < len; pos++)
    {
      crc ^= buf[pos];
      for (int i = 8; i != 0; i--)
        {
          if ((crc & 0x0001) != 0)
            {
              crc >>= 1;
              crc ^= 0xA001;
            }
          else
            crc >>= 1;
        }
    }
  return crc; // Alacsony byte először, magas byte utána!
}

// kérés kikuldése
void
rs485_send (volatile uint8_t *data, uint8_t len)
{
  //for (volatile int i = 0; i < 100; i++); // kis delay, DE előtt
  GPIO_PinOutSet (RS485_DE_PORT, RS485_DE_PIN); // DE High ez kell a rs485 transciverhez

  for (uint8_t i = 0; i < len; i++)
    {
      USART_Tx (USART0, data[i]);
    }

  while (!(USART0->STATUS & USART_STATUS_TXC))
    ; // Wait for completion
  GPIO_PinOutClear (RS485_DE_PORT, RS485_DE_PIN); // DE Low
}

uint16_t
keresfeldolgozas (uint8_t adress, uint16_t g)
{ // g-> a lekérdezni kivánt regiszter helye
  volatile uint16_t tombone = 0x0000;

  for (volatile uint8_t m = 0; m < sdf; m++)
    response[m] = 0x00;

  volatile uint8_t request[8] =
    { adress, 0x03, 0x00, (uint8_t) (g & 0xFF), 0x00, k };

  volatile uint16_t crc = modbus_crc16 (request, 6);
  request[7] = (uint8_t) ((crc >> 8) & 0xFF);
  request[6] = (uint8_t) (crc & 0xFF); // ténylegesen jo

  ii = 0;
  ready = 0;
  rs485_send (request, 8); //kérés elkuldése
  //IT //  beolvasas();
  volatile uint64_t f = 0;
  while (!ready)
    {
      f++;
      if (f == 100000) //100000->//200us várakozás
        {
          ready = 1;
        }
    }
  ready = 0;

  //keresrendezo();
  volatile uint8_t megvanj = 0;
  for (volatile int j = 0; j < sdf; j++) // belovasott adat megtalálása a bufferben
    if ((response[j] == adress) & (response[j + 1] == 3)
        & (response[j + 2] == (k * 2)))
      megvanj = j;
  for (volatile int j = 0; j < sdf; j++)
    response[j] = response[(megvanj + j)];

  crc = modbus_crc16 (response, (db - 2));
  volatile uint16_t rcrc = ((response[db - 1] << 8) | response[db - 2]);
  tombone = (response[3] << 8) | response[4];
  //hiba kezelés
  if (rcrc != crc)
    {
      tombone = 0x0000;
      invalid = 1;
    }
  //bajvan
  return tombone;
}

int
main (void)
{

  sl_system_init ();
  GPIO_init ();
  usart0_init ();
  SysTick_Config (SystemCoreClock / 1000);  // 1 ms-os időzítő

  sl_system_process_action ();

  volatile uint16_t tombone;

  volatile uint32_t performance[12] =
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
  volatile uint16_t performance_i = 0;
  k = 1;
  db = 7;

  while (1)
    {
      performance_i = 0;  // futo index

      for (volatile uint8_t adress = 1; adress < 3; adress++)
        for (l = 93; l < 96; l++)
          { // 93 regiszterben van 2byte-on a az adat 100 nagyobb dataérték A phase Active performance
            tombone = keresfeldolgozas (adress, l);

            performance[performance_i++] = tombone;

            for (volatile int j = 0; j < 1000; j++)
              ;

          }  //vége for 90esek
             //a 9, b 10, c 11, szumma
      performance[9] = performance[0] + performance[3] + performance[6];
      performance[10] = performance[1] + performance[4] + performance[7];
      performance[11] = performance[2] + performance[5] + performance[8];


      if (time_A >= 11628000)
        {
          performance[9] = 0;
        }
      if (time_B >= 11628000)
        {
          performance[10] = 0;
        }
      if (time_C >= 11628000)
        {
          performance[11] = 0;
        }
      //kis daley
      for (volatile int j = 0; j < 10000; j++)
        ;

      //nap lekérdezése a 11es regiszterből és az 1es cimrol
      tombone = keresfeldolgozas (1, 11);

      //ujra inditja minden nap a számlálót
      if ((invalid == 1))
        day_restart = day_restart;
      else if ((invalid != 1))
        {
          if (day_restart != (uint8_t) (tombone))
            {
              time_A = 0;
              time_B = 0;
              time_C = 0;
            }

          day_restart = (uint8_t) (tombone);
        }

      invalid = 0;

      billentes_ssr (performance[9], performance[10], performance[11]);
    } //while vége

}

