#include <stdio.h>
#include <bcm2835.h>

#define DHT_GPIO_OUTPUT bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_OUTP) 
#define DHT_GPIO_INPUT bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_INPT)
#define DHT_GPIO_PUDUP bcm2835_gpio_set_pud(pin, BCM2835_GPIO_PUD_UP)
#define DHT_GPIO_SET bcm2835_gpio_set(pin)
#define DHT_GPIO_CLR bcm2835_gpio_clr(pin)
#define DHT_GPIO_READ bcm2835_gpio_lev(pin)
#define DHT_wait_ms(ms) bcm2835_delay(ms)
#define DHT_wait_us(us) bcm2835_delayMicroseconds(us)
#define DHT_GPIO_HIGH HIGH
#define DHT_GPIO_LOW LOW

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))

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
} DHT_Error;


int DHT_read(DHT_Type type, uint8_t pin, float* humidity, float* temperature)
{
    int count;
    int stat_wait_ACK_start[2] = {9999,0};
    int stat_ACK_width[2] = {9999,0};
    int stat_data_start_width[2] = {9999,0};
    int stat_bit_start_width[2] = {9999,0};
    int stat_bit_width[2] = {9999,0};
    int bit;
    char sum;
    char data[5] = {0,0,0,0,0};

    // Send the activate pulse
    // Step 1: MCU send out start signal to DHT22 and DHT22 send
    //         response signal to MCU.
    // If always signal high-voltage-level, it means DHT22 is not 
    // working properly, plesee check the electrical connection status.
    DHT_GPIO_OUTPUT;
    DHT_wait_ms(1); //18 milliseconds
    DHT_GPIO_CLR; //MCU send out start signal to DHT22
    DHT_wait_ms(1); //18 ms
    DHT_GPIO_SET; //MCU pull up
    DHT_wait_us(40); //40us
    DHT_GPIO_INPUT;
    //DHT_GPIO_PUDUP;

    // Find the start of the ACK Pulse
    count = 0;
    do
    {        
        if (count++ > 10)
	{     // (Spec is 80 us, 40*2 == 80us
            fprintf(stderr,"not present ACK start\n");
            return DHT_ERROR_NOT_PRESENT;;
        }
        DHT_wait_us(2);
    }
    while(DHT_GPIO_READ == DHT_GPIO_HIGH);     // Exit on DHT22 pull low within 80us 
    stat_wait_ACK_start[0] = MIN(stat_wait_ACK_start[0], count);
    stat_wait_ACK_start[1] = MAX(stat_wait_ACK_start[1], count);
    
    // Find the last of the ACK Pulse
    count = 0;
    do
    {        
        if (count++ > 40)
	{     // (Spec is 80 us, 40*2 == 80us
            fprintf(stderr,"not present ACK last\n");
            return DHT_ERROR_NOT_PRESENT;;
        }
        DHT_wait_us(2);
    }
    while(DHT_GPIO_READ == DHT_GPIO_LOW);     // Exit on DHT22 pull High within 80us 
    stat_ACK_width[0] = MIN(stat_ACK_width[0], count);
    stat_ACK_width[1] = MAX(stat_ACK_width[1], count);
    
    // wait DHT pull low to sent the first bit.
    count = 0;
    do
    {        
        if (count++ > 50)
	{     // (Spec is 80 us, 40*2 == 80us
            fprintf(stderr,"no data sent\n");
            return DHT_ERROR_NOT_PRESENT;;
        }
        DHT_wait_us(2);
    }
    while(DHT_GPIO_READ == DHT_GPIO_HIGH);     // Exit on DHT22 pull low  within 80us 
    stat_data_start_width[0] = MIN(stat_data_start_width[0], count);
    stat_data_start_width[1] = MAX(stat_data_start_width[1], count);
    
     
    // Reading the 40 bit data stream
    // Step 2: DHT22 send data to MCU
    //         Start bit -> low volage within 50us
    //         0         -> high volage within 26-28 us
    //         1         -> high volage within 70us
    for (bit = 0; bit < 40; bit++)
    {
        // Getting start bit signal 
        count = 0;
        do
	{        
            if (count++ > 40)
	    {     // Spec is 50 us, 25*2 == 50us
                fprintf(stderr,"bit %d start timeout\n", bit);
                return DHT_ERROR_TIMEOUT;
            }
            DHT_wait_us(2);
        }
	while(DHT_GPIO_READ == DHT_GPIO_LOW);        // Exit on high volage within 50us
	stat_bit_start_width[0] = MIN(stat_bit_start_width[0], count);
	stat_bit_start_width[1] = MAX(stat_bit_start_width[1], count);
        
        // Measure the width of the data pulse
        count = 0;
        do
	{
            if (count++ > 50)
	    {     // Spec is 80 us, 40*2 == 80us
                fprintf(stderr,"bit %d width timeout\n", bit);
                return DHT_ERROR_TIMEOUT;
            }
            DHT_wait_us(2);
        }
	while(DHT_GPIO_READ == DHT_GPIO_HIGH);        // Exit on high volage within 50us
	stat_bit_width[0] = MIN(stat_bit_width[0], count);
	stat_bit_width[1] = MAX(stat_bit_width[1], count);
        
        if(count > 20)
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
        fprintf(stderr, "wait ACK %d %d\n",
	        stat_wait_ACK_start[0], stat_wait_ACK_start[1]);
        fprintf(stderr, "ACK width %d %d\n",
	        stat_ACK_width[0], stat_ACK_width[1]);
        fprintf(stderr, "data start %d %d\n",
	        stat_data_start_width[0], stat_data_start_width[1]);
        fprintf(stderr, "bit start %d %d\n",
	        stat_bit_start_width[0], stat_bit_start_width[1]);
        fprintf(stderr, "bit width %d %d\n",
	        stat_bit_width[0], stat_bit_width[1]);
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

int main(int argc, char* argv[])
{
    DHT_Type type = DHT22;
    RPiGPIOPin pin = RPI_BPLUS_GPIO_J8_03;
    float humidity = 0, temperature = 0;

    if (!bcm2835_init())
    {
        fprintf(stderr,"bcm init fail\n");
	return(1);
    }

    DHT_Error ret = DHT_read(type, pin, &humidity, &temperature);
    if (ret != DHT_ERROR_NONE){
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
