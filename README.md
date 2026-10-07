# ATmega8 Potentiometer Controller

Firmware AVR-GCC untuk **ATmega8** yang membaca:

- 4 potentiometer langsung melalui ADC ATmega8.
- 16 potentiometer melalui 2 buah **74HC4051D**.
- Menampilkan nilai potentiometer terakhir berubah pada **7-segment 3 digit common anode**.
- Mengirim nilai ke UART hanya ketika nilai potentiometer berubah.

Project ini mempertahankan mapping pin hardware hasil reverse engineering dan **tidak mengubah koneksi pin**.

---

## 1. Hardware

### ATmega8

Diasumsikan clock:

```c
F_CPU = 8000000UL
```

UART:

```text
9600 baud
8 data bits
1 stop bit
No parity
```

---

## 2. Seven Segment 3 Digit

Display adalah **common anode**.

### Mapping segment

| Segment | ATmega8 |
|---|---|
| A | PB7 |
| B | PB6 |
| C | PD3 |
| D | PD4 |
| E | PD5 |
| F | PD6 |
| G | PD7 |
| DP | PD2 |

### Mapping common digit

| Digit | ATmega8 |
|---|---|
| Digit 0 | PB0 |
| Digit 1 | PB1 |
| Digit 2 | PB2 |

Karena display common anode:

```text
Segment LOW  = ON
Segment HIGH = OFF

Digit LOW    = ON
Digit HIGH   = OFF
```

Nilai `0..127` ditampilkan sebagai tiga digit:

```text
0   -> 000
7   -> 007
64  -> 064
127 -> 127
```

---

## 3. Potentiometer Direct

Empat potentiometer terhubung langsung ke ADC:

| Nama | Pin | ADC |
|---|---|---|
| SLIDE | PC1 | ADC1 |
| BEND | PC2 | ADC2 |
| MOD | PC3 | ADC3 |
| AFT | PC4 | ADC4 |

ADC ATmega8 adalah 10-bit:

```text
0 ... 1023
```

Firmware mengubahnya menjadi:

```text
0 ... 127
```

Rumus:

```text
value = ADC * 127 / 1023
```

---

## 4. Dua buah 74HC4051D

Kedua 4051 menggunakan address line yang sama.

### Address

| 4051 | ATmega8 |
|---|---|
| S0 | PB3 |
| S1 | PB4 |
| S2 | PB5 |

### Enable

| IC | Enable | ATmega8 |
|---|---|---|
| 4051 #1 | EN | PB0 |
| 4051 #2 | EN | PB1 |

EN 74HC4051 adalah **active LOW**:

```text
LOW  = enabled
HIGH = disabled
```

### Output analog

Output kedua 4051 digabung ke:

```text
PC0 / ADC0
```

Jadi:

```text
4051 #1 CH0..CH7 -> CC0..CC7
4051 #2 CH0..CH7 -> CC8..CC15
```

---

## 5. Mapping CC

| Nama | IC | Channel |
|---|---|---|
| CC0 | 4051 #1 | CH0 |
| CC1 | 4051 #1 | CH1 |
| CC2 | 4051 #1 | CH2 |
| CC3 | 4051 #1 | CH3 |
| CC4 | 4051 #1 | CH4 |
| CC5 | 4051 #1 | CH5 |
| CC6 | 4051 #1 | CH6 |
| CC7 | 4051 #1 | CH7 |
| CC8 | 4051 #2 | CH0 |
| CC9 | 4051 #2 | CH1 |
| CC10 | 4051 #2 | CH2 |
| CC11 | 4051 #2 | CH3 |
| CC12 | 4051 #2 | CH4 |
| CC13 | 4051 #2 | CH5 |
| CC14 | 4051 #2 | CH6 |
| CC15 | 4051 #2 | CH7 |

---

## 6. Konflik PB0 dan PB1

Ini adalah bagian penting dari desain hardware.

PB0 dan PB1 dipakai untuk dua fungsi:

```text
PB0 -> Digit 0 + 4051 #1 EN
PB1 -> Digit 1 + 4051 #2 EN
```

Karena ini merupakan hardware reverse engineering dan pin tidak dapat diubah, firmware menggunakan **time multiplexing**.

Saat membaca 4051:

```text
PB0/PB1 digunakan sebagai EN 4051
```

Saat menampilkan display:

```text
PB0/PB1 digunakan sebagai common digit
```

Kedua operasi tidak dilakukan bersamaan.

Urutannya:

```text
disable 4051
      |
      v
scan display
      |
      v
set address 4051
      |
      v
enable salah satu 4051
      |
      v
ADC read PC0
      |
      v
disable 4051
      |
      v
scan display
```

Dengan cara ini tidak diperlukan perubahan hardware.

---

## 7. UART Output

Ketika nilai berubah, firmware mengirim:

```text
NAMA,NILAI
```

diikuti `CR/LF`.

Contoh:

```text
SLIDE,64
BEND,91
MOD,25
AFT,127
CC0,45
CC1,83
CC7,12
CC15,100
```

Nilai hanya dikirim jika hasil pembacaan 0..127 berubah.

---

## 8. Alur Program

Secara sederhana:

```text
                 +----------------+
                 |    ATmega8     |
                 +----------------+
                   |      |     |
                   |      |     |
                ADC1-4   ADC0   UART
                   |      |       |
              4 direct   4051   Serial
               pots       |
                         / \
                        /   \
                  4051 #1   4051 #2
                   |           |
                 CC0-7       CC8-15


        PB7/PB6/PD3..PD7
                |
                v
        3 Digit 7-Segment
          Common Anode
```

---

## 9. Build dengan avr-gcc

Contoh command:

```bash
avr-gcc -mmcu=atmega8 -DF_CPU=8000000UL -Os -Wall -Wextra \
    -o ATmega8_Pot_Controller.elf ATmega8_Pot_Controller.c
```

Generate HEX:

```bash
avr-objcopy -O ihex -R .eeprom \
    ATmega8_Pot_Controller.elf ATmega8_Pot_Controller.hex
```

Cek ukuran:

```bash
avr-size ATmega8_Pot_Controller.elf
```

---

## 10. Upload

Contoh menggunakan `avrdude` dengan USBasp:

```bash
avrdude -c usbasp -p m8 \
    -U flash:w:ATmega8_Pot_Controller.hex:i
```

Sesuaikan programmer jika menggunakan programmer lain.

---

## 11. Catatan ADC dan Potentiometer

Untuk mendapatkan pembacaan stabil:

- Gunakan ground yang sama antara ATmega8, potentiometer, dan 4051.
- Pastikan tegangan analog tidak melebihi tegangan referensi ADC.
- Potentiometer sebaiknya berada pada range yang sesuai, misalnya 0 V sampai AVCC.
- Karena 4051 mempunyai resistansi ON dan source impedance dapat mempengaruhi ADC, firmware memberikan waktu settling sebelum membaca ADC.

Firmware menggunakan:

```text
ADC clock = 125 kHz
```

dengan:

```text
F_CPU = 8 MHz
ADC prescaler = 64
```

---

## 12. Common Anode

Jika angka tampil terbalik atau semua segment menyala, periksa kembali jenis display.

Firmware ini khusus:

```text
COMMON ANODE
```

Bukan common cathode.

Untuk common anode:

```text
          +V
           |
       Common Anode
           |
      +----+----+
      |         |
    digit     digit
      |
    segment
      |
     MCU

MCU LOW  -> LED segment ON
MCU HIGH -> LED segment OFF
```

---

## 13. Troubleshooting

### UART benar tetapi display salah

Periksa:

1. Display benar-benar common anode.
2. Mapping A/B/C/D/E/F/G sesuai tabel.
3. Common digit 0/1/2 sesuai PB0/PB1/PB2.
4. Tidak ada transistor driver yang membalik logika common digit.
5. Pastikan segment dan common digit tidak tertukar.

### Hanya satu digit yang menyala

Periksa:

```text
PB0 -> Digit 0
PB1 -> Digit 1
PB2 -> Digit 2
```

### Angka terlihat terbalik

Kemungkinan besar mapping segment berbeda dari asumsi.

Urutan yang digunakan firmware:

```text
A = PB7
B = PB6
C = PD3
D = PD4
E = PD5
F = PD6
G = PD7
```

### CC0..CC15 salah

Periksa:

```text
S0 = PB3
S1 = PB4
S2 = PB5
```

dan:

```text
4051 #1 EN = PB0
4051 #2 EN = PB1
```

EN harus LOW untuk memilih IC.

---

## 14. File utama

```text
ATmega8_Pot_Controller.c
README.md
```

Firmware dibuat tanpa Arduino framework dan menggunakan register AVR langsung, sehingga dapat dikompilasi menggunakan **avr-gcc**.
