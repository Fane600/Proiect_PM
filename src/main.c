#ifndef F_CPU
#define F_CPU 16000000UL
#endif

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <string.h>

/* ===================== PINI PROIECT ===================== */

#define LED_ROSU    PB0
#define LED_VERDE   PB1
#define BUZZER      PB2

#define PIR_PIN     PD2
#define BUTTON      PD3

#define LCD_ADDR    0x27

/* ===================== STARI SISTEM ===================== */

typedef enum {
    STARE_INIT = 255,
    STARE_DEZARMAT = 0,
    STARE_ARMAT,
    STARE_ALARMA
} AlarmState;

AlarmState state = STARE_INIT;

/* Counter pentru miscarile detectate cat timp alarma este declansata */
uint16_t motion_count = 0;

/* Retine starea anterioara a senzorului PIR, ca sa numaram doar detectiile noi */
uint8_t last_pir_state = 0;

/* ===================== USART / BLUETOOTH ===================== */

#define BAUD 9600
#define UBRR_VALUE ((F_CPU / 16 / BAUD) - 1)

void USART_init(void)
{
    /*
      Initializam USART-ul pentru comunicatia cu modulul Bluetooth HC-05.
      Folosim 9600 baud, 8 biti de date, fara paritate si 1 bit de stop.
    */

    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)UBRR_VALUE;

    // Activam transmisia si receptia USART
    UCSR0B = (1 << TXEN0) | (1 << RXEN0);

    // Format frame: 8 biti date, 1 stop bit, fara paritate
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);
}

void USART_send_char(char c)
{
    /*
      Trimite un singur caracter prin USART.
      Asteapta pana cand registrul de transmisie este liber.
    */

    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = c;
}

void USART_send_string(const char *str)
{
    /*
      Trimite un sir de caractere prin Bluetooth.
      Este folosit pentru mesaje precum ARMAT, DEZARMAT, ALERTA etc.
    */

    while (*str) {
        USART_send_char(*str++);
    }
}

void USART_send_uint16(uint16_t value)
{
    /*
      Trimite un numar intreg prin USART.
      Este folosit pentru a trimite numarul de miscari detectate.
    */

    char buffer[6];
    uint8_t i = 0;

    if (value == 0) {
        USART_send_char('0');
        return;
    }

    // Construim numarul invers, cifra cu cifra
    while (value > 0) {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    // Trimitem cifrele in ordinea corecta
    while (i > 0) {
        USART_send_char(buffer[--i]);
    }
}

uint8_t USART_available(void)
{
    /*
      Verifica daca exista un caracter primit prin USART.
      Daca RXC0 este setat, inseamna ca putem citi din UDR0.
    */

    return (UCSR0A & (1 << RXC0));
}

char USART_read_char(void)
{
    /*
      Citeste un caracter primit prin Bluetooth.
      Caracterul este luat din registrul UDR0.
    */

    return UDR0;
}

/* ===================== I2C / TWI ===================== */

void TWI_init(void)
{
    /*
      Initializam interfata TWI/I2C pentru LCD.
      LCD-ul nostru a mers stabil cu pull-up intern pe SDA/SCL si frecventa mai mica.
    */

    // pull-up intern pe SDA/SCL, a fost necesar pentru LCD-ul nostru
    PORTC |= (1 << PC4) | (1 << PC5);

    TWSR = 0x00;

    // I2C mai lent, stabil pentru modulul LCD
    TWBR = 152;
}

void TWI_start(void)
{
    /*
      Trimite conditia START pe magistrala I2C.
      Dupa START, microcontrollerul poate trimite adresa dispozitivului.
    */

    TWCR = (1 << TWINT) | (1 << TWSTA) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

void TWI_stop(void)
{
    /*
      Trimite conditia STOP pe magistrala I2C.
      Asta marcheaza finalul unei transmisii.
    */

    TWCR = (1 << TWINT) | (1 << TWSTO) | (1 << TWEN);
    _delay_us(20);
}

void TWI_write(uint8_t data)
{
    /*
      Scrie un byte pe magistrala I2C.
      Este folosit atat pentru adresa LCD-ului, cat si pentru date/comenzi.
    */

    TWDR = data;
    TWCR = (1 << TWINT) | (1 << TWEN);
    while (!(TWCR & (1 << TWINT)));
}

/* ===================== LCD I2C ===================== */

#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE    0x04
#define LCD_RS        0x01

void LCD_expander_write(uint8_t data)
{
    /*
      Trimite un byte catre expanderul PCF8574 de pe adaptorul I2C al LCD-ului.
      LCD-ul este la adresa 0x27.
    */

    TWI_start();
    TWI_write(LCD_ADDR << 1);
    TWI_write(data | LCD_BACKLIGHT);
    TWI_stop();
}

void LCD_pulse_enable(uint8_t data)
{
    /*
      Genereaza impulsul ENABLE necesar pentru LCD.
      LCD-ul citeste datele cand semnalul ENABLE este pulsatoriu.
    */

    LCD_expander_write(data | LCD_ENABLE);
    _delay_us(2);
    LCD_expander_write(data & ~LCD_ENABLE);
    _delay_us(100);
}

void LCD_write4(uint8_t data)
{
    /*
      Trimite 4 biti catre LCD.
      LCD-ul este folosit in modul 4-bit, deci fiecare byte se trimite in doua parti.
    */

    LCD_expander_write(data);
    LCD_pulse_enable(data);
}

void LCD_send(uint8_t value, uint8_t mode)
{
    /*
      Trimite un byte catre LCD, fie comanda, fie caracter.
      mode = 0 inseamna comanda.
      mode = LCD_RS inseamna date/caracter.
    */

    uint8_t high = value & 0xF0;
    uint8_t low  = (value << 4) & 0xF0;

    LCD_write4(high | mode);
    LCD_write4(low | mode);
}

void LCD_command(uint8_t cmd)
{
    /*
      Trimite o comanda catre LCD.
      Exemple: clear display, set cursor, display on/off.
    */

    LCD_send(cmd, 0);
}

void LCD_data(uint8_t data)
{
    /*
      Trimite un caracter catre LCD.
      Este folosit de LCD_print().
    */

    LCD_send(data, LCD_RS);
}

void LCD_clear(void)
{
    /*
      Sterge continutul LCD-ului si muta cursorul la inceput.
    */

    LCD_command(0x01);
    _delay_ms(3);
}

void LCD_init(void)
{
    /*
      Initializarea LCD-ului in modul 4-bit.
      Delay-urile sunt mai mari pentru stabilitate, deoarece modulul nostru I2C era sensibil la timing.
    */

    _delay_ms(100);

    LCD_write4(0x30);
    _delay_ms(10);

    LCD_write4(0x30);
    _delay_ms(10);

    LCD_write4(0x30);
    _delay_ms(10);

    LCD_write4(0x20);
    _delay_ms(10);

    LCD_command(0x28); // 4-bit, 2 linii
    _delay_ms(2);

    LCD_command(0x08); // display off
    _delay_ms(2);

    LCD_clear();

    LCD_command(0x06); // cursor increment
    _delay_ms(2);

    LCD_command(0x0C); // display on, cursor off
    _delay_ms(2);
}

void LCD_set_cursor(uint8_t row, uint8_t col)
{
    /*
      Seteaza pozitia cursorului pe LCD.
      row = 0 pentru primul rand, row = 1 pentru al doilea rand.
    */

    uint8_t addr;

    if (row == 0)
        addr = 0x00 + col;
    else
        addr = 0x40 + col;

    LCD_command(0x80 | addr);
}

void LCD_print(const char *str)
{
    /*
      Afiseaza un sir de caractere pe LCD, de la pozitia curenta a cursorului.
    */

    while (*str) {
        LCD_data(*str++);
    }
}

void LCD_show_message(const char *line1, const char *line2)
{
    /*
      Afiseaza doua linii pe LCD.
      Este folosita pentru starile sistemului: dezarmat, armat, alerta.
    */

    LCD_clear();

    LCD_set_cursor(0, 0);
    LCD_print(line1);

    LCD_set_cursor(1, 0);
    LCD_print(line2);
}

/* ===================== BUZZER PWM ===================== */

void buzzer_start(void)
{
    DDRB |= (1 << BUZZER);

    /*
      Timer1 Fast PWM, TOP = ICR1
      PB2 = OC1B
      frecventa aproximativa = 2 kHz

      F_PWM = F_CPU / (prescaler * (1 + ICR1))
      ICR1 = 16000000 / (8 * 2000) - 1 = 999
    */

    ICR1 = 999;
    OCR1B = 100; // duty cycle ~10%

    // Fast PWM mode 14, OC1B non-inverting, prescaler 8
    TCCR1A = (1 << COM1B1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11);
}

void buzzer_stop(void)
{
    /*
      Opreste buzzerul.
      Modulul nostru este low level trigger, deci HIGH inseamna oprit.
    */

    // Oprim Timer1
    TCCR1A = 0;
    TCCR1B = 0;

    // PB2 ramane iesire
    DDRB |= (1 << BUZZER);

    // Pentru low level trigger: HIGH = oprit
    PORTB |= (1 << BUZZER);
}

/* ===================== GPIO ===================== */

void gpio_init(void)
{
    /*
      Configureaza pinii folositi in proiect.
      LED-urile si buzzerul sunt iesiri.
      PIR-ul si butonul sunt intrari.
    */

    // LED-uri si buzzer ca iesiri
    DDRB |= (1 << LED_ROSU) | (1 << LED_VERDE) | (1 << BUZZER);

    // PIR pe PD2 ca intrare, fara pull-up
    DDRD &= ~(1 << PIR_PIN);
    PORTD &= ~(1 << PIR_PIN);

    // Buton pe PD3 ca intrare, cu pull-up intern
    DDRD &= ~(1 << BUTTON);
    PORTD |= (1 << BUTTON);

    // starea initiala
    PORTB &= ~(1 << LED_ROSU);
    PORTB |= (1 << LED_VERDE);
    buzzer_stop();
}

/* ===================== LOGICA SISTEM ===================== */

void set_state(AlarmState new_state)
{
    /*
      Schimba starea sistemului si actualizeaza iesirile.
      Starile sunt: dezarmat, armat si alarma declansata.
    */

    if (state == new_state)
        return;

    AlarmState old_state = state;
    state = new_state;

    if (state == STARE_DEZARMAT) {
        PORTB |= (1 << LED_VERDE);
        PORTB &= ~(1 << LED_ROSU);
        buzzer_stop();

        LCD_show_message("Sistem", "dezarmat");
        USART_send_string("DEZARMAT\r\n");

        // Daca dezarmam dupa o alarma, trimitem numarul total de miscari
        if (old_state == STARE_ALARMA) {
            USART_send_string("TOTAL MISCARI: ");
            USART_send_uint16(motion_count);
            USART_send_string("\r\n");
        }

        // Resetam counterul cand sistemul ajunge in starea dezarmat
        motion_count = 0;
        last_pir_state = (PIND & (1 << PIR_PIN)) ? 1 : 0;
    }
    else if (state == STARE_ARMAT) {
        PORTB &= ~(1 << LED_VERDE);
        PORTB |= (1 << LED_ROSU);
        buzzer_stop();

        // La armare, resetam counterul de miscari
        motion_count = 0;
        last_pir_state = (PIND & (1 << PIR_PIN)) ? 1 : 0;

        LCD_show_message("Sistem", "armat");
        USART_send_string("ARMAT\r\n");
    }
    else if (state == STARE_ALARMA) {
        PORTB &= ~(1 << LED_VERDE);

        LCD_show_message("ALERTA!", "Miscare detect.");
        USART_send_string("ALERTA\r\n");
    }
}

void send_status(void)
{
    /*
      Trimite starea curenta prin Bluetooth.
      Daca alarma este declansata, trimite si numarul de miscari detectate.
    */

    if (state == STARE_DEZARMAT) {
        USART_send_string("STATUS: DEZARMAT\r\n");
    }
    else if (state == STARE_ARMAT) {
        USART_send_string("STATUS: ARMAT\r\n");
    }
    else {
        USART_send_string("STATUS: ALERTA, MISCARI: ");
        USART_send_uint16(motion_count);
        USART_send_string("\r\n");
    }
}

/* ===================== BUTON ===================== */

uint8_t button_pressed_event(void)
{
    /*
      Detecteaza o apasare noua a butonului.
      Butonul foloseste pull-up intern:
      - neapasat = HIGH
      - apasat = LOW
      Se foloseste un debounce simplu de 50 ms.
    */

    static uint8_t last_state = 1;
    uint8_t current_state = (PIND & (1 << BUTTON)) ? 1 : 0;

    if (last_state == 1 && current_state == 0) {
        _delay_ms(50);

        if (!(PIND & (1 << BUTTON))) {
            last_state = 0;
            return 1;
        }
    }

    if (current_state == 1) {
        last_state = 1;
    }

    return 0;
}

/* ===================== PIR / COUNTER MISCARI ===================== */

void pir_update(void)
{
    /*
      Citeste senzorul PIR si numara miscarile.
      Miscarea se numara doar pe tranzitia LOW -> HIGH, ca sa nu numaram aceeasi detectie de mai multe ori.
    */

    uint8_t current_pir_state = (PIND & (1 << PIR_PIN)) ? 1 : 0;

    // Numaram doar trecerea LOW -> HIGH, nu cat timp senzorul ramane HIGH
    if (last_pir_state == 0 && current_pir_state == 1) {

        if (state == STARE_ARMAT) {
            motion_count = 1;
            set_state(STARE_ALARMA);

            USART_send_string("MISCARE #");
            USART_send_uint16(motion_count);
            USART_send_string("\r\n");
        }
        else if (state == STARE_ALARMA) {
            motion_count++;

            USART_send_string("MISCARE #");
            USART_send_uint16(motion_count);
            USART_send_string("\r\n");
        }
    }

    last_pir_state = current_pir_state;
}

/* ===================== BLUETOOTH COMMANDS ===================== */

void process_command(char *cmd)
{
    /*
      Proceseaza comenzile primite prin Bluetooth.
      Comenzi acceptate:
      ARM - armeaza sistemul
      DISARM - dezarmeaza sistemul
      STATUS - trimite starea curenta
    */

    if (strcmp(cmd, "ARM") == 0) {
        set_state(STARE_ARMAT);
    }
    else if (strcmp(cmd, "DISARM") == 0) {
        set_state(STARE_DEZARMAT);
    }
    else if (strcmp(cmd, "STATUS") == 0) {
        send_status();
    }
    else {
        USART_send_string("COMANDA NECUNOSCUTA\r\n");
    }
}

void bluetooth_update(void)
{
    /*
      Citeste caracterele primite prin Bluetooth si le pune intr-un buffer.
      Comanda este procesata cand se primeste Enter, adica '\r' sau '\n'.
    */

    static char buffer[20];
    static uint8_t index = 0;

    while (USART_available()) {
        char c = USART_read_char();

        if (c == '\r' || c == '\n') {
            if (index > 0) {
                buffer[index] = '\0';
                process_command(buffer);
                index = 0;
            }
        }
        else {
            if (index < sizeof(buffer) - 1) {
                buffer[index++] = c;
            }
        }
    }
}

/* ===================== EFECT ALARMA ===================== */

void alarm_effect_update(void)
{
    /*
      Controleaza efectele din starea de alarma:
      - LED-ul rosu palpaie
      - buzzerul suna intermitent
      Functia este apelata la fiecare 10 ms in bucla principala.
    */

    static uint16_t blink_counter = 0;
    static uint16_t beep_counter = 0;
    static uint8_t buzzer_on = 0;

    if (state != STARE_ALARMA) {
        blink_counter = 0;
        beep_counter = 0;
        buzzer_on = 0;
        return;
    }

    // LED rosu palpaie la ~250 ms
    blink_counter += 10;
    if (blink_counter >= 250) {
        PORTB ^= (1 << LED_ROSU);
        blink_counter = 0;
    }

    // Buzzer intermitent: 200 ms ON, 200 ms OFF
    beep_counter += 10;
    if (beep_counter >= 200) {
        beep_counter = 0;
        buzzer_on = !buzzer_on;

        if (buzzer_on)
            buzzer_start();
        else
            buzzer_stop();
    }
}

/* ===================== MAIN ===================== */

int main(void)
{
    /*
      Initializam toate modulele:
      GPIO, Bluetooth USART, I2C si LCD.
      Apoi pornim sistemul in starea dezarmat.
    */

    gpio_init();
    USART_init();
    TWI_init();
    LCD_init();

    LCD_show_message("Sistem alarma", "pornit");
    USART_send_string("SISTEM PORNIT\r\n");

    _delay_ms(3000);

    set_state(STARE_DEZARMAT);

    while (1)
    {
        // buton local
        if (button_pressed_event()) {
            if (state == STARE_DEZARMAT) {
                set_state(STARE_ARMAT);
            }
            else {
                set_state(STARE_DEZARMAT);
            }
        }

        // comenzi Bluetooth: ARM, DISARM, STATUS
        bluetooth_update();

        // PIR este verificat si in starea ARMAT, si in starea ALARMA
        pir_update();

        // efecte pentru alarma declansata
        alarm_effect_update();

        _delay_ms(10);
    }

    return 0;
}
