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

#include <stddef.h>                     // Defines NULL
#include <stdbool.h>                    // Defines true
#include <stdlib.h>                     // Defines EXIT_FAILURE
#include "definitions.h"                // SYS function prototypes

#define MCP9808_ADDR       0x1C
#define MCP9808_REG_TEMP   0x05
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

int main(void) {
    /* Initialize all modules */
    SYS_Initialize(NULL);

    while (true) {

        mcp9808_temp_read();

        SYS_Tasks();
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE);
}


/*******************************************************************************
 End of File
 */

