#include <arch/antares.h>
#include <avr/boot.h>
#include <generated/usbconfig.h>
#include <arch/vusb/usbportability.h>
#include <arch/vusb/usbdrv.h>
#include <compat/deprecated.h>
#include <util/delay.h>
#include <stdbool.h>

/* 
   typedef struct usbRequest{
   uchar       bmRequestType;
   uchar       bRequest; //outlet
   usbWord_t   wValue; //status = 0,1 2 == read
   usbWord_t   wIndex; //
   usbWord_t   wLength;
   }usbRequest_t;
*/


#define CONF_EEPROM_OFFSET 2
#define USBDESCR_OFFSET    10 

static unsigned char usbreplybuf[32];
static uchar wrLen; 
static void *wrPos;

enum requests {
	RQ_SAVE=0, 
	RQ_LOAD,
	RQ_BIT_SET,
	RQ_BIT_GET,
	RQ_SETSERIAL,
	RQ_RAWIO
};

#define USBRQ_TYPE_MASK 0x60


enum { 
	REG_PORT=0,
	REG_DDR,
	REG_PIN
};

struct ioport {
	volatile uint8_t* port;
	volatile uint8_t* pin;
	volatile uint8_t* ddr;
} __attribute__((packed));

struct ioport ports[] = {
	{ &PORTB, &DDRB, &PINB },
	{ &PORTD, &DDRD, &PIND },
};

#define CLEN (ARRAY_SIZE(ports)*2)


void save_state()
{
	eeprom_write_byte((void *)1, DDRD);
	eeprom_write_byte((void *)2, PORTD);
	eeprom_write_byte((void *)3, DDRB);
	eeprom_write_byte((void *)4, PORTB);
}

void load_state()
{
	DDRD = eeprom_read_byte((void *)1);
	PORTD = eeprom_read_byte((void *)2);
	DDRB = eeprom_read_byte((void *)3);
	PORTB = eeprom_read_byte((void *)4);
}


usbMsgLen_t usbFunctionDescriptor(struct usbRequest *rq)
{
	int *rbuf = (int *) usbreplybuf;
	eeprom_read_block((void*) usbreplybuf, (const void *) USBDESCR_OFFSET, ARRAY_SIZE(usbreplybuf));
	usbMsgPtr = usbreplybuf;
	return (rbuf[0] & ~(3<<8));
}


void greg_write(uint8_t gpio, uint8_t op, uint8_t v)
{
	uint8_t i = 0; 
	while (gpio > 7) {
		i++;
		gpio-=8;
	};

	volatile uint8_t **r = (volatile uint8_t **) &ports[i];

	*(r[op]) &= ~(1<<gpio);
	*(r[op]) |=  (v<<gpio);
}

uint8_t greg_read(uint8_t gpio, uint8_t op)
{
	uint8_t i = 0; 
	while (gpio > 7) {
		i++;
		gpio-=8;
	};
	volatile uint8_t **r = (volatile uint8_t **) &ports[i];
	return (*(r[op]) & (1 << gpio));
}



uchar   usbFunctionSetup(uchar data[8])
{
	usbRequest_t *rq =  (usbRequest_t*) &data[0];
	uint8_t req = rq->bRequest;

	switch (req) { 
	case RQ_SAVE:
		save_state();
		break;
	case RQ_LOAD:
		load_state();
		break;
	case RQ_BIT_SET:
		greg_write(rq->wValue.bytes[1], rq->wValue.bytes[0], rq->wIndex.bytes[0]);
		break;
	case RQ_BIT_GET:
		usbreplybuf[0] = greg_read(rq->wValue.bytes[0], rq->wIndex.bytes[0]);
		usbMsgPtr = usbreplybuf;
		return 1;
		break;
	case RQ_SETSERIAL:
		wrPos = (void *) USBDESCR_OFFSET;
		wrLen = rq->wLength.bytes[0];
		return USB_NO_MSG;
		break;
	case RQ_RAWIO: { 
		volatile uint8_t *ptr = (volatile uint8_t *) rq->wIndex.word;
		*ptr  = rq->wValue.bytes[0];
		break;
	}
	};
	return 0;
}

uchar usbFunctionWrite(uchar *data, uchar len)
{
	eeprom_write_block(data, wrPos, len);
	wrPos += len;
	wrLen -= len;
	/* Break the protocol - save the flash! */
	return (wrLen == 0);
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
	load_state();
	usbReconnect();
	usbInit();
	while(1)
	{
		usbPoll();
	}
}
