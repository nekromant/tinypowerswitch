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
	uint8_t req = rq->bRequest;
	if (req == 0xff)
	{
		save_state((uint8_t*)rq->wValue.word);
	}else
		if (req == 0xfa)
		{
			load_state((uint8_t*)rq->wValue.word);
		} else {
			if (req < 8) {
				if (rq->wValue.bytes[0]==2)
				{
					set_reply(PORTB & (1 << req));
					usbMsgPtr = usbreply;
					return 2;
					
				}
				else if (rq->wValue.bytes[0])  {
					PORTB|=(1 << req) ;
				} else {
					PORTB&=~(1 << req);
				}	
			}
			req -= 8;
			if (req < 8)
			{
				if (rq->wValue.bytes[0]==2)
				{
					set_reply(PORTD & (1 << req));
					usbMsgPtr = usbreply;
					return 2;
					
				}
				else if (rq->wValue.bytes[0]) 
				{
					PORTD|=(1 << req) ;
				}else
				{
					PORTD&=~(1 << req);
				}	
			}
		}
	return 0;

}

#define USB_BITS (1 << CONFIG_USB_CFG_DMINUS_BIT | 1 << CONFIG_USB_CFG_DPLUS_BIT)
inline void usbReconnect()
{
	PORTD &= ~USB_BITS;
        DDRD  |= USB_BITS;
	_delay_ms(1250);
        DDRD  &= ~USB_BITS;
	/* Don't reenable pullup - screws up some hubs */
}


ANTARES_APP(main_app)
{
	DDRB=0xff;
	DDRD=0xff;
	PORTD=0xff;
	load_state((uint8_t*)0x0000);
	usbReconnect();
	usbInit();
	while(1)
	{
		usbPoll();
	}
}
