/*******************************************************************************
  Main Source File

  Company:
    Microchip Technology Inc.

  File Name:
    main.c

  Summary:
    This file contains the "main" function for a project.

  Description:
    This file contains the "main" function for a project.  The
    "main" function calls the "SYS_Initialize" function to initialize the state
    machines of all modules in the system
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include <stddef.h>                     
#include <stdbool.h>                    
#include <stdlib.h>                     
#include <string.h>
#include "definitions.h"
#include "interrupts.h"                

#define MCP9808_ADDR       0x1C
#define MCP9808_REG_TEMP   0x05

#define SPI_WREN           0x06
#define SPI_WRITE          0x02
#define SPI_READ           0x03 
#define SPI_RDSR           0x05
#define SPI_WIP            0x01
#define SPI_WEL            0x02

/* 4 Mbit = 512 KB, 24bitova adresa (3 bajty), stranka 256 B */
#define EEPROM_SIZE        (512UL * 1024UL)
#define EEPROM_PAGE_SIZE   256UL

#define N (RB_CAPACITY + 1)
#define RB_CAPACITY 10
#define RB_BASE_ADDR 0x0100UL

// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************
volatile static bool btn_flag = false;
int sensor_read = 0;
char sign;
float teplota;
uint16_t real_teplota;
uint8_t head = 0, tail = 0;

uint8_t data[2];

typedef struct {
    uint8_t perioda_mereni;
    uint8_t perioda_odesilani;

    char wifi_ssid[32];
    char wifi_pass[64];
    char mqtt_broker[64];

    uint16_t mqtt_port;
    uint8_t device_id;
    uint8_t CRC;
} DeviceConfig_t;

DeviceConfig_t config = {
    .perioda_mereni = 5,
    .perioda_odesilani = 30,
    .wifi_ssid = "idksnhasde",
    .wifi_pass = "monro",
    .mqtt_broker = "dasdsadad",
    .mqtt_port = 1883,
    .device_id = 1,
    .CRC = 0
};

DeviceConfig_t loaded_config;

static bool SPI_Transfer(uint8_t *tx, uint8_t *rx, size_t lenght) {
    if (!SERCOM2_SPI_WriteRead(tx, lenght, rx, lenght)) {
        return false;
    }
    while (SERCOM2_SPI_IsBusy()) {
    }
    return true;
}

static bool SPI_WriteOnly(const uint8_t *tx, size_t length) {
    if (!SERCOM2_SPI_Write((void *) tx, length)) {
        return false;
    }
    while (SERCOM2_SPI_IsBusy()) {
    }
    return true;
}

static bool SPI_ReadOnly(uint8_t *rx, size_t length) {
    if (!SERCOM2_SPI_Read(rx, length)) {
        return false;
    }
    while (SERCOM2_SPI_IsBusy()) {
    }
    return true;
}

static void EEPROM_SetHeader(uint8_t *hdr, uint8_t cmd, uint32_t address) {
    hdr[0] = cmd;
    hdr[1] = (uint8_t) (address >> 16);
    hdr[2] = (uint8_t) (address >> 8);
    hdr[3] = (uint8_t) (address & 0xFF);
}

static bool EEPROM_ReadStatus(uint8_t *status);
static bool EEPROM_ReadBuffer(uint32_t address, uint8_t *data, size_t length);

static bool EEPROM_WriteEnable(void) {
    uint8_t tx[] = {SPI_WREN};
    uint8_t rx[1];

    CS_Clear();
    bool ok = SPI_Transfer(tx, rx, sizeof (tx));
    CS_Set();
    if (!ok) {
        return false;
    }

    uint8_t status;
    return EEPROM_ReadStatus(&status) && ((status & SPI_WEL) != 0);
}

static bool EEPROM_ReadStatus(uint8_t *status) {
    uint8_t tx[] = {SPI_RDSR, 0x00};
    uint8_t rx[sizeof (tx)] = {0};

    CS_Clear();
    bool ok = SPI_Transfer(tx, rx, sizeof (tx));
    CS_Set();

    if (!ok) {
        return false;
    }
    *status = rx[1];
    return true;
}

static bool EEPROM_WaitReady(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t status;

        if (!EEPROM_ReadStatus(&status)) {
            return false;
        }
        if ((status & SPI_WIP) == 0) {
            return true;
        }

    }

    return false;
}

static bool EEPROM_WritePage(uint32_t address, const uint8_t *data, size_t length) {
    if (!EEPROM_WriteEnable()) {
        return false;
    }

    uint8_t hdr[4];
    EEPROM_SetHeader(hdr, SPI_WRITE, address);

    CS_Clear();
    bool ok = SPI_WriteOnly(hdr, sizeof (hdr)) && SPI_WriteOnly(data, length);
    CS_Set();

    if (!ok) {
        return false;
    }
    return EEPROM_WaitReady();
}

static bool EEPROM_Verify(uint32_t address, const uint8_t *data, size_t length) {
    uint8_t buf[16];

    while (length > 0) {
        size_t chunk = (length > sizeof (buf)) ? sizeof (buf) : length;

        if (!EEPROM_ReadBuffer(address, buf, chunk) || (memcmp(buf, data, chunk) != 0)) {
            return false;
        }
        address += chunk;
        data += chunk;
        length -= chunk;
    }
    return true;
}

static bool EEPROM_WriteBuffer(uint32_t address, const uint8_t *data, size_t lenght) {
    if ((address + lenght) > EEPROM_SIZE) {
        return false;
    }

    while (lenght > 0) {
        size_t chunk = EEPROM_PAGE_SIZE - (address % EEPROM_PAGE_SIZE);
        if (chunk > lenght) {
            chunk = lenght;
        }

        if (!EEPROM_WritePage(address, data, chunk) || !EEPROM_Verify(address, data, chunk)) {
            return false;
        }

        address += chunk;
        data += chunk;
        lenght -= chunk;
    }
    return true;
}

static bool EEPROM_ReadBuffer(uint32_t address, uint8_t *data, size_t length) {
    if (length == 0) {
        return true;
    }
    if ((address + length) > EEPROM_SIZE) {
        return false;
    }

    uint8_t hdr[4];
    EEPROM_SetHeader(hdr, SPI_READ, address);

    CS_Clear();
    bool ok = SPI_WriteOnly(hdr, sizeof (hdr)) && SPI_ReadOnly(data, length);
    CS_Set();

    return ok;
}

static bool EEPROM_WriteByte(uint32_t address, uint8_t data) {
    return EEPROM_WriteBuffer(address, &data, 1);
}

static bool EEPROM_ReadByte(uint32_t address, uint8_t *data) {
    return EEPROM_ReadBuffer(address, data, 1);
}

int I2C_scan_function() {
    uint8_t devices[20] = {0};
    uint8_t count = 0;

    bool status = SERCOM0_I2C_BusScan(
            0x08,
            0x77,
            devices,
            &count
            );

    printf("I2C scan status: %s\r\n", status ? "OK" : "FAIL");
    printf("Pocet nalezenych zarizeni: %u\r\n", count);

    for (uint8_t i = 0; i < count; i++) {
        printf("Zarizeni %u: adresa 0x%02X\r\n", i + 1, devices[i]);
    }

}

bool mcp9808_read_reg(uint8_t reg, uint8_t *data) {
    bool _d = SERCOM0_I2C_Write(MCP9808_ADDR, &reg, 1);
    while (SERCOM0_I2C_IsBusy());
    if (!_d) return false;
    SERCOM0_I2C_Read(MCP9808_ADDR, data, 2);
    while (SERCOM0_I2C_IsBusy());
    return !SERCOM0_I2C_ErrorGet();
}

void mcp9808_temp_read() {
    mcp9808_read_reg(MCP9808_REG_TEMP, data);
    real_teplota = ((uint16_t) data[0] << 8) | data [1];

    sign = (real_teplota & 0x1000) != 0;

    teplota = (real_teplota & 0xFFF) / 16.0f;


    //printf("%c %0.3f\r\n", (sign == 1) ? '-' : '+', teplota);
}

void zapis_conf(void) {
    bool conf_ok = EEPROM_WriteBuffer(0x0000, (const uint8_t *) &config, sizeof (config));

    if (!conf_ok) {
        uint8_t status = 0;
        bool st_ok = EEPROM_ReadStatus(&status);
        printf("Chyba zapisu (status %s: 0x%02X)\r\n", st_ok ? "OK" : "FAIL", status);
    } else {
        printf("Zapis OK\r\n");
    }
}

void nacti_conf(void) {
    bool ok = EEPROM_ReadBuffer(0x0000, (uint8_t *) & loaded_config, sizeof (loaded_config));
    bool strings_terminated =
            memchr(loaded_config.wifi_ssid, '\0', sizeof (loaded_config.wifi_ssid)) != NULL &&
            memchr(loaded_config.mqtt_broker, '\0', sizeof (loaded_config.mqtt_broker)) != NULL;

    if (ok && strings_terminated) {
        printf("SSID: %.*s\r\n", (int) sizeof (loaded_config.wifi_ssid), loaded_config.wifi_ssid);
        printf("Broker: %.*s\r\n", (int) sizeof (loaded_config.mqtt_broker), loaded_config.mqtt_broker);
        printf("MQTT port: %u\r\n", loaded_config.mqtt_port);
    } else {
        printf("Chyba cteni: neplatna nebo prazdna konfigurace EEPROM\r\n");
    }
}

static uint32_t rb_addr(uint8_t index){
    return RB_BASE_ADDR + ((uint32_t)index * sizeof(float));
}

bool push(float x){
    uint8_t next = (head + 1) % N;
    
    if(!EEPROM_WriteBuffer(rb_addr(head),(const uint8_t *)&x, sizeof(float))){
        return false;
    }
    
    if(next == tail){
        tail = (tail + 1) % N;
    }
    
    head = next;
    return true;
}

bool pop(float *x){
    
    if(head == tail)return false;
    
    if(!EEPROM_ReadBuffer(rb_addr(tail),(uint8_t *)x, sizeof(float))){
        return false;
    }
    tail = (tail + 1) % N;
    return true;
}

void btn_callback(uintptr_t context){
    btn_flag = true;
}

int main(void) {
    /* Initialize all modules */
    SYS_Initialize(NULL);
    EIC_CallbackRegister(EIC_PIN_15, btn_callback, 0);
    uint8_t readback = 0;
    zapis_conf();
    nacti_conf();

    while (true) {
        
        if(btn_flag == true){
            btn_flag = false;
            printf("tlac");
        
        }

//        mcp9808_temp_read();
//        push(teplota);
//        float hodnota;
//        while (pop(&hodnota)){
//            printf("Nacteno %0.3f C\r\n", hodnota);
//        }

        /*
        if (EEPROM_WriteByte(0x0010, 0xAB)) {
            if (EEPROM_ReadByte(0x0010, &readback)) {
                if (readback == 0xAB) {
                    printf("EEPROM OK: 0x%02X\r\n", readback);
                } else {
                    printf("Chyba: precteno 0x%02X\r\n", readback);
                }
            }
        }
*/

        SYS_Tasks();
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE);
}


/*******************************************************************************
 End of File
 */

