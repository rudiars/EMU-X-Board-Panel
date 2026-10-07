/*
 * ATmega8 Potentiometer Controller
 * AVR-GCC
 *
 * Hardware:
 *   ATmega8
 *   3-digit 7-segment COMMON ANODE
 *   4 direct potentiometers
 *   2 x 74HC4051D = 16 multiplexed potentiometers
 *
 * UART format:
 *   SLIDE,64
 *   BEND,80
 *   MOD,12
 *   AFT,127
 *   CC0,45
 *   ...
 *   CC15,100
 *
 * Notes:
 *   PB0 and PB1 are shared between:
 *     - 7-segment DIGIT 0 / DIGIT 1
 *     - 74HC4051 EN1 / EN2
 *
 *   Therefore display scanning and mux reading are time-multiplexed.
 */

#ifndef F_CPU
#define F_CPU 8000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>

/* =========================
   Configuration
   ========================= */

#define BAUD                9600UL
#define UBRR_VALUE          ((F_CPU / (16UL * BAUD)) - 1UL)

#define NUM_POTS             20
#define ADC_DIRECT_FIRST     1       /* PC1 */
#define MUX_ADC_CHANNEL      0       /* PC0 */

/*
 * Set to 1 if current values should be sent once after startup.
 * Set to 0 for "change only" behavior.
 */
#define REPORT_INITIAL_VALUES 0

/*
 * Display hold time after a pot changes.
 * The display continues to show the last changed pot value.
 */
#define DISPLAY_HOLD_MS       1500UL

/* =========================
   Potentiometer names
   ========================= */

static const char *pot_name[NUM_POTS] = {
    "SLIDE", "BEND", "MOD", "AFT",
    "CC0", "CC1", "CC2", "CC3",
    "CC4", "CC5", "CC6", "CC7",
    "CC8", "CC9", "CC10", "CC11",
    "CC12", "CC13", "CC14", "CC15"
};

/*
 * Last reported values.
 * 0..127 = valid value
 */
static uint8_t pot_value[NUM_POTS];

/* Value currently shown on 3-digit display */
static uint8_t display_value = 0;

/* =========================
   UART
   ========================= */

static void uart_init(void)
{
    UBRRH = (uint8_t)(UBRR_VALUE >> 8);
    UBRRL = (uint8_t)(UBRR_VALUE & 0xFF);

    /* 8 data bits, 1 stop bit, no parity */
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);

    UCSRB = (1 << TXEN);
}

static void uart_putc(char c)
{
    while (!(UCSRA & (1 << UDRE)))
        ;

    UDR = c;
}

static void uart_puts(const char *s)
{
    while (*s)
        uart_putc(*s++);
}

static void uart_send_pot(uint8_t index, uint8_t value)
{
    char buffer[24];

    snprintf(buffer, sizeof(buffer), "%s,%u\r\n",
             pot_name[index], value);

    uart_puts(buffer);
}

/* =========================
   ADC
   ========================= */

static void adc_init(void)
{
    /*
     * AVCC as reference, right adjusted.
     * ADC prescaler = 64.
     *
     * F_CPU = 8 MHz
     * ADC clock = 125 kHz
     */
    ADMUX = (1 << REFS0);

    ADCSRA =
        (1 << ADEN)  |
        (1 << ADPS2) |
        (1 << ADPS1);

    /* Dummy conversion after enabling ADC */
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC))
        ;
}

static uint16_t adc_read(uint8_t channel)
{
    ADMUX = (1 << REFS0) | (channel & 0x07);

    ADCSRA |= (1 << ADSC);

    while (ADCSRA & (1 << ADSC))
        ;

    return ADCW;
}

/*
 * Convert 10-bit ADC value 0..1023 to MIDI-style 7-bit 0..127.
 */
static uint8_t adc_to_127(uint16_t adc)
{
    return (uint8_t)(((uint32_t)adc * 127UL + 511UL) / 1023UL);
}

/* =========================
   74HC4051
   ========================= */

/*
 * 4051 address:
 *
 * S0 -> PB3
 * S1 -> PB4
 * S2 -> PB5
 *
 * Enable is active LOW:
 *   4051 #1 EN -> PB0
 *   4051 #2 EN -> PB1
 */

static void mux_address(uint8_t channel)
{
    uint8_t b = PORTB;

    b &= ~((1 << PB3) | (1 << PB4) | (1 << PB5));

    if (channel & 0x01)
        b |= (1 << PB3);

    if (channel & 0x02)
        b |= (1 << PB4);

    if (channel & 0x04)
        b |= (1 << PB5);

    PORTB = b;
}

static void mux_disable_all(void)
{
    /*
     * EN pins active LOW, therefore HIGH = disabled.
     */
    PORTB |= (1 << PB0) | (1 << PB1);
}

static void mux_enable(uint8_t mux)
{
    /*
     * Only one 4051 is enabled at a time.
     */
    PORTB |= (1 << PB0) | (1 << PB1);

    if (mux == 0)
        PORTB &= ~(1 << PB0);
    else
        PORTB &= ~(1 << PB1);
}

/* =========================
   7-Segment COMMON ANODE
   ========================= */

/*
 * Segment connections:
 *
 * A  -> PB7
 * B  -> PB6
 * C  -> PD3
 * D  -> PD4
 * E  -> PD5
 * F  -> PD6
 * G  -> PD7
 * DP -> PD2
 *
 * Common anode:
 *   segment LOW  = ON
 *   segment HIGH = OFF
 *
 * Digit commons:
 *   DIGIT 0 -> PB0
 *   DIGIT 1 -> PB1
 *   DIGIT 2 -> PB2
 *
 * Common anode digit:
 *   LOW  = ON
 *   HIGH = OFF
 */

static const uint8_t digit_segments[10] = {
    /* abcdefg, DP ignored */
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F  /* 9 */
};

static void display_all_off(void)
{
    /*
     * Because PB0/PB1 are also 4051 EN pins, setting them HIGH
     * simultaneously disables both muxes while no digit is active.
     */
    PORTB |= (1 << PB0) | (1 << PB1) | (1 << PB2);
}

static void display_set_segments(uint8_t number)
{
    uint8_t s = digit_segments[number];

    /*
     * Common anode: segment ON = LOW.
     */

    /* A -> PB7 */
    if (s & (1 << 0))
        PORTB &= ~(1 << PB7);
    else
        PORTB |= (1 << PB7);

    /* B -> PB6 */
    if (s & (1 << 1))
        PORTB &= ~(1 << PB6);
    else
        PORTB |= (1 << PB6);

    /* C -> PD3 */
    if (s & (1 << 2))
        PORTD &= ~(1 << PD3);
    else
        PORTD |= (1 << PD3);

    /* D -> PD4 */
    if (s & (1 << 3))
        PORTD &= ~(1 << PD4);
    else
        PORTD |= (1 << PD4);

    /* E -> PD5 */
    if (s & (1 << 4))
        PORTD &= ~(1 << PD5);
    else
        PORTD |= (1 << PD5);

    /* F -> PD6 */
    if (s & (1 << 5))
        PORTD &= ~(1 << PD6);
    else
        PORTD |= (1 << PD6);

    /* G -> PD7 */
    if (s & (1 << 6))
        PORTD &= ~(1 << PD7);
    else
        PORTD |= (1 << PD7);

    /* DP OFF */
    PORTD |= (1 << PD2);
}

static void display_scan(uint8_t value)
{
    uint8_t hundreds = value / 100;
    uint8_t tens     = (value / 10) % 10;
    uint8_t ones     = value % 10;

    /*
     * DIGIT 0
     */
    display_all_off();
    display_set_segments(hundreds);
    PORTB &= ~(1 << PB0);
    _delay_ms(1);

    /*
     * DIGIT 1
     */
    display_all_off();
    display_set_segments(tens);
    PORTB &= ~(1 << PB1);
    _delay_ms(1);

    /*
     * DIGIT 2
     */
    display_all_off();
    display_set_segments(ones);
    PORTB &= ~(1 << PB2);
    _delay_ms(1);

    display_all_off();
}

/* =========================
   Pot processing
   ========================= */

static void process_pot(uint8_t index, uint16_t adc)
{
    uint8_t value = adc_to_127(adc);

    if (value != pot_value[index]) {
        pot_value[index] = value;

        /* Update display immediately */
        display_value = value;

        /* Send only when value changes */
        uart_send_pot(index, value);
    }
}

/* =========================
   Main
   ========================= */

int main(void)
{
    uint8_t i;
    uint8_t mux;
    uint8_t channel;

    /*
     * PORT B:
     *
     * PB0 = DIGIT0 / 4051#1 EN
     * PB1 = DIGIT1 / 4051#2 EN
     * PB2 = DIGIT2
     * PB3 = 4051 S0
     * PB4 = 4051 S1
     * PB5 = 4051 S2
     * PB6 = Segment B
     * PB7 = Segment A
     */
    DDRB =
        (1 << PB0) |
        (1 << PB1) |
        (1 << PB2) |
        (1 << PB3) |
        (1 << PB4) |
        (1 << PB5) |
        (1 << PB6) |
        (1 << PB7);

    /*
     * PORT C:
     *
     * PC0 = 4051 common output / ADC0
     * PC1 = SLIDE / ADC1
     * PC2 = BEND  / ADC2
     * PC3 = MOD   / ADC3
     * PC4 = AFT   / ADC4
     */
    DDRC = 0x00;

    /*
     * PORT D:
     *
     * PD2 = DP
     * PD3 = C
     * PD4 = D
     * PD5 = E
     * PD6 = F
     * PD7 = G
     *
     * PD0/PD1 are UART RX/TX.
     */
    DDRD =
        (1 << PD2) |
        (1 << PD3) |
        (1 << PD4) |
        (1 << PD5) |
        (1 << PD6) |
        (1 << PD7);

    /* Initial states */
    PORTB = 0xFF;
    PORTD |=
        (1 << PD2) |
        (1 << PD3) |
        (1 << PD4) |
        (1 << PD5) |
        (1 << PD6) |
        (1 << PD7);

    uart_init();
    adc_init();

    /*
     * Initialize values.
     * Use the first scan as baseline so startup does not
     * generate 20 UART messages unless REPORT_INITIAL_VALUES=1.
     */
    for (i = 0; i < NUM_POTS; i++)
        pot_value[i] = 0;

    /*
     * Main scan loop.
     *
     * Display is scanned continuously.
     * ADC/mux reading is performed between display scans.
     */
    while (1)
    {
        /* -------------------------
           Direct pots: PC1..PC4
           ------------------------- */

        for (i = 0; i < 4; i++) {
            uint16_t adc = adc_read((uint8_t)(ADC_DIRECT_FIRST + i));

            process_pot(i, adc);

            display_scan(display_value);
        }

        /* -------------------------
           4051 pots: CC0..CC15
           ------------------------- */

        for (mux = 0; mux < 2; mux++) {

            for (channel = 0; channel < 8; channel++) {

                /*
                 * Disable both 4051 first.
                 * This also makes PB0/PB1 safe for display scanning.
                 */
                mux_disable_all();

                /*
                 * Select 4051 channel.
                 */
                mux_address(channel);

                /*
                 * Enable selected 4051.
                 */
                mux_enable(mux);

                /*
                 * Allow the analog switch and source impedance
                 * to settle before ADC conversion.
                 */
                _delay_us(20);

                /*
                 * PC0 = common output of the selected 4051.
                 */
                {
                    uint16_t adc = adc_read(MUX_ADC_CHANNEL);
                    uint8_t index = (uint8_t)(4 + mux * 8 + channel);

                    process_pot(index, adc);
                }

                /*
                 * Disable mux again before display scan.
                 */
                mux_disable_all();

                display_scan(display_value);
            }
        }

#if REPORT_INITIAL_VALUES
        /*
         * Optional startup reporting can be implemented here if desired.
         * Default is 0, so this block is not compiled.
         */
#endif
    }

    return 0;
}
