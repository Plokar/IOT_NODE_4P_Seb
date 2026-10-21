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
#include "definitions.h"                

#define MCP9808_ADDR       0x1C
#define MCP9808_REG_TEMP   0x05

#define SPI_WREN           0x06
#define SPI_WRITE          0x02
#define SPI_READ           0x03 
#define SPI_RDSR           0x05
#define SPI_WIP            0x01

// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

int sensor_read = 0;
char sign;
float teplota;
uint16_t real_teplota;

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
    .wifi_ssid = "cus",
    .wifi_pass = "",
    .mqtt_broker = "",
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

static bool EEPROM_WriteEnable(void) {
    uint8_t tx[] = {SPI_WREN};
    uint8_t rx[1];

    CS_Clear();
    bool ok = SPI_Transfer(tx, rx, sizeof (tx));
    CS_Set();
    return ok;
}

static bool EEPROM_ReadStatus(uint8_t *status) {
    uint8_t tx[] = {SPI_RDSR, 0x00};
    uint8_t rx[] = {0};

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

static bool EEPROM_WriteByte(uint16_t address, uint8_t data) {
    if (!EEPROM_WriteEnable()) {
        return false;
    }

    uint8_t tx[] = {
        SPI_WRITE,
        (uint8_t) (address >> 0),
        (uint8_t) (address & 0xFF),
        data
    };
    uint8_t rx[sizeof (tx)] = {0};

    CS_Clear();
    bool ok = SPI_Transfer(tx, rx, sizeof (tx));
    CS_Set();

    if (!ok) {
        return false;
    }
    return EEPROM_WaitReady();
}

static bool EEPROM_ReadByte(uint16_t address, uint8_t *data) {
    uint8_t tx[] = {
        SPI_READ,
        (uint8_t) (address >> 8),
        (uint8_t) (address & 0xFF),
        0x00
    };

    uint8_t rx[sizeof (tx)] = {0};

    CS_Clear();
    bool ok = SPI_Transfer(tx, rx, sizeof (tx));
    CS_Set();

    if (!ok) {
        return false;
    }

    *data = rx[3];
    return true;
}

static bool EEPROM_WriteBuffer(uint16_t address, const uint8_t *data, size_t lenght) {
    for (size_t i = 0; i < lenght; i++) {
        if (!EEPROM_WriteByte(address + i, data[i])) {
            return false;
        }
    }
    return true;
}

static bool EEPROM_ReadBuffer(uint16_t address, uint8_t *data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (!EEPROM_ReadByte(address + i, &data[i])) {
            return false;
        }
    }

    return true;
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

    sign = real_teplota & 0x1000;

    teplota = (real_teplota & 0xFFF) / 16.0f;


    printf("%c %0.3f\r\n", (sign == 1) ? '-' : '+', teplota);
}

void zapis_conf(void) {
    bool conf_ok = EEPROM_WriteBuffer(0x0000, (const uint8_t *) &config, sizeof (config));

    if (!conf_ok) {
        printf("Chyba zapisu\r\n");
    } else {
        printf("Zapis OK\r\n");
    }
}

void nacti_conf(void) {
    bool ok = EEPROM_ReadBuffer(0x0000, (uint8_t *) & loaded_config, sizeof (loaded_config));

    if (ok) {
        printf("SSID: %s\r\n", loaded_config.wifi_ssid);
        printf("Broker: %s\r\n", loaded_config.mqtt_broker);
        printf("MQTT port: %u\r\n", loaded_config.mqtt_port);
    } else {
        printf("Chyba cteni\r\n");
    }
}

int main(void) {
    /* Initialize all modules */
    SYS_Initialize(NULL);

    uint8_t readback = 0;
    zapis_conf();
    nacti_conf();

    while (true) {

        //mcp9808_temp_read();



        if (EEPROM_WriteByte(0x0010, 0xAB)) {
            if (EEPROM_ReadByte(0x0010, &readback)) {
                if (readback == 0xAB) {
                    printf("EEPROM OK: 0x%02X\r\n", readback);
                } else {
                    printf("Chyba: precteno 0x%02X\r\n", readback);
                }
            }
        }


        SYS_Tasks();
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE);
}


/*******************************************************************************
 End of File
 */

