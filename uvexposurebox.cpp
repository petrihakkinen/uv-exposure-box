// UV Exposure Box Controller Board
//
// I/O Pins
//
// PA0		Start button (low active)
// PA1		Mode button (low active)
// PD0 		Digit 1 (digit is enabled when output is high)
// PD1		Digit 2
// PD2		Digit 3
// PD3		Up led
// PD4		Down led
// PD5		MOSFET up
// PD6		MOSFET down
// PB0		Seg C
// PB1		Seg A	(segment lit when output is low)
// PB2		Seg G
// PB3		Seg B
// PB4		Seg F
// PB5		Seg D
// PB6		L1_L2	(dots are lit when output is high)
// PB7		Seg E

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

#define BUTTON_START	1
#define BUTTON_MODE		2

#define SEG_A		2
#define SEG_B		8
#define SEG_C		1
#define SEG_D		32
#define SEG_E		128
#define SEG_F		16
#define SEG_G		4
#define SEG_DOTS	64

// millis implementation

volatile uint32_t _millis = 0;
volatile uint16_t _1000us = 0;

void initMillis()
{ 
	// set timer0 prescaler
	// overflow timer0 every 0.256 ms
	TCCR0B |= 1<<CS00;		// prescaler 1, use this for F_CPU 1Mhz
	//TCCR0B |= 1<<CS01;		// prescaler 1/8, use this for F_CPU 8Mhz

	// enable timer overflow interrupt
	TIMSK  |= 1<<TOIE0;

	// enable global interrupts
	sei();
}

// interrupts routine
// timer overflow occur every 0.256 ms
ISR(TIMER0_OVF_vect)
{
	_1000us += 256;
	while(_1000us > 1000)
	{
		_millis++;
		_1000us -= 1000;
	}
}

uint32_t millis()
{
	uint32_t m;
	cli();
	m = _millis;
	sei();
	return m;
}

uint8_t segdata[] = 
{
	(SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F)^191,		// 0
	(SEG_B|SEG_C)^191,								// 1
	(SEG_A|SEG_B|SEG_D|SEG_E|SEG_G)^191,			// 2
	(SEG_A|SEG_B|SEG_C|SEG_D|SEG_G)^191,			// 3
	(SEG_B|SEG_C|SEG_F|SEG_G)^191,					// 4
	(SEG_A|SEG_C|SEG_D|SEG_F|SEG_G)^191,			// 5
	(SEG_A|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G)^191,		// 6
	(SEG_A|SEG_B|SEG_C)^191,						// 7
	(SEG_A|SEG_B|SEG_C|SEG_D|SEG_E|SEG_F|SEG_G)^191,// 8
	(SEG_A|SEG_B|SEG_C|SEG_D|SEG_F|SEG_G)^191,		// 9
};

uint8_t buttonState = 0;
uint8_t prevButtonState = 0;
uint8_t buttonPressed = 0;
uint8_t buttonReleased = 0;
uint8_t mode = 0;			// 0 = top, 1 = bottom, 2 = top+bottom
uint16_t exposureTime = 0;

void updateLeds()
{
	PORTD &= ~24;
	PORTD |= ((mode+1) & 3) << 3;
}

// Time in seconds. Dots are lit if dots == 0xff.
void updateDisplay(uint16_t time, uint8_t dots, uint8_t enableDigits = 0xff)
{
	static int digit = 0;
	digit++;
	if(digit == 3)
		digit = 0;

	// show minutes
	uint8_t mins = (time / 60) % 10;
	uint8_t secs = time % 60;
	uint8_t secs1 = secs / 10;
	uint8_t secs2 = secs % 10;

	if(digit == 0)
		time = mins;
	else if(digit == 1)
		time = secs1;
	else
		time = secs2;

	// turn off digits
	PORTD &= ~7;

	// set segments
	PORTB = segdata[time] | (dots & SEG_DOTS);

	// enable digit
	PORTD |= (1<<digit) & enableDigits;
}

void readConfig()
{
	exposureTime = eeprom_read_word(0);
	if(exposureTime >= 600)
		exposureTime = 30;	//DEBUG, was 120
}

void writeConfig()
{
	eeprom_write_word(0, exposureTime);
}

void updateButtons()
{
	// debouncing
	// read buttons only once every 5th ms
	static uint32_t lastButtonReadTime = 0;
	uint32_t ms = millis();
	if(millis() - lastButtonReadTime >= 5) {
		buttonState = (PINA & 3) ^ 3;
		lastButtonReadTime = ms;
	}

	buttonPressed = buttonState & (prevButtonState^255);
	buttonReleased = (buttonState^255) & prevButtonState;

	prevButtonState = buttonState;
}

void waitUntilButtonsReleased()
{
	do
	{
		updateButtons();
		updateDisplay(0, 0, 0);
	} while(buttonState != 0);
}

void timeSetup()
{
	uint8_t mins = exposureTime/60;
	uint8_t secs = exposureTime - mins*60;
	uint16_t blink = 512;

	// edit minutes
	while((buttonPressed & BUTTON_MODE) == 0)
	{
		updateButtons();
		exposureTime = mins * 60;
		updateDisplay(exposureTime, -1, blink < 512 ? 1 : 0);
		if(buttonPressed & BUTTON_START)
		{
			mins = (mins+1) % 10;
			blink = 0;
		}
		_delay_ms(1);
		blink = (blink+2) & 1023;
	}

	buttonPressed = 0;
	blink = 0;

	// edit seconds
	while((buttonPressed & BUTTON_MODE) == 0)
	{
		updateButtons();
		exposureTime = mins * 60 + secs;
		updateDisplay(exposureTime, -1, blink < 512 ? 7 : 1);
		if(buttonPressed & BUTTON_START)
		{
			secs = (secs+5) % 60;
			blink = 0;
		}
		_delay_ms(1);
		blink = (blink+2) & 1023;
	}

	writeConfig();
	waitUntilButtonsReleased();
}

void exposure()
{
	waitUntilButtonsReleased();

	uint32_t startTime = millis();
	uint16_t startButtonDownTime = 0;
	uint16_t blink = 0;

	// turn on mosfets
	PORTD |= (mode+1)<<5;

	while(true)
	{
		uint16_t elapsedTime = (millis() - startTime)/1000;
		uint16_t time = exposureTime - elapsedTime;
		if(elapsedTime >= exposureTime)
			time = 0;
		updateButtons();

		updateDisplay(time, blink < 512 ? -1 : 0, blink < 512 ? 0xff : 0);

		// exposure finished?
		if(time == 0)
		{
			blink = (blink + 2) & 1023;

			// turn off mosfets
			PORTD &= ~(32+64);

			if(buttonState & BUTTON_START)
				break;
		}

		// cancel exposure if start button is held down
		if(buttonState & BUTTON_START)
			startButtonDownTime++;
		else
			startButtonDownTime = 0;
		if(startButtonDownTime == 500)
			break;		

		_delay_ms(1);
	}

	// turn off mosfets
	PORTD &= ~(32+64);

	waitUntilButtonsReleased();
}

int main()
{
	// set button pins to input with pull ups enabled
	DDRA = 0;
	PORTA = 3;

	// set digit and segment pins to output
	DDRB = 255;
	DDRD = 127;

	readConfig();
	updateLeds();
	initMillis();

	uint16_t modeButtonDownTime = 0;
	uint16_t startButtonDownTime = 0;

	while(true)
	{
		updateButtons();
		updateDisplay(exposureTime, -1);

		// start exposure
		if(buttonState & BUTTON_START)
			startButtonDownTime++;
		else
			startButtonDownTime = 0;
		if(startButtonDownTime == 500)
			exposure();

		// toggle mode
		if(buttonReleased & BUTTON_MODE)
		{
			mode = (mode + 1) % 3;
			updateLeds();
		}

		// enter time setup mode?
		if(buttonState & BUTTON_MODE)
			modeButtonDownTime++;
		else
			modeButtonDownTime = 0;
		if(modeButtonDownTime == 1500)
			timeSetup();

		_delay_ms(1);
	}	

	return 0;
}
