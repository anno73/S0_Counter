#include <Arduino.h>

// #include <EEPROM.h>

#include <avr/eeprom.h>

#define S0_ADDR(A)	((EEPROM_S0_OFFSET) + ((id) * (EEPROM_S0_SIZE)) + (A))

/*
	EEPROM

	Size	Name

	2		Use DHCP
	4		IP
	4		Netmask
	4		Broadcast Address


	16		S0 name
	2		Impulses per kWh
	2		Active
	2		Delta before send base
	2		Delta before send factor
	2		Recurring sending interval base
	2		Recurring sending interval factor
	2
	2
	
*/

#define		EEPROM_S0_OFFSET		32
#define		EEPROM_S0_SIZE			32

#define		EEPROM_S0_FLAGS			0		// Bitvalues see below
#define		EEPROM_S0_CNT_PER_KWH	2
#define		EEPROM_S0_DELTA_BASE	4
#define		EEPROM_S0_DELTA_FACTOR	6
#define		EEPROM_S0_TIME_BASE		8
#define		EEPROM_S0_TIME_FACTOR	10
#define		EEPROM_S0_1				12
#define		EEPROM_S0_2				14
#define		EEPROM_S0_NAME			16
#define		EEPROM_S0_NAME_LENGTH	15

#define		EEPROM_S0_FLAG_ACTIVE				0
#define		EEPROM_S0_FLAG_UPDATE_VALUE_DELTA	1
#define		EEPROM_S0_FLAG_UPDATE_TIME_DELTA	2
#define		EEPROM_S0_FLAG_SEND_DELTA			3

// To use the library, define a class that subclasses CallBackInterface.
// And also, include a method (C++ talk for "subroutine") called "cbmethod()" in the class.
// Use this class as a template to create your own; it's not hard.  You don't
// even have to understand what you're doing at first.
// How do you subclass?  Like this:
class S0Impulse : public CallBackInterface
{
  public:
    uint16_t countHigh;
    uint16_t countLow;
    uint16_t countLowPerHigh;
	uint32_t count;

	uint16_t deltaBase;
	uint16_t deltaFactor;
	uint32_t delta;
	uint32_t reqNextUpdate;

	uint16_t timeBase;
	uint16_t timeFactor;
	uint32_t updateInterval;

    uint8_t pin;
    uint8_t id;
	uint8_t flags;
    uint8_t isActive;
	uint8_t reqDeltaUpdate;
	uint8_t reqTimeUpdate;
	uint8_t * requestUpdatePtr;

    S0Impulse(void)
    {
    };

#if 0
    S0Impulse(uint8_t _pin, uint16_t _countLowPerHigh): 
        pin(_pin), 
        countLowPerHigh(_countLowPerHigh)
    {
        init();
    };
#endif
	
    S0Impulse(uint8_t _pin, uint8_t _id): 
        pin(_pin), 
        id(_id)
    {
        initFromEEPROM();
    };

    S0Impulse(uint8_t _pin, uint16_t _countLowPerHigh, uint8_t _id): 
        pin(_pin), 
        countLowPerHigh(_countLowPerHigh),
        id(_id)
    {
        init();
    };

    // this gets called on a pin change event on pin "pin"
    // Increment low count. on overflow increment high count and reset low count.
    void cbmethod()
    {
        countLow ++;
        
        if (countLow >= countLowPerHigh)
        {
            countHigh ++;
            countLow = 0;
        }
    };
   
    uint16_t getCountLow() 
    {
        return countLow;
    }

    uint16_t getCountHigh() 
    {
        return countHigh;
    }

    uint16_t getCountLowPerHigh()
    {
        return countLowPerHigh;
    }

    uint16_t setCountLowPerHigh(uint16_t _countLowPerHigh)
    {
		eeprom_write_word((uint16_t *)S0_ADDR(EEPROM_S0_CNT_PER_KWH), _countLowPerHigh);
		
        return countLowPerHigh = _countLowPerHigh;
    }

    float getCount() 
    {
        // Disable interrupts - Prevent updates during calculations leading to loosing ticks
        // Disable interrupts as short as possible
        noInterrupts();
        // Make local copies of counters to work with
        uint16_t _high = countHigh;
        uint16_t _low = countLow;
        // Resume interrupts again
        interrupts();
        
        float f = (float) _high + (float) _low / (float) countLowPerHigh;
        return f;
    }

    float getCountClear() 
    {
        // Disable interrupts - Prevent updates during calculations leading to loosing ticks
        // Disable interrupts as short as possible
        noInterrupts();
        // Make local copies of counters to work with
        uint16_t _high = countHigh;
        uint16_t _low = countLow;
        // Clear counters
        countLow = countHigh = 0;
        // Resume interrupts again
        interrupts();
        
        float f = (float) _high + (float) _low / (float) countLowPerHigh;
        return f;
    }

    uint8_t getPin() 
	{
        return pin;
    }
    
    uint8_t getID() 
	{
        return id;
    }

    uint8_t setID(uint8_t _id) 
	{
        return id = _id;
    }

    uint8_t getIsActive ()
    {
        return isActive;
    }
    
    uint8_t setActive()
    {
		if (isActive)
			return isActive;
	
		isActive = 1;
		flags |= (1 << EEPROM_S0_FLAG_ACTIVE);
		eeprom_write_word((uint16_t *)S0_ADDR(EEPROM_S0_FLAGS), flags);
        return isActive;
    }

    uint8_t setInactive()
    {
		if (! isActive)
			return isActive;
		
		isActive = 0;
		flags &= ~(1 << EEPROM_S0_FLAG_ACTIVE);
		eeprom_write_word((uint16_t *)S0_ADDR(EEPROM_S0_FLAGS), flags);
        return isActive;
    }

    void reset() 
    {
        countLow = countHigh = 0;
    }

    void setName(const char * _cp) 
	{
		uint8_t nameLength = strlen(_cp);
		if (nameLength > EEPROM_S0_NAME_LENGTH)
			nameLength = EEPROM_S0_NAME_LENGTH;
			
		eeprom_write_block(_cp, (void *)S0_ADDR(EEPROM_S0_NAME), nameLength);
		eeprom_write_byte((uint8_t *)S0_ADDR(EEPROM_S0_NAME + nameLength), 0);
	
        return;
    }

	
 private:
    void init () 
    {
        pinMode(pin, INPUT);
        digitalWrite(pin, HIGH);
        PCintPort::attachInterrupt(pin, this, FALLING);
        countLow = countHigh = 0;
        isActive = 0;
    }
	
    void initFromEEPROM() 
    {
        pinMode(pin, INPUT);
        digitalWrite(pin, HIGH);
        PCintPort::attachInterrupt(pin, this, FALLING);
        countLow = countHigh = 0;

		// Flags
		flags = eeprom_read_byte((uint8_t *)S0_ADDR(EEPROM_S0_FLAGS));
        isActive 		= flags & (1 << EEPROM_S0_FLAG_ACTIVE);
		reqDeltaUpdate	= flags & (1 << EEPROM_S0_FLAG_UPDATE_VALUE_DELTA);
		reqTimeUpdate	= flags & (1 << EEPROM_S0_FLAG_UPDATE_TIME_DELTA);

		// countLowPerHigh
		countLowPerHigh = eeprom_read_word((uint16_t *)S0_ADDR(EEPROM_S0_CNT_PER_KWH));

		// delta values
		deltaBase = eeprom_read_word((uint16_t *)S0_ADDR(EEPROM_S0_DELTA_BASE));
		deltaFactor = eeprom_read_word((uint16_t *)S0_ADDR(EEPROM_S0_DELTA_FACTOR));

		// time values
		timeBase = eeprom_read_word((uint16_t *)S0_ADDR(EEPROM_S0_TIME_BASE));
		timeFactor = eeprom_read_word((uint16_t *)S0_ADDR(EEPROM_S0_TIME_FACTOR));

	}

};











