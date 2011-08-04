/* Name: main.c
 * Project: fan_controller
 * Author: David Kreitschmann
 * Creation Date: 2011-08-03
 * Tabsize: 4
 * Copyright: (c) 2011 David Kreitschmann
 * License: GNU GPL v2 (see License.txt), GNU GPL v3
 * Based on custom HID request example from Objective Development
*/


#define DEFAULT_FANSPEED     240

//Restore full Fanspeed after X seconds
#define FAN_WATCHDOG_TIME         128


#define FAN1_COMPARE         OCR0B
#define FAN2_COMPARE         OCR0A
#define FAN3_COMPARE         OCR1BL
#define FAN4_COMPARE         OCR1AL

#define FAN1_TACHO_PIN       PINB1
#define FAN2_TACHO_PIN       PINB0
#define FAN3_TACHO_PIN       PINB6
#define FAN4_TACHO_PIN       PINB5

#define NO_OF_FANS           4

#define ALL_TACHO_MASK      (1<<FAN1_TACHO_PIN) | (1<<FAN2_TACHO_PIN) | (1<<FAN3_TACHO_PIN) | (1<<FAN4_TACHO_PIN)


//USB Requests
#define CUSTOM_RQ_SET_SPEED    1
#define CUSTOM_RQ_GET_SPEED    2
#define CUSTOM_RQ_GET_TACHO    3


#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>  /* for sei() */
#include <util/delay.h>     /* for _delay_ms() */
#include <string.h>

#include <avr/pgmspace.h>   /* required by usbdrv.h */
#include "usbdrv.h"

/* ------------------------------------------------------------------------- */
/* ----------------------------- USB interface ----------------------------- */
/* ------------------------------------------------------------------------- */

PROGMEM char usbHidReportDescriptor[22] = {    /* USB report descriptor */
    0x06, 0x00, 0xff,              // USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    // USAGE (Vendor Usage 1)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x09, 0x00,                    //   USAGE (Undefined)
    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)
    0xc0                           // END_COLLECTION
};

//storage for tacho state, to detect which input changed in interrupt handler
uchar tacho_state = 0;

//counter variable for signals
uchar fan_tacho_count[NO_OF_FANS];
//after one second the number of signals is stored here
uchar fan_tacho[NO_OF_FANS];

uchar watchdog_count = 0;


 

/* The descriptor above is a dummy only, it silences the drivers. The report
 * it describes consists of one byte of undefined data.
 * We don't transfer our data through HID reports, we use custom requests
 * instead.
 */
 
void setupPWM() {
    /*
    Setup both Counters as 8-bit Phase Corrected PWM 
    */
    TCCR0A = (1<<COM0A1) | (1<<COM0B1) | (1<<WGM00 );
    TCCR1A = (1<<COM1A1) | (1<<COM1B1) | (1<<WGM10 );
    TCCR0B = (1<<CS00);
    TCCR1B = (1<<CS10);
    //PWM Ports are outputs
    DDRD |= (1<<DDD5); //Fan2
    DDRB |= (1<<DDB2) | (1<<DDB3) | (1<<DDB4); //Fans 1,4,3    
}

void setupTachoDetection() {
    //Setup Watchdog as Timer. Interrupt every second.
    WDTCSR |= (1<<WDCE);
    WDTCSR = (1<<WDIE) | (1<<WDP1)| (1<<WDP2);
    
    //Enable Interrupts
    PCMSK = ALL_TACHO_MASK;
    GIMSK |= (1<<PCIE);
    
    //Pull Ups
    PORTB |= ALL_TACHO_MASK;
    
    memset(fan_tacho, 0, NO_OF_FANS);
    memset(fan_tacho_count, 0, NO_OF_FANS);
    tacho_state = PINB;
}

void restoreDefaultFanspeed() {
    FAN1_COMPARE = DEFAULT_FANSPEED;
    FAN2_COMPARE = DEFAULT_FANSPEED;
    FAN3_COMPARE = DEFAULT_FANSPEED;
    FAN4_COMPARE = DEFAULT_FANSPEED;
}


/* ------------------------------------------------------------------------- */

uchar usbFunctionWrite(uchar *data, uchar len)
{
    watchdog_count=0;
    FAN1_COMPARE = data[0];
    FAN2_COMPARE = data[1];
    FAN3_COMPARE = data[2];
    FAN4_COMPARE = data[3];
    return 1;
}

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t    *rq = (void *)data;
    int i;
    static uchar dataBuffer[4];
    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_VENDOR){
        if(rq->bRequest == CUSTOM_RQ_SET_SPEED){    
            
            return USB_NO_MSG;
       } else if (rq->bRequest == CUSTOM_RQ_GET_TACHO){
           usbMsgPtr = fan_tacho;
           return NO_OF_FANS;
       } else if (rq->bRequest == CUSTOM_RQ_GET_SPEED){
           dataBuffer[0] = FAN1_COMPARE;
           dataBuffer[1] = FAN2_COMPARE;
           dataBuffer[2] = FAN3_COMPARE;
           dataBuffer[3] = FAN4_COMPARE;
           usbMsgPtr = dataBuffer;
           return NO_OF_FANS;
       }
//    }else{
        /* calss requests USBRQ_HID_GET_REPORT and USBRQ_HID_SET_REPORT are
         * not implemented since we never call them. The operating system
         * won't call them either because our descriptor defines no meaning.
         */
    }
    return 0;   /* default for not implemented requests: return no data back to host */
}

/* ------------------------------------------------------------------------- */

int __attribute__((noreturn)) main(void)
{
uchar   i;

    //wdt_enable(WDTO_1S);
    /* Even if you don't use the watchdog, turn it off here. On newer devices,
     * the status of the watchdog (on/off, period) is PRESERVED OVER RESET!
     */
    /* RESET status: all port bits are inputs without pull-up.
     * That's the way we need D+ and D-. Therefore we don't need any
     * additional hardware initialization.
     */
    usbInit();
    usbDeviceDisconnect();  /* enforce re-enumeration, do this while interrupts are disabled! */
    i = 0;
    while(--i){             /* fake USB disconnect for > 250 ms */
      //  wdt_reset();
        _delay_ms(1);
    }
    usbDeviceConnect();
    
    setupPWM();
    setupTachoDetection();
    restoreDefaultFanspeed();
    
    sei();
    for(;;){                /* main event loop */
        usbPoll();
    }
}

/* ------------------------------------------------------------------------- */


ISR(PCINT_vect) {
    //update speed counters for every low-high transition
    uchar current = PINB;
    uchar up_flank = (current ^ tacho_state) & current;
    tacho_state = current;
    fan_tacho_count[0] += (up_flank>>FAN1_TACHO_PIN) & 1;
    fan_tacho_count[1] += (up_flank>>FAN2_TACHO_PIN) & 1;
    fan_tacho_count[2] += (up_flank>>FAN3_TACHO_PIN) & 1;
    fan_tacho_count[3] += (up_flank>>FAN4_TACHO_PIN) & 1;
}

ISR(WDT_OVERFLOW_vect) {
    //runs every second
    memcpy(fan_tacho, fan_tacho_count, NO_OF_FANS);
    memset(fan_tacho_count, 0, NO_OF_FANS);
    //return to full speed if host sends no new data
    watchdog_count++;
    if (watchdog_count > FAN_WATCHDOG_TIME) {
        restoreDefaultFanspeed();
        watchdog_count=0;
    }
    

}