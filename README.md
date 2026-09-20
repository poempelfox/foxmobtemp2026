A mobile weather station, consisting of a nRF9151-DK and a DFRobot Lark Weather Station.

You will need to patch the file `(workspace)/modules/hal/nordic/nrfx/bsp/stable/mdk/nrf9120_bitfields.h` as follows:
```
#define TWIM_FREQUENCY_FREQUENCY_K100 (0x0028F000UL) /*!< 100 kbps */
```
(it originally says 0x01980000UL)
The API is a bit braindead. It only offers a few static speed settings, of which 100k is the slowest, which unfortunately is still way too fast if you use more than about 10mm of wire to attach the weatherstation. So the above change redefines "100kbps" in the library to actually mean "10kbps" (by modifying the value that is written to the hardwares configuration register). That makes things work smoothly in my experience.

