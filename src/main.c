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


// *****************************************************************************
// *****************************************************************************
// Section: Main Entry Point
// *****************************************************************************
// *****************************************************************************

int main(void) {
    /* Initialize all modules */
    SYS_Initialize(NULL);
    printf("start");
    
    
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

    while (true) {

        SYS_Tasks();
    }

    /* Execution should not come here during normal operation */

    return ( EXIT_FAILURE);
}


/*******************************************************************************
 End of File
 */

