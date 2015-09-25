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


#define CONFIG_SERVO_COUNT 8

static char r;
static uint16_t servo_nextpos[CONFIG_SERVO_COUNT];
static uint16_t servo_pos[CONFIG_SERVO_COUNT];
/* TODO: We support 12 16 18 and 20 Mhz crystal */
/* 12M is quite junky and unusable */

#if F_CPU == 20000000
static uint16_t next_icr=62500;
#endif

#if F_CPU == 18000000
static uint16_t next_icr=4500;
#endif

#if F_CPU == 16000000
static uint16_t next_icr=4000;
#endif

#if F_CPU == 12000000
static uint16_t next_icr=3000;
#endif


#define CONF_EEPROM_OFFSET 2
#define USBDESCR_OFFSET    10 

static unsigned char usbreplybuf[1];
static uchar wrLen; 
static void *wrPos;

enum requests {
	RQ_SAVE=0, 
	RQ_LOAD,
	RQ_BIT_SET,
	RQ_BIT_GET,
	RQ_WRPOS,
};

#define USBRQ_TYPE_MASK 0x60


enum { 
	REG_PORT=0,
	REG_DDR,
	REG_PIN
};

enum { 
	EEP_SHIT=0, /* Avoid byte 0 corruption at powerloss */
	EEP_SERVALID,
	EEP_CONFVALID,
	EEP_DDRD,
	EEP_PORTD,
	EEP_DDRB,
	EEP_PORTB
	
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


static inline void save_state()
{
	eeprom_write_byte((void *) EEP_SERVALID, 0xb7);
	eeprom_write_byte((void *) EEP_DDRD,     DDRD);
	eeprom_write_byte((void *) EEP_PORTD,    PORTD);
	eeprom_write_byte((void *) EEP_DDRB,     DDRB);
	eeprom_write_byte((void *) EEP_PORTB,    PORTB);
}

void load_state()
{
	if ((0xb7 == eeprom_read_byte((void *)  EEP_CONFVALID)))
		return; /* Virgin device, no serial */

	DDRD = eeprom_read_byte((void *)  EEP_DDRD);
	PORTD = eeprom_read_byte((void *) EEP_PORTD);
	DDRB = eeprom_read_byte((void *)  EEP_DDRB);
	PORTB = eeprom_read_byte((void *) EEP_PORTB);
}


usbMsgLen_t usbFunctionDescriptor(struct usbRequest *rq)
{
	if ((0xb7 == eeprom_read_byte((void *)  EEP_SERVALID)))
		return 0; /* Virgin device, no serial */
	
	int *rbuf = (int *) usbreplybuf;
	eeprom_read_block((void*) usbreplybuf, (const void *) USBDESCR_OFFSET, ARRAY_SIZE(usbreplybuf));
	usbMsgPtr = usbreplybuf;
	return (rbuf[0] & ~(3<<8));
}


static void inline greg_write(uint8_t gpio, uint8_t op, uint8_t v)
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

static uint8_t inline greg_read(uint8_t gpio, uint8_t op)
{
	uint8_t i = 0; 
	while (gpio > 7) {
		i++;
		gpio-=8;
	};
	volatile uint8_t **r = (volatile uint8_t **) &ports[i];
	return (*(r[op]) & (1 << gpio));
}

ISR(TIMER1_OVF_vect)
{
	ICR1=next_icr;
	r=1;
}

void flip()
{
/* load up next values, we're phase correct. Somewhat */
	char i;
	for (i=0; i<CONFIG_SERVO_COUNT; i++)
	{
		servo_pos[i]=servo_nextpos[i];
	}
/* Iterate over enabled channels */
	for (i=0;i<8;i++)
	{
		if (servo_pos[i]) PORTB |= (1<<i);
	}
	r=0;
}

void loop_outputs()
{
	int i;
	for (i=0;i<8;i++)
	{
		if (servo_pos[i] && (TCNT1 > servo_pos[i]))
		{
			PORTB &= ~(1<<i);
		}
	}
	if (r) flip();
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
	case RQ_WRPOS:
		servo_nextpos[rq->wIndex.bytes[0]]=rq->wValue.word;
		break;

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

int main() {
	load_state();
	usbReconnect();

	TCCR1A = (1<<WGM11);
	TCCR1B = (1<<WGM13|1<<WGM12|1<<CS11);
	TIMSK = (1<<TOIE1);
	ICR1=next_icr; //20ms

	usbInit();
	sei();
	while (1) {
		usbPoll();
		loop_outputs();
	}
}
