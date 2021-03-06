#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <bcm2835.h>
#include <common.h>

#define DHT_GPIO_OUTPUT bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_OUTP) 
#define DHT_GPIO_INPUT bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_INPT)
#define DHT_GPIO_PUDUP bcm2835_gpio_set_pud(pin, BCM2835_GPIO_PUD_UP)
#define DHT_GPIO_SET bcm2835_gpio_set(pin)
#define DHT_GPIO_CLR bcm2835_gpio_clr(pin)
#define DHT_GPIO_READ bcm2835_gpio_lev(pin)
#define DHT_GPIO_HIGH HIGH
#define DHT_GPIO_LOW LOW

// test shows bcm delay is worse than clock_gettime
//#define DHT_delay_ms(ms) bcm2835_delay(ms)
//#define DHT_delay_us(us) bcm2835_delayMicroseconds(us)
#define DHT_delay_ms(ms) cmn_delay_us((ms)*1000)
#define DHT_delay_us(us) cmn_delay_us(us)

typedef enum {
    DHT11   = 11,
    DHT21   = 21,
    DHT22   = 22,
    AM2302  = 22,
} DHT_Type;

typedef enum {
    DHT_ERROR_NONE = 0,
    DHT_ERROR_BUS_BUSY = 1,
    DHT_ERROR_NOT_PRESENT =2,
    DHT_ERROR_TIMEOUT = 3,
    DHT_ERROR_CHECKSUM = 4,
    DHT_ERROR_INVALID_PARAM = 5,
} DHT_Error;

int DHT_debug = 0;

int DHT_read(DHT_Type type, uint8_t pin, float* humidity, float* temperature)
{
    int count;
    int wait_ack_us;
    int ack_us;
    int data_ready_us;
    int bit_start_us[40];
    int bit_level_us[40];
    int bit;
    unsigned char sum;
    unsigned char data[5] = {0,0,0,0,0};

    // Send the activate pulse
    // Step 1: MCU send out start signal to DHT22 and DHT22 send
    //         response signal to MCU.
    // If always signal high-voltage-level, it means DHT22 is not 
    // working properly, plesee check the electrical connection status.
    DHT_GPIO_OUTPUT;
    DHT_delay_us(10); //10 us
    DHT_GPIO_CLR; //MCU send out start signal to DHT22
    DHT_delay_ms(1); //1 ms
    DHT_GPIO_SET; //MCU pull up
    DHT_delay_us(10); //10us
    DHT_GPIO_INPUT;
    //DHT_GPIO_PUDUP;

    // Find the start of the ACK Pulse
    count = 0;
    while(DHT_GPIO_READ == DHT_GPIO_HIGH)     // Exit on DHT22 pull low within 80us 
    {        
        if (count++ > 2)
	{
            fprintf(stderr,"no ACK\n");
            return DHT_ERROR_NOT_PRESENT;
        }
        DHT_delay_us(10);
    }
    wait_ack_us = count*10;
    
    // Find the last of the ACK Pulse
    count = 0;
    while(DHT_GPIO_READ == DHT_GPIO_LOW)     // Exit on DHT22 pull High within 80us 
    {        
        if (count++ > 10)
	{
            fprintf(stderr,"ACK pulse too long\n");
            return DHT_ERROR_NOT_PRESENT;
        }
        DHT_delay_us(10);
    }
    ack_us = count*10;
    
    // wait DHT pull low to sent the first bit.
    count = 0;
    while(DHT_GPIO_READ == DHT_GPIO_HIGH)     // Exit on DHT22 pull low  within 80us 
    {        
        if (count++ > 10)
	{
            fprintf(stderr,"no data sent\n");
            return DHT_ERROR_NOT_PRESENT;
        }
        DHT_delay_us(10);
    }
    data_ready_us = count*10;
    
     
    // Reading the 40 bit data stream
    // Step 2: DHT22 send data to MCU
    //         Start bit -> low volage within 50us
    //         0         -> high volage within 26-28 us
    //         1         -> high volage within 70us
    for (bit = 0; bit < 40; bit++)
    {
        // Getting start bit signal 
        count = 0;
	while(DHT_GPIO_READ == DHT_GPIO_LOW)        // Exit on high volage within 50us
	{        
            if (count++ > 10)
	    {
                fprintf(stderr,"bit %d start timeout\n", bit);
                return DHT_ERROR_TIMEOUT;
            }
            DHT_delay_us(10);
        }
	bit_start_us[bit] = count*10;
        
        // Measure the width of the data pulse
        count = 0;
	while(DHT_GPIO_READ == DHT_GPIO_HIGH)        // Exit on high volage within 50us
	{
            if (count++ > 10)
	    {
                fprintf(stderr,"bit %d width timeout\n", bit);
                return DHT_ERROR_TIMEOUT;
            }
            DHT_delay_us(10);
        }
	bit_level_us[bit] = count*10;

        if(count > 3)
	{
	    // connt > 20 is a 1(20*2 = 40us)
            data[bit/8] |= (1 << (7 - bit%8)); // start from MSB to LSB
        }
    }
    
    sum = data[0]+data[1]+data[2]+data[3];
    if(data[4] != sum)
    {
        fprintf(stderr,"error checksum %d %d %d %d %d %d \n",
                data[0], data[1], data[2], data[3], data[4], sum);
        return DHT_ERROR_CHECKSUM;
    }
    else
    {
        if (DHT_debug)
        {
            fprintf(stderr, "wait ack   %2d\n", wait_ack_us);
            fprintf(stderr, "ack        %2d\n", ack_us);
            fprintf(stderr, "data ready %2d\n", data_ready_us);
	    for (bit = 0; bit < 40; bit++)
	    {
                fprintf(stderr, "bit %2d start %2d level %2d\n",
	                bit, bit_start_us[bit], bit_level_us[bit]);
            }
        }
    }

    switch(type)
    {
        case DHT11:
            *humidity = (float)data[0];
            *temperature = (float)data[2];
            break; 

        case DHT21:
        case DHT22:
            *humidity = (float)((data[0] << 8)+data[1])/10;
            if((data[2]&0x80) == 0)
            {
                *temperature = (float)((data[2] << 8)+data[3])/10;
            }
            else
            {
                *temperature = -((float)(((data[2]&0x7F) << 8)+data[3])/10);
            }
            break;
       
        default:
            break;
    }
   
    return DHT_ERROR_NONE;
}

void DHT_usage(void)
{
    fprintf(stderr, "DHT <bcm|board> <pin>\n");
    fprintf(stderr, "bcm pin range: 2-26\n");
    fprintf(stderr, "board pin range: 1-40\n");
}

int main(int argc, char* argv[])
{
    DHT_Type type = DHT22;
    RPiGPIOPin pin;
    float humidity = 0, temperature = 0;

    if (argc != 3)
    {
        DHT_usage();
        return DHT_ERROR_INVALID_PARAM;
    }

    if (strcasecmp(argv[1], "bcm") == 0)
    {
        pin = (RPiGPIOPin)atoi(argv[2]);
        if (pin < 2 || pin > 26)
        {
            DHT_usage();
            return DHT_ERROR_INVALID_PARAM;
        }
    }
    else if (strcasecmp(argv[1], "board") == 0)
    {
        if ((pin = cmn_pin_board2bcm(atoi(argv[2]))) == -1)
        {
            DHT_usage();
            return DHT_ERROR_INVALID_PARAM;
        }
    }
    else
    {
        DHT_usage();
        return DHT_ERROR_INVALID_PARAM;
    }

    if (!bcm2835_init())
    {
        fprintf(stderr,"bcm init fail\n");
	return(1);
    }

    DHT_Error ret = DHT_read(type, pin, &humidity, &temperature);
    if (ret != DHT_ERROR_NONE)
    {
        bcm2835_close();
        return(1);
    }

    if (!bcm2835_close())
    {
        fprintf(stderr,"bcm close fail\n");
	return(1);
    }

    printf("temperature %.2f humidity %.2f\n", temperature, humidity);

    return(0);
}

