#include <arch/antares.h>
#include <avr/boot.h>
#include <generated/usbconfig.h>
#include <arch/vusb/usbportability.h>
#include <arch/vusb/usbdrv.h>
#include <compat/deprecated.h>
#include <util/delay.h>

/* 
   typedef struct usbRequest{
   uchar       bmRequestType;
   uchar       bRequest; //outlet
   usbWord_t   wValue; //status = 0,1 2 == read
   usbWord_t   wIndex; //
   usbWord_t   wLength;
   }usbRequest_t;
*/

#define USBRQ_TYPE_MASK 0x60


void load_state(uint8_t* off)
{
	PORTD=eeprom_read_byte(off);
	PORTB=eeprom_read_byte(off+1);
}

void save_state(uint8_t* off)
{
	eeprom_write_byte(off,PORTD);
	eeprom_write_byte(off+1,PORTB);
}

char usbreply[2];
void set_reply(int state) 
{
	if (state)
		usbreply[0]='1';
	else
		usbreply[0]='0';
}

uchar   usbFunctionSetup(uchar data[8])
{
	usbRequest_t *rq =  (usbRequest_t*) &data[0];
	if (rq->bRequest == 0xff)
	{
		save_state((uint8_t*)rq->wValue.word);
	}else
		if (rq->bRequest == 0xfa)
		{
			load_state((uint8_t*)rq->wValue.word);
		} else {
			if (rq->bRequest < 8) {
				if (rq->wValue.bytes[0]==2)
				{
					set_reply(PORTB & (1 << rq->bRequest));
					usbMsgPtr = usbreply;
					return 2;
					
				}
				else if (rq->wValue.bytes[0])  {
					PORTB|=(1 << rq->bRequest) ;
				} else {
					PORTB&=~(1 << rq->bRequest);
				}	
			}
			rq->bRequest -= 8;
			if (rq->bRequest < 8)
			{
				if (rq->wValue.bytes[0]==2)
				{
					set_reply(PORTD & (1 << rq->bRequest));
					usbMsgPtr = usbreply;
					return 2;
					
				}
				else if (rq->wValue.bytes[0]) 
				{
					PORTD|=(1 << rq->bRequest) ;
				}else
				{
					PORTD&=~(1 << rq->bRequest);
				}	
			}
		}
	return 0;

}

inline void usbReconnect()
{
        DDRD=0xff;
	_delay_ms(250);
        DDRD=0xf3;
}


ANTARES_APP(main_app)
{
	DDRB=0xff;
	usbReconnect();
	load_state((uint8_t*)0x0001);
	usbInit();
	while(1)
	{
		usbPoll();
	}
}
